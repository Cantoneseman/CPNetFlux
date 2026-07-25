#!/usr/bin/env python3
import argparse
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, make_tree, start_server, stop_server, run_checked


def main() -> int:
    parser = argparse.ArgumentParser(description="Run GridFlux tree corrupt manifest smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="gridflux-tree-corrupt.") as temp_text:
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
            manifest = Path(str(source) + ".gridflux.tree.upload.manifest")
            text = manifest.read_text(encoding="utf-8")
            manifest.write_text(text.replace("mode=upload", "mode=download"), encoding="utf-8")
            run_checked(base_cmd + ["--resume"], expect_success=False)
            print("tree corrupt manifest smoke passed")
        finally:
            stop_server(server, server_log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
