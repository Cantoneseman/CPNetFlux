import csv
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from range_observation import export_range_observations, parse_server_logs, write_evidence_bundle

MARKER = "CPNETFLUX_PERSISTENT_TIMING "


def timing_line(files, **extra):
    payload = {
        "batch_id": "batch-safe",
        "channel_index": 2,
        "range_observation_contract": {
            "schema": "cpnetflux_range_observation_v2",
            "metrics": {"file_write_at_all_seconds": {
                "unit": "seconds", "scope": "writeAtAll elapsed", "overlap": "nested"
            }},
        },
        "files": files,
    }
    payload.update(extra)
    return MARKER + json.dumps(payload)


class RangeObservationExportTest(unittest.TestCase):
    def test_exports_v2_metrics_and_redacts_non_allowlisted_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "server.log"
            file_record = {
                "file_id": 7,
                "range_id": 9,
                "attempt": 1,
                "range_observation_enabled": True,
                "range_observation": {
                    "schema_version": 2,
                    "data_frame_count": 12,
                    "effective_data_bytes": 8192,
                    "entry_lock_wait_seconds": 0.001,
                    "file_write_at_all_bytes": 8192,
                    "manifest_save": {
                        "attempts": 3,
                        "serialize_calls": 3,
                        "serialized_bytes": 1200,
                        "save_seconds": 0.02,
                        "temp_write_calls": 3,
                        "publish_calls": 3,
                        "publish_successes": 3,
                        "publish_seconds": 0.01,
                        "failures": 0,
                    },
                    "relative_path": "DO_NOT_EXPORT_PATH",
                    "transfer_id": "DO_NOT_EXPORT_TRANSFER",
                    "payload": "DO_NOT_EXPORT_PAYLOAD",
                },
                "relative_path": "DO_NOT_EXPORT_FILE_PATH",
            }
            raw = (timing_line([file_record]) + "\n").encode()
            log.write_bytes(raw)

            result = export_range_observations([log], root / "export")
            self.assertEqual(result["status"], "complete")
            self.assertEqual(result["counts"], {"enabled": 1})
            row = result["observations"][0]
            self.assertEqual(row["range_id"], 9)
            self.assertEqual(row["metrics"]["data_frame_count"], 12)
            self.assertEqual(row["metrics"]["effective_data_bytes"], 8192)
            self.assertEqual(row["metrics"]["manifest_save_seconds"], 0.02)
            self.assertEqual(row["metrics"]["manifest_publish_seconds"], 0.01)
            self.assertEqual(result["source_logs"][0]["sha256"], hashlib.sha256(raw).hexdigest())

            json_text = (root / "export" / "range-observations.json").read_text()
            csv_text = (root / "export" / "range-observations.csv").read_text()
            for secret in ("DO_NOT_EXPORT_PATH", "DO_NOT_EXPORT_FILE_PATH",
                           "DO_NOT_EXPORT_TRANSFER", "DO_NOT_EXPORT_PAYLOAD"):
                self.assertNotIn(secret, json_text)
                self.assertNotIn(secret, csv_text)
            with (root / "export" / "range-observations.csv").open(newline="") as stream:
                exported = next(csv.DictReader(stream))
            self.assertEqual(exported["file_write_at_all_bytes"], "8192")
            self.assertEqual(exported["observation_status"], "enabled")

    def test_disabled_missing_and_legacy_partial_are_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "server.log"
            rows = [
                {"range_id": 1, "range_observation_enabled": False,
                 "range_observation": {"schema_version": 2, "data_frame_count": 0}},
                {"range_id": 2, "range_observation_enabled": True},
                {"range_id": 3, "range_observation_enabled": True,
                 "range_observation": {"entry_lock_wait_seconds": 0.5}},
            ]
            log.write_text(timing_line(rows) + "\n")
            result = parse_server_logs([log])
            self.assertEqual(result["status"], "partial")
            self.assertEqual(result["counts"], {
                "disabled": 1, "missing": 1, "legacy_partial": 1
            })
            metrics = {row["range_id"]: row["metrics"] for row in result["observations"]}
            self.assertIsNone(metrics[1]["data_frame_count"])
            self.assertIsNone(metrics[2]["entry_lock_wait_seconds"])
            self.assertEqual(metrics[3]["entry_lock_wait_seconds"], 0.5)
            self.assertIsNone(metrics[3]["data_frame_count"])

    def test_historical_log_without_timing_record_stays_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "old-server.log"
            log.write_text("ordinary server output\n")
            result = parse_server_logs([log])
            self.assertEqual(result["status"], "missing")
            self.assertEqual(result["observations"], [])
            self.assertEqual(result["source_logs"][0]["status"], "missing")

    def test_evidence_bundle_uses_only_explicit_safe_allowlist(self):
        import tarfile

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "result.json").write_text("{}\n")
            (root / "range-observations.json").write_text("{}\n")
            (root / "key.pem").write_text("PRIVATE KEY MUST NOT BE PACKAGED")
            (root / "payload.bin").write_bytes(b"payload")
            archive = write_evidence_bundle(root, ["result.json", "range-observations.json"])
            with tarfile.open(archive) as bundle:
                names = set(bundle.getnames())
                self.assertEqual(names, {
                    "result.json", "range-observations.json", "evidence-manifest.json"
                })
                manifest = json.load(bundle.extractfile("evidence-manifest.json"))
            self.assertEqual(len(manifest["included_files"]), 2)
            checksum = Path(str(archive) + ".sha256").read_text()
            self.assertIn(archive.name, checksum)

    def test_missing_source_timing_record_prevents_complete_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            present = root / "present.log"
            missing = root / "missing.log"
            present.write_text(timing_line([{
                "range_id": 1,
                "range_observation_enabled": True,
                "range_observation": {"schema_version": 2, "data_frame_count": 1},
            }]) + "\n")
            missing.write_text("no timing marker here\n")
            result = parse_server_logs([present, missing])
            self.assertEqual(result["status"], "partial")
            self.assertEqual(result["counts"], {"enabled": 1})

    def test_evidence_bundle_rejects_symlink_entries(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as outside:
            root = Path(directory)
            secret = Path(outside) / "secret.pem"
            secret.write_text("private material")
            (root / "result.json").symlink_to(secret)
            with self.assertRaises(ValueError):
                write_evidence_bundle(root, ["result.json"])

    def test_malformed_timing_line_is_counted_and_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "server.log"
            log.write_text(MARKER + "{broken}\n")
            result = parse_server_logs([log])
            self.assertEqual(result["status"], "missing")
            self.assertEqual(result["source_logs"][0]["malformed_records"], 1)


if __name__ == "__main__":
    unittest.main()