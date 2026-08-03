#!/usr/bin/env bash
# TEST-001 Linux GCC13 GLFW/X11 platform graph (headless via Xvfb when available).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"
# shellcheck source=cmake-cache-source-guard.sh
source "${ROOT}/tools/linux/cmake-cache-source-guard.sh"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

echo "=== TEST-001 Linux GCC13 Platform (GLFW/X11) gate ==="
echo "root=${ROOT}"
echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname=$(uname -a)"
echo "gcc=$(gcc-13 --version | head -n1 || gcc --version | head -n1)"
echo "cmake=$(cmake --version | head -n1)"
echo "VCPKG_ROOT=${VCPKG_ROOT}"
echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
echo "DISPLAY=${DISPLAY:-unset}"

BUILD_DIR="${ROOT}/out/build/linux-gcc13-vnext-platform"
tina_guard_cmake_cache_source "${BUILD_DIR}" "${ROOT}"

cmake --preset linux-gcc13-vnext-platform
cmake --build --preset linux-gcc13-vnext-platform-debug \
  --target tina_tests tina_platform_glfw_tests tina_sample_platform

BIN="${BUILD_DIR}/bin"
fail=0
run_one() {
  local name="$1"
  shift
  echo "=== RUN ${name} ==="
  if command -v xvfb-run >/dev/null 2>&1; then
    if xvfb-run -a "${BIN}/${name}" "$@"; then
      echo "=== OK ${name} exit=0 (xvfb) ==="
    else
      local code=$?
      echo "=== FAIL ${name} exit=${code} (xvfb) ==="
      fail=1
    fi
  else
    if "${BIN}/${name}" "$@"; then
      echo "=== OK ${name} exit=0 ==="
    else
      local code=$?
      echo "=== FAIL ${name} exit=${code} ==="
      fail=1
    fi
  fi
}

run_one tina_tests --gtest_color=no
run_one tina_platform_glfw_tests --gtest_color=no
run_one tina_sample_platform --frames=60 --frame-delay-ms=0

if [[ "${fail}" -ne 0 ]]; then
  echo "TEST-001 Linux GCC13 Platform gate FAILED"
  exit 1
fi
echo "TEST-001 Linux GCC13 Platform gate OK"
exit 0
