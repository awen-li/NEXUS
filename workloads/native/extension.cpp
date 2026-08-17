#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "nexus_demo/probe.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr const char *kNativeComponent = "_nexus_bench_ext";
constexpr const char *kPluginComponent = "nexus_transform_plugin";
constexpr std::uint32_t kMaximumMessageSize = 4U * 1024U * 1024U;

using TransformFunction =
    long (*)(const char *, std::size_t, char *, std::size_t);

std::string absolute_path(const char *path) {
  return std::filesystem::absolute(path).lexically_normal().string();
}

bool emit_checked(const char *runtime,
                  const char *component,
                  const char *mechanism,
                  const char *role,
                  const std::string &object,
                  const char *provenance,
                  const char *context,
                  const char *resolution = "precise",
                  const char *peer_runtime = "",
                  const char *peer_component = "") {
  if (nexus_probe_emit(runtime, component, mechanism, role, object.c_str(),
                       provenance, context, resolution, peer_runtime,
                       peer_component) == 0) {
    return true;
  }
  PyErr_SetString(PyExc_RuntimeError, nexus_probe_last_error());
  return false;
}

bool socket_write_all(int descriptor,
                      const void *buffer,
                      std::size_t size) {
  const auto *bytes = static_cast<const unsigned char *>(buffer);
  std::size_t written = 0;
  while (written < size) {
    const ssize_t result =
        send(descriptor, bytes + written, size - written, MSG_NOSIGNAL);
    if (result == 0) {
      PyErr_SetString(PyExc_ConnectionError,
                      "service closed the socket during a frame");
      return false;
    }
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      PyErr_SetFromErrno(PyExc_OSError);
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  return true;
}

bool socket_read_all(int descriptor, void *buffer, std::size_t size) {
  auto *bytes = static_cast<unsigned char *>(buffer);
  std::size_t consumed = 0;
  while (consumed < size) {
    const ssize_t result =
        recv(descriptor, bytes + consumed, size - consumed, 0);
    if (result == 0) {
      PyErr_SetString(PyExc_ConnectionError,
                      "service closed the socket during a frame");
      return false;
    }
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      PyErr_SetFromErrno(PyExc_OSError);
      return false;
    }
    consumed += static_cast<std::size_t>(result);
  }
  return true;
}

bool socket_write_frame(int descriptor, const std::string &payload) {
  if (payload.size() > kMaximumMessageSize) {
    PyErr_SetString(PyExc_ValueError,
                    "request exceeds the demo message limit");
    return false;
  }
  const auto network_size =
      htonl(static_cast<std::uint32_t>(payload.size()));
  return socket_write_all(descriptor, &network_size, sizeof(network_size)) &&
         (payload.empty() ||
          socket_write_all(descriptor, payload.data(), payload.size()));
}

bool socket_read_frame(int descriptor, std::string &payload) {
  std::uint32_t network_size = 0;
  if (!socket_read_all(descriptor, &network_size, sizeof(network_size))) {
    return false;
  }

  const std::uint32_t size = ntohl(network_size);
  if (size > kMaximumMessageSize) {
    PyErr_SetString(PyExc_ValueError,
                    "response exceeds the demo message limit");
    return false;
  }
  payload.assign(size, '\0');
  return size == 0 ||
         socket_read_all(descriptor, payload.data(), payload.size());
}

int connect_to_service(const std::string &socket_path) {
  sockaddr_un address{};
  if (socket_path.size() >= sizeof(address.sun_path)) {
    PyErr_SetString(PyExc_ValueError, "Unix socket path is too long");
    return -1;
  }

  const int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
  if (descriptor < 0) {
    PyErr_SetFromErrno(PyExc_OSError);
    return -1;
  }

  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socket_path.c_str(),
              socket_path.size() + 1);
  if (connect(descriptor, reinterpret_cast<sockaddr *>(&address),
              sizeof(address)) < 0) {
    PyErr_SetFromErrnoWithFilename(PyExc_ConnectionError,
                                   socket_path.c_str());
    close(descriptor);
    return -1;
  }
  return descriptor;
}

