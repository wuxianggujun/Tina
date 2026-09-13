"""Regression tests for fresh, noninteractive GoogleTest evidence (no engine windows)."""

from __future__ import annotations

from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import run_unified_tests as runner


def report(*, outcome: str = "passed", count: int = 1) -> dict:
    failed = int(outcome == "failed" and count != 0)
    disabled = int(outcome == "disabled" and count != 0)
    case = {"name": "Example", "status": "RUN", "result": "COMPLETED"}
    if failed:
        case["failures"] = [{"failure": "expected failure", "type": ""}]
    if outcome == "disabled":
        case.update(status="NOTRUN", result="SUPPRESSED")
    if outcome == "skipped":
        case["result"] = "SKIPPED"
    return {
        "name": "AllTests", "tests": count, "failures": failed, "disabled": disabled, "errors": 0,
        "testsuites": [{
            "name": "Fixture", "tests": count, "failures": failed, "disabled": disabled, "errors": 0,
            "testsuite": [case] if count else [],
        }],
    }


class ReportValidationTests(unittest.TestCase):
    def test_complete_cases_are_counted(self):
        actual = runner.validate_report(report())
        self.assertEqual(actual["tests"], 1)
        self.assertEqual(actual["completed"], 1)
        self.assertEqual(actual["skipped"], 0)

    def test_skipped_disabled_and_failed_cases_are_distinct(self):
        for outcome, counter in (("skipped", "skipped"), ("disabled", "disabled"), ("failed", "failures")):
            with self.subTest(outcome=outcome):
                self.assertEqual(runner.validate_report(report(outcome=outcome))[counter], 1)

    def test_totals_cannot_hide_a_failed_case(self):
        value = report(outcome="failed")
        value["failures"] = 0
        with self.assertRaises(ValueError):
            runner.validate_report(value)

    def test_suite_counts_cannot_hide_a_failed_case(self):
        value = report(outcome="failed")
        value["testsuites"][0]["failures"] = 0
        with self.assertRaises(ValueError):
            runner.validate_report(value)

    def test_partial_unknown_or_duplicate_cases_are_rejected(self):
        for result in (None, "RUNNING", "UNKNOWN"):
            with self.subTest(result=result):
                value = report()
                value["testsuites"][0]["testsuite"][0]["result"] = result
                with self.assertRaises(ValueError):
                    runner.validate_report(value)
        value = report()
        value["tests"] = 2
        value["testsuites"][0]["tests"] = 2
        value["testsuites"][0]["testsuite"] *= 2
        with self.assertRaises(ValueError):
            runner.validate_report(value)

    def test_invalid_counts_and_missing_structure_are_rejected(self):
        for invalid in (True, -1, 1.0, "1", None):
            with self.subTest(invalid=invalid):
                value = report()
                value["tests"] = invalid
                with self.assertRaises(ValueError):
                    runner.validate_report(value)
        for value in (None, [], {}, {"name": "Other"}, {"name": "AllTests", "tests": 1}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                runner.validate_report(value)
        value = report()
        value["testsuites"][0]["testsuite"] = []
        with self.assertRaises(ValueError):
            runner.validate_report(value)


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="tina_runner_test_")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "evidence"
        self.output.mkdir()
        self.entry = {"target": "fixture_test", "path": sys.executable}
        self.launched_command = None
        self.launched_environment = None

    def run_script(self, body: str, *, value: object | None = None, timeout: float = 5) -> dict:
        script = self.root / "fixture.py"
        script.write_text(
            "import json, os, pathlib, sys, time\n"
            "sys.stdout.reconfigure(encoding='utf-8')\n"
            "output = pathlib.Path(next(a.split('json:', 1)[1] for a in sys.argv if a.startswith('--gtest_output=')))\n"
            + (f"output.write_text({json.dumps(value)!r}, encoding='utf-8')\n" if value is not None else "")
            + body + "\n",
            encoding="utf-8",
        )
        real_popen = subprocess.Popen

        def launch(command, **kwargs):
            self.launched_command = command
            self.launched_environment = kwargs["env"]
            # Preserve the runner's actual process options, but make Python act
            # as a tiny GoogleTest fixture rather than consume GoogleTest flags.
            return real_popen([sys.executable, str(script), *command[1:]], **kwargs)

        with mock.patch.object(runner.subprocess, "Popen", side_effect=launch):
            return runner.run_test(self.entry, self.output, timeout)

    def test_pass_requires_started_process_exit_zero_and_complete_report(self):
        result = self.run_script("print('无窗口测试证据', flush=True)", value=report())
        self.assertEqual(result["status"], "passed")
        self.assertTrue(result["started"])
        self.assertGreater(result["processId"], 0)
        self.assertEqual(result["exitCode"], 0)
        self.assertTrue(result["hasGoogleTestReport"])
        self.assertEqual(result["completed"], 1)
        self.assertEqual(len(result["sha256"]), 64)
        self.assertIn("无窗口测试证据", Path(result["log"]).read_text(encoding="utf-8"))

    def test_missing_executable_is_not_run_or_passed(self):
        self.entry["path"] = str(self.root / "missing.exe")
        result = runner.run_test(self.entry, self.output, 1)
        self.assertFalse(result["started"])
        self.assertIsNone(result["exitCode"])
        self.assertEqual(result["status"], "launch_error")

    def test_exit_zero_without_report_is_not_passed(self):
        result = self.run_script("pass")
        self.assertEqual(result["status"], "invalid_report")
        self.assertFalse(result["hasGoogleTestReport"])

    def test_zero_cases_are_not_a_successful_gate(self):
        result = self.run_script("pass", value=report(count=0))
        self.assertEqual(result["status"], "no_tests")

    def test_skip_is_incomplete_not_passed(self):
        result = self.run_script("pass", value=report(outcome="skipped"))
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["skipped"], 1)

    def test_disabled_is_incomplete_not_passed(self):
        result = self.run_script("pass", value=report(outcome="disabled"))
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["disabled"], 1)

    def test_reported_failure_cannot_be_overridden_by_exit_zero(self):
        result = self.run_script("pass", value=report(outcome="failed"))
        self.assertEqual(result["status"], "failed")

    def test_nonzero_exit_cannot_be_overridden_by_a_pass_report(self):
        result = self.run_script("sys.exit(7)", value=report())
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["exitCode"], 7)

    def test_corrupt_or_contradictory_json_is_not_passed(self):
        result = self.run_script("output.write_text('{broken', encoding='utf-8')")
        self.assertEqual(result["status"], "invalid_report")
        self.assertIsNotNone(result["reportError"])

    def test_contradictory_counts_are_not_passed(self):
        value = report()
        value["tests"] = 2
        result = self.run_script("pass", value=value)
        self.assertEqual(result["status"], "invalid_report")

    def test_oversized_report_is_not_parsed(self):
        with mock.patch.object(runner, "MAX_REPORT_BYTES", 8):
            result = self.run_script("pass", value=report())
        self.assertEqual(result["status"], "invalid_report")
        self.assertIn("byte budget", result["reportError"])

    def test_timeout_is_failed_and_owned_child_is_reaped(self):
        result = self.run_script("print('started', flush=True)\ntime.sleep(30)", timeout=0.2)
        self.assertTrue(result["started"])
        self.assertTrue(result["timedOut"])
        self.assertEqual(result["status"], "timeout")
        self.assertIsNotNone(result["exitCode"])
        self.assertIsNone(result["cleanupError"])

    def test_changed_executable_cannot_reuse_pass_report(self):
        with mock.patch.object(runner, "hash_file", side_effect=["before", "after"]):
            result = self.run_script("pass", value=report())
        self.assertEqual(result["status"], "invalid_report")
        self.assertIn("changed", result["reportError"])

    def test_cleanup_error_is_never_passed(self):
        with mock.patch.object(runner, "terminate_owned_process", side_effect=RuntimeError("cleanup fixture")):
            result = self.run_script("pass", value=report())
        self.assertEqual(result["status"], "cleanup_error")
        self.assertIn("cleanup fixture", result["cleanupError"])

    def test_previous_evidence_is_not_overwritten(self):
        existing = self.output / "fixture_test.json"
        existing.write_text(json.dumps(report()), encoding="utf-8")
        original = existing.read_bytes()
        with self.assertRaises(FileExistsError):
            runner.run_test(self.entry, self.output, 1)
        self.assertEqual(existing.read_bytes(), original)
        self.assertFalse((self.output / "fixture_test.log").exists())

    def test_gtest_environment_cannot_shrink_or_break_the_run(self):
        overrides = {"GTEST_FILTER": "Nothing", "GTEST_TOTAL_SHARDS": "8", "GTEST_SHARD_INDEX": "7",
                     "GTEST_REPEAT": "0", "GTEST_BREAK_ON_FAILURE": "1"}
        with mock.patch.dict(os.environ, overrides):
            result = self.run_script("assert not any(k.upper().startswith('GTEST_') for k in os.environ)", value=report())
        self.assertEqual(result["status"], "passed")
        self.assertIn("--gtest_filter=*", self.launched_command)
        self.assertIn("--gtest_repeat=1", self.launched_command)
        self.assertIn("--gtest_break_on_failure=0", self.launched_command)

    def test_invalid_target_cannot_escape_evidence_directory(self):
        self.entry["target"] = "../escape"
        with self.assertRaises(ValueError):
            runner.run_test(self.entry, self.output, 1)

    def test_main_records_selected_targets_and_cannot_overwrite_summary(self):
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps({
            "schemaVersion": 1, "buildId": "fixture", "configuration": "Release",
            "tests": [self.entry, {"target": "unselected", "path": sys.executable}],
        }), encoding="utf-8")
        args = ["run_unified_tests.py", "--manifest", str(manifest), "--output", str(self.output),
                "--target", self.entry["target"]]
        with mock.patch.object(sys, "argv", args), mock.patch.object(
            runner, "run_test", return_value={"target": self.entry["target"], "status": "not_run"}
        ) as run:
            self.assertEqual(runner.main(), 1)
            run.assert_called_once()
            summary = json.loads((self.output / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "failed")
            self.assertEqual(summary["selectedTargets"], ["fixture_test"])
            self.assertEqual(summary["schemaVersion"], 2)
            with self.assertRaises(FileExistsError):
                runner.main()


if __name__ == "__main__":
    unittest.main()
