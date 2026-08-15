#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

cd "${ROOT_DIR}"

echo "==> Pull latest source"
git pull --ff-only

echo "==> Configure"
CMAKE_EXTRA=()
if command -v brew >/dev/null 2>&1; then
    if OPENSSL_PREFIX="$(brew --prefix openssl@3 2>/dev/null)"; then
        CMAKE_EXTRA+=("-DOPENSSL_ROOT_DIR=${OPENSSL_PREFIX}")
        echo "==> Using Homebrew OpenSSL: ${OPENSSL_PREFIX}"
    fi
fi
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DGWL_AIRPLAY2_BUILD_DEMO=ON "${CMAKE_EXTRA[@]}"

echo "==> Build"
cmake --build "${BUILD_DIR}" --config Debug -j"${JOBS}"

echo ""
echo "============================================================"
echo " GWL AirPlay 2 - macOS terminal receiver"
echo "============================================================"
echo " Press Ctrl+C to stop the receiver."
echo ""

EXEC="${BUILD_DIR}/gwl-airplay2-macos-demo"
if [[ ! -x "${EXEC}" ]]; then
    echo "ERROR: receiver executable was not built: ${EXEC}" >&2
    exit 1
fi

trap 'echo; echo "==> Receiver stopped"; exit 130' INT TERM
exec "${EXEC}"
