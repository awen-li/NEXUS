# Demo implementation

This directory contains the NEXUS vertical-slice implementation:

- `include/nexus_demo/probe.h`: stable C event-emission API;
- `src/runtime_adapter.cpp`: runtime-domain normalization;
- `src/mechanism_analyzer.cpp`: shared mechanism semantics;
- `src/dependency_composer.cpp`: evidence-backed graph construction;
- `src/main.cpp`: `nexus-analyze` command-line interface.

`CMakeLists.txt` also builds the native components under `../workloads/` and
registers the manifest-driven suite with CTest. For normal use, invoke the
helpers at the repository root:

```sh
./scripts/run-demo.sh
./scripts/test.sh
```
