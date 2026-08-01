#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_benchmark_gate as gate


FIXTURE_EXECUTABLE_SHA256 = "a" * 64


def make_report(p99: int = 100, *, checksum: str = "stable") -> dict:
    return {
        "status": "ok",
        "schema": 1,
        "schemaName": "tina_bench",
        "conclusion": "provisional",
        "workload": {
            "id": "fixture_v1",
            "version": 1,
            "seed": 7,
            "parameters": {"warmup_frames": 600, "measure_frames": 2000},
        },
        "fingerprint": {
            "buildType": "Release",
            "hostOs": "fixture",
            "taskSystem": "DisabledTaskSystem",
        },
        "timing_ns": {"p99": p99},
        "checksum": checksum,
        "notes": ["tracy_disabled"],
    }


def make_profile(report: dict, *, max_relative_mad: float = 0.20) -> dict:
    return {
        "schema": 1,
        "schemaName": "tina_bench_machine_profile",
        "machineId": "fixture-host",
        "machine": {
            "os": "fixture-os",
            "cpu": "fixture-cpu",
            "gpu": "none",
            "gpuDriver": "none",
            "ramBytes": 16 * 1024 * 1024 * 1024,
            "powerPlan": "fixed",
            "affinity": "0-3",
            "workerCount": 0,
        },
        "build": {
            "gitCommit": "0123456789abcdef0123456789abcdef01234567",
            "gitDirty": False,
            "preset": "fixture-release",
            "configuration": "Release",
            "compiler": "fixture-compiler",
            "linker": "fixture-linker",
            "vcpkgBaseline": "fixture-baseline",
            "binarySha256": FIXTURE_EXECUTABLE_SHA256,
            "tracyEnabled": False,
        },
        "benchmarkFingerprint": copy.deepcopy(report["fingerprint"]),
        "noiseCalibration": {
            "metric": "timing_ns.p99",
            "processCount": 10,
            "observedMedianNs": 100,
            "observedMadNs": 2,
            "maxCandidateRelativeMad": max_relative_mad,
            "absoluteNoiseFloorNs": 5,
        },
        "review": {
            "status": "approved",
            "reviewedBy": "fixture-reviewer",
            "reviewedAt": "2026-08-01",
        },
    }


def make_baseline(report: dict, *, median_ns: int = 100, mad_ns: int = 2) -> dict:
    return {
        "schema": 1,
        "schemaName": "tina_bench_baseline",
        "machineId": "fixture-host",
        "metric": "timing_ns.p99",
        "workload": copy.deepcopy(report["workload"]),
        "fingerprint": copy.deepcopy(report["fingerprint"]),
        "checksum": report["checksum"],
        "processCount": 10,
        "medianNs": median_ns,
        "madNs": mad_ns,
        "review": {
            "status": "approved",
            "reviewedBy": "fixture-reviewer",
            "reviewedAt": "2026-08-01",
        },
    }


class StatisticsTests(unittest.TestCase):
    def test_median_and_mad_are_run_level_statistics(self) -> None:
        median, mad = gate.median_and_mad([80, 90, 100, 110, 120])
        self.assertEqual(100, median)
        self.assertEqual(10, mad)

    def test_default_analysis_remains_provisional(self) -> None:
        reports = [make_report(value) for value in (98, 99, 100, 101, 102)]
        result, exit_code = gate.analyze_reports(reports)
        self.assertEqual(0, exit_code)
        self.assertEqual("provisional", result["conclusion"])
        self.assertFalse(result["hardGateEligible"])
        self.assertEqual("unassessed", result["noiseAssessment"]["status"])


class CompatibilityTests(unittest.TestCase):
    def test_rejects_mismatched_fingerprint(self) -> None:
        reports = [make_report(), make_report()]
        reports[1]["fingerprint"]["hostOs"] = "other"
        with self.assertRaisesRegex(gate.ProtocolError, "incompatible fingerprint"):
            gate.validate_compatible_reports(reports)

    def test_rejects_mismatched_checksum(self) -> None:
        with self.assertRaisesRegex(gate.ProtocolError, "incompatible checksum"):
            gate.validate_compatible_reports([make_report(), make_report(checksum="other")])


