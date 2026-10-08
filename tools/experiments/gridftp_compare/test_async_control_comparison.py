#!/usr/bin/env python3
from __future__ import annotations

import unittest

from tools.experiments.gridftp_compare.compare_async_control_runs import compare_rows


def row(system: str, depth: int, repeat: int, speed: str, *, hash_match: str = "true") -> dict[str, str]:
    return {
        "system": system, "control_pipeline_depth": str(depth),
        "dataset": "tree_dense_128MiB", "direction": "local_to_remote",
        "file_parallelism": "4", "per_file_connections": "1",
        "repeat_index": str(repeat), "result": "pass",
        "hash_match": hash_match, "logical_goodput_mbps": speed,
    }


class AsyncControlComparisonTest(unittest.TestCase):
    def test_compares_only_three_hash_valid_repeats_and_reports_ratios(self) -> None:
        off = [row("gridftp", 0, i, str(100 + i)) for i in range(3)]
        off += [row("cpnetflux", 0, i, str(90 + i)) for i in range(3)]
        pipeline = [row("cpnetflux", 1, i, str(95 + i)) for i in range(3)]
        result = compare_rows(off, pipeline)[0]
        self.assertEqual(result["status"], "PASS_90_PERCENT")
        self.assertAlmostEqual(result["cpnetflux_depth0_ratio"], 91 / 101)
        self.assertAlmostEqual(result["cpnetflux_depth1_ratio"], 96 / 101)

    def test_hash_failure_makes_the_configuration_incomplete(self) -> None:
        off = [row("gridftp", 0, i, "100") for i in range(3)]
        off += [row("cpnetflux", 0, i, "100") for i in range(3)]
        pipeline = [row("cpnetflux", 1, i, "100") for i in range(3)]
        pipeline[0]["hash_match"] = "false"
        self.assertEqual(compare_rows(off, pipeline)[0]["status"], "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()
