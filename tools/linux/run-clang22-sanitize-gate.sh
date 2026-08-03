#!/usr/bin/env bash
# TEST-001 Linux Clang22 ASan/UBSan/LSan Null graph.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"
# shellcheck source=cmake-cache-source-guard.sh
source "${ROOT}/tools/linux/cmake-cache-source-guard.sh"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
export LSAN_OPTIONS="${LSAN_OPTIONS:-exitcode=23}"

echo "=== TEST-001 Linux Clang22 sanitizer Null gate ==="
echo "root=${ROOT}"
echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname=$(uname -a)"
echo "clang=$(clang-22 --version | head -n1 || clang --version | head -n1)"
echo "gxx15=$(g++-15 --version | head -n1)"
echo "cmake=$(cmake --version | head -n1)"
echo "ASAN_OPTIONS=${ASAN_OPTIONS}"
echo "UBSAN_OPTIONS=${UBSAN_OPTIONS}"
echo "LSAN_OPTIONS=${LSAN_OPTIONS}"
echo "VCPKG_ROOT=${VCPKG_ROOT}"
echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo unknown)"

BUILD_DIR="${ROOT}/out/build/linux-clang22-vnext-sanitize"
tina_guard_cmake_cache_source "${BUILD_DIR}" "${ROOT}"

cmake --preset linux-clang22-vnext-sanitize
cmake --build --preset linux-clang22-vnext-sanitize-debug \
  --target tina_tests tina_ui_tests tina_runtime_ui_tests tina_ui_render_integration_tests tina_sample_null

BIN="${BUILD_DIR}/bin"
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
  echo "TEST-001 Linux Clang22 sanitizer Null gate FAILED"
  exit 1
fi
echo "TEST-001 Linux Clang22 sanitizer Null gate OK"
exit 0
