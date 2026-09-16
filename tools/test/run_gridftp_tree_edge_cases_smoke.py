#!/usr/bin/env python3
import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

from tree_smoke_common import free_port, start_server, stop_server, tree_hash, run_checked


def make_edge_tree(root: Path) -> None:
    (root / "empty-dir").mkdir(parents=True)
    (root / "space dir" / "deeper" / "level" / "inside").mkdir(parents=True)
    (root / "space dir" / "hello world.txt").write_text("hello space\n", encoding="utf-8")
    (root / "symbols_-.@=+" / "name (1).bin").parent.mkdir(parents=True)
    (root / "symbols_-.@=+" / "name (1).bin").write_bytes(bytes(range(64)))
    (root / "space dir" / "deeper" / "level" / "inside" / "deep-file.txt").write_text(
        "deep\n", encoding="utf-8"
    )
    for index in range(96):
        path = root / "many-small" / f"small-{index:03d}.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"small {index}\n", encoding="utf-8")


def require_json_summary(path: Path, result: str) -> dict:
    if not path.is_file():
        raise RuntimeError(f"missing JSON summary: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("result") != result:
        raise RuntimeError(f"unexpected JSON summary result {data.get('result')}: {path}")
    return data


def assert_empty_dir_not_preserved(root: Path) -> None:
    if (root / "empty-dir").exists():
        raise RuntimeError("empty directory was unexpectedly preserved")


def run_edge_transfer(build_dir: Path, temp: Path) -> None:
    source = temp / "source"
    source.mkdir()
    make_edge_tree(source)
    server_root = temp / "server-root"
    server_root.mkdir()
    dest = temp / "downloaded"
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        upload_summary = temp / "upload-summary.json"
        run_checked(
            [
                str(build_dir / "cpnetflux-tree-upload-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(control_port),
                "--source-dir",
                str(source),
                "--dest-dir",
                "dataset",
                "--connections",
                "2",
                "--file-parallelism",
                "2",
                "--json-summary",
                str(upload_summary),
            ]
        )
        require_json_summary(upload_summary, "pass")
        expected, _, _ = tree_hash(source)
        uploaded, _, _ = tree_hash(server_root / "dataset")
        if expected != uploaded:
            raise RuntimeError("edge upload tree hash mismatch")
        assert_empty_dir_not_preserved(server_root / "dataset")

        download_summary = temp / "download-summary.json"
        run_checked(
            [
                str(build_dir / "cpnetflux-tree-download-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(control_port),
                "--source-dir",
                "dataset",
                "--dest-dir",
                str(dest),
                "--connections",
                "2",
                "--file-parallelism",
                "2",
                "--summary-json",
                str(download_summary),
            ]
        )
        require_json_summary(download_summary, "pass")
        downloaded, _, _ = tree_hash(dest)
        if expected != downloaded:
            raise RuntimeError("edge download tree hash mismatch")
        assert_empty_dir_not_preserved(dest)
    finally:
        stop_server(server, server_log)


def run_symlink_rejected(build_dir: Path, temp: Path) -> None:
    source = temp / "symlink-source"
    source.mkdir()
    (source / "regular.txt").write_text("regular\n", encoding="utf-8")
    os.symlink(source / "regular.txt", source / "link.txt")
    server_root = temp / "symlink-server-root"
    server_root.mkdir()
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "symlink-server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        completed = run_checked(
            [
                str(build_dir / "cpnetflux-tree-upload-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(control_port),
                "--source-dir",
                str(source),
                "--dest-dir",
                "dataset",
            ],
            expect_success=False,
        )
        if "symlink" not in (completed.stdout + completed.stderr):
            raise RuntimeError("symlink rejection did not mention symlink")
    finally:
        stop_server(server, server_log)


def run_same_size_mtime_changed(build_dir: Path, temp: Path) -> None:
    source = temp / "mtime-source"
    source.mkdir()
    make_edge_tree(source)
    server_root = temp / "mtime-server-root"
    server_root.mkdir()
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "mtime-server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        summary = temp / "mtime-fail-summary.json"
        base = [
            str(build_dir / "cpnetflux-tree-upload-client"),
            "--host",
            "127.0.0.1",
            "--port",
            str(control_port),
            "--source-dir",
            str(source),
            "--dest-dir",
            "dataset",
            "--json-summary",
            str(summary),
        ]
        run_checked(base + ["--max-files", "1"], expect_success=False)
        changed = source / "space dir" / "hello world.txt"
        original_size = changed.stat().st_size
        time.sleep(1.1)
        changed.write_text("HELLO SPACE\n", encoding="utf-8")
        if changed.stat().st_size != original_size:
            changed.write_text("hello space\n", encoding="utf-8")
            time.sleep(1.1)
            changed.write_text("HELLO SPACE\n", encoding="utf-8")
        completed = run_checked(base + ["--resume"], expect_success=False)
        text = completed.stdout + completed.stderr
        if "hello world.txt" not in text or "manifest_mtime=" not in text:
            raise RuntimeError("same-size mtime drift did not produce changed-file detail")
        data = require_json_summary(summary, "fail")
        error = data.get("error") or {}
        if error.get("changed_path") != "space dir/hello world.txt":
            raise RuntimeError(f"JSON changed_path missing or wrong: {error}")
    finally:
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux tree edge-case smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-edge.") as temp_text:
        temp = Path(temp_text)
        run_edge_transfer(build_dir, temp)
        run_symlink_rejected(build_dir, temp)
        run_same_size_mtime_changed(build_dir, temp)
    print("tree edge-case smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
