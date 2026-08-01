#!/usr/bin/env bash
# SDK-001 Linux installed-prefix consumer gate.
# Defaults to the GCC13 Null graph; override the preset/build directory through
# TINA_SDK_CONFIGURE_PRESET and TINA_SDK_BUILD_DIRECTORY when validating another toolchain.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

CONSUMER="${TINA_SDK_CONSUMER:-GameSDK}"
case "${CONSUMER}" in
  GameSDK)
    DEFAULT_CONFIGURE_PRESET="linux-gcc13-vnext"
    CONSUMER_DIRECTORY_NAME="sdk-consumer"
    CONSUMER_SOURCE_DIRECTORY="${ROOT}/tests/sdk_consumer"
    CONSUMER_TARGET="tina_sdk_consumer"
    ;;
  PlatformGlfw)
    DEFAULT_CONFIGURE_PRESET="linux-gcc13-vnext-platform"
    CONSUMER_DIRECTORY_NAME="sdk-platform-glfw-consumer"
    CONSUMER_SOURCE_DIRECTORY="${ROOT}/tests/sdk_consumer_platform_glfw"
    CONSUMER_TARGET="tina_sdk_platform_glfw_consumer"
    ;;
  *)
    echo "TINA_SDK_CONSUMER must be GameSDK or PlatformGlfw, got '${CONSUMER}'" >&2
    exit 2
    ;;
esac

CONFIGURE_PRESET="${TINA_SDK_CONFIGURE_PRESET:-${DEFAULT_CONFIGURE_PRESET}}"
BUILD_DIRECTORY_INPUT="${TINA_SDK_BUILD_DIRECTORY:-${ROOT}/out/build/${CONFIGURE_PRESET}}"
CONFIGURATION="${TINA_SDK_CONFIGURATION:-Debug}"
BUILD_JOBS="${TINA_SDK_BUILD_JOBS:-2}"

