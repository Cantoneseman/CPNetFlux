#!/usr/bin/env python3
"""Lightweight tests for alpha demo helper scripts."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

import make_demo_dataset
import run_alpha_demo


def directory_size(root: Path) -> int:
    return sum(path.stat().st_size for path in root.rglob("*") if path.is_file())


def test_dataset_is_deterministic() -> None:
    with tempfile.TemporaryDirectory(prefix="cpnetflux-demo-dataset.") as temp_text:
        base = Path(temp_text)
        one = base / "one"
        two = base / "two"
        manifest_one = make_demo_dataset.make_dataset(one, profile="tiny", seed=123)
        manifest_two = make_demo_dataset.make_dataset(two, profile="tiny", seed=123)
        if manifest_one["single"]["sha256"] != manifest_two["single"]["sha256"]:
            raise AssertionError("single file hash is not deterministic")
        if manifest_one["tree_mixed"]["tree_hash"] != manifest_two["tree_mixed"]["tree_hash"]:
            raise AssertionError("mixed tree hash is not deterministic")
        if manifest_one["tree_small"]["tree_hash"] != manifest_two["tree_small"]["tree_hash"]:
            raise AssertionError("small tree hash is not deterministic")


def test_profiles_are_size_bounded_and_ordered() -> None:
    with tempfile.TemporaryDirectory(prefix="cpnetflux-demo-profiles.") as temp_text:
        base = Path(temp_text)
        totals = []
        for profile in ["tiny", "small", "mixed"]:
            root = base / profile
            make_demo_dataset.make_dataset(root, profile=profile, seed=123)
            totals.append(directory_size(root))
        if not (totals[0] < totals[1] < totals[2]):
            raise AssertionError(f"profile sizes are not ordered: {totals}")
        if totals[-1] > 80 * 1024 * 1024:
            raise AssertionError(f"largest demo profile is too large: {totals[-1]}")


def test_demo_case_json_shape() -> None:
    case = run_alpha_demo.finish_case(
        "shape",
        1.0,
        result="pass",
        bytes=1024,
        source_hash="a" * 64,
        dest_hash="b" * 64,
        logs=["demo.log"],
    )
    required = {
        "name",
        "result",
        "elapsed_seconds",
        "bytes",
        "throughput_gbps",
        "source_hash",
        "dest_hash",
        "error",
        "logs",
    }
    if not required <= set(case):
        raise AssertionError(f"demo case missing fields: {required - set(case)}")
    json.dumps({"timestamp": "now", "mode": "local", "cases": [case]})


def test_private_output_hash_parser_accepts_smoke_text() -> None:
    with tempfile.TemporaryDirectory(prefix="cpnetflux-demo-private-parse.") as temp_text:
        log = Path(temp_text) / "private.log"
        command = ["python3", "-c", "print('source_sha256=' + 'a'*64); print('dest_sha256=' + 'a'*64)"]
        result = run_alpha_demo.run_private_case("fake_private", command, log, env={})
        if result["result"] != "pass":
            raise AssertionError(f"fake private case failed: {result}")
        if result["source_hash"] != "a" * 64 or result["dest_hash"] != "a" * 64:
            raise AssertionError(f"hashes were not parsed: {result}")


def main() -> int:
    test_dataset_is_deterministic()
    test_profiles_are_size_bounded_and_ordered()
    test_demo_case_json_shape()
    test_private_output_hash_parser_accepts_smoke_text()
    print("alpha demo helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
