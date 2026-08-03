#!/usr/bin/env bash
# SDK-001 cross-distribution consumer: Debian 13 / GCC 14 consumes a read-only producer artifact.
set -euo pipefail
export LC_ALL=C

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"

INPUT_DIRECTORY="$(realpath -m "${TINA_CROSS_DISTRO_INPUT_DIR:-/input}")"
WORK_ROOT="$(realpath -m "${TINA_CROSS_DISTRO_WORK_DIR:-/work/cross-distro-gate}")"
CONSUMER_SOURCE_DIRECTORY="$(realpath -m "${TINA_SDK_CONSUMER_SOURCE_DIR:-/opt/tina-sdk-consumer}")"
VERIFICATION_DIRECTORY="$(realpath -m "${TINA_SDK_VERIFICATION_DIR:-/opt/tina-sdk-gate/cmake}")"
BUILD_JOBS="${TINA_SDK_BUILD_JOBS:-2}"
PACKAGE_ROOT="tina-sdk-0.0.1-linux-x64-release"
ARCHIVE_NAME="${PACKAGE_ROOT}.tar.gz"
CHECKSUM_NAME="${ARCHIVE_NAME}.sha256"
METADATA_NAME="${PACKAGE_ROOT}.metadata.json"
EXTRACT_DIRECTORY="${WORK_ROOT}/extracted"
CONSUMER_BUILD_DIRECTORY="${WORK_ROOT}/build"
TMP_DIRECTORY="${WORK_ROOT}/tmp"
RELOCATED_PREFIX="${EXTRACT_DIRECTORY}/${PACKAGE_ROOT}"

