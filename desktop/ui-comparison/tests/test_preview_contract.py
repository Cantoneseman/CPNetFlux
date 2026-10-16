"""Safety and input equality gates for the explicit design-only launcher."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from fixture_rpc import FixtureRpc, task_for


class PreviewContract(unittest.TestCase):
    def test_preview_never_reports_real_connection_or_success(self):
        for scenario in ["tasks", "scanning", "completed", "failed", "offline", "empty"]:
            rpc = FixtureRpc(scenario)
            self.assertFalse(rpc.connected)
            self.assertFalse(rpc.apiCompatible)
            self.assertTrue(rpc.demoAdapter)
            original = copy.deepcopy(rpc.tasks)
            rpc.create("upload", "/not-read", "not-written", False, "v2", "preview-node")
            rpc.action("transfer.cancel", {})
            rpc.saveProfile({}, False)
            rpc.removeTask("preview-001", "demo")
            self.assertEqual(rpc.tasks, original)
            self.assertIn("禁止", rpc.createError)
            self.assertIn("未发送", rpc.actionError)

    def test_unknown_scanning_metrics_stay_null(self):
        task = task_for("scanning")
        for key in ["bytes_total", "bytes_committed", "speed_bps", "files_total", "eta_seconds"]:
            self.assertIsNone(task["metrics"][key])
        self.assertIsNone(task["backend_evidence"]["checksum"])

    def test_original_source_has_not_changed(self):
        baseline = "8253126c9baaa6737b81afdbf2911802eb6671cd"
        repo = HERE.parents[1]
        # Compare git blobs, so Windows checkout line endings cannot create a false mismatch.
        for path in ["desktop/ui/Main.qml", "desktop/ui/rpc_client.cpp", "desktop/ui/rpc_client.h",
                     "desktop/agent/cpnetflux_agent.py", "desktop/schemas/desktop-agent.schema.json"]:
            actual = subprocess.check_output(["git", "hash-object", "--path=" + path, path], cwd=repo).strip()
            expected = subprocess.check_output(["git", "rev-parse", baseline + ":" + path], cwd=repo).strip()
            self.assertEqual(actual, expected, path)

    def test_candidate_a_preserves_rpc_boundary(self):
        source = (HERE / "candidates/a/Main.qml").read_text(encoding="utf-8")
        for method in ["rpc.create(", 'rpc.action("transfer.cancel"', 'rpc.action("transfer.resume"',
                       "rpc.saveProfile(", "rpc.testNode(", "rpc.refresh()"]:
            self.assertIn(method, source)
        self.assertIn("rpc.apiCompatible && !rpc.stale", source)
        self.assertNotIn("XMLHttpRequest", source)
        self.assertNotIn("indeterminate:", source)

    def test_missing_candidate_is_an_error(self):
        if (HERE / "candidates/b/Main.qml").is_file():
            self.skipTest("B is available")
        result = subprocess.run([sys.executable, "-X", "utf8", str(HERE / "preview.py"), "--variant", "b"], capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(result.returncode, 2)
        self.assertIn("不会自动使用其他方案", result.stderr)


if __name__ == "__main__":
    unittest.main()
