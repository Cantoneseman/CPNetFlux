#!/usr/bin/env python3
import argparse
import json
import re
import socket
import time
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
    small = root / "small"
    small.mkdir()
    for index in range(32):
        (small / f"file-{index:03}.dat").write_bytes(bytes([index]) * 8192)


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


def assert_counts(
    summary_path: Path,
    expected_count: int,
    direction: str,
    expected_connections: int | None = None,
) -> None:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    for field in ("file_count", "data_transfer_count"):
        if int(summary.get(field, -1)) != expected_count:
            raise RuntimeError(f"{direction} {field} expected {expected_count}, got {summary.get(field)}")
    if expected_connections is not None:
        actual_connections = int(summary.get("control_connect_count", -1))
        if actual_connections != expected_connections:
            raise RuntimeError(
                f"{direction} control_connect_count expected {expected_connections}, "
                f"got {actual_connections}"
            )
    depth = int(summary.get("control_pipeline_depth", 0))
    if depth > 0:
        expected_slots = depth + 1
        actual_slots = int(summary.get("control_pipeline_slot_count", -1))
        if actual_slots != expected_slots:
            raise RuntimeError(
                f"{direction} control_pipeline_slot_count expected {expected_slots}, "
                f"got {summary.get('control_pipeline_slot_count')}"
            )
        pending = int(summary.get("control_pipeline_pending_high_watermark", -1))
        if pending < 1 or pending > depth:
            raise RuntimeError(
                f"{direction} control_pipeline_pending_high_watermark expected 1..{depth}, "
                f"got {pending}"
            )
        for field in ("control_prepare_count", "transfer_complete_wait_count"):
            if int(summary.get(field, -1)) != expected_count:
                raise RuntimeError(
                    f"{direction} {field} expected {expected_count}, got {summary.get(field)}"
                )
    if int(summary.get("failed_files", 0)) != 0:
        raise RuntimeError(f"{direction} reported failed files: {summary}")


def run_depth(build_dir: Path, temp: Path, depth: int, parallelism: int) -> None:
    source = temp / f"source-{depth}-{parallelism}"
    source.mkdir()
    make_tree(source)
    (source / "notes.cpnetflux.user.txt").write_text("ordinary user file", encoding="utf-8")
    expected = business_files(source)
    server_root = temp / f"server-{depth}-{parallelism}"
    server_root.mkdir()
    download_root = temp / f"download-{depth}-{parallelism}"
    upload_summary = temp / f"upload-{depth}-{parallelism}.json"
    download_summary = temp / f"download-{depth}-{parallelism}.json"
    server_log = temp / f"server-{depth}-{parallelism}.log"
    control_port = free_port()
    data_port = free_port()
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        remote_tree_name = f"dataset-{depth}-{parallelism}"
        common = [
            "--host", "127.0.0.1", "--port", str(control_port),
            "--connections", "1", "--file-parallelism", str(parallelism),
            "--control-reuse", "worker", "--control-pipeline-depth", str(depth),
            "--scheduler", "off", "--compression", "off", "--checksum", "crc32c",
            "--phase-timing", "on",
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
            raise RuntimeError(
                f"depth={depth}, file_parallelism={parallelism} uploaded raw file set mismatch: "
                f"{sorted(actual_upload_paths)}"
            )
        upload_connections = parallelism if depth == 0 else depth + parallelism
        assert_counts(upload_summary, len(expected), "upload", upload_connections)

        run_checked([
            str(build_dir / "cpnetflux-tree-download-client"), *common,
            "--source-dir", remote_tree_name, "--dest-dir", str(download_root),
            "--json-summary", str(download_summary),
        ])
        if business_files(download_root) != expected:
            raise RuntimeError(
                f"depth={depth}, file_parallelism={parallelism} download business file set "
                "or SHA-256 mismatch"
            )
        expected_download_paths = set(expected) | {
            name + ".cpnetflux.download.manifest" for name in expected
        }
        actual_download_paths = all_file_paths(download_root)
        if actual_download_paths != expected_download_paths:
            raise RuntimeError(
                f"depth={depth}, file_parallelism={parallelism} downloaded raw file set mismatch: "
                f"{sorted(actual_download_paths)}"
            )
        # Download tree discovery uses one control session before the file
        # worker pool starts; the pipeline pool then adds depth+1 slots.
        download_connections = parallelism + 1 if depth == 0 else depth + parallelism + 1
        assert_counts(download_summary, len(expected), "download", download_connections)
    finally:
        stop_server(server, server_log)



def recv_reply(sock: socket.socket, expected: int) -> str:
    sock.settimeout(3.0)
    chunks = bytearray()
    while not chunks.endswith(b"\r\n"):
        part = sock.recv(1)
        if not part:
            raise RuntimeError("control connection closed while waiting for reply")
        chunks.extend(part)
    line = chunks.decode("ascii", errors="replace").strip()
    if not line.startswith(f"{expected} "):
        raise RuntimeError(f"expected control reply {expected}, got {line!r}")
    return line


def send_command(sock: socket.socket, command: str, expected: int) -> str:
    sock.sendall(command.encode("ascii") + b"\r\n")
    return recv_reply(sock, expected)


def assert_async_control_window(build_dir: Path, temp: Path) -> None:
    root = temp / "async-control-root"
    root.mkdir()
    log = temp / "async-control-server.log"
    control_port = free_port()
    data_port = free_port()
    server = start_server(build_dir, root, control_port, data_port, log)
    sock = socket.create_connection(("127.0.0.1", control_port), timeout=3.0)
    try:
        recv_reply(sock, 220)
        send_command(sock, "USER cpnetflux", 331)
        send_command(sock, "PASS cpnetflux", 230)
        send_command(sock, "TYPE I", 200)
        send_command(sock, "OPTS PIPELINE=1", 200)

        for index in range(2):
            sock.sendall(f"EPSV\r\nSTOR async-{index}.bin\r\n".encode("ascii"))
            epsv = recv_reply(sock, 229)
            if not re.search(r"\(\|\|\|[0-9]+\|\)", epsv):
                raise RuntimeError(f"invalid EPSV reply: {epsv!r}")
            prelude = recv_reply(sock, 150)
            if "transfer_id=GFID:" not in prelude:
                raise RuntimeError(f"async STOR omitted transfer identity: {prelude!r}")

        sock.sendall(b"EPSV\r\nSTOR async-overflow.bin\r\n")
        recv_reply(sock, 229)
        overflow = recv_reply(sock, 450)
        if "window is full" not in overflow:
            raise RuntimeError(f"unexpected async window rejection: {overflow!r}")
    finally:
        sock.close()
        # Closing one opted-in control channel must release its data workers
        # without stopping the service process.
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            status_path = Path(f"/proc/{server.pid}/status")
            if not status_path.exists():
                raise RuntimeError("control server exited after client disconnect")
            status = status_path.read_text(encoding="ascii")
            thread_line = next(line for line in status.splitlines()
                               if line.startswith("Threads:"))
            if int(thread_line.split()[1]) == 1:
                break
            time.sleep(0.02)
        else:
            raise RuntimeError("async control/data workers did not stop after EOF")
        stop_server(server, log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify tree sidecars stay out of business file listings.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-pipeline-sidecar.") as temp_text:
        temp = Path(temp_text)
        assert_async_control_window(build_dir, temp)
        for depth in (0, 1, 2, 4):
            for parallelism in (1, 8):
                run_depth(build_dir, temp, depth, parallelism)
    print("tree pipeline sidecar smoke passed: depth=0/1/2/4, file_parallelism=1/8, upload/download, file sets, SHA-256, counts and phase metrics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
