"""Run already-built GoogleTest executables; never configure, build or install."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import time


def terminate_owned_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        os.killpg(process.pid, signal.SIGKILL)
    process.wait(timeout=15)


def run_test(entry: dict[str, str], output: Path, timeout: float) -> dict[str, object]:
    executable = Path(entry["path"]).resolve(strict=True)
    name = entry["target"]
    if not name.replace("_", "").isalnum():
        raise ValueError(f"Invalid test target name: {name!r}")
    result_path = output / f"{name}.json"
    log_path = output / f"{name}.log"
    if result_path.exists() or log_path.exists():
        raise FileExistsError(f"Refusing to overwrite test evidence for {name}")
    command = [str(executable), "--gtest_color=no", f"--gtest_output=json:{result_path}"]
    start = time.monotonic()
    timed_out = False
    exit_code: int | None = None
    launch_error: str | None = None
    process: subprocess.Popen[bytes] | None = None
    with log_path.open("wb") as log:
        try:
            try:
                process = subprocess.Popen(
                    command,
                    cwd=executable.parent,
                    stdin=subprocess.DEVNULL,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    start_new_session=os.name != "nt",
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                )
            except OSError as error:
                launch_error = str(error)
                log.write(f"Unable to launch {executable}: {error}\n".encode("utf-8"))
            try:
                if process is not None:
                    exit_code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                terminate_owned_process(process)
                exit_code = process.returncode
        finally:
            if process is not None:
                terminate_owned_process(process)
    details = json.loads(result_path.read_text(encoding="utf-8")) if result_path.exists() else {}
    result = {
        "target": name,
        "executable": str(executable),
        "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "exitCode": exit_code,
        "timedOut": timed_out,
        "launchError": launch_error,
        "elapsedSeconds": round(time.monotonic() - start, 3),
        "tests": details.get("tests", 0),
        "failures": details.get("failures", 0),
        "disabled": details.get("disabled", 0),
        "skipped": sum(
            case.get("result") == "SKIPPED"
            for suite in details.get("testsuites", [])
            for case in suite.get("testsuite", [])
        ),
        "hasGoogleTestReport": bool(details),
        "log": str(log_path),
    }
    result["passed"] = exit_code == 0 and not timed_out and bool(details) and result["failures"] == 0
    return result


def main() -> int:
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
    if summary_path.exists():
        parser.error("Use a new output directory; existing evidence is immutable")
    summary = {
        "schemaVersion": 1,
        "buildId": manifest["buildId"],
        "configuration": manifest["configuration"],
        "results": [],
    }
    for entry in tests:
        result = run_test(entry, output, arguments.timeout)
        summary["results"].append(result)
        summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(json.dumps(result, ensure_ascii=False), flush=True)
    return 0 if all(result["passed"] for result in summary["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