class HardGateTests(unittest.TestCase):
    def analyze(self, values: tuple[int, ...], **kwargs):
        reports = [make_report(value) for value in values]
        return gate.analyze_reports(
            reports,
            hard_gate=True,
            machine_profile=kwargs.get("profile", make_profile(reports[0])),
            baseline=kwargs.get("baseline", make_baseline(reports[0])),
            executable_sha256=FIXTURE_EXECUTABLE_SHA256,
        )

    def test_requires_reviewed_profile_and_baseline(self) -> None:
        reports = [make_report(100) for _ in range(5)]
        with self.assertRaisesRegex(gate.ProtocolError, "requires both"):
            gate.analyze_reports(reports, hard_gate=True)

    def test_rejects_unapproved_machine_profile(self) -> None:
        reports = [make_report(100) for _ in range(5)]
        profile = make_profile(reports[0])
        profile["review"]["status"] = "draft"
        with self.assertRaisesRegex(gate.ProtocolError, "machine profile is not approved"):
            gate.analyze_reports(
                reports,
                hard_gate=True,
                machine_profile=profile,
                baseline=make_baseline(reports[0]),
                executable_sha256=FIXTURE_EXECUTABLE_SHA256,
            )

    def test_rejects_baseline_from_another_machine(self) -> None:
        reports = [make_report(100) for _ in range(5)]
        baseline = make_baseline(reports[0])
        baseline["machineId"] = "other-host"
        with self.assertRaisesRegex(gate.ProtocolError, "baseline machineId"):
            gate.analyze_reports(
                reports,
                hard_gate=True,
                machine_profile=make_profile(reports[0]),
                baseline=baseline,
                executable_sha256=FIXTURE_EXECUTABLE_SHA256,
            )

    def test_rejects_stale_benchmark_executable(self) -> None:
        reports = [make_report(100) for _ in range(5)]
        with self.assertRaisesRegex(gate.ProtocolError, "binarySha256"):
            gate.analyze_reports(
                reports,
                hard_gate=True,
                machine_profile=make_profile(reports[0]),
                baseline=make_baseline(reports[0]),
                executable_sha256="b" * 64,
            )

    def test_compatible_quiet_candidate_passes(self) -> None:
        result, exit_code = self.analyze((100, 101, 102, 103, 104))
        self.assertEqual(0, exit_code)
        self.assertEqual("hard_gate_pass", result["conclusion"])
        self.assertTrue(result["hardGateEligible"])

    def test_relative_and_absolute_thresholds_must_both_exceed(self) -> None:
        result, exit_code = self.analyze((111, 111, 111, 111, 111))
        self.assertEqual(1, exit_code)
        self.assertEqual("hard_gate_fail", result["conclusion"])
        comparison = result["baselineComparison"]
        self.assertTrue(comparison["relativeThresholdExceeded"])
        self.assertTrue(comparison["absoluteNoiseThresholdExceeded"])

    def test_absolute_noise_floor_prevents_false_regression(self) -> None:
        reports = [make_report(111) for _ in range(5)]
        profile = make_profile(reports[0])
        profile["noiseCalibration"]["absoluteNoiseFloorNs"] = 20
        result, exit_code = gate.analyze_reports(
            reports,
            hard_gate=True,
            machine_profile=profile,
            baseline=make_baseline(reports[0]),
            executable_sha256=FIXTURE_EXECUTABLE_SHA256,
        )
        self.assertEqual(0, exit_code)
        self.assertEqual("hard_gate_pass", result["conclusion"])
        self.assertFalse(result["baselineComparison"]["absoluteNoiseThresholdExceeded"])

    def test_noisy_candidate_is_rejected_before_comparison(self) -> None:
        reports = [make_report(value) for value in (60, 80, 100, 120, 140)]
        profile = make_profile(reports[0], max_relative_mad=0.10)
        result, exit_code = gate.analyze_reports(
            reports,
            hard_gate=True,
            machine_profile=profile,
            baseline=make_baseline(reports[0]),
            executable_sha256=FIXTURE_EXECUTABLE_SHA256,
        )
        self.assertEqual(1, exit_code)
        self.assertEqual("hard_gate_rejected_noise", result["conclusion"])


class ProcessCollectionTests(unittest.TestCase):
    def test_collects_one_json_report_from_each_independent_process(self) -> None:
        report = make_report(123)
        script = textwrap.dedent(
            f"""
            import json
            print(json.dumps({report!r}, separators=(\",\", \":\")))
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "fixture_bench.py"
            fixture.write_text(script, encoding="utf-8")
            reports = gate.collect_reports([sys.executable, str(fixture)], 3, 10.0)
        self.assertEqual(3, len(reports))
        self.assertEqual([123, 123, 123], gate.validate_compatible_reports(reports))


if __name__ == "__main__":
    unittest.main()
