#ifndef NEXUS_DEMO_DEPENDENCY_COMPOSER_HPP
#define NEXUS_DEMO_DEPENDENCY_COMPOSER_HPP

#include "nexus_demo/mechanism_analyzer.hpp"
#include "nexus_demo/runtime_adapter.hpp"

#include <memory>
#include <vector>

namespace nexus::demo {

class DependencyComposer {
 public:
  DependencyComposer(
      RuntimeAdapterRegistry adapters,
      std::vector<std::unique_ptr<MechanismAnalyzer>> analyzers);

  Graph compose(const std::vector<RawEvent> &events) const;

 private:
  RuntimeAdapterRegistry adapters_;
  std::vector<std::unique_ptr<MechanismAnalyzer>> analyzers_;
};

DependencyComposer make_default_composer();

}  // namespace nexus::demo

#endif