if [[ ! "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "TINA_SDK_BUILD_JOBS must be a positive integer, got '${BUILD_JOBS}'" >&2
  exit 2
fi

echo "=== SDK-001 Linux installed-prefix consumer gate ==="
echo "root=${ROOT}"
echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname=$(uname -a)"
echo "cmake=$(cmake --version | head -n1)"
echo "preset=${CONFIGURE_PRESET}"
echo "configuration=${CONFIGURATION}"
echo "consumer=${CONSUMER}"
echo "VCPKG_ROOT=${VCPKG_ROOT}"
echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo unknown)"

cmake --preset "${CONFIGURE_PRESET}" -B "${BUILD_DIRECTORY_INPUT}"

if [[ ! -f "${BUILD_DIRECTORY_INPUT}/CMakeCache.txt" ]]; then
  echo "Tina build tree is not configured: ${BUILD_DIRECTORY_INPUT}" >&2
  exit 1
fi

BUILD_DIRECTORY="$(cd "${BUILD_DIRECTORY_INPUT}" && pwd -P)"
INSTALL_PREFIX="${BUILD_DIRECTORY}/${CONSUMER_DIRECTORY_NAME}-prefix"
CONSUMER_BUILD_DIRECTORY="${BUILD_DIRECTORY}/${CONSUMER_DIRECTORY_NAME}-build"
MISSING_COMPONENT_BUILD_DIRECTORY="${BUILD_DIRECTORY}/${CONSUMER_DIRECTORY_NAME}-missing-component-build"
VERIFICATION_SCRIPT="${ROOT}/cmake/VerifyInstalledTinaSdkHeaders.cmake"
CACHE_PATH="${BUILD_DIRECTORY}/CMakeCache.txt"

case "${INSTALL_PREFIX}" in
  "${BUILD_DIRECTORY}"/*) ;;
  *)
    echo "Refusing to refresh SDK prefix outside the Tina build tree: ${INSTALL_PREFIX}" >&2
    exit 1
    ;;
esac
case "${CONSUMER_BUILD_DIRECTORY}" in
  "${BUILD_DIRECTORY}"/*) ;;
  *)
    echo "Refusing to refresh consumer build outside the Tina build tree: ${CONSUMER_BUILD_DIRECTORY}" >&2
    exit 1
    ;;
esac

cache_value() {
  local key="$1"
  awk -v prefix="${key}:" '
    index($0, prefix) == 1 {
      sub(/^[^=]*=/, "")
      print
      exit
    }
  ' "${CACHE_PATH}"
}

GENERATOR="$(cache_value CMAKE_GENERATOR)"
if [[ -z "${GENERATOR}" ]]; then
  echo "CMAKE_GENERATOR is missing from ${CACHE_PATH}" >&2
  exit 1
fi

rm -rf -- "${INSTALL_PREFIX}" "${CONSUMER_BUILD_DIRECTORY}" "${MISSING_COMPONENT_BUILD_DIRECTORY}"

sdk_build_targets=(tina_runtime tina_scene tina_asset)
if [[ "${CONSUMER}" == "PlatformGlfw" ]]; then
  sdk_build_targets+=(tina_platform_glfw)
fi
cmake --build "${BUILD_DIRECTORY}" --config "${CONFIGURATION}" \
  --target "${sdk_build_targets[@]}" --parallel "${BUILD_JOBS}"
cmake --install "${BUILD_DIRECTORY}" --config "${CONFIGURATION}" --prefix "${INSTALL_PREFIX}"
cmake "-DTINA_SDK_INCLUDE_DIR=${INSTALL_PREFIX}/include" -P "${VERIFICATION_SCRIPT}"

common_configure_arguments=(
  -G "${GENERATOR}"
  "-DCMAKE_BUILD_TYPE=${CONFIGURATION}"
  "-DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}"
)

for cache_key in \
  CMAKE_CXX_COMPILER \
  CMAKE_TOOLCHAIN_FILE \
  VCPKG_CHAINLOAD_TOOLCHAIN_FILE \
  VCPKG_INSTALLED_DIR \
  VCPKG_TARGET_TRIPLET \
  VCPKG_HOST_TRIPLET; do
  cache_entry="$(cache_value "${cache_key}")"
  if [[ -n "${cache_entry}" ]]; then
    common_configure_arguments+=("-D${cache_key}=${cache_entry}")
  fi
done

configure_arguments=(
  -S "${CONSUMER_SOURCE_DIRECTORY}"
  -B "${CONSUMER_BUILD_DIRECTORY}"
  "-DTINA_EXPECTED_INSTALL_PREFIX=${INSTALL_PREFIX}"
  "-DTINA_FORBIDDEN_SOURCE_DIR=${ROOT}/include"
  "${common_configure_arguments[@]}"
)
cmake "${configure_arguments[@]}"

missing_components="DefinitelyMissing"
if [[ "${CONSUMER}" == "GameSDK" ]]; then
  missing_components="PlatformGlfw;${missing_components}"
fi
cmake \
  -S "${ROOT}/tests/sdk_consumer_missing_component" \
  -B "${MISSING_COMPONENT_BUILD_DIRECTORY}" \
  "-DTINA_EXPECT_MISSING_COMPONENTS=${missing_components}" \
  "${common_configure_arguments[@]}"

cmake --build "${CONSUMER_BUILD_DIRECTORY}" --config "${CONFIGURATION}" \
  --target "${CONSUMER_TARGET}" --parallel "${BUILD_JOBS}"

consumer_executable=""
for candidate in \
  "${CONSUMER_BUILD_DIRECTORY}/${CONSUMER_TARGET}" \
  "${CONSUMER_BUILD_DIRECTORY}/bin/${CONSUMER_TARGET}" \
  "${CONSUMER_BUILD_DIRECTORY}/bin/${CONFIGURATION}/${CONSUMER_TARGET}" \
  "${CONSUMER_BUILD_DIRECTORY}/${CONFIGURATION}/${CONSUMER_TARGET}"; do
  if [[ -x "${candidate}" ]]; then
    consumer_executable="${candidate}"
    break
  fi
done
if [[ -z "${consumer_executable}" ]]; then
  echo "Installed SDK consumer executable was not produced under ${CONSUMER_BUILD_DIRECTORY}" >&2
  exit 1
fi

if [[ "${CONSUMER}" == "PlatformGlfw" ]]; then
  if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "${consumer_executable}"
  elif [[ -n "${DISPLAY:-}" ]]; then
    "${consumer_executable}"
  else
    echo "PlatformGlfw consumer requires xvfb-run or DISPLAY" >&2
    exit 1
  fi
else
  "${consumer_executable}"
fi
echo "SDK-001 Linux installed-prefix consumer gate OK"
