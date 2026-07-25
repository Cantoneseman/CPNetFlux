#!/usr/bin/env python3
import argparse
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, make_tree, start_server, stop_server, tree_hash, run_checked


def run_upload_resume(build_dir: Path, temp: Path) -> None:
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
            str(build_dir / "gridflux-tree-upload-client"),
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
        run_checked(base_cmd + ["--resume"])
        expected, _, _ = tree_hash(source)
        actual, _, _ = tree_hash(server_root / "dataset")
        if expected != actual:
            raise RuntimeError("upload resume tree hash mismatch")
    finally:
        stop_server(server, server_log)


def run_download_resume(build_dir: Path, temp: Path) -> None:
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
            str(build_dir / "gridflux-tree-download-client"),
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
        run_checked(base_cmd + ["--resume"])
        expected, _, _ = tree_hash(source)
        actual, _, _ = tree_hash(dest)
        if expected != actual:
            raise RuntimeError("download resume tree hash mismatch")
    finally:
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run GridFlux tree resume smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="gridflux-tree-resume.") as temp_text:
        temp = Path(temp_text)
        run_upload_resume(build_dir, temp)
        run_download_resume(build_dir, temp)
    print("tree resume smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
