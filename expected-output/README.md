# Expected output

Each benchmark directory contains:

- `dependencies.json`: the stable expected source, target, and mechanism set;
- `graph.dot`: Graphviz representation emitted by `nexus-analyze`;
- `graph.svg`: rendered dependency graph embedded by the root README.

Raw traces and full analyzed JSON graphs are intentionally generated only
under `build/output/` because they contain execution-specific process IDs,
timestamps, and build paths.

Regenerate these stable artifacts with:

```sh
./scripts/generate-expected-output.sh
```
