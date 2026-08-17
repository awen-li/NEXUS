#ifndef NEXUS_DEMO_MECHANISM_ANALYZER_HPP
#define NEXUS_DEMO_MECHANISM_ANALYZER_HPP

#include "nexus_demo/model.hpp"

#include <memory>
#include <string>
#include <vector>

namespace nexus::demo {

class MechanismAnalyzer {
 public:
  virtual ~MechanismAnalyzer() = default;
  virtual bool supports(const std::string &mechanism) const = 0;
  virtual std::vector<Dependency> analyze(
      const std::vector<Interaction> &interactions) const = 0;
};

std::vector<std::unique_ptr<MechanismAnalyzer>>
make_default_mechanism_analyzers();

}  // namespace nexus::demo

#endif
