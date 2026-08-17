#ifndef NEXUS_DEMO_RUNTIME_ADAPTER_HPP
#define NEXUS_DEMO_RUNTIME_ADAPTER_HPP

#include "nexus_demo/model.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nexus::demo {

class RuntimeAdapter {
 public:
  virtual ~RuntimeAdapter() = default;
  virtual bool supports(const std::string &runtime) const = 0;
  virtual Interaction normalize(const RawEvent &event) const = 0;
};

class RuntimeAdapterRegistry {
 public:
  void add(std::unique_ptr<RuntimeAdapter> adapter);
  Interaction normalize(const RawEvent &event) const;

 private:
  std::vector<std::unique_ptr<RuntimeAdapter>> adapters_;
};

RuntimeAdapterRegistry make_default_runtime_adapters();
std::string canonical_component(const std::string &runtime,
                                const std::string &component,
                                long pid);

}  // namespace nexus::demo

#endif