if [[ ! "${BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
  echo "TINA_SDK_BUILD_JOBS must be a positive integer, got '${BUILD_JOBS}'" >&2
  exit 2
fi
export VCPKG_MAX_CONCURRENCY="${BUILD_JOBS}"
if [[ "${INPUT_DIRECTORY}" != /* || "${WORK_ROOT}" != /* ]]; then
  echo "Input and work directories must be absolute paths" >&2
  exit 2
fi
if [[ "${WORK_ROOT}" == "/" ]]; then
  echo "TINA_CROSS_DISTRO_WORK_DIR must not be the filesystem root" >&2
  exit 2
fi
for managed_directory in "${EXTRACT_DIRECTORY}" "${CONSUMER_BUILD_DIRECTORY}" "${TMP_DIRECTORY}"; do
  case "${managed_directory}" in
    "${WORK_ROOT}/"*) ;;
    *)
      echo "Refusing to refresh directory outside ${WORK_ROOT}: ${managed_directory}" >&2
      exit 1
      ;;
  esac
done
if [[ ! -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" || \
      ! -f "${VCPKG_ROOT}/installed/x64-linux/share/xxhash/xxHashConfig.cmake" ]]; then
  echo "The consumer image does not contain its independent vcpkg xxHash installation" >&2
  exit 1
fi
if [[ ! -f "${CONSUMER_SOURCE_DIRECTORY}/CMakeLists.txt" || \
      ! -f "${CONSUMER_SOURCE_DIRECTORY}/main.cpp" || \
      ! -f "${CONSUMER_SOURCE_DIRECTORY}/VerifyInstalledTargets.cmake" ]]; then
  echo "The standalone Tina SDK consumer source is incomplete: ${CONSUMER_SOURCE_DIRECTORY}" >&2
  exit 1
fi
for verification_script in VerifyInstalledTinaSdkHeaders.cmake VerifyRelocatedTinaSdkPackage.cmake; do
  if [[ ! -f "${VERIFICATION_DIRECTORY}/${verification_script}" ]]; then
    echo "SDK verification script is missing: ${VERIFICATION_DIRECTORY}/${verification_script}" >&2
    exit 1
  fi
done

source /etc/os-release
if [[ "${ID:-}" != "debian" || "${VERSION_ID:-}" != "13" ]]; then
  echo "The consumer must run on Debian 13, got ${ID:-unknown} ${VERSION_ID:-unknown}" >&2
  exit 1
fi

COMPILER_VERSION="$(g++-14 -dumpfullversion -dumpversion)"
if [[ "${COMPILER_VERSION}" != 14.* ]]; then
  echo "The consumer must use GCC 14, got ${COMPILER_VERSION}" >&2
  exit 1
fi

if [[ ! -d "${INPUT_DIRECTORY}" ]]; then
  echo "Artifact input directory does not exist: ${INPUT_DIRECTORY}" >&2
  exit 1
fi
INPUT_MOUNT_OPTIONS="$(findmnt -no OPTIONS -T "${INPUT_DIRECTORY}")"
if [[ ",${INPUT_MOUNT_OPTIONS}," != *,ro,* ]]; then
  echo "Artifact input must be mounted read-only; options=${INPUT_MOUNT_OPTIONS}" >&2
  exit 1
fi

ARCHIVE_PATH="${INPUT_DIRECTORY}/${ARCHIVE_NAME}"
CHECKSUM_PATH="${INPUT_DIRECTORY}/${CHECKSUM_NAME}"
METADATA_PATH="${INPUT_DIRECTORY}/${METADATA_NAME}"
for required_file in "${ARCHIVE_PATH}" "${CHECKSUM_PATH}" "${METADATA_PATH}"; do
  if [[ ! -f "${required_file}" ]]; then
    echo "Required producer artifact is missing: ${required_file}" >&2
    exit 1
  fi
done

metadata_value() {
  local key="$1"
  python3 - "${METADATA_PATH}" "${key}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as metadata_file:
    metadata = json.load(metadata_file)
value = metadata[sys.argv[2]]
if not isinstance(value, (str, int)):
    raise TypeError(f"metadata value {sys.argv[2]} must be a string or integer")
print(value)
PY
}

METADATA_SCHEMA="$(metadata_value schema)"
METADATA_PACKAGE="$(metadata_value package_name)"
METADATA_VERSION="$(metadata_value package_version)"
METADATA_COMPONENT="$(metadata_value component)"
METADATA_BUILD_TYPE="$(metadata_value build_type)"
METADATA_PLATFORM="$(metadata_value platform)"
METADATA_ARCHIVE="$(metadata_value archive)"
METADATA_SHA256="$(metadata_value sha256)"
METADATA_PACKAGE_ROOT="$(metadata_value package_root)"
PRODUCER_DISTRIBUTION="$(metadata_value producer_distribution)"
PRODUCER_DISTRIBUTION_VERSION="$(metadata_value producer_distribution_version)"
PRODUCER_COMPILER="$(metadata_value producer_compiler)"
PRODUCER_COMPILER_VERSION="$(metadata_value producer_compiler_version)"
PRODUCER_SOURCE_DIRECTORY="$(metadata_value producer_source_dir)"
PRODUCER_BUILD_DIRECTORY="$(metadata_value producer_build_dir)"
PRODUCER_STAGING_PREFIX="$(metadata_value producer_staging_prefix)"
PRODUCER_PACKAGE_PREFIX="$(metadata_value producer_package_prefix)"

if [[ "${METADATA_SCHEMA}" != "1" || "${METADATA_PACKAGE}" != "Tina" || \
      "${METADATA_VERSION}" != "0.0.1" || "${METADATA_COMPONENT}" != "GameSDK" || \
      "${METADATA_BUILD_TYPE}" != "Release" || "${METADATA_PLATFORM}" != "linux-x64" || \
      "${METADATA_ARCHIVE}" != "${ARCHIVE_NAME}" || "${METADATA_PACKAGE_ROOT}" != "${PACKAGE_ROOT}" ]]; then
  echo "Producer metadata does not describe the expected Tina GameSDK artifact" >&2
  exit 1
fi
if [[ "${PRODUCER_DISTRIBUTION}" != "ubuntu" || "${PRODUCER_DISTRIBUTION_VERSION}" != "24.04" || \
      "${PRODUCER_COMPILER}" != "g++" || \
      "${PRODUCER_COMPILER_VERSION}" != 13.* ]]; then
  echo "Expected an Ubuntu 24.04 / GCC 13 producer artifact" >&2
  exit 1
fi
if [[ ! "${METADATA_SHA256}" =~ ^[0-9a-f]{64}$ ]]; then
  echo "Producer metadata contains an invalid SHA256 value" >&2
  exit 1
fi

EXPECTED_CHECKSUM_LINE="${METADATA_SHA256}  ${ARCHIVE_NAME}"
if [[ "$(cat "${CHECKSUM_PATH}")" != "${EXPECTED_CHECKSUM_LINE}" ]]; then
  echo "Checksum manifest must contain exactly the expected archive entry" >&2
  exit 1
fi
(cd "${INPUT_DIRECTORY}" && sha256sum --check --strict "${CHECKSUM_NAME}")
ACTUAL_SHA256="$(sha256sum "${ARCHIVE_PATH}" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" != "${METADATA_SHA256}" ]]; then
  echo "Archive SHA256 does not match producer metadata" >&2
  exit 1
fi

# Refuse absolute paths and traversal before extracting an external archive.
while IFS= read -r archive_entry; do
  case "${archive_entry}" in
    "${PACKAGE_ROOT}"|"${PACKAGE_ROOT}/"|"${PACKAGE_ROOT}/"*) ;;
    *)
      echo "Archive contains an unexpected or unsafe path: ${archive_entry}" >&2
      exit 1
      ;;
  esac
  case "/${archive_entry}/" in
    */../*)
      echo "Archive contains path traversal: ${archive_entry}" >&2
      exit 1
      ;;
  esac
done < <(tar -tzf "${ARCHIVE_PATH}")
if tar -tvzf "${ARCHIVE_PATH}" | awk '$1 !~ /^[-d]/ { found = 1 } END { exit(found ? 0 : 1) }'; then
  echo "Archive contains a link or unsupported filesystem entry" >&2
  exit 1
fi

rm -rf -- "${EXTRACT_DIRECTORY}" "${CONSUMER_BUILD_DIRECTORY}" "${TMP_DIRECTORY}"
mkdir -p -- "${EXTRACT_DIRECTORY}" "${TMP_DIRECTORY}"
tar --extract --gzip --file "${ARCHIVE_PATH}" --directory "${EXTRACT_DIRECTORY}" \
  --no-same-owner --no-same-permissions
if [[ ! -d "${RELOCATED_PREFIX}" || "${RELOCATED_PREFIX}" == "${PRODUCER_PACKAGE_PREFIX}" ]]; then
  echo "SDK was not extracted to a distinct consumer prefix: ${RELOCATED_PREFIX}" >&2
  exit 1
fi

mapfile -t PACKAGE_CONFIGS < <(
  find "${RELOCATED_PREFIX}" -type f -path '*/cmake/Tina/TinaConfig.cmake' -print
)
if [[ "${#PACKAGE_CONFIGS[@]}" -ne 1 ]]; then
  echo "Expected exactly one installed TinaConfig.cmake, found ${#PACKAGE_CONFIGS[@]}" >&2
  exit 1
fi
PACKAGE_METADATA_DIRECTORY="$(dirname "${PACKAGE_CONFIGS[0]}")"

for forbidden_path in \
  "${PRODUCER_SOURCE_DIRECTORY}" \
  "${PRODUCER_BUILD_DIRECTORY}" \
  "${PRODUCER_STAGING_PREFIX}" \
  "${PRODUCER_PACKAGE_PREFIX}"; do
  if [[ -z "${forbidden_path}" || "${forbidden_path}" != /* ]]; then
    echo "Producer metadata contains an invalid forbidden path: ${forbidden_path}" >&2
    exit 1
  fi
  # Scan the whole extracted package (CMake package files + installed static
  # libraries). Producer builds must use -ffile-prefix-map so .a objects do not
  # embed absolute checkout paths.
  if grep -R -a -F -l -- "${forbidden_path}" "${RELOCATED_PREFIX}" \
      > "${TMP_DIRECTORY}/path-leaks.txt"; then
    echo "Extracted SDK leaks producer path ${forbidden_path}:" >&2
    sed -n '1,20p' "${TMP_DIRECTORY}/path-leaks.txt" >&2
    exit 1
  fi
done

cmake \
  "-DTINA_SDK_RELOCATED_PREFIX=${RELOCATED_PREFIX}" \
  "-DTINA_SDK_ORIGINAL_PREFIX=${PRODUCER_STAGING_PREFIX}" \
  "-DTINA_FORBIDDEN_BUILD_DIR=${PRODUCER_BUILD_DIRECTORY}" \
  "-DTINA_FORBIDDEN_SOURCE_DIR=${PRODUCER_SOURCE_DIRECTORY}" \
  -P "${VERIFICATION_DIRECTORY}/VerifyRelocatedTinaSdkPackage.cmake"
cmake \
  "-DTINA_SDK_INCLUDE_DIR=${RELOCATED_PREFIX}/include" \
  -DTINA_EXPECT_AUDIO_MINIAUDIO=OFF \
  -P "${VERIFICATION_DIRECTORY}/VerifyInstalledTinaSdkHeaders.cmake"

cmake \
  -S "${CONSUMER_SOURCE_DIRECTORY}" \
  -B "${CONSUMER_BUILD_DIRECTORY}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-14 \
  "-DCMAKE_PREFIX_PATH=${RELOCATED_PREFIX}" \
  "-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  "-DVCPKG_INSTALLED_DIR=${VCPKG_ROOT}/installed" \
  "-DTINA_EXPECTED_INSTALL_PREFIX=${RELOCATED_PREFIX}" \
  "-DTINA_FORBIDDEN_SOURCE_DIR=${PRODUCER_SOURCE_DIRECTORY}/include"
cmake --build "${CONSUMER_BUILD_DIRECTORY}" \
  --target tina_sdk_consumer \
  --parallel "${BUILD_JOBS}"
"${CONSUMER_BUILD_DIRECTORY}/tina_sdk_consumer"

echo "producer=${PRODUCER_DISTRIBUTION}-${PRODUCER_DISTRIBUTION_VERSION}/gcc-${PRODUCER_COMPILER_VERSION}"
echo "consumer=${ID}-${VERSION_ID}/gcc-${COMPILER_VERSION}"
echo "sha256=${ACTUAL_SHA256}"
echo "SDK-001 cross-distribution consumer gate OK"
