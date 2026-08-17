#include "nexus_demo/model.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace nexus::demo {
namespace {

using Fields = std::unordered_map<std::string, std::string>;

void skip_space(const std::string &text, std::size_t &position) {
  while (position < text.size() &&
         std::isspace(static_cast<unsigned char>(text[position]))) {
    ++position;
  }
}

std::string parse_quoted(const std::string &text, std::size_t &position) {
  if (position >= text.size() || text[position] != '"') {
    throw std::runtime_error("expected JSON string");
  }
  ++position;

  std::string value;
  while (position < text.size()) {
    const char ch = text[position++];
    if (ch == '"') {
      return value;
    }
    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }
    if (position >= text.size()) {
      throw std::runtime_error("unterminated JSON escape");
    }
    const char escaped = text[position++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        value.push_back(escaped);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      case 'u':
        if (position + 4 > text.size()) {
          throw std::runtime_error("short JSON unicode escape");
        }
        value.append("\\u");
        value.append(text, position, 4);
        position += 4;
        break;
      default:
        throw std::runtime_error("unsupported JSON escape");
    }
  }
  throw std::runtime_error("unterminated JSON string");
}

Fields parse_flat_object(const std::string &line) {
  Fields fields;
  std::size_t position = 0;
  skip_space(line, position);
  if (position >= line.size() || line[position++] != '{') {
    throw std::runtime_error("expected JSON object");
  }

  while (true) {
    skip_space(line, position);
    if (position < line.size() && line[position] == '}') {
      ++position;
      break;
    }

    const std::string key = parse_quoted(line, position);
    skip_space(line, position);
    if (position >= line.size() || line[position++] != ':') {
      throw std::runtime_error("expected ':' after JSON key");
    }
    skip_space(line, position);

    std::string value;
    if (position < line.size() && line[position] == '"') {
      value = parse_quoted(line, position);
    } else {
      const std::size_t start = position;
      while (position < line.size() && line[position] != ',' &&
             line[position] != '}') {
        ++position;
      }
      value = line.substr(start, position - start);
      while (!value.empty() &&
             std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
      }
    }
    fields.emplace(key, value);

    skip_space(line, position);
    if (position < line.size() && line[position] == ',') {
      ++position;
      continue;
    }
    if (position < line.size() && line[position] == '}') {
      ++position;
      break;
    }
    throw std::runtime_error("expected ',' or '}' in JSON object");
  }

  skip_space(line, position);
  if (position != line.size()) {
    throw std::runtime_error("unexpected text after JSON object");
  }
  return fields;
}

const std::string &required(const Fields &fields, const std::string &key) {
  const auto found = fields.find(key);
  if (found == fields.end()) {
    throw std::runtime_error("event is missing field '" + key + "'");
  }
  return found->second;
}

std::string optional(const Fields &fields, const std::string &key) {
  const auto found = fields.find(key);
  return found == fields.end() ? "" : found->second;
}

std::uint64_t parse_u64(const Fields &fields, const std::string &key) {
  return std::stoull(required(fields, key));
}

long parse_long(const Fields &fields, const std::string &key) {
  return std::stol(required(fields, key));
}

RawEvent parse_event(const std::string &line) {
  const Fields fields = parse_flat_object(line);
  RawEvent event;
  event.sequence = parse_u64(fields, "sequence");
  event.timestamp_ns = parse_u64(fields, "timestamp_ns");
  event.pid = parse_long(fields, "pid");
  event.tid = parse_long(fields, "tid");
  event.runtime = required(fields, "runtime");
  event.component = optional(fields, "component");
  event.mechanism = required(fields, "mechanism");
  event.role = required(fields, "role");
  event.object = optional(fields, "object");
  event.provenance = optional(fields, "provenance");
  event.context = optional(fields, "context");
  event.resolution = optional(fields, "resolution");
  event.peer_runtime = optional(fields, "peer_runtime");
  event.peer_component = optional(fields, "peer_component");
  return event;
}

void write_json_string(std::ostream &output, const std::string &value) {
  output << '"' << json_escape(value) << '"';
}

std::string dot_escape(const std::string &value) {
  std::string escaped;
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') {
      escaped.push_back('\\');
    }
    if (ch == '\n' || ch == '\r') {
      escaped.append("\\n");
    } else {
      escaped.push_back(ch);
    }
  }
  return escaped;
}

}  // namespace

std::string json_escape(const std::string &value) {
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
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(ch) << std::dec;
        } else {
          output << static_cast<char>(ch);
        }
    }
  }
  return output.str();
}

