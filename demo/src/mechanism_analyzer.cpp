#include "nexus_demo/mechanism_analyzer.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace nexus::demo {
namespace {

int resolution_rank(const std::string &resolution) {
  if (resolution == "unresolved") {
    return 3;
  }
  if (resolution == "coarse") {
    return 2;
  }
  if (resolution == "candidate") {
    return 1;
  }
  return 0;
}

std::string combine_resolution(const Interaction &left,
                               const Interaction *right = nullptr) {
  const std::string left_value =
      left.raw.resolution.empty() ? "precise" : left.raw.resolution;
  if (right == nullptr) {
    return left_value;
  }
  const std::string right_value =
      right->raw.resolution.empty() ? "precise" : right->raw.resolution;
  return resolution_rank(left_value) >= resolution_rank(right_value)
             ? left_value
             : right_value;
}

std::string combine_provenance(const Interaction &left,
                               const Interaction *right = nullptr) {
  if (right == nullptr || right->raw.provenance.empty()) {
    return left.raw.provenance;
  }
  if (left.raw.provenance.empty() ||
      left.raw.provenance == right->raw.provenance) {
    return right->raw.provenance;
  }
  return left.raw.provenance + " | " + right->raw.provenance;
}

std::string evidence_id(const Interaction &interaction) {
  return "pid:" + std::to_string(interaction.raw.pid) +
         "/tid:" + std::to_string(interaction.raw.tid) +
         "/seq:" + std::to_string(interaction.raw.sequence);
}

Dependency unary_dependency(const Interaction &interaction,
                            const std::string &kind) {
  Dependency dependency;
  dependency.source = interaction.component;
  dependency.target = interaction.peer_component;
  dependency.kind = kind;
  dependency.mechanism = interaction.raw.mechanism;
  dependency.object = interaction.canonical_object;
  dependency.resolution = combine_resolution(interaction);
  dependency.provenance = combine_provenance(interaction);
  dependency.evidence = {evidence_id(interaction)};
  return dependency;
}

Dependency paired_dependency(const Interaction &source,
                             const Interaction &target,
                             const std::string &kind) {
  Dependency dependency;
  dependency.source = source.component;
  dependency.target = target.component;
  dependency.kind = kind;
  dependency.mechanism = target.raw.mechanism;
  dependency.object = target.canonical_object;
  dependency.resolution = combine_resolution(source, &target);
  dependency.provenance = combine_provenance(source, &target);
  dependency.evidence = {evidence_id(source), evidence_id(target)};
  return dependency;
}

class DirectAnalyzer final : public MechanismAnalyzer {
 public:
  bool supports(const std::string &mechanism) const override {
    return mechanism == "direct";
  }

  std::vector<Dependency> analyze(
      const std::vector<Interaction> &interactions) const override {
    std::vector<Dependency> dependencies;
    for (const auto &interaction : interactions) {
      if (!supports(interaction.raw.mechanism) ||
          interaction.peer_component.empty() ||
          interaction.component == interaction.peer_component) {
        continue;
      }
      dependencies.push_back(unary_dependency(interaction, "direct"));
    }
    return dependencies;
  }
};

class DynamicLoadAnalyzer final : public MechanismAnalyzer {
 public:
  bool supports(const std::string &mechanism) const override {
    return mechanism == "dynamic_load";
  }

  std::vector<Dependency> analyze(
      const std::vector<Interaction> &interactions) const override {
    std::vector<Dependency> dependencies;
    for (const auto &interaction : interactions) {
      if (!supports(interaction.raw.mechanism) ||
          interaction.raw.role != "load" ||
          interaction.peer_component.empty() ||
          interaction.component == interaction.peer_component) {
        continue;
      }
      dependencies.push_back(
          unary_dependency(interaction, "mechanism-mediated"));
    }
    return dependencies;
  }
};

bool is_read_role(const std::string &role) {
  return role == "read" || role == "consume" || role == "read_write";
}

bool is_write_role(const std::string &role) {
  return role == "write" || role == "create" || role == "append" ||
         role == "produce" || role == "read_write";
}

class FileAnalyzer final : public MechanismAnalyzer {
 public:
  bool supports(const std::string &mechanism) const override {
    return mechanism == "file";
  }

  std::vector<Dependency> analyze(
      const std::vector<Interaction> &interactions) const override {
    std::map<std::string, const Interaction *> last_writer;
    std::vector<Dependency> dependencies;

    for (const auto &interaction : interactions) {
      if (!supports(interaction.raw.mechanism)) {
        continue;
      }

      if (is_read_role(interaction.raw.role)) {
        const auto writer = last_writer.find(interaction.canonical_object);
        if (writer != last_writer.end() &&
            writer->second->component != interaction.component) {
          dependencies.push_back(paired_dependency(
              *writer->second, interaction, "mechanism-mediated"));
        }
      }
      if (is_write_role(interaction.raw.role)) {
        last_writer[interaction.canonical_object] = &interaction;
      }
    }
    return dependencies;
  }
};

bool is_send_role(const std::string &role) {
  return role == "send" || role == "publish" || role == "write";
}

bool is_receive_role(const std::string &role) {
  return role == "receive" || role == "consume" || role == "read";
}

class ChannelAnalyzer final : public MechanismAnalyzer {
 public:
  bool supports(const std::string &mechanism) const override {
    return mechanism == "socket" || mechanism == "ipc";
  }

  std::vector<Dependency> analyze(
      const std::vector<Interaction> &interactions) const override {
    std::map<std::string, const Interaction *> last_sender;
    std::vector<Dependency> dependencies;

    for (const auto &interaction : interactions) {
      if (!supports(interaction.raw.mechanism)) {
        continue;
      }
      if (is_receive_role(interaction.raw.role)) {
        const auto sender = last_sender.find(interaction.canonical_object);
        if (sender != last_sender.end() &&
            sender->second->component != interaction.component) {
          dependencies.push_back(paired_dependency(
              *sender->second, interaction, "mechanism-mediated"));
        }
      }
      if (is_send_role(interaction.raw.role)) {
        last_sender[interaction.canonical_object] = &interaction;
      }
    }
    return dependencies;
  }
};

}  // namespace

std::vector<std::unique_ptr<MechanismAnalyzer>>
make_default_mechanism_analyzers() {
  std::vector<std::unique_ptr<MechanismAnalyzer>> analyzers;
  analyzers.push_back(std::make_unique<DirectAnalyzer>());
  analyzers.push_back(std::make_unique<FileAnalyzer>());
  analyzers.push_back(std::make_unique<DynamicLoadAnalyzer>());
  analyzers.push_back(std::make_unique<ChannelAnalyzer>());
  return analyzers;
}

}  // namespace nexus::demo
