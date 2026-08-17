#define NEXUS_PROBE_BUILD
#include "nexus_demo/probe.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::mutex trace_mutex;
std::ofstream trace_stream;
std::string trace_path;
std::atomic<std::uint64_t> next_sequence{1};
thread_local std::string last_error;

void set_error(const std::string &message) { last_error = message; }

std::string escape_json(const char *raw) {
  const std::string value = raw == nullptr ? "" : raw;
  std::ostringstream output;
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (ch < 0x20) {
          static constexpr char hex[] = "0123456789abcdef";
          output << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
        } else {
          output << static_cast<char>(ch);
        }
    }
  }
  return output.str();
}

long process_id() {
#if defined(_WIN32)
  return static_cast<long>(_getpid());
#else
  return static_cast<long>(getpid());
#endif
}

long thread_id() {
#if defined(__linux__)
  return static_cast<long>(syscall(SYS_gettid));
#else
  return static_cast<long>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

bool open_trace_locked(const char *path) {
  if (path == nullptr || *path == '\0') {
    set_error("trace path is empty; set NEXUS_TRACE_FILE or call nexus_probe_init");
    return false;
  }

  trace_stream.close();
  trace_stream.clear();
  trace_stream.open(path, std::ios::out | std::ios::app);
  if (!trace_stream) {
    set_error(std::string("cannot open trace file: ") + path);
    return false;
  }

  trace_path = path;
  return true;
}

bool ensure_trace_locked() {
  if (trace_stream.is_open()) {
    return true;
  }
  return open_trace_locked(std::getenv("NEXUS_TRACE_FILE"));
}

void write_string_field(std::ostream &output,
                        const char *name,
                        const char *value,
                        bool trailing_comma = true) {
  output << "\"" << name << "\":\"" << escape_json(value) << "\"";
  if (trailing_comma) {
    output << ',';
  }
}

}  // namespace

extern "C" int nexus_probe_init(const char *path) {
  try {
    std::lock_guard<std::mutex> lock(trace_mutex);
    next_sequence.store(1);
    return open_trace_locked(path) ? 0 : -1;
  } catch (const std::exception &error) {
    set_error(error.what());
    return -1;
  }
}

extern "C" int nexus_probe_emit(
    const char *runtime_domain,
    const char *component,
    const char *mechanism,
    const char *role,
    const char *object_evidence,
    const char *provenance,
    const char *context,
    const char *resolution,
    const char *peer_runtime_domain,
    const char *peer_component) {
  try {
    if (runtime_domain == nullptr || *runtime_domain == '\0' ||
        mechanism == nullptr || *mechanism == '\0' ||
        role == nullptr || *role == '\0') {
      set_error("runtime_domain, mechanism, and role are required");
      return -1;
    }

    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto sequence = next_sequence.fetch_add(1);

    std::lock_guard<std::mutex> lock(trace_mutex);
    if (!ensure_trace_locked()) {
      return -1;
    }

    trace_stream << '{';
    write_string_field(trace_stream, "schema", "nexus.demo.event.v1");
    trace_stream << "\"sequence\":" << sequence << ',';
    trace_stream << "\"timestamp_ns\":" << timestamp << ',';
    trace_stream << "\"pid\":" << process_id() << ',';
    trace_stream << "\"tid\":" << thread_id() << ',';
    write_string_field(trace_stream, "runtime", runtime_domain);
    write_string_field(trace_stream, "component", component);
    write_string_field(trace_stream, "mechanism", mechanism);
    write_string_field(trace_stream, "role", role);
    write_string_field(trace_stream, "object", object_evidence);
    write_string_field(trace_stream, "provenance", provenance);
    write_string_field(trace_stream, "context", context);
    write_string_field(trace_stream, "resolution",
                       resolution == nullptr || *resolution == '\0'
                           ? "precise"
                           : resolution);
    write_string_field(trace_stream, "peer_runtime", peer_runtime_domain);
    write_string_field(trace_stream, "peer_component", peer_component, false);
    trace_stream << "}\n";
    trace_stream.flush();

    if (!trace_stream) {
      set_error(std::string("failed while writing trace file: ") + trace_path);
      return -1;
    }
    last_error.clear();
    return 0;
  } catch (const std::exception &error) {
    set_error(error.what());
    return -1;
  }
}

extern "C" int nexus_probe_flush(void) {
  try {
    std::lock_guard<std::mutex> lock(trace_mutex);
    if (!ensure_trace_locked()) {
      return -1;
    }
    trace_stream.flush();
    if (!trace_stream) {
      set_error(std::string("failed while flushing trace file: ") + trace_path);
      return -1;
    }
    return 0;
  } catch (const std::exception &error) {
    set_error(error.what());
    return -1;
  }
}

extern "C" void nexus_probe_close(void) {
  std::lock_guard<std::mutex> lock(trace_mutex);
  trace_stream.close();
  trace_path.clear();
}

extern "C" const char *nexus_probe_last_error(void) {
  return last_error.c_str();
}
