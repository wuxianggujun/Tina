#!/usr/bin/env bash
# SDK-001 cross-distribution producer: Ubuntu 24.04 / GCC 13 -> relocatable GameSDK archive.
set -euo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
cd "${ROOT}"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

BUILD_JOBS="${TINA_SDK_BUILD_JOBS:-2}"
BUILD_DIRECTORY="$(realpath -m "${TINA_CROSS_DISTRO_BUILD_DIR:-${ROOT}/out/build/sdk-cross-distro-producer}")"
OUTPUT_DIRECTORY="$(realpath -m "${TINA_CROSS_DISTRO_OUTPUT_DIR:-/output}")"
PACKAGE_ROOT="tina-sdk-0.0.1-linux-x64-release"
ARCHIVE_NAME="${PACKAGE_ROOT}.tar.gz"
CHECKSUM_NAME="${ARCHIVE_NAME}.sha256"
METADATA_NAME="${PACKAGE_ROOT}.metadata.json"

if [[ ! "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "TINA_SDK_BUILD_JOBS must be a positive integer, got '${BUILD_JOBS}'" >&2
  exit 2
fi
export VCPKG_MAX_CONCURRENCY="${BUILD_JOBS}"
if [[ "${BUILD_DIRECTORY}" != "${ROOT}/out/build/"* ]]; then
  echo "TINA_CROSS_DISTRO_BUILD_DIR must be inside ${ROOT}/out/build" >&2
  exit 2
fi
if [[ "${OUTPUT_DIRECTORY}" != /* ]]; then
  echo "TINA_CROSS_DISTRO_OUTPUT_DIR must be an absolute path" >&2
  exit 2
fi
if [[ "${OUTPUT_DIRECTORY}" == "/" ]]; then
  echo "TINA_CROSS_DISTRO_OUTPUT_DIR must not be the filesystem root" >&2
  exit 2
fi

source /etc/os-release
if [[ "${ID:-}" != "ubuntu" || "${VERSION_ID:-}" != "24.04" ]]; then
  echo "The producer must run on Ubuntu 24.04, got ${ID:-unknown} ${VERSION_ID:-unknown}" >&2
  exit 1
fi

COMPILER_VERSION="$(g++-13 -dumpfullversion -dumpversion)"
if [[ "${COMPILER_VERSION}" != 13.* ]]; then
  echo "The producer must use GCC 13, got ${COMPILER_VERSION}" >&2
  exit 1
fi

STAGING_PREFIX="${BUILD_DIRECTORY}/sdk-cross-distro-staging-prefix"
PACKAGE_PARENT="${BUILD_DIRECTORY}/sdk-cross-distro-artifact"
RELOCATED_PREFIX="${PACKAGE_PARENT}/${PACKAGE_ROOT}"
OUTPUT_TMP_DIRECTORY="${OUTPUT_DIRECTORY}/.tina-sdk-cross-distro-tmp"

for managed_directory in \
  "${BUILD_DIRECTORY}" \
  "${STAGING_PREFIX}" \
  "${PACKAGE_PARENT}"; do
  case "${managed_directory}" in
    "${ROOT}/out/build/"*) ;;
    *)
      echo "Refusing to refresh directory outside ${ROOT}/out/build: ${managed_directory}" >&2
      exit 1
      ;;
  esac
done
case "${OUTPUT_TMP_DIRECTORY}" in
  "${OUTPUT_DIRECTORY}/.tina-sdk-cross-distro-tmp") ;;
  *)
    echo "Refusing to refresh unexpected output temporary directory: ${OUTPUT_TMP_DIRECTORY}" >&2
    exit 1
    ;;
esac

echo "=== SDK-001 cross-distribution producer gate ==="
echo "source=${ROOT}"
echo "build=${BUILD_DIRECTORY}"
echo "output=${OUTPUT_DIRECTORY}"
echo "distribution=${ID} ${VERSION_ID}"
echo "compiler=g++ ${COMPILER_VERSION}"
echo "cmake=$(cmake --version | head -n1)"
echo "jobs=${BUILD_JOBS}"

# Preserve the gate-owned CMake/vcpkg cache while refreshing only transient
# install and packaging directories. Drop CMakeCache when the absolute source
# root changed (WSL /mnt/c vs Docker /work/tina) so -ffile-prefix-map is reapplied.
# shellcheck source=cmake-cache-source-guard.sh
source "${ROOT}/tools/linux/cmake-cache-source-guard.sh"
tina_guard_cmake_cache_source "${BUILD_DIRECTORY}" "${ROOT}"

mkdir -p -- "${BUILD_DIRECTORY}" "${OUTPUT_DIRECTORY}"
rm -rf -- "${STAGING_PREFIX}" "${PACKAGE_PARENT}" "${OUTPUT_TMP_DIRECTORY}"

# Force a clean object graph under the producer tree so path-remapped objects
# replace any previously compiled absolute-path .o files.
if [[ -d "${BUILD_DIRECTORY}/src" ]]; then
  echo "Refreshing producer object trees for path-remapped Release objects"
  find "${BUILD_DIRECTORY}/src" -type f \( -name '*.o' -o -name '*.a' \) -delete
fi

cmake --preset linux-gcc13-vnext \
  -B "${BUILD_DIRECTORY}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTINA_BUILD_EXAMPLES=OFF \
  -DTINA_BUILD_TESTING=OFF \
  -DTINA_BUILD_PLATFORM_GLFW=OFF \
  -DTINA_BUILD_RENDER_BGFX=OFF \
  -DTINA_BUILD_UI_FREETYPE=OFF \
  -DTINA_BUILD_AUDIO_MINIAUDIO=OFF \
  -DVCPKG_MANIFEST_FEATURES=

cmake --build "${BUILD_DIRECTORY}" \
  --target tina_runtime tina_scene tina_asset \
  --parallel "${BUILD_JOBS}"
cmake --install "${BUILD_DIRECTORY}" --prefix "${STAGING_PREFIX}"

cmake \
  "-DTINA_SDK_INCLUDE_DIR=${STAGING_PREFIX}/include" \
  -DTINA_EXPECT_AUDIO_MINIAUDIO=OFF \
  -P "${ROOT}/cmake/VerifyInstalledTinaSdkHeaders.cmake"

mkdir -p -- "${PACKAGE_PARENT}"
mv -- "${STAGING_PREFIX}" "${RELOCATED_PREFIX}"
cmake \
  "-DTINA_SDK_RELOCATED_PREFIX=${RELOCATED_PREFIX}" \
  "-DTINA_SDK_ORIGINAL_PREFIX=${STAGING_PREFIX}" \
  "-DTINA_FORBIDDEN_BUILD_DIR=${BUILD_DIRECTORY}" \
  "-DTINA_FORBIDDEN_SOURCE_DIR=${ROOT}" \
  -P "${ROOT}/cmake/VerifyRelocatedTinaSdkPackage.cmake"

rm -rf -- "${OUTPUT_TMP_DIRECTORY}"
mkdir -p -- "${OUTPUT_TMP_DIRECTORY}"
tar -C "${PACKAGE_PARENT}" -czf "${OUTPUT_TMP_DIRECTORY}/${ARCHIVE_NAME}" "${PACKAGE_ROOT}"

ARCHIVE_SHA256="$(sha256sum "${OUTPUT_TMP_DIRECTORY}/${ARCHIVE_NAME}" | awk '{print $1}')"
printf '%s  %s\n' "${ARCHIVE_SHA256}" "${ARCHIVE_NAME}" \
  > "${OUTPUT_TMP_DIRECTORY}/${CHECKSUM_NAME}"

SOURCE_COMMIT="$(git rev-parse HEAD 2>/dev/null || printf unknown)"
CREATED_AT_UTC="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
CMAKE_VERSION="$(cmake --version | awk 'NR == 1 {print $3}')"
python3 - \
  "${OUTPUT_TMP_DIRECTORY}/${METADATA_NAME}" \
  "${ARCHIVE_NAME}" \
  "${ARCHIVE_SHA256}" \
  "${PACKAGE_ROOT}" \
  "${ID}" \
  "${VERSION_ID}" \
  "${COMPILER_VERSION}" \
  "${CMAKE_VERSION}" \
  "${SOURCE_COMMIT}" \
  "${ROOT}" \
  "${BUILD_DIRECTORY}" \
  "${STAGING_PREFIX}" \
  "${RELOCATED_PREFIX}" \
  "${CREATED_AT_UTC}" <<'PY'
import json
import sys

(
    output_path,
    archive,
    sha256,
    package_root,
    distribution,
    distribution_version,
    compiler_version,
    cmake_version,
    commit,
    source_dir,
    build_dir,
    staging_prefix,
    package_prefix,
    created_at_utc,
) = sys.argv[1:]

metadata = {
    "schema": 1,
    "package_name": "Tina",
    "package_version": "0.0.1",
    "component": "GameSDK",
    "build_type": "Release",
    "platform": "linux-x64",
    "archive": archive,
    "sha256": sha256,
    "package_root": package_root,
    "producer_distribution": distribution,
    "producer_distribution_version": distribution_version,
    "producer_compiler": "g++",
    "producer_compiler_version": compiler_version,
    "producer_cmake_version": cmake_version,
    "producer_commit": commit,
    "producer_source_dir": source_dir,
    "producer_build_dir": build_dir,
    "producer_staging_prefix": staging_prefix,
    "producer_package_prefix": package_prefix,
    "created_at_utc": created_at_utc,
}
with open(output_path, "w", encoding="utf-8", newline="\n") as metadata_file:
    json.dump(metadata, metadata_file, ensure_ascii=True, indent=2, sort_keys=True)
    metadata_file.write("\n")
PY

# Rename within one volume so consumers never observe partially-written files.
mv -f -- \
  "${OUTPUT_TMP_DIRECTORY}/${ARCHIVE_NAME}" \
  "${OUTPUT_TMP_DIRECTORY}/${CHECKSUM_NAME}" \
  "${OUTPUT_TMP_DIRECTORY}/${METADATA_NAME}" \
  "${OUTPUT_DIRECTORY}/"
rm -rf -- "${OUTPUT_TMP_DIRECTORY}"

echo "archive=${OUTPUT_DIRECTORY}/${ARCHIVE_NAME}"
echo "sha256=${ARCHIVE_SHA256}"
echo "metadata=${OUTPUT_DIRECTORY}/${METADATA_NAME}"
echo "SDK-001 cross-distribution producer gate OK"
