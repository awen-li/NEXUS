#include "nexus_demo/dependency_composer.hpp"
#include "nexus_demo/model.hpp"

#include <fstream>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
  std::string trace;
  std::string json_output;
  std::string dot_output;
};

void print_usage(std::ostream &output, const char *program) {
  output << "Usage: " << program
         << " --trace TRACE.jsonl [--json GRAPH.json] [--dot GRAPH.dot]\n";
}

Options parse_options(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      print_usage(std::cout, argv[0]);
      std::exit(0);
    }

    if (index + 1 >= argc) {
      throw std::runtime_error("missing value after " + argument);
    }
    const std::string value = argv[++index];
    if (argument == "--trace") {
      options.trace = value;
    } else if (argument == "--json") {
      options.json_output = value;
    } else if (argument == "--dot") {
      options.dot_output = value;
    } else {
      throw std::runtime_error("unknown option: " + argument);
    }
  }

  if (options.trace.empty()) {
    throw std::runtime_error("--trace is required");
  }
  return options;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    const Options options = parse_options(argc, argv);
    const auto events = nexus::demo::read_trace(options.trace);
    auto composer = nexus::demo::make_default_composer();
    const auto graph = composer.compose(events);

    if (!options.json_output.empty()) {
      std::ofstream output(options.json_output);
      if (!output) {
        throw std::runtime_error("cannot open JSON output: " +
                                 options.json_output);
      }
      nexus::demo::write_graph_json(graph, output);
    }

    if (!options.dot_output.empty()) {
      std::ofstream output(options.dot_output);
      if (!output) {
        throw std::runtime_error("cannot open DOT output: " +
                                 options.dot_output);
      }
      nexus::demo::write_graph_dot(graph, output);
    }

    std::cout << "NEXUS demo analyzed " << graph.interactions.size()
              << " normalized interactions and recovered "
              << graph.dependencies.size() << " dependencies.\n";
    for (const auto &dependency : graph.dependencies) {
      std::cout << "  " << dependency.source << " -> " << dependency.target
                << " [" << dependency.kind << ", "
                << dependency.mechanism << ", "
                << dependency.resolution << "]\n";
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "nexus-analyze: " << error.what() << '\n';
    print_usage(std::cerr, argc > 0 ? argv[0] : "nexus-analyze");
    return 1;
  }
}
