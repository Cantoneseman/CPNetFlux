#!/usr/bin/env python3
import argparse
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import (
    free_port,
    make_tree,
    start_server,
    stop_server,
    tree_hash,
    run_checked,
)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run GridFlux tree upload smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="gridflux-tree-upload.") as temp_text:
        temp = Path(temp_text)
        source = temp / "source"
        source.mkdir()
        make_tree(source)
        server_root = temp / "server-root"
        server_root.mkdir()
        control_port = free_port()
        data_port = free_port()
        server_log = temp / "server.log"
        server = start_server(build_dir, server_root, control_port, data_port, server_log)
        try:
            run_checked(
                [
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
            )
            expected, files, total = tree_hash(source)
            actual, _, _ = tree_hash(server_root / "dataset")
            if expected != actual:
                raise RuntimeError(f"tree hash mismatch: {expected} != {actual}")
            print(f"tree upload smoke passed files={files} total_bytes={total} tree_hash={actual}")
        finally:
            stop_server(server, server_log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
