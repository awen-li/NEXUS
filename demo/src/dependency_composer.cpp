#include "nexus_demo/dependency_composer.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace nexus::demo {

DependencyComposer::DependencyComposer(
    RuntimeAdapterRegistry adapters,
    std::vector<std::unique_ptr<MechanismAnalyzer>> analyzers)
    : adapters_(std::move(adapters)), analyzers_(std::move(analyzers)) {}

Graph DependencyComposer::compose(const std::vector<RawEvent> &events) const {
  Graph graph;
  std::set<std::string> components;
  std::map<std::string, ObjectNode> objects;

  for (const auto &event : events) {
    Interaction interaction = adapters_.normalize(event);
    components.insert(interaction.component);
    if (!interaction.peer_component.empty()) {
      components.insert(interaction.peer_component);
    }
    if (interaction.raw.mechanism != "direct") {
      objects.emplace(
          interaction.canonical_object,
          ObjectNode{interaction.canonical_object, interaction.raw.mechanism});
    }
    graph.interactions.push_back(std::move(interaction));
  }

  for (const auto &analyzer : analyzers_) {
    auto dependencies = analyzer->analyze(graph.interactions);
    graph.dependencies.insert(graph.dependencies.end(),
                              dependencies.begin(), dependencies.end());
  }

  using DependencyKey =
      std::tuple<std::string, std::string, std::string, std::string,
                 std::string>;
  std::map<DependencyKey, Dependency> deduplicated;
  for (auto &dependency : graph.dependencies) {
    DependencyKey key{dependency.source, dependency.target, dependency.kind,
                      dependency.mechanism, dependency.object};
    const auto found = deduplicated.find(key);
    if (found == deduplicated.end()) {
      deduplicated.emplace(std::move(key), std::move(dependency));
      continue;
    }
    auto &existing = found->second;
    existing.evidence.insert(existing.evidence.end(),
                             dependency.evidence.begin(),
                             dependency.evidence.end());
    std::sort(existing.evidence.begin(), existing.evidence.end());
    existing.evidence.erase(
        std::unique(existing.evidence.begin(), existing.evidence.end()),
        existing.evidence.end());
  }

  graph.dependencies.clear();
  for (auto &[key, dependency] : deduplicated) {
    (void)key;
    graph.dependencies.push_back(std::move(dependency));
  }

  graph.components.assign(components.begin(), components.end());
  for (const auto &[id, object] : objects) {
    (void)id;
    graph.objects.push_back(object);
  }

  return graph;
}

DependencyComposer make_default_composer() {
  return DependencyComposer(make_default_runtime_adapters(),
                            make_default_mechanism_analyzers());
}

}  // namespace nexus::demo
