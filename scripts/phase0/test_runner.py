"""Gate regression tests use child processes, not mocks of test outcomes."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

import run


class BaselineGateTests(unittest.TestCase):
    def inventory(self):
        return [{"name": name, "command": [sys.executable],
                 "properties": [{"name": "LABELS", "value": ["host"]}]}
                for name in sorted(run.expected_names(os.name == "nt"))]

    def child(self, code, will_fail=False, timeout=5, label="host"):
        test = {"name": "injected", "command": [sys.executable, "-c", code],
                "properties": [{"name": "LABELS", "value": [label]},
                               {"name": "WILL_FAIL", "value": will_fail}]}
        with tempfile.TemporaryDirectory() as folder:
            return run.run_test(test, Path(folder), timeout, os.environ.copy())

    def test_complete_inventory(self):
        run.verify_inventory(self.inventory(), os.name == "nt")

    def test_missing_added_duplicate_inventory_fails(self):
        original = self.inventory()
        for changed in (original[1:], original + [original[0]], original + [{"name": "unexpected"}]):
            with self.subTest(names=[x["name"] for x in changed]), self.assertRaises(ValueError):
                run.verify_inventory(changed, os.name == "nt")

    def test_unclassified_test_fails(self):
        tests = self.inventory()
        tests[0]["properties"] = []
        with self.assertRaises(ValueError):
            run.verify_inventory(tests, os.name == "nt")

    def test_controlled_output_mismatch_fails(self):
        result, output = self.child('print("expected=42 actual=11"); raise SystemExit(1)')
        self.assertEqual(result["status"], "fail")
        self.assertIsNone(run.matching_known(result, output, [], "windows"))
        self.assertEqual(run.unexpected_failures([result]), ["injected"])

    def test_success_and_expected_error(self):
        self.assertEqual(self.child('print("ok")')[0]["status"], "pass")
        self.assertEqual(self.child('raise SystemExit(2)', will_fail=True)[0]["status"], "pass")
        self.assertEqual(self.child('raise SystemExit(0)', will_fail=True)[0]["status"], "fail")

    def test_signals_cannot_satisfy_will_fail(self):
        for code in (-6, 0xc0000005, -1073741819):
            self.assertEqual(run.classify(code, False, True), "crash")

    def test_unavailable_requires_gpu_marker_and_exit_code(self):
        code = 'print("PHASE0_UNAVAILABLE required extension absent"); raise SystemExit(77)'
        result, _ = self.child(code, label="gpu")
        self.assertEqual(result["status"], "unavailable")
        self.assertEqual(result["reason"], "required extension absent")
        self.assertEqual(self.child(code)[0]["status"], "fail")
        self.assertEqual(self.child('raise SystemExit(77)', label="gpu")[0]["status"], "fail")
        self.assertEqual(self.child(code.replace('Exit(77)', 'Exit(1)'), label="gpu")[0]["status"], "fail")

    def test_actual_timeout_is_not_pass(self):
        result, _ = self.child('import time; time.sleep(30)', will_fail=True, timeout=0.1)
        self.assertEqual(result["status"], "timeout")
        self.assertLess(result["duration_seconds"], 5)

    def test_missing_binary_is_not_pass(self):
        test = {"name": "missing", "command": ["phase0-nonexistent-test-binary"],
                "properties": [{"name": "LABELS", "value": ["host"]}, {"name": "WILL_FAIL", "value": True}]}
        with tempfile.TemporaryDirectory() as folder:
            result, _ = run.run_test(test, Path(folder), 1, os.environ.copy())
        self.assertEqual(result["status"], "missing_executable")

    def test_allowance_cannot_hide_a_different_failure(self):
        result = {"name": "one", "status": "fail", "returncode": 1}
        known = [{"id": "P0-TEST", "test": "one", "platform": "windows", "status": "fail", "returncode": 1,
                  "signature": r'^exact expected=42 actual=11$'}]
        self.assertEqual(run.matching_known(result, "exact expected=42 actual=11", known, "windows"), "P0-TEST")
        for change in ({"name": "two"}, {"status": "crash"}, {"returncode": 2}):
            changed = dict(result, **change)
            self.assertIsNone(run.matching_known(changed, "exact expected=42 actual=11", known, "windows"))
        self.assertIsNone(run.matching_known(result, "different failure", known, "windows"))
        self.assertIsNone(run.matching_known(result, "exact expected=42 actual=11", known, "linux"))


if __name__ == "__main__":
    unittest.main()
