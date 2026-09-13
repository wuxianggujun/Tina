"""Run built GoogleTest executables headlessly; success requires complete fresh evidence."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


SUMMARY_SCHEMA_VERSION = 2
MAX_REPORT_BYTES = 64 * 1024 * 1024


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def count_field(value: dict, name: str) -> int:
    count = value.get(name)
    if type(count) is not int or count < 0:
        raise ValueError(f"Invalid GoogleTest count: {name}")
    return count


def validate_report(details: object) -> dict[str, int]:
    """Check individual cases, not just exit 0 or a truthy JSON document."""
    if not isinstance(details, dict) or details.get("name") != "AllTests":
        raise ValueError("Not a complete GoogleTest JSON report")
    counts = {name: count_field(details, name) for name in ("tests", "failures", "disabled", "errors")}
    suites = details.get("testsuites")
    if not isinstance(suites, list):
        raise ValueError("Missing GoogleTest suites")
    actual = dict.fromkeys((*counts, "completed", "skipped"), 0)
    identities: set[tuple[str, str]] = set()
    for suite in suites:
        if not isinstance(suite, dict) or not isinstance(suite.get("name"), str):
            raise ValueError("Invalid GoogleTest suite")
        cases = suite.get("testsuite")
        if not isinstance(cases, list) or len(cases) != count_field(suite, "tests"):
            raise ValueError("GoogleTest suite case count disagrees with its report")
        failed = disabled = 0
        for case in cases:
            if not isinstance(case, dict) or not isinstance(case.get("name"), str):
                raise ValueError("Invalid GoogleTest case")
            identity = (suite["name"], case["name"])
            if identity in identities:
                raise ValueError("Duplicate GoogleTest case")
            identities.add(identity)
            outcome = (case.get("status"), case.get("result"))
            if outcome == ("RUN", "COMPLETED"):
                actual["completed"] += 1
            elif outcome == ("RUN", "SKIPPED"):
                actual["skipped"] += 1
            elif outcome == ("NOTRUN", "SUPPRESSED"):
                disabled += 1
            else:
                raise ValueError(f"Incomplete GoogleTest case: {identity}")
            failures = case.get("failures", [])
            if not isinstance(failures, list):
                raise ValueError("Invalid GoogleTest case failures")
            failed += bool(failures)
        if failed != count_field(suite, "failures") or disabled != count_field(suite, "disabled"):
            raise ValueError("GoogleTest suite result counts disagree with individual cases")
        actual["tests"] += len(cases)
        actual["failures"] += failed
        actual["disabled"] += disabled
        actual["errors"] += count_field(suite, "errors")
    if any(actual[name] != count for name, count in counts.items()):
        raise ValueError("GoogleTest totals disagree with individual suites")
    return actual


@contextmanager
def noninteractive_child_errors():
    """Inherit stderr-only OS crash handling; never change UAC/firewall/approval policy."""
    if os.name != "nt":
        yield
        return
    import ctypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.SetErrorMode.argtypes = [ctypes.c_uint]
    kernel.SetErrorMode.restype = ctypes.c_uint
    previous = kernel.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    kernel.SetErrorMode(previous | 0x0001 | 0x0002 | 0x8000)
    try:
        yield
    finally:
        kernel.SetErrorMode(previous)


def terminate_owned_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        killed = subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
            creationflags=subprocess.CREATE_NO_WINDOW,
            timeout=15,
        )
        if killed.returncode != 0 and process.poll() is None:
            raise RuntimeError(f"Unable to terminate owned test process {process.pid}")
    else:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The owned process exited between poll and kill.
    process.wait(timeout=15)


def run_test(entry: dict[str, str], output: Path, timeout: float) -> dict[str, object]:
    name = entry["target"]
    if not name or not name.replace("_", "").isalnum():
        raise ValueError(f"Invalid test target name: {name!r}")
    executable = Path(entry["path"]).resolve()
    result_path = output / f"{name}.json"
    log_path = output / f"{name}.log"
    if result_path.exists() or log_path.exists():
        raise FileExistsError(f"Refusing to overwrite test evidence for {name}")
    command = [str(executable), "--gtest_color=no", "--gtest_filter=*", "--gtest_repeat=1",
               "--gtest_break_on_failure=0", "--gtest_throw_on_failure=0", "--gtest_fail_fast=0",
               f"--gtest_output=json:{result_path}"]
    # Shell filter/shard/repeat settings cannot silently shrink a full-target gate.
    environment = {key: value for key, value in os.environ.items() if not key.upper().startswith("GTEST_")}
    start = time.monotonic()
    result: dict[str, object] = {
        "target": name, "executable": str(executable), "command": command,
        "sha256": None, "startedAt": datetime.now(timezone.utc).isoformat(),
        "started": False, "processId": None, "exitCode": None, "timedOut": False,
        "launchError": None, "cleanupError": None, "reportError": None,
        "tests": 0, "completed": 0, "failures": 0, "errors": 0, "disabled": 0, "skipped": 0,
        "hasGoogleTestReport": False, "log": str(log_path), "report": str(result_path),
        "status": "not_run",
    }
    process: subprocess.Popen[bytes] | None = None
    with log_path.open("xb") as log:
        try:
            try:
                result["sha256"] = hash_file(executable)
                with noninteractive_child_errors():
                    process = subprocess.Popen(
                        command, cwd=executable.parent, env=environment,
                        stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                        start_new_session=os.name != "nt",
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                    )
                result["started"] = True
                result["processId"] = process.pid
            except OSError as error:
                result["launchError"] = str(error)
                log.write(f"Unable to launch {executable}: {error}\n".encode("utf-8"))
            if process is not None:
                try:
                    result["exitCode"] = process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    result["timedOut"] = True
        finally:
            if process is not None:
                try:
                    terminate_owned_process(process)
                except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                    result["cleanupError"] = str(error)
                    log.write(f"Test process cleanup failed: {error}\n".encode("utf-8"))
                result["exitCode"] = process.returncode
    result["elapsedSeconds"] = round(time.monotonic() - start, 3)
    try:
        if not result_path.is_file():
            raise ValueError("No fresh GoogleTest report was produced")
        if result_path.stat().st_size > MAX_REPORT_BYTES:
            raise ValueError("GoogleTest report exceeds its byte budget")
        result.update(validate_report(json.loads(result_path.read_text(encoding="utf-8"))))
        result["hasGoogleTestReport"] = True
        if hash_file(executable) != result["sha256"]:
            raise ValueError("Test executable changed during the run")
    except (OSError, ValueError, TypeError) as error:
        result["reportError"] = str(error)
    if result["cleanupError"]:
        result["status"] = "cleanup_error"
    elif result["launchError"] or not result["started"]:
        result["status"] = "launch_error"
    elif result["timedOut"]:
        result["status"] = "timeout"
    elif result["exitCode"] != 0:
        result["status"] = "failed"
    elif result["reportError"]:
        result["status"] = "invalid_report"
    elif result["failures"] or result["errors"]:
        result["status"] = "failed"
    elif result["tests"] == 0:
        result["status"] = "no_tests"
    elif result["skipped"] or result["disabled"]:
        result["status"] = "incomplete"
    else:
        result["status"] = "passed"
    return result


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--target", action="append", help="Repeat to rerun only directly affected targets")
    arguments = parser.parse_args()
    if not math.isfinite(arguments.timeout) or arguments.timeout <= 0:
        parser.error("--timeout must be finite and positive")
    manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != 1:
        parser.error("Unsupported validation manifest schema")
    tests = manifest["tests"]
    names = [entry["target"] for entry in tests]
    if len(set(names)) != len(names):
        parser.error("Duplicate test targets in validation manifest")
    if arguments.target:
        requested = set(arguments.target)
        available = {entry["target"] for entry in tests}
        if requested - available:
            parser.error(f"Unknown test targets: {sorted(requested - available)}")
        tests = [entry for entry in tests if entry["target"] in requested]
    if not tests:
        parser.error("The manifest contains no selected tests")
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    summary_path = output / "summary.json"
    summary = {
        "schemaVersion": SUMMARY_SCHEMA_VERSION,
        "buildId": manifest["buildId"],
        "configuration": manifest["configuration"],
        "manifestSha256": hash_file(arguments.manifest),
        "selectedTargets": [entry["target"] for entry in tests],
        "status": "running",
        "results": [],
    }
    # Exclusive creation prevents two runners from claiming the same evidence.
    with summary_path.open("x", encoding="utf-8") as stream:
        json.dump(summary, stream, indent=2, ensure_ascii=False)

    def publish_summary() -> None:
        summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    try:
        for entry in tests:
            result = run_test(entry, output, arguments.timeout)
            summary["results"].append(result)
            publish_summary()
            print(json.dumps(result, ensure_ascii=False), flush=True)
    except BaseException:
        summary["status"] = "interrupted"
        publish_summary()
        raise
    passed = all(result["status"] == "passed" for result in summary["results"])
    summary["status"] = "passed" if passed else "failed"
    publish_summary()
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
