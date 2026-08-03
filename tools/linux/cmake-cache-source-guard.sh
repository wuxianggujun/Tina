#!/usr/bin/env bash
# Drop CMakeCache only when CMAKE_HOME_DIRECTORY no longer matches this checkout.
# Typical case: WSL /mnt/c/... cache reused under Docker /work/tina mount.
# Does NOT wipe build trees, object files, or vcpkg_installed.
set -euo pipefail

tina_guard_cmake_cache_source() {
  local build_dir="$1"
  local expected_source="$2"
  local cache="${build_dir}/CMakeCache.txt"
  if [[ ! -f "${cache}" ]]; then
    return 0
  fi
  local cached
  cached="$(grep '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${cache}" | head -n1 | cut -d= -f2- || true)"
  if [[ -z "${cached}" ]]; then
    return 0
  fi
  if [[ "${cached}" == "${expected_source}" ]]; then
    return 0
  fi
  echo "CMake cache source mismatch:"
  echo "  cached=${cached}"
  echo "  expected=${expected_source}"
  echo "  action=remove CMakeCache only (keep objects/vcpkg_installed)"
  rm -f "${cache}"
  rm -f "${build_dir}/CMakeFiles/cmake.check_cache" 2>/dev/null || true
  rm -f "${build_dir}/CMakeFiles/CMakeConfigureLog.yaml" 2>/dev/null || true
}
