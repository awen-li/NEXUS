#ifndef NEXUS_DEMO_MODEL_HPP
#define NEXUS_DEMO_MODEL_HPP

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace nexus::demo {

struct RawEvent {
  std::uint64_t sequence{};
  std::uint64_t timestamp_ns{};
  long pid{};
  long tid{};
  std::string runtime;
  std::string component;
  std::string mechanism;
  std::string role;
  std::string object;
  std::string provenance;
  std::string context;
  std::string resolution;
  std::string peer_runtime;
  std::string peer_component;
};

struct Interaction {
  RawEvent raw;
  std::string actor;
  std::string component;
  std::string peer_component;
  std::string canonical_object;
};

struct ObjectNode {
  std::string id;
  std::string mechanism;
};

struct Dependency {
  std::string source;
  std::string target;
  std::string kind;
  std::string mechanism;
  std::string object;
  std::string resolution;
  std::string provenance;
  std::vector<std::string> evidence;
};

struct Graph {
  std::vector<std::string> components;
  std::vector<ObjectNode> objects;
  std::vector<Interaction> interactions;
  std::vector<Dependency> dependencies;
};

std::vector<RawEvent> read_trace(const std::string &path);
void write_graph_json(const Graph &graph, std::ostream &output);
void write_graph_dot(const Graph &graph, std::ostream &output);
std::string json_escape(const std::string &value);

}  // namespace nexus::demo

#endif
