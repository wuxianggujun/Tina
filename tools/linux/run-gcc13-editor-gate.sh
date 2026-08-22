#!/usr/bin/env bash
# 2D-EDITOR Linux GCC13/bgfx build, workspace smoke, and real file-dialog gate.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT_PATH="${ROOT}/tools/linux/run-gcc13-editor-gate.sh"
cd "${ROOT}"
# shellcheck source=cmake-cache-source-guard.sh
source "${ROOT}/tools/linux/cmake-cache-source-guard.sh"

: "${VCPKG_ROOT:?VCPKG_ROOT must be set}"
: "${TINA_EDITOR_DIALOG_HELPER:?TINA_EDITOR_DIALOG_HELPER must be zenity or kdialog}"

case "${TINA_EDITOR_DIALOG_HELPER}" in
  zenity|kdialog)
    ;;
  *)
    echo "TINA_EDITOR_DIALOG_HELPER must be zenity or kdialog" >&2
    exit 2
    ;;
esac

EXPECTED_HELPER="${TINA_EDITOR_DIALOG_HELPER}"
OTHER_HELPER="zenity"
if [[ "${EXPECTED_HELPER}" == "zenity" ]]; then
  OTHER_HELPER="kdialog"
fi

if ! command -v flock >/dev/null 2>&1; then
  echo "required editor gate command is missing: flock" >&2
  exit 1
fi
if [[ "${TINA_EDITOR_GATE_LOCK_HELD:-0}" != "1" ]]; then
  mkdir -p -- "${ROOT}/out/build"
  exec flock -x "${ROOT}/out/build/.tina-editor-gate.lock" \
    env TINA_EDITOR_GATE_LOCK_HELD=1 bash "${SCRIPT_PATH}"
fi

for required_command in dbus-run-session git realpath sha256sum sort xvfb-run openbox xclip xdotool python3; do
  if ! command -v "${required_command}" >/dev/null 2>&1; then
    echo "required editor gate command is missing: ${required_command}" >&2
    exit 1
  fi
done
if ! command -v "${EXPECTED_HELPER}" >/dev/null 2>&1; then
  echo "expected editor dialog helper is missing: ${EXPECTED_HELPER}" >&2
  exit 1
fi
if command -v "${OTHER_HELPER}" >/dev/null 2>&1; then
  echo "unexpected helper is installed; fallback path would not be proven: ${OTHER_HELPER}" >&2
  exit 1
fi

BUILD_DIR="${TINA_EDITOR_BUILD_DIRECTORY:-${ROOT}/out/build/docker-linux-gcc13-vnext-bgfx-editor}"
BIN_DIR="${BUILD_DIR}/bin"
BUILD_STAMP="${BUILD_DIR}/.tina-editor-gate-build-stamp"
REUSE_BUILD="${TINA_EDITOR_REUSE_BUILD:-0}"
REMOVE_BUILD_AFTER="${TINA_EDITOR_REMOVE_BUILD_AFTER:-0}"

if [[ "${REUSE_BUILD}" != "0" && "${REUSE_BUILD}" != "1" ]]; then
  echo "TINA_EDITOR_REUSE_BUILD must be 0 or 1" >&2
  exit 2
fi
if [[ "${REMOVE_BUILD_AFTER}" != "0" && "${REMOVE_BUILD_AFTER}" != "1" ]]; then
  echo "TINA_EDITOR_REMOVE_BUILD_AFTER must be 0 or 1" >&2
  exit 2
fi
if [[ "${REMOVE_BUILD_AFTER}" == "1" && "${REUSE_BUILD}" != "1" ]]; then
  echo "TINA_EDITOR_REMOVE_BUILD_AFTER is only valid for the reuse phase" >&2
  exit 2
fi

