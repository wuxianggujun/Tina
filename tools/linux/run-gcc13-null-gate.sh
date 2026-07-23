#!/usr/bin/env bash
# TEST-001 Linux GCC13 Null graph: configure, build, run GoogleTests + sample_null.
# Run inside docker/linux-gcc13 (or any Linux host with GCC13 + VCPKG_ROOT).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

echo "=== TEST-001 Linux GCC13 Null gate ==="
echo "root=${ROOT}"
echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname=$(uname -a)"
echo "gcc=$(gcc-13 --version | head -n1 || gcc --version | head -n1)"
echo "gxx=$(g++-13 --version | head -n1 || g++ --version | head -n1)"
echo "cmake=$(cmake --version | head -n1)"
echo "ninja=$(ninja --version 2>/dev/null || true)"
echo "VCPKG_ROOT=${VCPKG_ROOT}"
echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo unknown)"

cmake --preset linux-gcc13-vnext
cmake --build --preset linux-gcc13-vnext-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null

BIN="${ROOT}/out/build/linux-gcc13-vnext/bin"
fail=0
run_one() {
  local name="$1"
  shift
  echo "=== RUN ${name} ==="
  if "${BIN}/${name}" "$@"; then
    echo "=== OK ${name} exit=0 ==="
  else
    local code=$?
    echo "=== FAIL ${name} exit=${code} ==="
    fail=1
  fi
}

run_one tina_tests --gtest_color=no
run_one tina_ui_tests --gtest_color=no
run_one tina_runtime_ui_tests --gtest_color=no
run_one tina_ui_render_integration_tests --gtest_color=no
run_one tina_sample_null --frames=300

if [[ "${fail}" -ne 0 ]]; then
  echo "TEST-001 Linux GCC13 Null gate FAILED"
  exit 1
fi
echo "TEST-001 Linux GCC13 Null gate OK"
exit 0
