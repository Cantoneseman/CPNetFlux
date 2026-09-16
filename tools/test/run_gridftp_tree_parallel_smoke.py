#!/usr/bin/env python3
import argparse
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, make_tree, start_server, stop_server, tree_hash, run_checked


def run_once(build_dir: Path, parallelism: int, temp: Path) -> None:
    source = temp / f"source-p{parallelism}"
    source.mkdir()
    make_tree(source)
    server_root = temp / f"server-root-p{parallelism}"
    server_root.mkdir()
    control_port = free_port()
    data_port = free_port()
    server_log = temp / f"server-p{parallelism}.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
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
                str(parallelism),
            ]
        )
        dest = temp / f"download-p{parallelism}"
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
                str(parallelism),
            ]
        )
        expected, files, total = tree_hash(source)
        uploaded, _, _ = tree_hash(server_root / "dataset")
        downloaded, _, _ = tree_hash(dest)
        if expected != uploaded or expected != downloaded:
            raise RuntimeError("parallel tree hash mismatch")
        print(
            f"tree parallel smoke passed parallelism={parallelism} "
            f"files={files} total_bytes={total} tree_hash={expected}"
        )
    finally:
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux tree parallel smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-parallel.") as temp_text:
        temp = Path(temp_text)
        run_once(build_dir, 2, temp)
        run_once(build_dir, 4, temp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