current_source_fingerprint() {
  local source_list fingerprint
  if ! git submodule foreach --quiet --recursive '
    if [ -n "$(git status --porcelain --untracked-files=normal)" ]; then
      echo "dirty submodule: $displaypath" >&2
      exit 1
    fi
  '; then
    echo "editor gate refuses to fingerprint dirty submodules" >&2
    return 1
  fi
  source_list="$(mktemp "${TMPDIR:-/tmp}/tina-editor-source-list.XXXXXX")"
  if ! git ls-files -co --exclude-standard -z | sort -z >"${source_list}"; then
    rm -f -- "${source_list}"
    echo "editor gate source list could not be captured" >&2
    return 1
  fi
  if ! fingerprint="$(
    {
      git submodule status --recursive || exit 1
      while IFS= read -r -d '' source_path; do
        printf 'path=%s\0' "${source_path}"
        if [[ -f "${source_path}" ]]; then
          sha256sum -- "${source_path}" || exit 1
        elif [[ -L "${source_path}" ]]; then
          printf 'symlink=%s\n' "$(readlink "${source_path}")" || exit 1
        elif [[ -d "${source_path}/.git" || -f "${source_path}/.git" ]]; then
          printf 'gitlink=%s\n' "$(git -C "${source_path}" rev-parse HEAD)" || exit 1
        else
          printf 'missing\n'
        fi
      done <"${source_list}"
    } | sha256sum | cut -d' ' -f1
  )"; then
    rm -f -- "${source_list}"
    echo "editor gate source fingerprint could not be calculated" >&2
    return 1
  fi
  rm -f -- "${source_list}"
  printf '%s\n' "${fingerprint}"
}

binary_sha256() {
  local binary_path="$1"
  if [[ ! -x "${binary_path}" ]]; then
    echo "required reusable editor gate binary is missing: ${binary_path}" >&2
    return 1
  fi
  sha256sum -- "${binary_path}" | cut -d' ' -f1
}

validate_reusable_build() {
  if [[ ! -f "${BUILD_STAMP}" ]]; then
    echo "reusable editor build stamp is missing; run the zenity primary gate first" >&2
    return 1
  fi
  local source_hash editor_hash tests_hash dialog_hash
  source_hash="$(current_source_fingerprint)"
  editor_hash="$(binary_sha256 "${BIN_DIR}/TinaEditor")"
  tests_hash="$(binary_sha256 "${BIN_DIR}/tina_editor_app_tests")"
  dialog_hash="$(binary_sha256 "${BIN_DIR}/tina_editor_file_dialog_gate")"
  grep -Fqx "schema=1" "${BUILD_STAMP}" &&
    grep -Fqx "sourceFingerprint=${source_hash}" "${BUILD_STAMP}" &&
    grep -Fqx "TinaEditorSha256=${editor_hash}" "${BUILD_STAMP}" &&
    grep -Fqx "EditorAppTestsSha256=${tests_hash}" "${BUILD_STAMP}" &&
    grep -Fqx "DialogGateSha256=${dialog_hash}" "${BUILD_STAMP}" || {
      echo "reusable editor build stamp does not match the current source or binaries" >&2
      return 1
    }
}

write_reusable_build_stamp() {
  local source_hash="$1"
  local temporary_stamp editor_hash tests_hash dialog_hash
  editor_hash="$(binary_sha256 "${BIN_DIR}/TinaEditor")"
  tests_hash="$(binary_sha256 "${BIN_DIR}/tina_editor_app_tests")"
  dialog_hash="$(binary_sha256 "${BIN_DIR}/tina_editor_file_dialog_gate")"
  temporary_stamp="$(mktemp "${BUILD_STAMP}.XXXXXX")"
  printf 'schema=1\nsourceFingerprint=%s\nTinaEditorSha256=%s\nEditorAppTestsSha256=%s\nDialogGateSha256=%s\n' \
    "${source_hash}" "${editor_hash}" "${tests_hash}" "${dialog_hash}" >"${temporary_stamp}"
  mv -f -- "${temporary_stamp}" "${BUILD_STAMP}"
}