std::vector<RawEvent> read_trace(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open trace: " + path);
  }

  std::vector<RawEvent> events;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }
    try {
      events.push_back(parse_event(line));
    } catch (const std::exception &error) {
      throw std::runtime_error("trace line " + std::to_string(line_number) +
                               ": " + error.what());
    }
  }

  std::stable_sort(events.begin(), events.end(),
                   [](const RawEvent &left, const RawEvent &right) {
                     if (left.timestamp_ns != right.timestamp_ns) {
                       return left.timestamp_ns < right.timestamp_ns;
                     }
                     return left.sequence < right.sequence;
                   });
  return events;
}

void write_graph_json(const Graph &graph, std::ostream &output) {
  output << "{\n  \"schema\": \"nexus.demo.graph.v1\",\n";

  output << "  \"components\": [";
  for (std::size_t index = 0; index < graph.components.size(); ++index) {
    output << (index == 0 ? "\n    " : ",\n    ");
    write_json_string(output, graph.components[index]);
  }
  output << (graph.components.empty() ? "" : "\n  ") << "],\n";

  output << "  \"objects\": [";
  for (std::size_t index = 0; index < graph.objects.size(); ++index) {
    const auto &object = graph.objects[index];
    output << (index == 0 ? "\n    " : ",\n    ");
    output << "{\"id\":";
    write_json_string(output, object.id);
    output << ",\"mechanism\":";
    write_json_string(output, object.mechanism);
    output << '}';
  }
  output << (graph.objects.empty() ? "" : "\n  ") << "],\n";

  output << "  \"interactions\": [";
  for (std::size_t index = 0; index < graph.interactions.size(); ++index) {
    const auto &interaction = graph.interactions[index];
    output << (index == 0 ? "\n    " : ",\n    ");
    output << "{\"sequence\":" << interaction.raw.sequence;
    output << ",\"runtime\":";
    write_json_string(output, interaction.raw.runtime);
    output << ",\"actor\":";
    write_json_string(output, interaction.actor);
    output << ",\"component\":";
    write_json_string(output, interaction.component);
    output << ",\"mechanism\":";
    write_json_string(output, interaction.raw.mechanism);
    output << ",\"role\":";
    write_json_string(output, interaction.raw.role);
    output << ",\"object\":";
    write_json_string(output, interaction.canonical_object);
    output << ",\"peer_component\":";
    write_json_string(output, interaction.peer_component);
    output << ",\"provenance\":";
    write_json_string(output, interaction.raw.provenance);
    output << ",\"context\":";
    write_json_string(output, interaction.raw.context);
    output << ",\"resolution\":";
    write_json_string(output, interaction.raw.resolution);
    output << '}';
  }
  output << (graph.interactions.empty() ? "" : "\n  ") << "],\n";

  output << "  \"dependencies\": [";
  for (std::size_t index = 0; index < graph.dependencies.size(); ++index) {
    const auto &dependency = graph.dependencies[index];
    output << (index == 0 ? "\n    " : ",\n    ");
    output << "{\"source\":";
    write_json_string(output, dependency.source);
    output << ",\"target\":";
    write_json_string(output, dependency.target);
    output << ",\"kind\":";
    write_json_string(output, dependency.kind);
    output << ",\"mechanism\":";
    write_json_string(output, dependency.mechanism);
    output << ",\"object\":";
    write_json_string(output, dependency.object);
    output << ",\"resolution\":";
    write_json_string(output, dependency.resolution);
    output << ",\"provenance\":";
    write_json_string(output, dependency.provenance);
    output << ",\"evidence\":[";
    for (std::size_t evidence_index = 0;
         evidence_index < dependency.evidence.size(); ++evidence_index) {
      if (evidence_index != 0) {
        output << ',';
      }
      write_json_string(output, dependency.evidence[evidence_index]);
    }
    output << "]}";
  }
  output << (graph.dependencies.empty() ? "" : "\n  ") << "]\n}\n";
}

void write_graph_dot(const Graph &graph, std::ostream &output) {
  output << "digraph nexus_demo {\n"
         << "  rankdir=LR;\n"
         << "  graph [fontname=\"Helvetica\"];\n"
         << "  node [fontname=\"Helvetica\"];\n"
         << "  edge [fontname=\"Helvetica\"];\n";

  for (const auto &component : graph.components) {
    output << "  \"" << dot_escape(component)
           << "\" [shape=box,style=rounded];\n";
  }
  for (const auto &dependency : graph.dependencies) {
    output << "  \"" << dot_escape(dependency.source) << "\" -> \""
           << dot_escape(dependency.target) << "\" [label=\""
           << dot_escape(dependency.mechanism) << "\"];\n";
  }
  output << "}\n";
}

}  // namespace nexus::demo