PyObject *record_interaction(PyObject *,
                             PyObject *args,
                             PyObject *kwargs) {
  const char *runtime = nullptr;
  const char *component = nullptr;
  const char *mechanism = nullptr;
  const char *role = nullptr;
  const char *object = nullptr;
  const char *provenance = "CPython demo adapter";
  const char *context = "";
  const char *resolution = "precise";
  const char *peer_runtime = "";
  const char *peer_component = "";

  static const char *keyword_names[] = {
      "runtime",       "component",   "mechanism", "role",
      "object",        "provenance",  "context",   "resolution",
      "peer_runtime",  "peer_component", nullptr};
  auto **keywords = const_cast<char **>(keyword_names);

  if (!PyArg_ParseTupleAndKeywords(
          args, kwargs, "sssss|sssss:record_interaction", keywords,
          &runtime, &component, &mechanism, &role, &object, &provenance,
          &context, &resolution, &peer_runtime, &peer_component)) {
    return nullptr;
  }

  if (!emit_checked(runtime, component, mechanism, role,
                    std::string(object), provenance, context, resolution,
                    peer_runtime, peer_component)) {
    return nullptr;
  }
  Py_RETURN_NONE;
}

bool emit_python_call(const char *caller_component, const char *context) {
  return emit_checked(
      "cpython", caller_component, "direct", "call",
      "cpython-extension:_nexus_bench_ext", "CPython C API boundary",
      context, "precise", "native", kNativeComponent);
}

PyObject *transform_inline(PyObject *, PyObject *args) {
  const char *input = nullptr;
  Py_ssize_t input_size = 0;
  const char *caller_component = nullptr;
  if (!PyArg_ParseTuple(args, "s#s:transform_inline", &input, &input_size,
                        &caller_component)) {
    return nullptr;
  }

  if (!emit_python_call(caller_component,
                        "b2-python-extension:transform-inline")) {
    return nullptr;
  }

  std::string output(input, static_cast<std::size_t>(input_size));
  for (char &character : output) {
    character = static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  }
  return PyUnicode_DecodeUTF8(output.data(),
                              static_cast<Py_ssize_t>(output.size()),
                              "strict");
}

PyObject *transform_via_plugin(PyObject *, PyObject *args) {
  const char *input = nullptr;
  Py_ssize_t input_size = 0;
  const char *plugin_argument = nullptr;
  const char *caller_component = nullptr;
  if (!PyArg_ParseTuple(args, "s#ss:transform_via_plugin", &input,
                        &input_size, &plugin_argument, &caller_component)) {
    return nullptr;
  }

  if (!emit_python_call(caller_component,
                        "b3-extension-plugin:call-extension")) {
    return nullptr;
  }

  const std::string plugin_path = absolute_path(plugin_argument);
  void *plugin = dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (plugin == nullptr) {
    PyErr_Format(PyExc_RuntimeError, "dlopen failed: %s", dlerror());
    return nullptr;
  }

  if (!emit_checked("native", kNativeComponent, "dynamic_load", "load",
                    plugin_path, "native extension dynamic loader",
                    "b3-extension-plugin:load-transform", "precise",
                    "native", kPluginComponent)) {
    dlclose(plugin);
    return nullptr;
  }

  dlerror();
  void *symbol = dlsym(plugin, "nexus_transform");
  const char *symbol_error = dlerror();
  if (symbol_error != nullptr) {
    const std::string message =
        std::string("dlsym(nexus_transform) failed: ") + symbol_error;
    dlclose(plugin);
    PyErr_SetString(PyExc_RuntimeError, message.c_str());
    return nullptr;
  }

  const auto transform = reinterpret_cast<TransformFunction>(symbol);
  std::vector<char> output(static_cast<std::size_t>(input_size) + 1U);
  const long output_size =
      transform(input, static_cast<std::size_t>(input_size), output.data(),
                output.size());
  dlclose(plugin);

  if (output_size < 0 ||
      static_cast<std::size_t>(output_size) > output.size()) {
    PyErr_SetString(PyExc_RuntimeError,
                    "native transform rejected the output buffer");
    return nullptr;
  }
  return PyUnicode_DecodeUTF8(output.data(), output_size, "strict");
}

