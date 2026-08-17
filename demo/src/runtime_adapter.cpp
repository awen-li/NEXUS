#include "nexus_demo/runtime_adapter.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace nexus::demo {
namespace {

Interaction normalize_event(const RawEvent &event) {
  Interaction normalized;
  normalized.raw = event;
  if (normalized.raw.resolution.empty()) {
    normalized.raw.resolution = "precise";
  }
  if (event.component.empty()) {
    normalized.raw.resolution = "coarse";
  }

  std::ostringstream actor;
  actor << "pid:" << event.pid << "/tid:" << event.tid;
  normalized.actor = actor.str();
  normalized.component =
      canonical_component(event.runtime, event.component, event.pid);

  if (!event.peer_component.empty()) {
    const std::string peer_runtime =
        event.peer_runtime.empty() ? event.runtime : event.peer_runtime;
    normalized.peer_component =
        canonical_component(peer_runtime, event.peer_component, event.pid);
  }

  normalized.canonical_object =
      event.mechanism + ":" +
      (event.object.empty() ? std::string("unresolved") : event.object);
  return normalized;
}

class NamedRuntimeAdapter final : public RuntimeAdapter {
 public:
  explicit NamedRuntimeAdapter(std::string runtime)
      : runtime_(std::move(runtime)) {}

  bool supports(const std::string &runtime) const override {
    return runtime == runtime_;
  }

  Interaction normalize(const RawEvent &event) const override {
    return normalize_event(event);
  }

 private:
  std::string runtime_;
};

class FallbackRuntimeAdapter final : public RuntimeAdapter {
 public:
  bool supports(const std::string &) const override { return true; }

  Interaction normalize(const RawEvent &event) const override {
    Interaction normalized = normalize_event(event);
    if (normalized.raw.runtime.empty()) {
      normalized.raw.runtime = "unknown";
      normalized.component =
          canonical_component("unknown", event.component, event.pid);
    }
    if (normalized.raw.resolution == "precise") {
      normalized.raw.resolution = "candidate";
    }
    return normalized;
  }
};

}  // namespace

std::string canonical_component(const std::string &runtime,
                                const std::string &component,
                                long pid) {
  const std::string domain = runtime.empty() ? "unknown" : runtime;
  if (!component.empty()) {
    return domain + ":" + component;
  }
  return domain + ":process:" + std::to_string(pid);
}

void RuntimeAdapterRegistry::add(std::unique_ptr<RuntimeAdapter> adapter) {
  adapters_.push_back(std::move(adapter));
}

Interaction RuntimeAdapterRegistry::normalize(const RawEvent &event) const {
  for (const auto &adapter : adapters_) {
    if (adapter->supports(event.runtime)) {
      return adapter->normalize(event);
    }
  }
  throw std::runtime_error("no runtime adapter for domain: " + event.runtime);
}

RuntimeAdapterRegistry make_default_runtime_adapters() {
  RuntimeAdapterRegistry registry;
  registry.add(std::make_unique<NamedRuntimeAdapter>("native"));
  registry.add(std::make_unique<NamedRuntimeAdapter>("cpython"));
  registry.add(std::make_unique<NamedRuntimeAdapter>("jvm"));
  registry.add(std::make_unique<FallbackRuntimeAdapter>());
  return registry;
}

}  // namespace nexus::demo
