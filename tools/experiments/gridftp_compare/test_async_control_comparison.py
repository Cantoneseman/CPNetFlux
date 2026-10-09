#!/usr/bin/env python3
from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.experiments.gridftp_compare.compare_async_control_runs import (
    compare_rows, optional_float, validate_plan, valid_goodput,
)
from tools.experiments.gridftp_compare.runner import (
    build_cases, build_parser, build_tree_client_command, run_link_baseline,
)

DIRECTIONS = ["local_to_remote", "remote_to_local"]


def row(system, depth, repeat, speed=100, direction="local_to_remote"):
    return {
        "case_id": f"{system}-{depth}-{direction}-{repeat}",
        "system": system, "control_pipeline_depth": str(depth),
        "dataset": "tree_dense_128MiB", "direction": direction,
        "file_parallelism": "1", "per_file_connections": "1",
        "repeat_index": str(repeat), "result": "pass", "hash_match": "true",
        "integrity_status": "pass", "logical_goodput_mbps": str(speed),
        "elapsed_seconds": str(128 * 1024 * 1024 * 8 / (speed * 1e6)),
    }


def fixture():
    baseline = [row(system, 0, repeat, 100 if system == "gridftp" else 80, direction)
                for direction in DIRECTIONS for system in ("gridftp", "cpnetflux")
                for repeat in range(3)]
    depths = {depth: [row("cpnetflux", depth, repeat, 90 + depth, direction)
                      for direction in DIRECTIONS for repeat in range(3)]
              for depth in (1, 2, 4)}
    return baseline, depths


class AsyncControlComparisonTest(unittest.TestCase):
    def test_complete_two_directions_have_30_unique_repeats(self):
        baseline, depths = fixture()
        results, repeats = compare_rows(baseline, depths)
        self.assertEqual(len(results), 6)
        self.assertEqual(len(repeats), 30)
        self.assertEqual(len({(r["system"], r["depth"], r["direction"], r["repeat_index"])
                              for r in repeats}), 30)
        self.assertTrue(all(r["status"] == "COMPLETE" for r in results))
        self.assertTrue(all(r["wall_per_file_decreased_vs_depth0"] for r in results))
        depth1 = results[0]
        self.assertAlmostEqual(depth1["cpnetflux_gridftp_ratio"], .91)
        self.assertAlmostEqual(depth1["depth_vs_depth0_delta"], 91 / 80 - 1)

    def test_missing_baseline_or_depth_repeats_never_produce_complete_ratio(self):
        for missing in ("gridftp", "cpnetflux", "depth"):
            with self.subTest(missing=missing):
                baseline, depths = fixture()
                if missing == "depth":
                    depths[1].pop(0)
                else:
                    baseline.remove(next(r for r in baseline if r["system"] == missing))
                results, _ = compare_rows(baseline, depths)
                first = results[0]
                self.assertEqual(first["status"], "INCOMPLETE")
                self.assertIsNone(first["cpnetflux_gridftp_ratio"])
                if missing != "gridftp":
                    self.assertIsNone(first["depth_vs_depth0_delta"])

    def test_hash_failure_duplicate_and_nonfinite_results(self):
        baseline, depths = fixture()
        depths[1][0]["hash_match"] = "false"
        self.assertEqual(compare_rows(baseline, depths)[0][0]["status"], "INCOMPLETE")
        depths[1].append(copy.deepcopy(depths[1][0]))
        with self.assertRaises(ValueError):
            compare_rows(baseline, depths)
        for bad in ("inf", "nan", "-1"):
            sample = row("cpnetflux", 1, 0)
            sample["logical_goodput_mbps"] = bad
            self.assertIsNone(valid_goodput(sample))

    def test_zero_stage_duration_and_missing_are_distinct(self):
        self.assertEqual(optional_float({"duration": 0}, ("duration",)), 0)
        self.assertIsNone(optional_float({"duration": None}, ("duration",)))
        baseline, depths = fixture()
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            case = run / "cases" / depths[1][0]["case_id"]
            case.mkdir(parents=True)
            (case / "client_summary.json").write_text(json.dumps({
                "control_prepare_seconds": 0, "transfer_complete_wait_seconds": .1
            }))
            _, repeats = compare_rows(baseline, depths, summary_sources={("cpnetflux", 1): run})
            found = next(r for r in repeats if r["system"] == "cpnetflux" and r["depth"] == 1)
            self.assertEqual(found["control_prepare_seconds"], 0)
            self.assertEqual(found["transfer_complete_wait_seconds"], .1)

    def test_exact_case_plan_and_phase_timing(self):
        total = 0
        for depth in (0, 1, 2, 4):
            systems = ["cpnetflux", "gridftp"] if depth == 0 else ["cpnetflux"]
            cases = build_cases(stage="async-control", systems=systems, directions=DIRECTIONS,
                                repeat=3, scheduler_repeat=1, io_repeat=1, control_reuse="worker",
                                control_pipeline_depth=depth, case_preset="async-control-short")
            total += len(cases)
            self.assertTrue(all(c.dataset == "tree_dense_128MiB" and c.file_parallelism == 1
                                and c.per_file_connections == 1 and c.checksum == "none"
                                and c.compression == "off" for c in cases))
            args = build_parser().parse_args(["--stage", "async-control", "--case-preset",
                                              "async-control-short", "--phase-timing", "on"])
            args.active_control_port = 21210
            for case in cases:
                if case.system == "cpnetflux":
                    cmd = build_tree_client_command(args=args, case=case, case_dir=Path("/tmp/test"),
                                                    source_dir="/tmp/source", dest_dir="/tmp/dest")
                    self.assertEqual(cmd[cmd.index("--phase-timing") + 1], "on")
            with tempfile.TemporaryDirectory() as tmp:
                run = Path(tmp)
                plan = {"case_count": len(cases), "cases": [c.to_dict() for c in cases]}
                (run / "case_plan.json").write_text(json.dumps(plan))
                kwargs = dict(systems=systems, depths={depth}, directions=DIRECTIONS, repeats=3,
                              dataset="tree_dense_128MiB", file_parallelism=1, connections=1)
                self.assertEqual(validate_plan(run, **kwargs)[0], "PASS")
                plan["cases"].pop()
                (run / "case_plan.json").write_text(json.dumps(plan))
                self.assertEqual(validate_plan(run, **kwargs)[0], "PLAN_MISMATCH")
        self.assertEqual(total, 30)

    def test_short_matrix_does_not_launch_additional_iperf(self):
        args = build_parser().parse_args(["--stage", "async-control", "--case-preset",
                                          "async-control-short"])
        with tempfile.TemporaryDirectory() as tmp, patch(
            "tools.experiments.gridftp_compare.runner.run_remote_capture",
            side_effect=AssertionError("unexpected remote command"),
        ):
            result = run_link_baseline(args, Path(tmp), "test")
            self.assertEqual(result["status"], "not_run_short_matrix")


if __name__ == "__main__":
    unittest.main()