PyObject *run_via_service(PyObject *, PyObject *args) {
  const char *input_argument = nullptr;
  const char *output_argument = nullptr;
  const char *socket_argument = nullptr;
  const char *caller_component = nullptr;

  if (!PyArg_ParseTuple(args, "ssss:run_via_service", &input_argument,
                        &output_argument, &socket_argument,
                        &caller_component)) {
    return nullptr;
  }

  const std::string input_path = absolute_path(input_argument);
  const std::string output_path = absolute_path(output_argument);
  const std::string socket_path = absolute_path(socket_argument);

  if (!emit_python_call(caller_component,
                        "b4-extension-service:call-extension")) {
    return nullptr;
  }

  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    PyErr_Format(PyExc_OSError, "cannot open benchmark input: %s",
                 input_path.c_str());
    return nullptr;
  }
  const std::string input_text((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());

  if (!emit_checked("native", kNativeComponent, "file", "read",
                    input_path, "native std::ifstream",
                    "run_via_service:read-input")) {
    return nullptr;
  }

  const int service = connect_to_service(socket_path);
  if (service < 0) {
    return nullptr;
  }

  if (!emit_checked("native", kNativeComponent, "socket", "send",
                    socket_path, "native Unix-domain client",
                    "extension:send-request") ||
      !socket_write_frame(service, input_text)) {
    close(service);
    return nullptr;
  }

  std::string response;
  if (!socket_read_frame(service, response)) {
    close(service);
    return nullptr;
  }
  close(service);

  if (!emit_checked("native", kNativeComponent, "socket", "receive",
                    socket_path, "native Unix-domain client",
                    "extension:receive-response")) {
    return nullptr;
  }

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    PyErr_Format(PyExc_OSError, "cannot open benchmark output: %s",
                 output_path.c_str());
    return nullptr;
  }
  output.write(response.data(), response.size());
  output.close();
  if (!output) {
    PyErr_Format(PyExc_OSError, "failed to write benchmark output: %s",
                 output_path.c_str());
    return nullptr;
  }

  if (!emit_checked("native", kNativeComponent, "file", "write",
                    output_path, "native std::ofstream",
                    "run_via_service:write-output")) {
    return nullptr;
  }

  return PyUnicode_DecodeUTF8(response.data(), response.size(), "strict");
}

PyMethodDef module_methods[] = {
    {"record_interaction",
     reinterpret_cast<PyCFunction>(record_interaction),
     METH_VARARGS | METH_KEYWORDS,
     "Emit one attributed normalized interaction through the C ABI."},
    {"transform_inline", transform_inline, METH_VARARGS,
     "Transform text directly inside the C++ extension."},
    {"transform_via_plugin", transform_via_plugin, METH_VARARGS,
     "Transform text through a C++ plugin loaded by the extension."},
    {"run_via_service", run_via_service, METH_VARARGS,
     "Exchange a framed request/reply with the native C++ service."},
    {nullptr, nullptr, 0, nullptr}};

PyModuleDef module_definition = {
    PyModuleDef_HEAD_INIT,
    "_nexus_bench_ext",
    "Native component used by the NEXUS CPython benchmark.",
    -1,
    module_methods,
    nullptr,
    nullptr,
    nullptr,
    nullptr};

}  // namespace

PyMODINIT_FUNC PyInit__nexus_bench_ext(void) {
  return PyModule_Create(&module_definition);
}