if [[ "${TINA_EDITOR_GATE_X11_READY:-0}" != "1" ]]; then
  echo "=== 2D-EDITOR Linux GCC13/bgfx gate ==="
  echo "root=${ROOT}"
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "uname=$(uname -a)"
  echo "gcc=$(gcc-13 --version | head -n1 || gcc --version | head -n1)"
  echo "HEAD=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "helper=${EXPECTED_HELPER}"
  dpkg-query -W -f='helper_package=${Package} helper_version=${Version}\n' \
    "${EXPECTED_HELPER}"

  if [[ "${REUSE_BUILD}" == "1" ]]; then
    echo "build_mode=reuse (configure/build/tests/workspace-smoke are not invoked)"
    validate_reusable_build
  else
    if ! command -v cmake >/dev/null 2>&1; then
      echo "required primary editor gate command is missing: cmake" >&2
      exit 1
    fi
    echo "cmake=$(cmake --version | head -n1)"
    echo "build_mode=primary"
    rm -f -- "${BUILD_STAMP}"
    PRIMARY_SOURCE_FINGERPRINT="$(current_source_fingerprint)"
    tina_guard_cmake_cache_source "${BUILD_DIR}" "${ROOT}"
    cmake --preset linux-gcc13-vnext-bgfx -B "${BUILD_DIR}"
    cmake --build "${BUILD_DIR}" \
      --target tina_editor_desktop tina_editor_app_tests tina_editor_file_dialog_gate \
      --parallel "${TINA_BUILD_PARALLELISM:-2}"
  fi

  exec dbus-run-session -- xvfb-run -a -s "-screen 0 1280x800x24" \
    env TINA_EDITOR_GATE_X11_READY=1 \
        TINA_EDITOR_DIALOG_HELPER="${EXPECTED_HELPER}" \
        TINA_EDITOR_BUILD_DIRECTORY="${BUILD_DIR}" \
        TINA_EDITOR_REUSE_BUILD="${REUSE_BUILD}" \
        TINA_EDITOR_REMOVE_BUILD_AFTER="${REMOVE_BUILD_AFTER}" \
        TINA_EDITOR_PRIMARY_SOURCE_FINGERPRINT="${PRIMARY_SOURCE_FINGERPRINT:-}" \
        VCPKG_ROOT="${VCPKG_ROOT}" \
        LC_ALL=C.UTF-8 \
        bash "${SCRIPT_PATH}"
fi

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tina-editor-dialog-gate.XXXXXX")"
XDG_RUNTIME_DIR="${WORK_DIR}/xdg-runtime"
mkdir -p -- "${XDG_RUNTIME_DIR}"
chmod 0700 "${XDG_RUNTIME_DIR}"
export XDG_RUNTIME_DIR
ACTIVE_PROBE_PID=""
ACTIVE_WATCHDOG_PID=""
ACTIVE_CLIPBOARD_PID=""
WINDOW_MANAGER_PID=""

terminate_process_group() {
  local process_id="$1"
  if [[ -z "${process_id}" ]]; then
    return
  fi
  kill -TERM -- "-${process_id}" 2>/dev/null || true
  sleep 0.2
  kill -KILL -- "-${process_id}" 2>/dev/null || true
  wait "${process_id}" 2>/dev/null || true
}

stop_window_manager() {
  if [[ -n "${WINDOW_MANAGER_PID}" ]]; then
    kill -TERM "${WINDOW_MANAGER_PID}" 2>/dev/null || true
    wait "${WINDOW_MANAGER_PID}" 2>/dev/null || true
    WINDOW_MANAGER_PID=""
  fi
}

