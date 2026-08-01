#!/usr/bin/env python3
"""Run tina_bench in independent processes and evaluate ADR 0018 evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any, Sequence


RUNNER_SCHEMA_NAME = "tina_bench_multi_process"
RUNNER_SCHEMA_VERSION = 1
BENCH_SCHEMA_NAME = "tina_bench"
BENCH_SCHEMA_VERSION = 1
MACHINE_PROFILE_SCHEMA_NAME = "tina_bench_machine_profile"
BASELINE_SCHEMA_NAME = "tina_bench_baseline"
PROFILE_BASELINE_SCHEMA_VERSION = 1
METRIC = "timing_ns.p99"
MIN_CANDIDATE_PROCESSES = 5
MIN_BASELINE_PROCESSES = 10
MIN_WARMUP_ITERATIONS = 600
MIN_MEASURE_ITERATIONS = 2_000
RELATIVE_REGRESSION_THRESHOLD = 0.10
MAD_NOISE_MULTIPLIER = 3.0


class ProtocolError(RuntimeError):
    pass


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _require_object(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ProtocolError(f"{name} must be a JSON object")
    return value


def _require_non_negative_number(value: Any, name: str) -> float:
    if not _is_number(value) or value < 0:
        raise ProtocolError(f"{name} must be a finite non-negative number")
    return float(value)


def _require_positive_integer(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise ProtocolError(f"{name} must be a positive integer")
    return value


def _require_non_negative_integer(value: Any, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ProtocolError(f"{name} must be a non-negative integer")
    return value


def _require_non_empty_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ProtocolError(f"{name} must be a non-empty string")
    return value


def _canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def benchmark_sha256(executable: str) -> str:
    resolved = Path(executable)
    if not resolved.is_file():
        found = shutil.which(executable)
        if found is None:
            raise ProtocolError(f"benchmark executable was not found: {executable}")
        resolved = Path(found)
    digest = hashlib.sha256()
    try:
        with resolved.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise ProtocolError(f"failed to hash benchmark executable {resolved}: {error}") from error
    return digest.hexdigest()


def _require_sha256(value: Any, name: str) -> str:
    encoded = _require_non_empty_string(value, name)
    if len(encoded) != 64 or any(character not in "0123456789abcdef" for character in encoded):
        raise ProtocolError(f"{name} must be a lowercase SHA-256 hex digest")
    return encoded


def _median(values: Sequence[float]) -> float:
    if not values:
        raise ProtocolError("at least one process result is required")
    return float(statistics.median(values))


def median_and_mad(values: Sequence[float]) -> tuple[float, float]:
    median = _median(values)
    mad = _median([abs(value - median) for value in values])
    return median, mad


def _extract_child_report(stdout: str, process_index: int) -> dict[str, Any]:
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    if len(lines) != 1:
        raise ProtocolError(
            f"benchmark process {process_index} must write exactly one non-empty JSON line"
        )
    try:
        return _require_object(json.loads(lines[0]), f"benchmark process {process_index} report")
    except json.JSONDecodeError as error:
        raise ProtocolError(
            f"benchmark process {process_index} emitted invalid JSON: {error.msg}"
        ) from error


def collect_reports(command: Sequence[str], process_count: int, timeout_seconds: float) -> list[dict[str, Any]]:
    if process_count <= 0:
        raise ProtocolError("process count must be positive")
    if timeout_seconds <= 0 or not math.isfinite(timeout_seconds):
        raise ProtocolError("timeout must be a finite positive number")
    if not command:
        raise ProtocolError("benchmark command is empty")

    reports: list[dict[str, Any]] = []
    for process_index in range(1, process_count + 1):
        try:
            completed = subprocess.run(
                list(command),
                capture_output=True,
                check=False,
                encoding="utf-8",
                errors="strict",
                timeout=timeout_seconds,
            )
        except (OSError, subprocess.SubprocessError, UnicodeError) as error:
            raise ProtocolError(f"benchmark process {process_index} failed to execute: {error}") from error
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip() or "no diagnostic output"
            raise ProtocolError(
                f"benchmark process {process_index} exited {completed.returncode}: {detail}"
            )
        reports.append(_extract_child_report(completed.stdout, process_index))
    return reports


def _validate_child_report(report: dict[str, Any], process_index: int) -> int:
    prefix = f"benchmark process {process_index}"
    if report.get("schemaName") != BENCH_SCHEMA_NAME or report.get("schema") != BENCH_SCHEMA_VERSION:
        raise ProtocolError(f"{prefix} uses an unsupported tina_bench schema")
    if report.get("status") != "ok":
        raise ProtocolError(f"{prefix} did not report status=ok")
    if report.get("conclusion") != "provisional":
        raise ProtocolError(f"{prefix} must remain provisional before runner evaluation")

    workload = _require_object(report.get("workload"), f"{prefix} workload")
    if not isinstance(workload.get("id"), str) or not workload["id"]:
        raise ProtocolError(f"{prefix} workload.id must be a non-empty string")
    _require_positive_integer(workload.get("version"), f"{prefix} workload.version")
    _require_object(workload.get("parameters"), f"{prefix} workload.parameters")
    _require_object(report.get("fingerprint"), f"{prefix} fingerprint")
    checksum = report.get("checksum")
    if not isinstance(checksum, str) or not checksum:
        raise ProtocolError(f"{prefix} checksum must be a non-empty string")
    timing = _require_object(report.get("timing_ns"), f"{prefix} timing_ns")
    return _require_non_negative_integer(timing.get("p99"), f"{prefix} timing_ns.p99")


def validate_compatible_reports(reports: Sequence[dict[str, Any]]) -> list[int]:
    if not reports:
        raise ProtocolError("at least one benchmark process report is required")

    metrics: list[int] = []
    reference = reports[0]
    for process_index, report in enumerate(reports, start=1):
        metrics.append(_validate_child_report(report, process_index))
        if process_index == 1:
            continue
        for field in ("workload", "fingerprint", "checksum"):
            if _canonical_json(report.get(field)) != _canonical_json(reference.get(field)):
                raise ProtocolError(
                    f"benchmark process {process_index} has incompatible {field}"
                )
    return metrics


def _validate_review(document: dict[str, Any], name: str) -> None:
    review = _require_object(document.get("review"), f"{name} review")
    if review.get("status") != "approved":
        raise ProtocolError(f"{name} is not approved")
    for field in ("reviewedBy", "reviewedAt"):
        _require_non_empty_string(review.get(field), f"{name} review.{field}")


def _validate_formal_sample_size(workload: dict[str, Any]) -> None:
    parameters = _require_object(workload.get("parameters"), "candidate workload.parameters")
    warmup = parameters.get("warmup_frames", parameters.get("warmup_iterations"))
    measured = parameters.get("measure_frames", parameters.get("measure_iterations"))
    if not isinstance(warmup, int) or isinstance(warmup, bool) or warmup < MIN_WARMUP_ITERATIONS:
        raise ProtocolError(
            f"hard gate requires at least {MIN_WARMUP_ITERATIONS} warmup iterations per process"
        )
    if not isinstance(measured, int) or isinstance(measured, bool) or measured < MIN_MEASURE_ITERATIONS:
        raise ProtocolError(
            f"hard gate requires at least {MIN_MEASURE_ITERATIONS} measured iterations per process"
        )


def _validate_machine_profile(
    profile: dict[str, Any], reference: dict[str, Any], executable_sha256: str
) -> tuple[float, float]:
    if (
        profile.get("schemaName") != MACHINE_PROFILE_SCHEMA_NAME
        or profile.get("schema") != PROFILE_BASELINE_SCHEMA_VERSION
    ):
        raise ProtocolError("machine profile uses an unsupported schema")
    _validate_review(profile, "machine profile")
    _require_non_empty_string(profile.get("machineId"), "machine profile machineId")
    machine = _require_object(profile.get("machine"), "machine profile machine")
    for field in ("os", "cpu", "gpu", "gpuDriver", "powerPlan", "affinity"):
        _require_non_empty_string(machine.get(field), f"machine profile machine.{field}")
    _require_positive_integer(machine.get("ramBytes"), "machine profile machine.ramBytes")
    _require_non_negative_integer(
        machine.get("workerCount"), "machine profile machine.workerCount"
    )
    build = _require_object(profile.get("build"), "machine profile build")
    for field in ("gitCommit", "preset", "compiler", "linker", "vcpkgBaseline"):
        _require_non_empty_string(build.get(field), f"machine profile build.{field}")
    profile_sha256 = _require_sha256(
        build.get("binarySha256"), "machine profile build.binarySha256"
    )
    if profile_sha256 != executable_sha256:
        raise ProtocolError("machine profile build.binarySha256 does not match the benchmark executable")
    if build.get("gitDirty") is not False:
        raise ProtocolError("machine profile hard-gate build must have gitDirty=false")
    if build.get("configuration") != "Release":
        raise ProtocolError("machine profile hard-gate build.configuration must be Release")
    if build.get("tracyEnabled") is not False:
        raise ProtocolError("machine profile hard-gate build must have tracyEnabled=false")
    if _canonical_json(profile.get("benchmarkFingerprint")) != _canonical_json(
        reference["fingerprint"]
    ):
        raise ProtocolError("machine profile benchmarkFingerprint is incompatible")

    calibration = _require_object(profile.get("noiseCalibration"), "machine profile noiseCalibration")
    if calibration.get("metric") != METRIC:
        raise ProtocolError(f"machine profile noiseCalibration.metric must be {METRIC}")
    _require_positive_integer(
        calibration.get("processCount"), "machine profile noiseCalibration.processCount"
    )
    if calibration["processCount"] < MIN_BASELINE_PROCESSES:
        raise ProtocolError(
            f"machine profile noise calibration requires at least {MIN_BASELINE_PROCESSES} processes"
        )
    observed_median = _require_non_negative_number(
        calibration.get("observedMedianNs"),
        "machine profile noiseCalibration.observedMedianNs",
    )
    if observed_median == 0:
        raise ProtocolError("machine profile noiseCalibration.observedMedianNs must be greater than zero")
    _require_non_negative_number(
        calibration.get("observedMadNs"),
        "machine profile noiseCalibration.observedMadNs",
    )
    max_relative_mad = _require_non_negative_number(
        calibration.get("maxCandidateRelativeMad"),
        "machine profile noiseCalibration.maxCandidateRelativeMad",
    )
    if max_relative_mad > 1.0:
        raise ProtocolError("machine profile maxCandidateRelativeMad must not exceed 1")
    absolute_noise_floor = _require_non_negative_number(
        calibration.get("absoluteNoiseFloorNs"),
        "machine profile noiseCalibration.absoluteNoiseFloorNs",
    )
    return max_relative_mad, absolute_noise_floor


def _validate_baseline(
    baseline: dict[str, Any], reference: dict[str, Any], machine_id: str
) -> tuple[float, float]:
    if (
        baseline.get("schemaName") != BASELINE_SCHEMA_NAME
        or baseline.get("schema") != PROFILE_BASELINE_SCHEMA_VERSION
    ):
        raise ProtocolError("baseline uses an unsupported schema")
    _validate_review(baseline, "baseline")
    if baseline.get("machineId") != machine_id:
        raise ProtocolError("baseline machineId does not match the approved machine profile")
    if baseline.get("metric") != METRIC:
        raise ProtocolError(f"baseline metric must be {METRIC}")
    for field in ("workload", "fingerprint", "checksum"):
        if _canonical_json(baseline.get(field)) != _canonical_json(reference[field]):
            raise ProtocolError(f"baseline {field} is incompatible")
    process_count = _require_positive_integer(baseline.get("processCount"), "baseline processCount")
    if process_count < MIN_BASELINE_PROCESSES:
        raise ProtocolError(f"baseline requires at least {MIN_BASELINE_PROCESSES} processes")
    median_ns = _require_non_negative_number(baseline.get("medianNs"), "baseline medianNs")
    if median_ns == 0:
        raise ProtocolError("baseline medianNs must be greater than zero")
    mad_ns = _require_non_negative_number(baseline.get("madNs"), "baseline madNs")
    return median_ns, mad_ns


def analyze_reports(
    reports: Sequence[dict[str, Any]],
    *,
    hard_gate: bool = False,
    machine_profile: dict[str, Any] | None = None,
    baseline: dict[str, Any] | None = None,
    executable_sha256: str | None = None,
) -> tuple[dict[str, Any], int]:
    metrics = validate_compatible_reports(reports)
    median_ns, mad_ns = median_and_mad(metrics)
    relative_mad = 0.0 if median_ns == 0 and mad_ns == 0 else (
        math.inf if median_ns == 0 else mad_ns / median_ns
    )
    reference = reports[0]

    result: dict[str, Any] = {
        "status": "ok",
        "schema": RUNNER_SCHEMA_VERSION,
        "schemaName": RUNNER_SCHEMA_NAME,
        "conclusion": "provisional",
        "mode": "provisional",
        "hardGateEligible": False,
        "protocol": {
            "metric": METRIC,
            "statistic": "run_level_median_and_mad",
            "minimumCandidateProcesses": MIN_CANDIDATE_PROCESSES,
            "minimumBaselineProcesses": MIN_BASELINE_PROCESSES,
            "minimumWarmupIterations": MIN_WARMUP_ITERATIONS,
            "minimumMeasureIterations": MIN_MEASURE_ITERATIONS,
            "relativeRegressionThreshold": RELATIVE_REGRESSION_THRESHOLD,
            "madNoiseMultiplier": MAD_NOISE_MULTIPLIER,
        },
        "candidate": {
            "processCount": len(reports),
            "valuesNs": metrics,
            "medianNs": median_ns,
            "madNs": mad_ns,
            "relativeMad": relative_mad,
        },
        "compatibility": {
            "childSchema": BENCH_SCHEMA_VERSION,
            "workload": reference["workload"],
            "fingerprint": reference["fingerprint"],
            "checksum": reference["checksum"],
        },
        "noiseAssessment": {
            "status": "unassessed",
            "reason": "no_approved_machine_noise_calibration",
        },
        "notes": [
            "independent_processes_sampled_sequentially",
            "default_dev_or_ci_result_is_provisional_not_hard_gate",
        ],
    }
    if executable_sha256 is not None:
        result["compatibility"]["executableSha256"] = executable_sha256
    if not hard_gate:
        return result, 0

    if machine_profile is None or baseline is None:
        raise ProtocolError("hard gate requires both an approved machine profile and baseline")
    if executable_sha256 is None:
        raise ProtocolError("hard gate requires the benchmark executable SHA-256")
    _require_sha256(executable_sha256, "benchmark executable SHA-256")
    if len(reports) < MIN_CANDIDATE_PROCESSES:
        raise ProtocolError(f"hard gate requires at least {MIN_CANDIDATE_PROCESSES} candidate processes")
    _validate_formal_sample_size(reference["workload"])
    if reference["fingerprint"].get("buildType") != "Release":
        raise ProtocolError("hard gate requires a Release tina_bench build")
    notes = reference.get("notes")
    if not isinstance(notes, list) or "tracy_disabled" not in notes:
        raise ProtocolError("hard gate requires tina_bench to report tracy_disabled")

    max_relative_mad, absolute_noise_floor = _validate_machine_profile(
        machine_profile, reference, executable_sha256
    )
    baseline_median, baseline_mad = _validate_baseline(
        baseline, reference, machine_profile["machineId"]
    )
    result["mode"] = "hard"
    result["hardGateEligible"] = True
    result["machineProfile"] = {
        "machineId": machine_profile["machineId"],
        "review": machine_profile["review"],
    }
    result["baseline"] = {
        "processCount": baseline["processCount"],
        "medianNs": baseline_median,
        "madNs": baseline_mad,
        "review": baseline["review"],
    }

    noisy = relative_mad > max_relative_mad
    result["noiseAssessment"] = {
        "status": "rejected" if noisy else "accepted",
        "candidateRelativeMad": relative_mad,
        "maximumRelativeMad": max_relative_mad,
    }
    if noisy:
        result["status"] = "error"
        result["conclusion"] = "hard_gate_rejected_noise"
        return result, 1

    delta_ns = median_ns - baseline_median
    relative_delta = delta_ns / baseline_median
    absolute_noise_threshold = max(
        absolute_noise_floor,
        MAD_NOISE_MULTIPLIER * baseline_mad,
        MAD_NOISE_MULTIPLIER * mad_ns,
    )
    relative_exceeded = delta_ns > baseline_median * RELATIVE_REGRESSION_THRESHOLD
    absolute_exceeded = delta_ns > absolute_noise_threshold
    regression = relative_exceeded and absolute_exceeded
    result["baselineComparison"] = {
        "deltaNs": delta_ns,
        "relativeDelta": relative_delta,
        "absoluteNoiseThresholdNs": absolute_noise_threshold,
        "relativeThresholdExceeded": relative_exceeded,
        "absoluteNoiseThresholdExceeded": absolute_exceeded,
        "regression": regression,
    }
    result["conclusion"] = "hard_gate_fail" if regression else "hard_gate_pass"
    if regression:
        result["status"] = "error"
        return result, 1
    return result, 0


def _read_json(path: Path, name: str) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as stream:
            return _require_object(json.load(stream), name)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProtocolError(f"failed to read {name} {path}: {error}") from error


def _write_result(result: dict[str, Any], output_path: Path | None) -> None:
    encoded = json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    print(encoded)
    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = output_path.with_name(output_path.name + ".tmp")
        temporary.write_text(encoded + "\n", encoding="utf-8")
        temporary.replace(output_path)


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect independent tina_bench processes and evaluate ADR 0018 median/MAD evidence."
    )
    parser.add_argument("--processes", type=int, default=MIN_CANDIDATE_PROCESSES)
    parser.add_argument("--timeout-seconds", type=float, default=300.0)
    parser.add_argument("--machine-profile", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--hard-gate", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("benchmark", help="path to tina_bench executable")
    parser.add_argument(
        "benchmark_args",
        nargs=argparse.REMAINDER,
        help="arguments passed to tina_bench; an optional leading -- is removed",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    benchmark_args = list(args.benchmark_args)
    if benchmark_args and benchmark_args[0] == "--":
        benchmark_args.pop(0)
    command = [args.benchmark, *benchmark_args]
    try:
        if args.hard_gate and (args.machine_profile is None or args.baseline is None):
            raise ProtocolError(
                "hard gate requires --machine-profile and --baseline before benchmark execution"
            )
        profile = _read_json(args.machine_profile, "machine profile") if args.machine_profile else None
        baseline = _read_json(args.baseline, "baseline") if args.baseline else None
        executable_sha256 = benchmark_sha256(args.benchmark)
        reports = collect_reports(command, args.processes, args.timeout_seconds)
        result, exit_code = analyze_reports(
            reports,
            hard_gate=args.hard_gate,
            machine_profile=profile,
            baseline=baseline,
            executable_sha256=executable_sha256,
        )
    except ProtocolError as error:
        result = {
            "status": "error",
            "schema": RUNNER_SCHEMA_VERSION,
            "schemaName": RUNNER_SCHEMA_NAME,
            "conclusion": "provisional",
            "mode": "provisional",
            "hardGateEligible": False,
            "message": str(error),
        }
        exit_code = 2
    _write_result(result, args.output)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
