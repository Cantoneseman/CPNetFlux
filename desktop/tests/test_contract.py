"""Offline draft contract gate. Does not implement or test a live transfer backend."""
import copy
import json
from pathlib import Path
import unittest
from jsonschema import Draft7Validator, FormatChecker, ValidationError

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / "schemas/desktop-agent.schema.json").read_text())
VECTORS = json.loads((ROOT / "tests/fixtures.json").read_text())
VALIDATOR = Draft7Validator(SCHEMA, format_checker=FormatChecker())
ERROR_CODES = {
    -32001: {"unsupported"}, -32002: {"invalid_state", "revision_conflict"},
    -32003: {"not_found"}, -32004: {"auth_failed", "keyring_locked"},
    -32005: {"tls_identity_failed"}, -32006: {"permission_denied"},
    -32007: {"data_port_unreachable"}, -32008: {"capability_mismatch"},
    -32009: {"checkpoint_missing"}, -32010: {"source_changed"},
    -32011: {"cursor_expired"}, -32012: {"protocol_error"},
    -32700: {"protocol_error"}, -32600: {"protocol_error"},
    -32601: {"protocol_error"}, -32602: {"protocol_error"},
    -32603: {"internal_error"},
}

def snapshot_semantics(s):
    m = s["metrics"]
    for committed, total in [("bytes_committed", "bytes_total"),
                             ("files_committed", "files_total")]:
        if m[committed] is not None and m[total] is not None:
            if m[committed] > m[total]:
                raise ValueError("committed exceeds total")
    if s["state"] == "completed":
        if m["bytes_committed"] != m["bytes_total"]:
            raise ValueError("partial bytes cannot complete")
        if m["files_committed"] + m["files_skipped"] != m["files_total"]:
            raise ValueError("partial files cannot complete")
        if s["policy"]["checksum_policy"] != "none" and s["integrity_status"] != "pass":
            raise ValueError("required checksum missing")
    if s["actual_mode"] == "v1" and s["policy"]["requested_mode"] == "v2":
        if s["policy"]["fallback_policy"] == "reject":
            raise ValueError("fallback forbidden")
    if s["state"] in {"paused", "recoverable"} and s["recovery"]["eligibility"] != "eligible":
        raise ValueError("recovery evidence missing")

def validate(message):
    VALIDATOR.validate(message)
    if "error" in message:
        e = message["error"]
        if e["data"]["code"] not in ERROR_CODES[e["code"]]:
            raise ValueError("error code mismatch")
    result = message.get("result", {})
    snapshots = result.get("snapshots", [])
    if "snapshot" in result:
        snapshots = [result["snapshot"]]
    if result.get("kind") == "subscription" and not result["reset"] and snapshots:
        raise ValueError("replay subscription has snapshots")
    for s in snapshots:
        snapshot_semantics(s)
    if message.get("method") == "transfer.event":
        p = message["params"]
        entity = p["payload"]
        if entity.get("transfer_id") != p["transfer_id"]:
            raise ValueError("transfer identity mismatch")
        if p["type"].startswith("transfer.") and p["type"] != "transfer.error":
            snapshot_semantics(entity)
            if entity["revision"] != p["revision"] or entity["attempt"]["attempt_id"] != p["attempt_id"]:
                raise ValueError("snapshot event mismatch")
        elif p["type"] != "transfer.error" and entity["attempt_id"] != p["attempt_id"]:
            raise ValueError("attempt identity mismatch")
        if p["type"] == "range.state" and entity["bytes_transferred"] is not None:
            if entity["bytes_transferred"] > entity["length"]:
                raise ValueError("range bytes exceed length")

class ContractTests(unittest.TestCase):
    def test_schema(self):
        Draft7Validator.check_schema(SCHEMA)

    def test_valid_messages(self):
        for vector in VECTORS["valid"]:
            with self.subTest(vector=vector["name"]):
                validate(vector["message"])

    def test_rejected_messages(self):
        for vector in VECTORS["invalid"]:
            with self.subTest(vector=vector["name"]):
                if vector["semantic"]:
                    VALIDATOR.validate(vector["message"])
                with self.assertRaises((ValidationError, ValueError)):
                    validate(vector["message"])

    def test_method_coverage(self):
        methods = {v["message"].get("method") for v in VECTORS["valid"]}
        required = {"node.testConnection", "node.capabilities", "directory.list",
                    "transfer.create", "transfer.start", "transfer.pause",
                    "transfer.cancel", "transfer.resume", "transfer.get",
                    "transfer.subscribe", "transfer.list", "profile.list",
                    "profile.create", "profile.update", "profile.delete"}
        self.assertTrue(required <= methods)

    def test_recovery_attempt_and_cancel_contract_vectors(self):
        by_name = {v["name"]: v["message"] for v in VECTORS["valid"]}
        pending = by_name["cancel_pending"]["result"]["snapshot"]
        self.assertEqual(pending["state"], "transferring")
        self.assertEqual(pending["attempt"]["state"], "cancelling")
        terminal = by_name["cancel_completed"]["result"]
        self.assertEqual(terminal["outcome"], "already_terminal")
        self.assertEqual(terminal["snapshot"]["state"], "completed")
        recovered = copy.deepcopy(by_name["recovery_eligible"]["result"]["snapshot"])
        old_attempt = copy.deepcopy(recovered["attempt"])
        recovered.update(state="queued", revision=3)
        recovered["attempt"].update(attempt_id="a2", attempt_number=2, state="queued", cancel_requested=False)
        validate({"jsonrpc": "2.0", "id": "resume1", "result": {"kind": "operation", "outcome": "accepted", "snapshot": recovered}})
        self.assertNotEqual(old_attempt["attempt_id"], recovered["attempt"]["attempt_id"])
        self.assertEqual(by_name["unsupported"]["error"]["code"], -32001)

    def test_reconnect_contract_vectors(self):
        by_name = {v["name"]: v["message"]["result"] for v in VECTORS["valid"] if "result" in v["message"]}
        initial, replay, reset = [by_name[k] for k in ("initial_sync", "replay_sync", "epoch_reset")]
        self.assertTrue(initial["reset"])
        self.assertFalse(replay["reset"])
        self.assertEqual(replay["snapshots"], [])
        self.assertEqual(replay["sequence"], initial["sequence"])
        self.assertNotEqual(initial["agent_epoch"], reset["agent_epoch"])
        self.assertTrue(reset["reset"])
        # Snapshot watermark is 4; example following events begin at 5.
        events = [v["message"]["params"] for v in VECTORS["valid"] if v["message"].get("method") == "transfer.event"]
        self.assertEqual([p["sequence"] for p in events], list(range(5, 11)))

if __name__ == "__main__":
    unittest.main(verbosity=2)
