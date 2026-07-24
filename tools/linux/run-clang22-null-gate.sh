#!/usr/bin/env bash
# TEST-001 Linux Clang22 Null graph (libstdc++15 via chainload toolchain).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

echo "=== TEST-001 Linux Clang22 Null gate ==="
echo "root=${ROOT}"
echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname=$(uname -a)"
echo "clang=$(clang-22 --version | head -n1 || clang --version | head -n1)"
echo "gxx15=$(g++-15 --version | head -n1)"
echo "cmake=$(cmake --version | head -n1)"
echo "VCPKG_ROOT=${VCPKG_ROOT}"
echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo unknown)"

cmake --preset linux-clang22-vnext
cmake --build --preset linux-clang22-vnext-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null

BIN="${ROOT}/out/build/linux-clang22-vnext/bin"
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
  echo "TEST-001 Linux Clang22 Null gate FAILED"
  exit 1
fi
echo "TEST-001 Linux Clang22 Null gate OK"
exit 0
