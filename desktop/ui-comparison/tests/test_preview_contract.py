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

    def test_candidate_b_preserves_rpc_boundary_and_is_independent(self):
        source = (HERE / "candidates/b/Main.qml").read_text(encoding="utf-8")
        baseline = subprocess.check_output(
            ["git", "show", "8253126c9baaa6737b81afdbf2911802eb6671cd:desktop/ui/Main.qml"],
            cwd=HERE.parents[1], text=True, encoding="utf-8")
        for method in ["rpc.create(", 'rpc.action("transfer.cancel"', 'rpc.action("transfer.resume"',
                       "rpc.saveProfile(", "rpc.testNode(", "rpc.refresh()"]:
            self.assertIn(method, source)
        self.assertIn("rpc.apiCompatible && !rpc.stale", source)
        self.assertNotIn("XMLHttpRequest", source)
        self.assertNotIn("indeterminate:", source)
        self.assertNotEqual(source, (HERE / "candidates/a/Main.qml").read_text(encoding="utf-8"))
        self.assertEqual(source.count("rpc.create("), baseline.count("rpc.create("))

    def test_candidate_b_is_a_distinct_control_console_with_truthful_gaps(self):
        source = (HERE / "candidates/b/Main.qml").read_text(encoding="utf-8")
        for page in ["任务控制台", "新建传输", "任务指挥", "节点与身份", "文件与分段", "历史任务", "能力与诊断"]:
            self.assertIn(page, source)
        for fake_profile_value in ["node.example", "/home/user/.config/cpnetflux/ca.pem",
                                   "secret-service:cpnetflux/research-node", 'text: "research-node"']:
            self.assertNotIn(fake_profile_value, source)
        for reserved_action in ["选择客户端证书", "选择私钥", "选择 token 文件", "查看 agent 日志"]:
            self.assertIn(f'text: "{reserved_action}"; enabled: false', source)
        self.assertIn("恢复", source)
        self.assertIn("agent API 当前不支持", source)
        self.assertIn("root.selectedProfile().default_root_id", source)

    def test_candidates_are_explicitly_present(self):
        self.assertTrue((HERE / "candidates/a/Main.qml").is_file())
        self.assertTrue((HERE / "candidates/b/Main.qml").is_file())

        # A missing candidate must remain an explicit launcher error; preview.py
        # never silently substitutes another candidate or fixture mode.
        result = subprocess.run([sys.executable, "-X", "utf8", str(HERE / "preview.py"), "--variant", "missing"], capture_output=True, text=True, encoding="utf-8")
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
