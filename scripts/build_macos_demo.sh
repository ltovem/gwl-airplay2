#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/macos-demo"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script must be run on macOS." >&2
  exit 1
fi

command -v cmake >/dev/null 2>&1 || { echo "CMake is required." >&2; exit 1; }

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DGWL_AIRPLAY2_BUILD_DEMO=ON \
  -DGWL_AIRPLAY2_BUILD_TESTS=ON

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --target gwl-airplay2-macos-demo --parallel "$(sysctl -n hw.logicalcpu)"

ctest --test-dir "${BUILD_DIR}" -C "${BUILD_TYPE}" --output-on-failure

echo
echo "Starting gwl-airplay2-macos-demo..."
echo "Press Ctrl-C to stop."
exec "${BUILD_DIR}/gwl-airplay2-macos-demo"
