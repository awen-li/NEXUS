#!/usr/bin/env bash
set -euo pipefail

nexus_repository_root=$(
  cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
  pwd
)
nexus_build_dir=${NEXUS_BUILD_DIR:-"${nexus_repository_root}/build"}
nexus_build_type=${CMAKE_BUILD_TYPE:-Debug}

cmake \
  -S "${nexus_repository_root}/demo" \
  -B "${nexus_build_dir}" \
  -DCMAKE_BUILD_TYPE="${nexus_build_type}" \
  -DBUILD_TESTING=ON
cmake --build "${nexus_build_dir}" --parallel
