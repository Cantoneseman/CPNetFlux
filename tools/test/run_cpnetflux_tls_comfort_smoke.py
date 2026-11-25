#!/usr/bin/env python3
"""Exercise the 512 MiB V3 mmap comfort profile with bounded security modes."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from gridftp_port_window import clamp_passive_data_port_base
from run_gridftp_data_tls_smoke import free_port, generate_cert
from tree_smoke_common import stop_server, wait_for_control

SIZE = 512 * 1024 * 1024
CHUNK = 1024 * 1024
FRAME = 256 * 1024
RATE = 1_500_000_000


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def create_payload(path: Path) -> str:
    block = bytes((index * 131 + 17) % 256 for index in range(CHUNK))
    digest = hashlib.sha256()
    with path.open("wb") as handle:
        for _ in range(SIZE // CHUNK):
            handle.write(block)
            digest.update(block)
    return digest.hexdigest()


def manifest_fields(path: Path) -> dict[str, str]:
    fields = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    return fields


def wait_partial(process: subprocess.Popen, checkpoint: Path, log: Path) -> int:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"client finished before a partial checkpoint: {log.read_text(errors='replace')}")
        if checkpoint.is_file():
            fields = manifest_fields(checkpoint)
            chunks = [item for item in fields.get("verified_chunks", "").split(",") if item]
            if fields.get("state") != "committed" and 0 < len(chunks) < SIZE // CHUNK:
                return len(chunks)
        time.sleep(0.002)
    raise RuntimeError(f"timed out waiting for a partial checkpoint: {log.read_text(errors='replace')}")


def mode_config(mode: str, cert: Path, key: Path, token: Path):
    auth = "token" if mode == "token" else "anonymous"
    control_tls = mode in ("control-tls", "control-data-tls")
    data_tls = mode == "control-data-tls"
    server = ["--auth-mode", auth]
    client = ["--auth-mode", auth]
    if auth == "token":
        server += ["--auth-token-file", str(token)]
        client += ["--auth-token-file", str(token)]
    if control_tls:
        server += ["--tls-mode", "required", "--tls-cert-file", str(cert), "--tls-key-file", str(key)]
        client += ["--tls-mode", "required", "--tls-ca-file", str(cert), "--tls-server-name", "localhost"]
    if data_tls:
        server += ["--data-tls-mode", "required"]
        client += ["--data-tls-mode", "required"]
    return server, client, control_tls, data_tls


def wait_server(port: int, cert: Path, control_tls: bool) -> None:
    if not control_tls:
        wait_for_control(port)
        return
    import ssl
    deadline = time.monotonic() + 10
    last_error = None
    while time.monotonic() < deadline:
        try:
            context = ssl.create_default_context(cafile=str(cert))
            context.check_hostname = False
            with socket.create_connection(("127.0.0.1", port), timeout=0.5) as raw:
                with context.wrap_socket(raw, server_hostname="localhost") as secure:
                    if secure.recv(512).startswith(b"220"):
                        return
        except (OSError, ssl.SSLError) as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"TLS control listener did not become ready: {last_error}")


def client_command(build: Path, port: int, source: str, destination: Path,
                   summary: Path, auth_args: list[str], resume: bool) -> list[str]:
    command = [str(build / "cpnetflux-tree-download-client"),
        "--host", "127.0.0.1", "--port", str(port),
        "--source-dir", source, "--dest-dir", str(destination),
        "--connections", "4", "--file-parallelism", "4",
        "--control-reuse", "worker", "--data-session-reuse", "tree",
        "--data-pending-window", "2", "--checksum", "crc32c",
        "--chunk-size", str(CHUNK), "--buffer-size", "65536",
        "--data-frame-bytes", str(FRAME), "--data-transport", "tcp",
        "--compression", "off", "--final-verify-policy", "verified_chunks",
        "--commit-sync-policy", "none", "--manifest-flush-policy", "every_n_chunks",
        "--manifest-flush-interval-chunks", "16", "--range-checkpoint-batch-chunks", "16",
        "--posix-write-strategy", "direct", "--file-io-buffer-size", "0",
        "--phase-timing", "on", "--transfer-timeout-ms", "120000",
        "--json-summary", str(summary)] + auth_args
    if resume:
        command.append("--resume")
    return command


def pacing_receipts(log: Path) -> list[dict]:
    prefix = "CPNETFLUX_SENDER_PACING "
    rows = []
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            rows.append(json.loads(line[len(prefix):]))
    assert rows and {row["lane"] for row in rows} == set(range(4)), rows
    for row in rows:
        assert row["rate_bytes_per_second"] == RATE and row["spacing_violations"] == 0, row
        assert row["wait_backend"] == "timerfd" and row["timer_fd_creates"] == 1, row
        assert row["timer_arm_calls"] == row["sleep_calls"] and row["timer_read_calls"] >= row["timer_arm_calls"], row
        assert row["frames"] > 1 and row["wire_bytes"] == row["frames"] * (FRAME + 68), row
    return rows


def verify_summary(summary: dict, control_tls: bool, data_tls: bool) -> None:
    assert summary["result"] == "pass" and summary["actual_transport"] == "v3", summary
    assert summary["connections"] == 4 and summary["data_transport"] == "tcp", summary
    assert summary["data_frame_logical_bytes"] == FRAME, summary
    assert summary["control_tls_verified"] is control_tls, summary
    assert summary["data_tls_verified"] is data_tls, summary
    endpoints = summary["lane_endpoints"]
    assert len(endpoints) == 4 and {row["channel_index"] for row in endpoints} == set(range(4)), endpoints
    for row in endpoints:
        assert 1_048_576 <= row["tcp_receive_buffer_bytes"] <= 2_097_152, row
    ranges = [row for row in summary["file_timings"] if row["range_id"]]
    assert 4 <= len(ranges) <= 8, ranges
    assert sum(row["length"] for row in ranges) == SIZE, ranges
    assert all(row["receiver_payload_execution"] == "inline" and
               row["receiver_write_backend"] == "mmap_experimental" for row in ranges), ranges
    assert all(row["mapped_write"]["direct_receive_calls"] == 0 and
               row["mapped_write"]["direct_received_bytes"] == 0 for row in ranges), ranges


def run_mode(build: Path, mode: str, root: Path) -> dict:
    case = root / mode
    source = case / "server-root" / "source"
    destination = case / "download"
    source.mkdir(parents=True)
    destination.mkdir()
    cert, key, token = case / "server.crt", case / "server.key", case / "token"
    assert generate_cert(cert, key), "openssl unavailable"
    token.write_text("cpnetflux-tls-comfort-test-token\n", encoding="ascii")
    token.chmod(0o600)
    expected_hash = create_payload(source / "payload.bin")
    port = free_port()
    data_base = clamp_passive_data_port_base(free_port())
    server_log = case / "server.log"
    server_auth, client_auth, control_tls, data_tls = mode_config(mode, cert, key, token)
    server_cmd = [str(build / "cpnetflux-gridftp-server"), "--host", "127.0.0.1",
        "--port", str(port), "--root", str(case / "server-root"),
        "--data-port-base", str(data_base), "--connections", "4",
        "--link-set-max-links", "4", "--checksum", "crc32c",
        "--chunk-size", str(CHUNK), "--buffer-size", "65536",
        "--manifest-flush-policy", "every_n_chunks", "--manifest-flush-interval-chunks", "16",
        "--final-verify-policy", "verified_chunks", "--commit-sync-policy", "none"] + server_auth
    server_env = dict(os.environ, CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=str(RATE),
        CPNETFLUX_RANGE_CHECKPOINT_BATCH="16")
    with server_log.open("w", encoding="utf-8") as output:
        server = subprocess.Popen(server_cmd, stdout=output, stderr=subprocess.STDOUT, env=server_env)
    try:
        try:
            wait_server(port, cert, control_tls)
        except BaseException as error:
            raise RuntimeError(f"{mode} control listener failed: {server_log.read_text(errors='replace')}") from error
        environment = dict(os.environ)
        for name in list(environment):
            if name.startswith("CPNETFLUX_"):
                environment.pop(name)
        environment.update(CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION="inline",
            CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE="mmap",
            CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE="off",
            CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="1048576",
            CPNETFLUX_RECEIVER_WRITE_BATCH="1",
            CPNETFLUX_STAGE_READ_CONCURRENCY="0",
            CPNETFLUX_STAGE_WRITE_CONCURRENCY="0",
            CPNETFLUX_RANGE_DIRECT_WRITE="parallel")
        summary_path = case / "summary.json"
        command = client_command(build, port, "source", destination, summary_path, client_auth, False)
        interrupted_chunks = 0
        if mode == "control-data-tls":
            interrupted_log = case / "interrupted.log"
            checkpoint = destination / "payload.bin.cpnetflux.download.manifest"
            with interrupted_log.open("w", encoding="utf-8") as output:
                process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                           env=environment)
                interrupted_chunks = wait_partial(process, checkpoint, interrupted_log)
                process.send_signal(signal.SIGTERM)
                if process.wait(timeout=15) != -signal.SIGTERM:
                    raise RuntimeError("interrupted TLS transfer did not terminate by SIGTERM")
            persisted = manifest_fields(checkpoint)
            assert persisted.get("state") != "committed", persisted
            assert len([item for item in persisted.get("verified_chunks", "").split(",") if item]) == interrupted_chunks
            resume_log = case / "resume.log"
            resume_command = client_command(build, port, "source", destination, summary_path,
                                            client_auth, True)
            with resume_log.open("w", encoding="utf-8") as output:
                resumed = subprocess.run(resume_command, stdout=output, stderr=subprocess.STDOUT,
                                         env=environment, timeout=300, check=False)
            assert resumed.returncode == 0, resume_log.read_text(errors="replace")[-8000:]
        else:
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, env=environment, timeout=300, check=False)
            (case / "client.log").write_text(result.stdout, encoding="utf-8")
            assert result.returncode == 0, result.stdout[-8000:]
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        verify_summary(summary, control_tls, data_tls)
        if mode == "control-data-tls":
            assert summary.get("resume_skipped_bytes", 0) > 0, summary
        target = destination / "payload.bin"
        assert target.stat().st_size == SIZE and sha256_file(target) == expected_hash
        manifests = list(destination.glob("*.cpnetflux.download.manifest"))
        assert len(manifests) == 1, manifests
        fields = manifest_fields(manifests[0])
        chunks = [item for item in fields.get("verified_chunks", "").split(",") if item]
        assert len(chunks) == SIZE // CHUNK and fields.get("state") == "committed", fields
        receipts = pacing_receipts(server_log)
        return dict(mode=mode, bytes=SIZE, source_sha256=expected_hash,
            destination_sha256=sha256_file(target), verified_chunks=len(chunks),
            control_tls_verified=control_tls, data_tls_verified=data_tls,
            receive_buffer_bytes=[row["tcp_receive_buffer_bytes"] for row in summary["lane_endpoints"]],
            receiver_backend="buffered inline mmap", direct_receive_calls=0,
            pacing_receipt_count=len(receipts), interrupted_checkpoint_chunks=interrupted_chunks,
            resume_skipped_bytes=summary.get("resume_skipped_bytes", 0), result="pass")
    finally:
        stop_server(server, server_log)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tls-comfort-") as temp:
        root = Path(temp)
        results = [run_mode(build, mode, root) for mode in
                   ("token", "control-tls", "control-data-tls")]
    print(json.dumps(dict(result="pass", size_bytes=SIZE, profiles=results), sort_keys=True))


if __name__ == "__main__":
    main()
