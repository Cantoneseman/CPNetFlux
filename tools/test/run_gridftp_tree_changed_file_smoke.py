#!/usr/bin/env python3
import argparse
import sys
import tempfile
import time
from pathlib import Path

from tree_smoke_common import free_port, make_tree, start_server, stop_server, tree_hash, run_checked


def assert_changed_failure(completed, path_hint: str) -> None:
    text = completed.stdout + completed.stderr
    if "changed" not in text or path_hint not in text or "manifest_size=" not in text:
        raise RuntimeError("changed-file error did not include expected detail:\n" + text)


def run_upload_changed(build_dir: Path, temp: Path) -> None:
    source = temp / "upload-source"
    source.mkdir()
    make_tree(source)
    server_root = temp / "upload-root"
    server_root.mkdir()
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "upload-server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        base_cmd = [
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
        ]
        run_checked(base_cmd + ["--max-files", "1"], expect_success=False)
        before, _, _ = tree_hash(server_root / "dataset")
        changed = source / "nested" / "beta.bin"
        time.sleep(1.1)
        changed.write_bytes(b"changed upload source")
        completed = run_checked(base_cmd + ["--resume"], expect_success=False)
        assert_changed_failure(completed, "nested/beta.bin")
        after, _, _ = tree_hash(server_root / "dataset")
        if before != after:
            raise RuntimeError("remote completed upload file was modified after changed-file failure")
    finally:
        stop_server(server, server_log)


def run_download_remote_changed(build_dir: Path, temp: Path) -> None:
    server_root = temp / "download-root"
    source = server_root / "dataset"
    source.mkdir(parents=True)
    make_tree(source)
    dest = temp / "download-dest"
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "download-server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        base_cmd = [
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
        ]
        run_checked(base_cmd + ["--max-files", "1"], expect_success=False)
        changed = source / "nested" / "beta.bin"
        time.sleep(1.1)
        changed.write_bytes(b"changed remote download source")
        completed = run_checked(base_cmd + ["--resume"], expect_success=False)
        assert_changed_failure(completed, "nested/beta.bin")
    finally:
        stop_server(server, server_log)


def run_download_local_completed_changed(build_dir: Path, temp: Path) -> None:
    server_root = temp / "local-change-root"
    source = server_root / "dataset"
    source.mkdir(parents=True)
    make_tree(source)
    dest = temp / "local-change-dest"
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "local-change-server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        base_cmd = [
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
        ]
        run_checked(base_cmd)
        changed = dest / "alpha.txt"
        time.sleep(1.1)
        changed.write_text("changed local completed file", encoding="utf-8")
        completed = run_checked(base_cmd + ["--resume"], expect_success=False)
        assert_changed_failure(completed, "alpha.txt")
    finally:
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux tree changed-file smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-changed.") as temp_text:
        temp = Path(temp_text)
        run_upload_changed(build_dir, temp)
        run_download_remote_changed(build_dir, temp)
        run_download_local_completed_changed(build_dir, temp)
    print("tree changed-file smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
