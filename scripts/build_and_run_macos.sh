#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

cd "${ROOT_DIR}"

echo "==> GWL AirPlay 2: pull latest source"
git pull --ff-only

echo "==> GWL AirPlay 2: configure"
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DGWL_AIRPLAY2_BUILD_DEMO=ON

echo "==> GWL AirPlay 2: build (${JOBS} jobs)"
cmake --build "${BUILD_DIR}" --config Debug -j"${JOBS}"

echo "==> GWL AirPlay 2: start macOS UI"
APP="${BUILD_DIR}/gwl-airplay2-macos-ui.app"
if [[ ! -d "${APP}" ]]; then
    echo "ERROR: macOS UI application was not built: ${APP}" >&2
    exit 1
fi

open "${APP}"
echo "==> Started: ${APP}"
