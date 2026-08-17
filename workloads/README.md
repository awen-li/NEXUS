# Workloads

The workload suite is defined by `cases.json`, which records each benchmark's
name, design, expected interaction/process counts, and exact dependency edges.

- `cpython/nexus_bench/` contains the Python drivers.
- `native/extension.cpp` implements the CPython C++ boundary.
- `native/plugin.cpp` is the dynamically loaded transform component.
- `native/service.cpp` is the standalone Unix-domain socket service.
- `smoke_test.py` runs every case in isolation and validates its recovered
  graph against `cases.json`.

Use `../scripts/run-demo.sh` rather than invoking the service manually.
