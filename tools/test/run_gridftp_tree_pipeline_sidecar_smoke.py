#!/usr/bin/env python3
import argparse
import json
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, file_sha256, run_checked, start_server, stop_server


def make_tree(root: Path) -> None:
    (root / "nested").mkdir(parents=True)
    (root / "alpha.txt").write_bytes(b"alpha")
    (root / "nested" / "beta.bin").write_bytes(bytes(range(251)) * 3)
    (root / "empty.bin").write_bytes(b"")
    (root / "notes.cpnetflux.user.txt").write_text("ordinary user file", encoding="utf-8")


def business_files(root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if relative.endswith(".cpnetflux.manifest") or relative.endswith(".cpnetflux.download.manifest"):
            continue
        result[relative] = file_sha256(path)
    return result


def all_file_paths(root: Path) -> set[str]:
    return {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}


def assert_counts(summary_path: Path, expected_count: int, direction: str) -> None:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    for field in ("file_count", "data_transfer_count"):
        if int(summary.get(field, -1)) != expected_count:
            raise RuntimeError(f"{direction} {field} expected {expected_count}, got {summary.get(field)}")
    if int(summary.get("failed_files", 0)) != 0:
        raise RuntimeError(f"{direction} reported failed files: {summary}")


def run_depth(build_dir: Path, temp: Path, depth: int) -> None:
    source = temp / f"source-{depth}"
    source.mkdir()
    make_tree(source)
    expected = business_files(source)
    server_root = temp / f"server-{depth}"
    server_root.mkdir()
    download_root = temp / f"download-{depth}"
    upload_summary = temp / f"upload-{depth}.json"
    download_summary = temp / f"download-{depth}.json"
    server_log = temp / f"server-{depth}.log"
    control_port = free_port()
    data_port = free_port()
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        remote_tree_name = f"dataset-{depth}"
        common = [
            "--host", "127.0.0.1", "--port", str(control_port),
            "--connections", "1", "--file-parallelism", "1",
            "--control-reuse", "worker", "--control-pipeline-depth", str(depth),
            "--scheduler", "off", "--compression", "off", "--checksum", "crc32c",
        ]
        run_checked([
            str(build_dir / "cpnetflux-tree-upload-client"), *common,
            "--source-dir", str(source), "--dest-dir", remote_tree_name,
            "--json-summary", str(upload_summary),
        ])
        server_tree = server_root / remote_tree_name
        expected_upload_paths = set(expected) | {name + ".cpnetflux.manifest" for name in expected}
        actual_upload_paths = all_file_paths(server_tree)
        if actual_upload_paths != expected_upload_paths:
            raise RuntimeError(f"depth={depth} uploaded raw file set mismatch: {sorted(actual_upload_paths)}")
        assert_counts(upload_summary, len(expected), "upload")

        run_checked([
            str(build_dir / "cpnetflux-tree-download-client"), *common,
            "--source-dir", remote_tree_name, "--dest-dir", str(download_root),
            "--json-summary", str(download_summary),
        ])
        if business_files(download_root) != expected:
            raise RuntimeError(f"depth={depth} download business file set or SHA-256 mismatch")
        expected_download_paths = set(expected) | {
            name + ".cpnetflux.download.manifest" for name in expected
        }
        actual_download_paths = all_file_paths(download_root)
        if actual_download_paths != expected_download_paths:
            raise RuntimeError(f"depth={depth} downloaded raw file set mismatch: {sorted(actual_download_paths)}")
        assert_counts(download_summary, len(expected), "download")
    finally:
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify tree sidecars stay out of business file listings.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-pipeline-sidecar.") as temp_text:
        temp = Path(temp_text)
        for depth in (0, 1):
            run_depth(build_dir, temp, depth)
    print("tree pipeline sidecar smoke passed: depth=0/1, upload/download, file sets, SHA-256 and counts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