cleanup() {
  if [[ -n "${ACTIVE_WATCHDOG_PID}" ]]; then
    terminate_process_group "${ACTIVE_WATCHDOG_PID}"
    ACTIVE_WATCHDOG_PID=""
  fi
  if [[ -n "${ACTIVE_CLIPBOARD_PID}" ]]; then
    terminate_process_group "${ACTIVE_CLIPBOARD_PID}"
    ACTIVE_CLIPBOARD_PID=""
  fi
  if [[ -n "${ACTIVE_PROBE_PID}" ]]; then
    terminate_process_group "${ACTIVE_PROBE_PID}"
    ACTIVE_PROBE_PID=""
  fi
  stop_window_manager
  if [[ -n "${WORK_DIR}" ]]; then
    rm -rf -- "${WORK_DIR}"
    WORK_DIR=""
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

openbox --sm-disable >"${WORK_DIR}/openbox.log" 2>&1 &
WINDOW_MANAGER_PID=$!
sleep 0.5
if ! kill -0 "${WINDOW_MANAGER_PID}" 2>/dev/null; then
  echo "openbox failed to stay running" >&2
  exit 1
fi

run_editor_smoke() {
  local workspace="$1"
  echo "=== RUN TinaEditor workspace=${workspace} ==="
  timeout --signal=TERM --kill-after=5s 180s \
    "${BIN_DIR}/TinaEditor" --frames=60 --frame-delay-ms=0 \
      "--workspace=${workspace}"
}

if [[ "${REUSE_BUILD}" == "0" ]]; then
  echo "=== RUN tina_editor_app_tests ==="
  "${BIN_DIR}/tina_editor_app_tests" --gtest_color=no
  run_editor_smoke 2d
  run_editor_smoke 3d
fi

FIXTURE_DIR="${WORK_DIR}/编辑器 gate with spaces"
OPEN_PATH="${FIXTURE_DIR}/现有场景.tworld"
SAVE_BASE_PATH="${FIXTURE_DIR}/新场景"
SAVE_PATH="${SAVE_BASE_PATH}.tworld"
FOLDER_PATH="${FIXTURE_DIR}/素材 folder"
mkdir -p -- "${FOLDER_PATH}"
printf '{"fixture":true}\n' >"${OPEN_PATH}"

contains_argument() {
  local expected="$1"
  shift
  local argument
  for argument in "$@"; do
    if [[ "${argument}" == "${expected}" ]]; then
      return 0
    fi
  done
  return 1
}

wait_for_direct_child() {
  local parent_pid="$1"
  local children_file="/proc/${parent_pid}/task/${parent_pid}/children"
  local child_pid=""
  for _ in $(seq 1 200); do
    if [[ -r "${children_file}" ]]; then
      read -r child_pid _ <"${children_file}" || true
      if [[ -n "${child_pid}" && -r "/proc/${child_pid}/cmdline" ]]; then
        printf '%s\n' "${child_pid}"
        return 0
      fi
    fi
    if ! kill -0 "${parent_pid}" 2>/dev/null; then
      break
    fi
    sleep 0.05
  done
  return 1
}

validate_result_json() {
  local output_file="$1"
  local operation="$2"
  local expected_outcome="$3"
  local expected_path="$4"
  python3 - "${output_file}" "${operation}" "${expected_outcome}" "${expected_path}" <<'PY'
import json
import pathlib
import sys

output_path, operation, outcome, expected_path = sys.argv[1:]
lines = pathlib.Path(output_path).read_text(encoding="utf-8").splitlines()
if len(lines) != 1:
    raise SystemExit(f"expected one JSON line, got {len(lines)}")
result = json.loads(lines[0])
expected = {
    "schema": 1,
    "operation": operation,
    "outcome": outcome,
    "path": expected_path,
}
if result != expected:
    raise SystemExit(f"unexpected dialog result: {result!r} != {expected!r}")
PY
}

run_dialog_case() {
  local operation="$1"
  local expected_outcome="$2"
  local selection_path="$3"
  local expected_path="$4"
  local sequence="$5"
  local title_token="${EXPECTED_HELPER}-${operation}-${expected_outcome}-${sequence}-$$"
  local window_title="TinaEditorDialogGate-${title_token}"
  local output_file="${WORK_DIR}/${sequence}.out"
  local error_file="${WORK_DIR}/${sequence}.err"
  local timeout_marker="${WORK_DIR}/${sequence}.timeout"
  local -a probe_arguments=(
    "--operation=${operation}"
    "--initial-directory=${FIXTURE_DIR}"
    "--title-token=${title_token}"
  )
  if [[ "${operation}" == "save" ]]; then
    probe_arguments+=("--suggested-name=新场景" "--default-extension=tworld")
  fi

  echo "=== RUN dialog helper=${EXPECTED_HELPER} operation=${operation} outcome=${expected_outcome} ==="
  setsid "${BIN_DIR}/tina_editor_file_dialog_gate" "${probe_arguments[@]}" \
    >"${output_file}" 2>"${error_file}" &
  ACTIVE_PROBE_PID=$!
  setsid bash -c '
    sleep 20
    : >"$1"
    kill -TERM -- "-$2" 2>/dev/null || exit 0
    sleep 2
    kill -KILL -- "-$2" 2>/dev/null || true
  ' _ "${timeout_marker}" "${ACTIVE_PROBE_PID}" &
  ACTIVE_WATCHDOG_PID=$!

  local helper_pid
  if ! helper_pid="$(wait_for_direct_child "${ACTIVE_PROBE_PID}")"; then
    echo "dialog probe did not expose a live direct helper child" >&2
    return 1
  fi

  local helper_executable expected_executable
  helper_executable="$(readlink -f "/proc/${helper_pid}/exe")"
  expected_executable="$(readlink -f "$(command -v "${EXPECTED_HELPER}")")"
  if [[ "${helper_executable}" != "${expected_executable}" ]]; then
    echo "unexpected helper executable: ${helper_executable}" >&2
    return 1
  fi

  local -a helper_arguments=()
  mapfile -d '' -t helper_arguments <"/proc/${helper_pid}/cmdline"
  if [[ "${EXPECTED_HELPER}" == "zenity" ]]; then
    contains_argument "--file-selection" "${helper_arguments[@]}" || {
      echo "zenity command did not contain --file-selection" >&2
      return 1
    }
    if [[ "${operation}" == "save" ]]; then
      contains_argument "--save" "${helper_arguments[@]}" || {
        echo "zenity save command did not contain --save" >&2
        return 1
      }
    elif [[ "${operation}" == "folder" ]]; then
      contains_argument "--directory" "${helper_arguments[@]}" || {
        echo "zenity folder command did not contain --directory" >&2
        return 1
      }
    fi
  else
    local required_kdialog_argument="--getopenfilename"
    if [[ "${operation}" == "save" ]]; then
      required_kdialog_argument="--getsavefilename"
    elif [[ "${operation}" == "folder" ]]; then
      required_kdialog_argument="--getexistingdirectory"
    fi
    contains_argument "${required_kdialog_argument}" "${helper_arguments[@]}" || {
      echo "kdialog command did not contain ${required_kdialog_argument}" >&2
      return 1
    }
  fi
  printf 'helper_pid=%s helper_executable=%s helper_command=' \
    "${helper_pid}" "${helper_executable}"
  printf '%q ' "${helper_arguments[@]}"
  printf '\n'

  local window_ids window_id
  if ! window_ids="$(timeout 10s xdotool search --sync --name "${window_title}")"; then
    echo "dialog window was not found: ${window_title}" >&2
    return 1
  fi
  window_id="${window_ids%%$'\n'*}"
  timeout 5s xdotool windowactivate --sync "${window_id}"

  if [[ "${expected_outcome}" == "cancelled" ]]; then
    timeout 5s xdotool key --clearmodifiers Escape
  else
    timeout 5s xdotool key --clearmodifiers ctrl+l
    sleep 0.1
    printf '%s' "${selection_path}" | \
      setsid timeout --signal=TERM --kill-after=1s 5s \
        xclip -selection clipboard -loops 1 -in &
    ACTIVE_CLIPBOARD_PID=$!
    sleep 0.1
    timeout 5s xdotool key --clearmodifiers ctrl+v
    wait "${ACTIVE_CLIPBOARD_PID}"
    ACTIVE_CLIPBOARD_PID=""
    timeout 5s xdotool key --clearmodifiers Return
    if [[ "${operation}" == "folder" ]]; then
      sleep 0.4
      if kill -0 "${ACTIVE_PROBE_PID}" 2>/dev/null; then
        timeout 5s xdotool key --clearmodifiers Return || true
      fi
    fi
  fi

  local probe_exit=0
  if wait "${ACTIVE_PROBE_PID}"; then
    probe_exit=0
  else
    probe_exit=$?
  fi
  ACTIVE_PROBE_PID=""
  terminate_process_group "${ACTIVE_WATCHDOG_PID}"
  ACTIVE_WATCHDOG_PID=""

  if [[ -e "${timeout_marker}" ]]; then
    echo "dialog probe timed out" >&2
    return 1
  fi
  if [[ "${probe_exit}" -ne 0 ]]; then
    echo "dialog probe failed exit=${probe_exit}" >&2
    sed -n '1,40p' "${error_file}" >&2
    return 1
  fi
  if [[ -s "${error_file}" ]]; then
    echo "dialog probe produced unexpected stderr" >&2
    sed -n '1,40p' "${error_file}" >&2
    return 1
  fi
  if [[ -e "/proc/${helper_pid}" ]]; then
    echo "dialog helper was not reaped: pid=${helper_pid}" >&2
    return 1
  fi
  validate_result_json "${output_file}" "${operation}" \
    "${expected_outcome}" "${expected_path}"
}

run_dialog_case open selected "${OPEN_PATH}" "${OPEN_PATH}" 1
run_dialog_case save selected "${SAVE_BASE_PATH}" "${SAVE_PATH}" 2
run_dialog_case folder selected "${FOLDER_PATH}" "${FOLDER_PATH}" 3
run_dialog_case open cancelled "" "" 4
run_dialog_case save cancelled "" "" 5
run_dialog_case folder cancelled "" "" 6

cleanup

if [[ "${REUSE_BUILD}" == "0" ]]; then
  final_source_fingerprint="$(current_source_fingerprint)"
  if [[ "${final_source_fingerprint}" != "${TINA_EDITOR_PRIMARY_SOURCE_FINGERPRINT:-}" ]]; then
    echo "source changed after the primary build; refusing to publish a reusable stamp" >&2
    exit 1
  fi
  write_reusable_build_stamp "${final_source_fingerprint}"
  echo "resource_build_tree=retained-for-kdialog-reuse path=${BUILD_DIR}"
elif [[ "${REMOVE_BUILD_AFTER}" == "1" ]]; then
  resolved_build_dir="$(realpath -m "${BUILD_DIR}")"
  resolved_build_root="$(realpath -m "${ROOT}/out/build")"
  case "${resolved_build_dir}" in
    "${resolved_build_root}"/*)
      rm -rf -- "${resolved_build_dir}"
      echo "resource_build_tree=removed path=${resolved_build_dir}"
      ;;
    *)
      echo "refusing to remove editor build tree outside ${resolved_build_root}" >&2
      exit 1
      ;;
  esac
else
  echo "resource_build_tree=retained path=${BUILD_DIR}"
fi
echo "resource_processes=stopped probes=0 helpers=0 clipboard=0 window_manager=0"
echo "resource_temporary_directory=removed"
echo "2D-EDITOR Linux GCC13/bgfx helper=${EXPECTED_HELPER} gate OK"
