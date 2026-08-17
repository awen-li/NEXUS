#!/usr/bin/env bash
set -euo pipefail

nexus_repository_root=$(
  cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
  pwd
)
nexus_build_dir=${NEXUS_BUILD_DIR:-"${nexus_repository_root}/build"}

"${nexus_repository_root}/scripts/build.sh"
cmake --build "${nexus_build_dir}" --target generate-expected-output
