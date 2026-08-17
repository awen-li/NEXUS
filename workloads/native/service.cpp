#include "nexus_demo/probe.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char *kServiceComponent = "nexus_demo_service";
constexpr const char *kPluginComponent = "nexus_transform_plugin";
constexpr std::uint32_t kMaximumMessageSize = 4U * 1024U * 1024U;

using TransformFunction =
    long (*)(const char *, std::size_t, char *, std::size_t);

class FileDescriptor {
 public:
  explicit FileDescriptor(int descriptor = -1) : descriptor_(descriptor) {}
  ~FileDescriptor() {
    if (descriptor_ >= 0) {
      close(descriptor_);
    }
  }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  int get() const { return descriptor_; }

 private:
  int descriptor_;
};

class SocketPathCleanup {
 public:
  explicit SocketPathCleanup(std::string path) : path_(std::move(path)) {}
  ~SocketPathCleanup() { unlink(path_.c_str()); }

  SocketPathCleanup(const SocketPathCleanup &) = delete;
  SocketPathCleanup &operator=(const SocketPathCleanup &) = delete;

 private:
  std::string path_;
};

[[noreturn]] void throw_system_error(const std::string &operation) {
  throw std::runtime_error(operation + ": " + std::strerror(errno));
}

void read_all(int descriptor, void *buffer, std::size_t size) {
  auto *bytes = static_cast<unsigned char *>(buffer);
  std::size_t consumed = 0;
  while (consumed < size) {
    const ssize_t result =
        read(descriptor, bytes + consumed, size - consumed);
    if (result == 0) {
      throw std::runtime_error("peer closed the socket during a frame");
    }
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw_system_error("read");
    }
    consumed += static_cast<std::size_t>(result);
  }
}

void write_all(int descriptor, const void *buffer, std::size_t size) {
  const auto *bytes = static_cast<const unsigned char *>(buffer);
  std::size_t written = 0;
  while (written < size) {
    const ssize_t result =
        write(descriptor, bytes + written, size - written);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw_system_error("write");
    }
    written += static_cast<std::size_t>(result);
  }
}

std::string read_frame(int descriptor) {
  std::uint32_t network_size = 0;
  read_all(descriptor, &network_size, sizeof(network_size));
  const std::uint32_t size = ntohl(network_size);
  if (size > kMaximumMessageSize) {
    throw std::runtime_error("request exceeds the demo message limit");
  }

  std::string payload(size, '\0');
  if (size != 0) {
    read_all(descriptor, payload.data(), payload.size());
  }
  return payload;
}

void write_frame(int descriptor, const std::string &payload) {
  if (payload.size() > kMaximumMessageSize) {
    throw std::runtime_error("response exceeds the demo message limit");
  }

  const auto network_size =
      htonl(static_cast<std::uint32_t>(payload.size()));
  write_all(descriptor, &network_size, sizeof(network_size));
  if (!payload.empty()) {
    write_all(descriptor, payload.data(), payload.size());
  }
}

void emit(const char *mechanism,
          const char *role,
          const std::string &object,
          const char *provenance,
          const char *context,
          const char *peer_component = "") {
  if (nexus_probe_emit("native", kServiceComponent, mechanism, role,
                       object.c_str(), provenance, context, "precise",
                       peer_component[0] == '\0' ? "" : "native",
                       peer_component) != 0) {
    throw std::runtime_error(nexus_probe_last_error());
  }
}

std::string transform_with_plugin(const std::string &input,
                                  const std::string &plugin_path) {
  void *plugin = dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (plugin == nullptr) {
    throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
  }

  emit("dynamic_load", "load", plugin_path, "native service dynamic loader",
       "service:load-transform", kPluginComponent);

  dlerror();
  void *symbol = dlsym(plugin, "nexus_transform");
  const char *symbol_error = dlerror();
  if (symbol_error != nullptr) {
    const std::string message =
        std::string("dlsym(nexus_transform) failed: ") + symbol_error;
    dlclose(plugin);
    throw std::runtime_error(message);
  }

  const auto transform = reinterpret_cast<TransformFunction>(symbol);
  std::vector<char> output(input.size() + 1);
  const long output_size =
      transform(input.data(), input.size(), output.data(), output.size());
  dlclose(plugin);

  if (output_size < 0 ||
      static_cast<std::size_t>(output_size) > output.size()) {
    throw std::runtime_error("native transform rejected the output buffer");
  }
  return std::string(output.data(), static_cast<std::size_t>(output_size));
}

struct Options {
  std::string socket_path;
  std::string plugin_path;
};

Options parse_options(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (index + 1 >= argc) {
      throw std::runtime_error("missing value after " + argument);
    }
    const std::string value = argv[++index];
    if (argument == "--socket") {
      options.socket_path =
          std::filesystem::absolute(value).lexically_normal().string();
    } else if (argument == "--plugin") {
      options.plugin_path =
          std::filesystem::absolute(value).lexically_normal().string();
    } else {
      throw std::runtime_error("unknown option: " + argument);
    }
  }

  if (options.socket_path.empty() || options.plugin_path.empty()) {
    throw std::runtime_error("--socket and --plugin are required");
  }
  return options;
}

void serve_once(const Options &options) {
  if (options.socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
    throw std::runtime_error("Unix socket path is too long");
  }

  unlink(options.socket_path.c_str());
  SocketPathCleanup cleanup(options.socket_path);

  FileDescriptor listener(socket(AF_UNIX, SOCK_STREAM, 0));
  if (listener.get() < 0) {
    throw_system_error("socket");
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, options.socket_path.c_str(),
              options.socket_path.size() + 1);
  if (bind(listener.get(), reinterpret_cast<sockaddr *>(&address),
           sizeof(address)) < 0) {
    throw_system_error("bind");
  }
  if (listen(listener.get(), 1) < 0) {
    throw_system_error("listen");
  }

  std::cout << "NEXUS demo service listening on " << options.socket_path
            << std::endl;

  FileDescriptor client(accept(listener.get(), nullptr, nullptr));
  if (client.get() < 0) {
    throw_system_error("accept");
  }

  const std::string request = read_frame(client.get());
  emit("socket", "receive", options.socket_path,
       "native Unix-domain service", "service:receive-request");

  const std::string response =
      transform_with_plugin(request, options.plugin_path);

  emit("socket", "send", options.socket_path,
       "native Unix-domain service", "service:send-response");
  write_frame(client.get(), response);
}

}  // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);
    serve_once(options);
    std::cout << "NEXUS demo service completed one request." << std::endl;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "nexus-demo-service: " << error.what() << '\n';
    return 1;
  }
}
