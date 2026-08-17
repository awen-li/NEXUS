cmake_minimum_required(VERSION 3.20)
project(nexus VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(
  NEXUS_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE
)
set(NEXUS_WORKLOAD_DIR "${NEXUS_REPOSITORY_ROOT}/workloads")
set(NEXUS_EXPECTED_OUTPUT_DIR "${NEXUS_REPOSITORY_ROOT}/expected-output")

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
find_program(GRAPHVIZ_DOT NAMES dot REQUIRED)

add_library(nexus_probe SHARED
  src/probe.cpp
)
target_include_directories(nexus_probe PUBLIC include)
target_compile_options(nexus_probe PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(nexus_probe PROPERTIES
  OUTPUT_NAME nexus_probe
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

add_library(nexus_core STATIC
  src/model.cpp
  src/runtime_adapter.cpp
  src/mechanism_analyzer.cpp
  src/dependency_composer.cpp
)
target_include_directories(nexus_core PUBLIC include)
target_compile_options(nexus_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(nexus-analyze src/main.cpp)
target_link_libraries(nexus-analyze PRIVATE nexus_core)
target_compile_options(nexus-analyze PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(nexus-analyze PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(
  nexus-demo-service
  "${NEXUS_WORKLOAD_DIR}/native/service.cpp"
)
target_include_directories(nexus-demo-service PRIVATE include)
target_link_libraries(nexus-demo-service PRIVATE nexus_probe ${CMAKE_DL_LIBS})
target_compile_options(nexus-demo-service PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(nexus-demo-service PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  BUILD_RPATH "${CMAKE_BINARY_DIR}/lib"
)

Python3_add_library(_nexus_bench_ext MODULE WITH_SOABI
  "${NEXUS_WORKLOAD_DIR}/native/extension.cpp"
)
target_include_directories(_nexus_bench_ext PRIVATE include)
target_link_libraries(_nexus_bench_ext PRIVATE nexus_probe ${CMAKE_DL_LIBS})
# PyMethodDef stores keyword-aware callbacks in the legacy PyCFunction slot;
# CPython's documented cast triggers GCC's incompatible-function-type warning.
target_compile_options(_nexus_bench_ext PRIVATE
  -Wall -Wextra -Wpedantic -Wno-cast-function-type
)
set_target_properties(_nexus_bench_ext PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/nexus_bench"
  BUILD_RPATH "${CMAKE_BINARY_DIR}/lib"
)

add_library(nexus_transform MODULE
  "${NEXUS_WORKLOAD_DIR}/native/plugin.cpp"
)
target_compile_options(nexus_transform PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(nexus_transform PROPERTIES
  OUTPUT_NAME nexus_transform
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)

add_custom_target(nexus-python-package ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${NEXUS_WORKLOAD_DIR}/cpython/nexus_bench"
          "${CMAKE_BINARY_DIR}/python/nexus_bench"
  DEPENDS _nexus_bench_ext
  COMMENT "Copying the NEXUS CPython workload package"
)

set(NEXUS_SUITE_COMMAND
  ${Python3_EXECUTABLE}
  ${NEXUS_WORKLOAD_DIR}/smoke_test.py
  --python ${Python3_EXECUTABLE}
  --package-dir ${CMAKE_BINARY_DIR}/python
  --probe-lib-dir ${CMAKE_BINARY_DIR}/lib
  --plugin $<TARGET_FILE:nexus_transform>
  --service $<TARGET_FILE:nexus-demo-service>
  --analyzer $<TARGET_FILE:nexus-analyze>
  --manifest ${NEXUS_WORKLOAD_DIR}/cases.json
  --graphviz ${GRAPHVIZ_DOT}
  --work-dir ${CMAKE_BINARY_DIR}/output
)

add_custom_target(run-demo
  COMMAND ${NEXUS_SUITE_COMMAND}
  DEPENDS nexus-analyze nexus-demo-service nexus_probe
          nexus-python-package nexus_transform
  USES_TERMINAL
)

add_custom_target(generate-expected-output
  COMMAND ${NEXUS_SUITE_COMMAND}
          --expected-output-dir ${NEXUS_EXPECTED_OUTPUT_DIR}
  DEPENDS nexus-analyze nexus-demo-service nexus_probe
          nexus-python-package nexus_transform
  USES_TERMINAL
)

if(BUILD_TESTING)
  add_test(NAME nexus_demo_end_to_end COMMAND ${NEXUS_SUITE_COMMAND})
endif()
