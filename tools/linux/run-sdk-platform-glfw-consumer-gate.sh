#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export TINA_SDK_CONSUMER=PlatformGlfw
export TINA_SDK_CONFIGURE_PRESET="${TINA_SDK_CONFIGURE_PRESET:-linux-gcc13-vnext-platform}"
export TINA_SDK_BUILD_DIRECTORY="${TINA_SDK_BUILD_DIRECTORY:-${ROOT}/out/build/${TINA_SDK_CONFIGURE_PRESET}}"

exec "${ROOT}/tools/linux/run-sdk-consumer-gate.sh"
