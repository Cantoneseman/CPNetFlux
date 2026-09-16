#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tools.experiments.beta_matrix.model import (  # noqa: E402
    LinkProfile,
    WorkloadProfile,
    build_method_matrix,
    classify_regime,
    choose_b5_preset,
    choose_b6_preset,
    plan_cpnetflux_method,
)
from tools.experiments.beta_matrix.runner import run_dry_run  # noqa: E402
from tools.experiments.beta_matrix.staging import stage_blocks  # noqa: E402


class BetaMatrixPlannerTest(unittest.TestCase):
    def test_small_file_profile_prefers_control_reuse(self) -> None:
        profile = WorkloadProfile.from_manifest_row(
            "esnet_tiny", {"file_count": 75, "total_mb": 0.2, "domain": "ESnet"}
        )
        self.assertEqual(classify_regime(profile), "small_files")

        plan = plan_cpnetflux_method(profile, LinkProfile(), "cpnetflux_b1_control_reuse")

        self.assertEqual(plan.method, "cpnetflux_b1_control_reuse")
        self.assertEqual(plan.control_reuse_mode, "worker")
        self.assertEqual(plan.transfer_params.session_reuse, True)
        self.assertEqual(plan.planner_preset, "B1_session_reuse")

    def test_b2_proxy_is_explicitly_unsupported(self) -> None:
        profile = WorkloadProfile.from_manifest_row(
            "globus_mix", {"file_count": 48, "total_mb": 60.0, "domain": "Globus"}
        )
        plan = plan_cpnetflux_method(profile, LinkProfile(), "cpnetflux_b2_read_pipeline_proxy")

        self.assertEqual(plan.status, "unsupported")
        self.assertIn("read pipeline", plan.reason)

    def test_method_matrix_contains_cpnetflux_and_gridftp_compression_rows(self) -> None:
        methods = [row.method for row in build_method_matrix()]

        self.assertIn("cpnetflux_b0_baseline", methods)
        self.assertIn("cpnetflux_cpss", methods)
        self.assertIn("gridftp_raw", methods)
        self.assertIn("gridftp_lz4", methods)

    def test_b5_and_b6_presets_exist(self) -> None:
        profile = WorkloadProfile.from_manifest_row(
            "mixed_job", {"file_count": 16, "total_mb": 189.13, "domain": "mixed"}
        )
        b5_name, _, _ = choose_b5_preset(profile, LinkProfile())
        b6_name, _, _ = choose_b6_preset(profile, LinkProfile())

        self.assertIn(b5_name, {"B1_session_reuse", "B2_read_pipeline", "B3_full"})
        self.assertEqual(b6_name, "B3_full")


class BetaMatrixDryRunTest(unittest.TestCase):
    def test_dry_run_writes_required_report_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cpnetflux-beta-matrix.") as temp_text:
            temp = Path(temp_text)
            manifest = temp / "manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "datasets": {
                            "esnet_tiny": {
                                "domain": "ESnet",
                                "file_count": 75,
                                "total_mb": 0.2,
                                "description": "tiny files",
                            }
                        }
                    }
                ),
                encoding="utf-8",
            )
            output = temp / "out"

            result = run_dry_run(manifest_path=manifest, output_dir=output)

            self.assertEqual(result["status"], "pass")
            for name in (
                "summary.csv",
                "dataset_summary.csv",
                "method_comparison.csv",
                "live_metrics.jsonl",
                "audit.json",
                "final_summary_zh.md",
            ):
                self.assertTrue((output / name).is_file(), name)

            with (output / "summary.csv").open(newline="", encoding="utf-8") as handle:
                rows = list(csv.DictReader(handle))
            self.assertTrue(any(row["method"] == "cpnetflux_b2_read_pipeline_proxy" and row["status"] == "unsupported" for row in rows))

    def test_gzip_staging_roundtrip_records_per_block_sha256(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cpnetflux-beta-staging.") as temp_text:
            temp = Path(temp_text)
            source = temp / "source.bin"
            source.write_bytes((b"alpha-beta-gamma\n" * 8) + b"tail")
            staging_root = temp / "staged"

            manifest = stage_blocks(
                source,
                staging_root,
                method="gzip",
                block_size=32,
            )

            self.assertEqual(manifest["method"], "gzip")
            self.assertGreater(len(manifest["blocks"]), 1)
            for block in manifest["blocks"]:
                self.assertTrue(block["sha256_ok"])
                self.assertGreater(block["compressed_size"], 0)
                self.assertTrue(Path(block["compressed_path"]).is_file())
                self.assertTrue(Path(block["restore_path"]).is_file())


if __name__ == "__main__":
    unittest.main()
