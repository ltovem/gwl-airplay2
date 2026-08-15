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

if [[ ! -f "${ROOT_DIR}/third_party/alac/codec/EndianPortable.c" ]]; then
  echo "ALAC submodule is missing or incomplete; initializing it..."
  git -C "${ROOT_DIR}" submodule sync --recursive
  git -C "${ROOT_DIR}" submodule update --init --recursive
fi

if [[ ! -f "${ROOT_DIR}/third_party/alac/codec/EndianPortable.c" ]]; then
  echo "ERROR: ALAC decoder sources are still missing." >&2
  echo "Run: git submodule update --init --recursive" >&2
  exit 1
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DGWL_AIRPLAY2_BUILD_DEMO=ON \
  -DGWL_AIRPLAY2_BUILD_TESTS=ON

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" \
  --target gwl-airplay2-macos-ui gwl-airplay2-macos-demo gwl-airplay2-tests \
  --parallel "$(sysctl -n hw.logicalcpu)"

ctest --test-dir "${BUILD_DIR}" -C "${BUILD_TYPE}" --output-on-failure

APP_PATH="${BUILD_DIR}/gwl-airplay2-macos-ui.app"
if [[ ! -d "${APP_PATH}" ]]; then
  echo "ERROR: macOS UI bundle was not produced: ${APP_PATH}" >&2
  exit 1
fi

echo
echo "Launching GWL AirPlay Receiver UI..."
open -W "${APP_PATH}"
