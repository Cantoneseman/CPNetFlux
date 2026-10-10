import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from jsonschema import Draft7Validator

D = Path(__file__).resolve().parents[1]
AGENT = Path(os.environ.get("CPNETFLUX_AGENT", D / "agent/cpnetflux_agent.py"))
SCHEMA = json.loads((D / "schemas/desktop-agent.schema.json").read_text())
VALIDATOR = Draft7Validator(SCHEMA)
FIXTURES = json.loads((D / "tests/fixtures.json").read_text())
CREATE = next(x["message"]["params"] for x in FIXTURES["valid"] if x["name"] == "transfer.create")

class Peer:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(3)
        self.sock.connect(str(path))
        self.file = self.sock.makefile("rb")
        self.count = 0
        self.events = []
    def read(self):
        result = json.loads(self.file.readline())
        VALIDATOR.validate(result)
        return result
    def call(self, method, params):
        self.count += 1
        rid = str(self.count)
        self.sock.sendall((json.dumps({"jsonrpc":"2.0", "id":rid, "method":method, "params":params}) + "\n").encode())
        while True:
            msg = self.read()
            if msg.get("id") == rid:
                return msg
            if msg.get("method") == "transfer.event": self.events.append(msg)
    def close(self):
        self.file.close()
        self.sock.close()

class AgentTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(AGENT.exists(), "DESKTOP-SHELL-01 agent not implemented")
        self.tmp = tempfile.TemporaryDirectory()
        self.runtime = Path(self.tmp.name) / "runtime"
        self.state = Path(self.tmp.name) / "state"
        self.path = self.runtime / "agent.sock"
        self.launch()
        self.p = Peer(self.path)
    def launch(self):
        self.proc = subprocess.Popen([sys.executable, str(AGENT), "--demo", "--runtime-dir", str(self.runtime), "--state-dir", str(self.state), "--tick-ms", "30"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        end = time.monotonic() + 5
        while not self.path.exists() and time.monotonic() < end and self.proc.poll() is None:
            time.sleep(.02)
        self.assertTrue(self.path.exists(), "agent socket missing")
    def tearDown(self):
        if hasattr(self, "p"): self.p.close()
        if hasattr(self, "proc"):
            self.proc.terminate()
            self.proc.communicate(timeout=5)
        if hasattr(self, "tmp"): self.tmp.cleanup()
    def create(self, fail=False):
        p = json.loads(json.dumps(CREATE)); p["operation_id"] = "create-" + str(time.monotonic_ns())
        if fail: p["destination"]["path"] = "demo-fail"
        return self.p.call("transfer.create", p)["result"]["snapshot"]
    def mutate(self, method, t, **extra):
        return self.p.call(method, {"transfer_id":t["transfer_id"], "operation_id":str(time.monotonic_ns()), "expected_revision":t["revision"]} | extra)
    def get(self, t): return self.p.call("transfer.get", {"transfer_id":t["transfer_id"]})["result"]["snapshot"]
    def terminal(self, t):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            s = self.get(t)
            if s["state"] in ["completed", "failed", "cancelled", "recoverable"]: return s
            time.sleep(.02)
        self.fail("demo did not reach terminal state")
    def test_schema_permissions_and_bad_frames(self):
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.runtime.stat().st_mode & 0o777, 0o700)
        self.assertEqual(self.p.call("unknown.method", {})["error"]["code"], -32601)
        self.p.sock.sendall(b"{oops}\n")
        self.assertEqual(self.p.read()["error"]["code"], -32700)
        self.p.sock.sendall(b"x" * (1024*1024+1))
        self.assertEqual(self.p.read()["error"]["code"], -32600)
    def test_demo_survives_client_exit_and_completes(self):
        t = self.create()
        self.assertEqual(t["adapter"], "demo")
        self.assertIsNone(t["metrics"]["eta_seconds"])
        self.mutate("transfer.start", t)
        self.p.close(); self.p = Peer(self.path)
        t = self.terminal(t)
        self.assertEqual(t["state"], "completed")
        self.assertTrue(t["commit_confirmed"])
        self.assertEqual(t["metrics"]["bytes_total"], t["metrics"]["bytes_committed"])
    def test_failure_resume_and_unsupported_pause(self):
        t = self.create(True)
        self.assertEqual(self.mutate("transfer.pause", t)["error"]["data"]["code"], "unsupported")
        self.assertEqual(self.get(t)["revision"], t["revision"])
        self.assertEqual(self.mutate("transfer.resume", t, checkpoint_id="bad")["error"]["data"]["code"], "checkpoint_missing")
        self.mutate("transfer.start", t)
        t = self.terminal(t)
        self.assertEqual(t["state"], "failed")
        self.assertEqual(t["recovery"]["eligibility"], "eligible")
        old = t["attempt"]["attempt_id"]
        resumed = self.mutate("transfer.resume", t, checkpoint_id=t["recovery"]["checkpoint_id"])["result"]["snapshot"]
        self.assertNotEqual(resumed["attempt"]["attempt_id"], old)
        self.assertEqual(self.terminal(resumed)["state"], "completed")
    def test_cancel_is_real_for_demo_and_terminal_is_preserved(self):
        t = self.create(); t = self.mutate("transfer.start", t)["result"]["snapshot"]
        t = self.mutate("transfer.cancel", t)["result"]["snapshot"]
        self.assertEqual(self.terminal(t)["state"], "cancelled")
        done = self.create(); self.mutate("transfer.start", done); done = self.terminal(done)
        reply = self.mutate("transfer.cancel", done)["result"]
        self.assertEqual(reply["outcome"], "already_terminal")
        self.assertEqual(reply["snapshot"]["state"], "completed")
    def test_subscription_replay_epoch_and_persistence(self):
        t = self.create()
        sub = self.p.call("transfer.subscribe", {"transfer_ids":[], "agent_epoch":None, "after_sequence":None})["result"]
        self.mutate("transfer.start", t)
        event = (self.p.events[0] if self.p.events else self.p.read())["params"]
        self.assertGreater(event["sequence"], sub["sequence"])
        self.p.close(); self.p = Peer(self.path)
        replay = self.p.call("transfer.subscribe", {"transfer_ids":[], "agent_epoch":sub["agent_epoch"], "after_sequence":sub["sequence"]})["result"]
        self.assertFalse(replay["reset"])
        replayed = self.p.read()["params"]
        self.assertEqual(replayed["sequence"], event["sequence"])
        self.p.close(); self.proc.terminate(); self.proc.communicate(timeout=5)
        self.launch(); self.p = Peer(self.path)
        reset = self.p.call("transfer.subscribe", {"transfer_ids":[], "agent_epoch":sub["agent_epoch"], "after_sequence":event["sequence"]})["result"]
        self.assertTrue(reset["reset"])
        self.assertNotEqual(reset["agent_epoch"], sub["agent_epoch"])
        self.assertEqual(len(reset["snapshots"]), 1)
        self.assertNotEqual(reset["snapshots"][0]["state"], "completed")
    def test_profile_secrets_and_real_probe_unsupported(self):
        profile = next(v["message"]["params"] for v in FIXTURES["valid"] if v["name"] == "profile.create")
        p = json.loads(json.dumps(profile)); p["profile"]["token"] = "forbidden-secret"
        self.assertEqual(self.p.call("profile.create", p)["error"]["code"], -32602)
        self.p.call("profile.create", profile)
        self.assertNotIn("forbidden-secret", (self.state/"state.json").read_text())
        reply = self.p.call("node.testConnection", {"node_id":"n1", "root_id":"home", "path":"", "access":"read"})["result"]
        self.assertFalse(reply["ready"])
        self.assertTrue(all(c["state"] == "unsupported" for c in reply["checks"].values()))
        self.assertEqual(self.p.call("directory.list", {"node_id":"n1", "root_id":"home", "path":"", "cursor":None, "limit":100})["error"]["data"]["code"], "unsupported")

if __name__ == "__main__": unittest.main(verbosity=2)
