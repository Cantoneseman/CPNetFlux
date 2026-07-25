#!/usr/bin/env python3
"""Run private GridFTP-like framed STOR/RETR performance matrices.

The script runs on machine one. It starts gridflux-gridftp-server locally and
uses SSH to run the framed GridFlux-aware data client on machine two.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import os
import re
import shlex
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

UPLOAD_CLIENT_ROLE = "file_client"
UPLOAD_SERVER_ROLE = "file_server"
DOWNLOAD_SENDER_ROLE = "file_download_sender"
DOWNLOAD_CLIENT_ROLE = "file_download_client"
STAGE_FIELDS = [
    "stage_recv_seconds",
    "stage_recv_bytes",
    "stage_send_seconds",
    "stage_send_bytes",
    "stage_read_seconds",
    "stage_read_bytes",
    "stage_write_seconds",
    "stage_write_bytes",
    "stage_checksum_seconds",
    "stage_checksum_bytes",
    "stage_manifest_flush_seconds",
    "stage_manifest_flush_bytes",
    "stage_resume_precheck_seconds",
    "stage_resume_precheck_bytes",
    "stage_final_verify_seconds",
    "stage_final_verify_bytes",
    "stage_rename_commit_seconds",
    "stage_rename_commit_bytes",
    "stage_overall_seconds",
    "stage_overall_bytes",
    "stage_read_calls",
    "stage_write_calls",
    "stage_read_avg_bytes_per_call",
    "stage_write_avg_bytes_per_call",
    "file_io_wait_seconds",
    "file_io_wait_bytes",
    "write_call_count",
    "write_syscall_count",
    "write_retry_count",
    "write_short_count",
    "write_zero_count",
    "write_total_bytes",
    "write_avg_bytes_per_call",
    "write_avg_bytes_per_syscall",
    "io_uring_submit_count",
    "io_uring_wait_count",
    "io_uring_completion_count",
    "io_uring_sqe_count",
    "io_uring_partial_completion_count",
    "io_uring_retry_count",
    "io_uring_avg_bytes_per_sqe",
]

IO_URING_SUMMARY_FIELDS = [
    "io_uring_submit_count",
    "io_uring_wait_count",
    "io_uring_completion_count",
    "io_uring_sqe_count",
    "io_uring_partial_completion_count",
    "io_uring_retry_count",
    "io_uring_avg_bytes_per_sqe",
]

ROLE_METRIC_FIELDS = [
    "elapsed",
    "throughput_gbps",
    "skipped_bytes",
    "resent_bytes",
    "verified_bytes",
    "manifest_flush_count",
    "manifest_flush_policy",
    "manifest_flush_interval_chunks",
    "final_verify_policy",
    "final_verify_policy_effective",
    "commit_sync_policy",
    "preallocate",
    "file_io_backend",
    "file_io_buffer_size",
    "file_io_queue_depth",
    "file_io_batch_size",
    "file_io_advice",
    "posix_write_strategy",
    "posix_write_strategy_effective",
]

PHASE_ALIAS_FIELDS = [
    "data_receive_seconds",
    "data_receive_bytes",
    "temp_write_seconds",
    "temp_write_bytes",
    "checksum_seconds",
    "checksum_bytes",
    "manifest_flush_seconds",
    "manifest_flush_bytes",
    "final_verify_seconds",
    "final_verify_bytes",
    "finalize_rename_seconds",
    "finalize_rename_bytes",
    "source_read_seconds",
    "source_read_bytes",
    "network_send_seconds",
    "network_send_bytes",
    "download_temp_write_seconds",
    "download_temp_write_bytes",
]

CSV_FIELDS = [
    "timestamp",
    "mode",
    "direction",
    "bytes",
    "connections",
    "chunk_size",
    "buffer_size",
    "checksum_algorithm",
    "checksum_backend",
    "preallocate",
    "file_io_backend",
    "file_io_buffer_size",
    "file_io_queue_depth",
    "file_io_batch_size",
    "file_io_advice",
    "posix_write_strategy",
    "posix_write_strategy_effective",
    "repeat_index",
    "elapsed",
    "throughput_gbps",
    "skipped_bytes",
    "resent_bytes",
    "verified_bytes",
    "manifest_flush_count",
    "manifest_flush_policy",
    "manifest_flush_interval_chunks",
    "commit_sync_policy",
    "final_verify_policy",
    "final_verify_policy_effective",
    *STAGE_FIELDS,
    *PHASE_ALIAS_FIELDS,
    *[f"sender_{field}" for field in ROLE_METRIC_FIELDS],
    *[f"sender_{field}" for field in STAGE_FIELDS],
    *[f"sender_{field}" for field in PHASE_ALIAS_FIELDS],
    *[f"receiver_{field}" for field in ROLE_METRIC_FIELDS],
    *[f"receiver_{field}" for field in STAGE_FIELDS],
    *[f"receiver_{field}" for field in PHASE_ALIAS_FIELDS],
    "host_baseline_csv",
    "storage_bench_csv",
    "source_sha256",
    "dest_sha256",
    "result",
    "server_log",
    "client_log",
    "server_env_before_log",
    "server_env_after_log",
    "client_env_before_log",
    "client_env_after_log",
    "server_hostname",
    "client_hostname",
    "server_kernel",
    "client_kernel",
    "server_cpu_flags",
    "client_cpu_flags",
    "server_fs_type",
    "client_fs_type",
    "server_free_bytes",
    "client_free_bytes",
    "transfer_id",
    "control_port",
    "data_port_base",
    "temp_root",
    "remote_path",
    "local_path",
    "error",
]


@dataclass(frozen=True)
class Case:
    index: int
    direction: str
    total_bytes: int
    connections: int
    chunk_size: int
    buffer_size: int
    checksum: str
    preallocate: str
    manifest_flush_policy: str
    manifest_flush_interval_chunks: int
    commit_sync_policy: str
    final_verify_policy: str
    file_io_backend: str
    file_io_buffer_size: int
    file_io_queue_depth: int
    file_io_batch_size: int
    file_io_advice: str
    posix_write_strategy: str
    repeat_index: int


@dataclass
class EnvironmentSnapshot:
    server_hostname: str = ""
    client_hostname: str = ""
    server_kernel: str = ""
    client_kernel: str = ""
    server_cpu_flags: str = ""
    client_cpu_flags: str = ""
    server_fs_type: str = ""
    client_fs_type: str = ""
    server_free_bytes: str = ""
    client_free_bytes: str = ""


class ControlConnection:
    def __init__(self, host: str, port: int) -> None:
        self.sock = connect_control(host, port)
        self.buffer = bytearray()
        greeting = self.read_reply()
        if reply_code(greeting) != 220:
            raise RuntimeError(f"unexpected greeting: {greeting!r}")

    def close(self) -> None:
        self.sock.close()

    def read_reply(self) -> list[str]:
        first = read_line(self.sock, self.buffer)
        lines = [first]
        if len(first) >= 4 and first[:3].isdigit() and first[3] == "-":
            expected = first[:3] + " "
            while True:
                line = read_line(self.sock, self.buffer)
                lines.append(line)
                if line.startswith(expected):
                    break
        return lines

    def send(self, command: str) -> list[str]:
        self.sock.sendall((command + "\r\n").encode("utf-8"))
        return self.read_reply()

    def login_type_i(self) -> None:
        user = self.send("USER gridflux")
        if reply_code(user) != 331:
            raise RuntimeError(f"USER failed: {user!r}")
        password = self.send("PASS gridflux")
        if reply_code(password) != 230:
            raise RuntimeError(f"PASS failed: {password!r}")
        type_i = self.send("TYPE I")
        if reply_code(type_i) != 200:
            raise RuntimeError(f"TYPE I failed: {type_i!r}")

    def quit(self) -> None:
        try:
            self.send("QUIT")
        except RuntimeError:
            pass


def run_local(command: list[str], *, check: bool = True, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, check=check, timeout=timeout)


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("GRIDFLUX_SSH_PASSWORD"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_remote(
    remote: str,
    command: str,
    *,
    input_text: str | None = None,
    check: bool = True,
    timeout: int | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("GRIDFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["GRIDFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        ssh_prefix(remote) + [command],
        input=input_text,
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
        env=env,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return completed


def read_line(sock: socket.socket, buffer: bytearray) -> str:
    while b"\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("control connection closed")
        buffer.extend(chunk)
    index = buffer.index(ord("\n"))
    line = bytes(buffer[: index + 1]).decode("utf-8", errors="replace").rstrip("\r\n")
    del buffer[: index + 1]
    return line


def reply_code(lines: list[str]) -> int:
    if lines and len(lines[0]) >= 3 and lines[0][:3].isdigit():
        return int(lines[0][:3])
    return 0


def connect_control(host: str, port: int) -> socket.socket:
    deadline = time.monotonic() + 15.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            return socket.create_connection((host, port), timeout=3.0)
        except OSError as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"failed to connect control server: {last_error}")


def parse_epsv_port(lines: list[str]) -> int:
    match = re.search(r"\(\|\|\|(\d+)\|\)", "\n".join(lines))
    if not match:
        raise RuntimeError(f"failed to parse EPSV port from {lines!r}")
    return int(match.group(1))


def parse_transfer_id(lines: list[str]) -> str:
    match = re.search(r"transfer_id=GFID:([A-Za-z0-9._-]+)", "\n".join(lines))
    if not match:
        raise RuntimeError(f"failed to parse transfer id from {lines!r}")
    return match.group(1)


def parse_size_token(token: str) -> int:
    normalized = token.strip().lower()
    multipliers = [
        ("gib", 1024**3),
        ("gb", 1000**3),
        ("g", 1024**3),
        ("mib", 1024**2),
        ("mb", 1000**2),
        ("m", 1024**2),
        ("kib", 1024),
        ("kb", 1000),
        ("k", 1024),
    ]
    for suffix, multiplier in multipliers:
        if normalized.endswith(suffix):
            number = normalized[: -len(suffix)]
            if not number:
                raise ValueError(f"invalid size: {token}")
            return int(number) * multiplier
    return int(normalized)


def parse_int_list(text: str) -> list[int]:
    return [parse_size_token(part) for part in text.split(",") if part.strip()]


def parse_choice_list(text: str, choices: set[str], name: str) -> list[str]:
    result = [part.strip().lower() for part in text.split(",") if part.strip()]
    invalid = [part for part in result if part not in choices]
    if invalid:
        raise argparse.ArgumentTypeError(f"invalid {name}: {','.join(invalid)}")
    return result


def timestamp_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def remote_sha256(remote: str, path: str) -> str:
    completed = run_remote(remote, f"sha256sum {shlex.quote(path)}")
    return completed.stdout.split()[0]


def make_local_file(path: Path, total_bytes: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    block = bytes((index * 31 + 7) % 251 for index in range(1024 * 1024))
    remaining = total_bytes
    with path.open("wb") as handle:
        while remaining > 0:
            size = min(remaining, len(block))
            handle.write(block[:size])
            remaining -= size


def make_remote_file(remote: str, path: str, total_bytes: int) -> None:
    script = """
import os
import sys
path = sys.argv[1]
remaining = int(sys.argv[2])
os.makedirs(os.path.dirname(path), exist_ok=True)
block = bytes((index * 31 + 7) % 251 for index in range(1024 * 1024))
with open(path, "wb") as handle:
    while remaining > 0:
        size = min(remaining, len(block))
        handle.write(block[:size])
        remaining -= size
"""
    run_remote(remote, f"python3 - {shlex.quote(path)} {total_bytes}", input_text=script)


def command_output(command: list[str]) -> str:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    return completed.stdout.strip() if completed.returncode == 0 else ""


def remote_output(remote: str, command: str) -> str:
    completed = run_remote(remote, command, check=False, timeout=30)
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def cpu_flags_from_proc() -> str:
    try:
        with open("/proc/cpuinfo", "r", encoding="utf-8") as handle:
            for line in handle:
                if line.lower().startswith("flags"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        return ""
    return ""


def fs_snapshot(path: str) -> tuple[str, str]:
    completed = subprocess.run(["df", "-PT", path], text=True, capture_output=True, check=False)
    if completed.returncode != 0:
        return "", ""
    lines = completed.stdout.strip().splitlines()
    if len(lines) < 2:
        return "", ""
    parts = lines[1].split()
    fs_type = parts[1] if len(parts) > 1 else ""
    free_bytes = str(int(parts[4]) * 1024) if len(parts) > 4 and parts[4].isdigit() else ""
    return fs_type, free_bytes


def remote_fs_snapshot(remote: str, path: str) -> tuple[str, str]:
    output = remote_output(remote, f"df -PT {shlex.quote(path)} | tail -n 1")
    parts = output.split()
    fs_type = parts[1] if len(parts) > 1 else ""
    free_bytes = str(int(parts[4]) * 1024) if len(parts) > 4 and parts[4].isdigit() else ""
    return fs_type, free_bytes


def collect_environment(remote: str) -> EnvironmentSnapshot:
    server_fs, server_free = fs_snapshot("/tmp")
    client_fs, client_free = remote_fs_snapshot(remote, "/tmp")
    remote_flags = remote_output(
        remote,
        "awk -F: '/^flags/ {gsub(/^ /, \"\", $2); print $2; exit}' /proc/cpuinfo",
    )
    return EnvironmentSnapshot(
        server_hostname=command_output(["hostname"]),
        client_hostname=remote_output(remote, "hostname"),
        server_kernel=command_output(["uname", "-r"]),
        client_kernel=remote_output(remote, "uname -r"),
        server_cpu_flags=cpu_flags_from_proc(),
        client_cpu_flags=remote_flags,
        server_fs_type=server_fs,
        client_fs_type=client_fs,
        server_free_bytes=server_free,
        client_free_bytes=client_free,
    )


def parse_role_metrics(text: str, role: str) -> dict[str, str]:
    result: dict[str, str] = {}
    role_prefix = role + " "
    for line in text.splitlines():
        if not line.startswith(role_prefix):
            continue
        values = dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^ \n]+)", line))
        if values:
            result = values
    return result


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def start_control_server(args: argparse.Namespace, case: Case, root: Path, server_log: Path) -> subprocess.Popen[str]:
    server_bin = Path(args.local_build_dir) / "gridflux-gridftp-server"
    command = [
        str(server_bin),
        "--host",
        args.server_host,
        "--port",
        str(control_port(args, case.index)),
        "--root",
        str(root),
        "--data-port-base",
        str(data_port_base(args, case.index)),
        "--connections",
        str(case.connections),
        "--chunk-size",
        str(case.chunk_size),
        "--buffer-size",
        str(case.buffer_size),
        "--checksum",
        case.checksum,
        "--checksum-backend",
        args.checksum_backend,
        "--manifest-flush-policy",
        case.manifest_flush_policy,
        "--manifest-flush-interval-chunks",
        str(case.manifest_flush_interval_chunks),
        "--commit-sync-policy",
        case.commit_sync_policy,
        "--final-verify-policy",
        case.final_verify_policy,
        "--preallocate",
        case.preallocate,
        "--file-io-backend",
        case.file_io_backend,
        "--file-io-buffer-size",
        str(case.file_io_buffer_size),
        "--file-io-queue-depth",
        str(case.file_io_queue_depth),
        "--file-io-batch-size",
        str(case.file_io_batch_size),
        "--file-io-advice",
        case.file_io_advice,
        "--posix-write-strategy",
        case.posix_write_strategy,
    ]
    log_handle = server_log.open("w", encoding="utf-8")
    return subprocess.Popen(
        command,
        stdout=log_handle,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )


def stop_process(process: subprocess.Popen[str] | None) -> None:
    if process is None:
        return
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=5)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def control_port(args: argparse.Namespace, case_index: int) -> int:
    return args.control_port_base + case_index * args.port_stride


def data_port_base(args: argparse.Namespace, case_index: int) -> int:
    return args.data_port_base + case_index * args.port_stride


def login_control(args: argparse.Namespace, case: Case) -> ControlConnection:
    control = ControlConnection(args.server_host, control_port(args, case.index))
    control.login_type_i()
    return control


def open_epsv(control: ControlConnection) -> int:
    epsv = control.send("EPSV")
    if reply_code(epsv) != 229:
        raise RuntimeError(f"EPSV failed: {epsv!r}")
    return parse_epsv_port(epsv)


def run_upload_client(
    args: argparse.Namespace,
    case: Case,
    source: str,
    data_port: int,
    transfer_id: str,
    *,
    resume: bool,
    max_chunks: int | None,
) -> subprocess.CompletedProcess[str]:
    remote_client = f"{args.remote_build_dir.rstrip('/')}/gridflux-file-client"
    pieces = [
        shlex.quote(remote_client),
        "--host",
        shlex.quote(args.server_host),
        "--port",
        str(data_port),
        "--input",
        shlex.quote(source),
        "--connections",
        str(case.connections),
        "--chunk-size",
        str(case.chunk_size),
        "--buffer-size",
        str(case.buffer_size),
        "--checksum",
        shlex.quote(case.checksum),
        "--checksum-backend",
        shlex.quote(args.checksum_backend),
        "--transfer-id",
        shlex.quote(transfer_id),
        "--file-io-backend",
        shlex.quote(case.file_io_backend),
        "--file-io-buffer-size",
        str(case.file_io_buffer_size),
        "--file-io-queue-depth",
        str(case.file_io_queue_depth),
        "--file-io-batch-size",
        str(case.file_io_batch_size),
        "--file-io-advice",
        shlex.quote(case.file_io_advice),
        "--posix-write-strategy",
        shlex.quote(case.posix_write_strategy),
    ]
    if resume:
        pieces.append("--resume")
    if max_chunks is not None:
        pieces.extend(["--max-chunks", str(max_chunks)])
    return run_remote(args.remote, " ".join(pieces), check=False, timeout=args.case_timeout)


def run_download_client(
    args: argparse.Namespace,
    case: Case,
    output: str,
    data_port: int,
    transfer_id: str,
    *,
    resume: bool,
    max_chunks: int | None,
) -> subprocess.CompletedProcess[str]:
    remote_client = f"{args.remote_build_dir.rstrip('/')}/gridflux-file-download-client"
    pieces = [
        shlex.quote(remote_client),
        "--host",
        shlex.quote(args.server_host),
        "--port",
        str(data_port),
        "--output",
        shlex.quote(output),
        "--connections",
        str(case.connections),
        "--buffer-size",
        str(case.buffer_size),
        "--checksum",
        shlex.quote(case.checksum),
        "--checksum-backend",
        shlex.quote(args.checksum_backend),
        "--transfer-id",
        shlex.quote(transfer_id),
        "--manifest-flush-policy",
        shlex.quote(case.manifest_flush_policy),
        "--manifest-flush-interval-chunks",
        str(case.manifest_flush_interval_chunks),
        "--commit-sync-policy",
        shlex.quote(case.commit_sync_policy),
        "--final-verify-policy",
        shlex.quote(case.final_verify_policy),
        "--preallocate",
        shlex.quote(case.preallocate),
        "--file-io-backend",
        shlex.quote(case.file_io_backend),
        "--file-io-buffer-size",
        str(case.file_io_buffer_size),
        "--file-io-queue-depth",
        str(case.file_io_queue_depth),
        "--file-io-batch-size",
        str(case.file_io_batch_size),
        "--file-io-advice",
        shlex.quote(case.file_io_advice),
        "--posix-write-strategy",
        shlex.quote(case.posix_write_strategy),
    ]
    if resume:
        pieces.append("--resume")
    if max_chunks is not None:
        pieces.extend(["--max-chunks", str(max_chunks)])
    return run_remote(args.remote, " ".join(pieces), check=False, timeout=args.case_timeout)


def run_stor_once(args: argparse.Namespace, case: Case, remote_source: str, output_name: str) -> tuple[str, str]:
    control = login_control(args, case)
    try:
        data_port = open_epsv(control)
        stor = control.send(f"STOR {output_name}")
        if reply_code(stor) != 150:
            raise RuntimeError(f"STOR failed: {stor!r}")
        transfer_id = parse_transfer_id(stor)
        client = run_upload_client(
            args, case, remote_source, data_port, transfer_id, resume=False, max_chunks=None
        )
        if client.returncode != 0:
            raise RuntimeError(client.stdout + client.stderr)
        final = control.read_reply()
        if reply_code(final) != 226:
            raise RuntimeError(f"STOR completion failed: {final!r}")
        control.quit()
        return transfer_id, client.stdout + client.stderr
    finally:
        control.close()


def run_stor_resume(args: argparse.Namespace, case: Case, remote_source: str, output_name: str) -> tuple[str, str]:
    client_text = ""
    control = login_control(args, case)
    try:
        data_port = open_epsv(control)
        stor = control.send(f"STOR {output_name}")
        if reply_code(stor) != 150:
            raise RuntimeError(f"STOR failed: {stor!r}")
        transfer_id = parse_transfer_id(stor)
        partial = run_upload_client(
            args,
            case,
            remote_source,
            data_port,
            transfer_id,
            resume=False,
            max_chunks=args.max_chunks,
        )
        client_text += partial.stdout + partial.stderr
        if partial.returncode == 0:
            raise RuntimeError("partial STOR unexpectedly succeeded")
        failed = control.read_reply()
        if reply_code(failed) != 550:
            raise RuntimeError(f"partial STOR expected 550: {failed!r}")
    finally:
        control.close()

    control = login_control(args, case)
    try:
        rest = control.send(f"REST GFID:{transfer_id}")
        if reply_code(rest) != 350:
            raise RuntimeError(f"REST failed: {rest!r}")
        data_port = open_epsv(control)
        stor = control.send(f"STOR {output_name}")
        if reply_code(stor) != 150 or parse_transfer_id(stor) != transfer_id:
            raise RuntimeError(f"resume STOR failed: {stor!r}")
        resumed = run_upload_client(
            args, case, remote_source, data_port, transfer_id, resume=True, max_chunks=None
        )
        client_text += resumed.stdout + resumed.stderr
        if resumed.returncode != 0:
            raise RuntimeError(resumed.stdout + resumed.stderr)
        final = control.read_reply()
        if reply_code(final) != 226:
            raise RuntimeError(f"resume STOR completion failed: {final!r}")
        control.quit()
        return transfer_id, client_text
    finally:
        control.close()


def run_retr_once(args: argparse.Namespace, case: Case, source_name: str, remote_output: str) -> tuple[str, str]:
    control = login_control(args, case)
    try:
        data_port = open_epsv(control)
        retr = control.send(f"RETR {source_name}")
        if reply_code(retr) != 150:
            raise RuntimeError(f"RETR failed: {retr!r}")
        transfer_id = parse_transfer_id(retr)
        client = run_download_client(
            args, case, remote_output, data_port, transfer_id, resume=False, max_chunks=None
        )
        if client.returncode != 0:
            raise RuntimeError(client.stdout + client.stderr)
        final = control.read_reply()
        if reply_code(final) != 226:
            raise RuntimeError(f"RETR completion failed: {final!r}")
        control.quit()
        return transfer_id, client.stdout + client.stderr
    finally:
        control.close()


def run_retr_resume(args: argparse.Namespace, case: Case, source_name: str, remote_output: str) -> tuple[str, str]:
    client_text = ""
    control = login_control(args, case)
    try:
        data_port = open_epsv(control)
        retr = control.send(f"RETR {source_name}")
        if reply_code(retr) != 150:
            raise RuntimeError(f"RETR failed: {retr!r}")
        transfer_id = parse_transfer_id(retr)
        partial = run_download_client(
            args,
            case,
            remote_output,
            data_port,
            transfer_id,
            resume=False,
            max_chunks=args.max_chunks,
        )
        client_text += partial.stdout + partial.stderr
        if partial.returncode == 0:
            raise RuntimeError("partial RETR unexpectedly succeeded")
        failed = control.read_reply()
        if reply_code(failed) != 550:
            raise RuntimeError(f"partial RETR expected 550: {failed!r}")
    finally:
        control.close()

    control = login_control(args, case)
    try:
        rest = control.send(f"REST GFID:{transfer_id}")
        if reply_code(rest) != 350:
            raise RuntimeError(f"REST failed: {rest!r}")
        data_port = open_epsv(control)
        retr = control.send(f"RETR {source_name}")
        if reply_code(retr) != 150 or parse_transfer_id(retr) != transfer_id:
            raise RuntimeError(f"resume RETR failed: {retr!r}")
        resumed = run_download_client(
            args, case, remote_output, data_port, transfer_id, resume=True, max_chunks=None
        )
        client_text += resumed.stdout + resumed.stderr
        if resumed.returncode != 0:
            raise RuntimeError(resumed.stdout + resumed.stderr)
        final = control.read_reply()
        if reply_code(final) != 226:
            raise RuntimeError(f"resume RETR completion failed: {final!r}")
        control.quit()
        return transfer_id, client_text
    finally:
        control.close()


def cleanup_remote_paths(args: argparse.Namespace, *paths: str) -> None:
    commands: list[str] = []
    for path in paths:
        if not path:
            continue
        quoted = shlex.quote(path)
        commands.append(
            f"rm -rf {quoted} {quoted}.part.* {quoted}.gridflux.download.manifest "
            f"{quoted}.gridflux.manifest"
        )
    if commands:
        run_remote(args.remote, " ; ".join(commands), check=False, timeout=60)


def check_binaries(args: argparse.Namespace) -> None:
    local_server = Path(args.local_build_dir) / "gridflux-gridftp-server"
    if not local_server.exists():
        raise RuntimeError(f"missing local server: {local_server}")
    remote_checks = [
        f"test -x {shlex.quote(args.remote_build_dir.rstrip('/') + '/gridflux-file-client')}",
        f"test -x {shlex.quote(args.remote_build_dir.rstrip('/') + '/gridflux-file-download-client')}",
    ]
    run_remote(args.remote, " && ".join(remote_checks), timeout=30)


def initial_row(args: argparse.Namespace, case: Case, env: EnvironmentSnapshot, server_log: Path, client_log: Path) -> dict[str, str]:
    row = {
        "timestamp": timestamp_utc(),
        "mode": "full" if args.full else "smoke",
        "direction": case.direction,
        "bytes": str(case.total_bytes),
        "connections": str(case.connections),
        "chunk_size": str(case.chunk_size),
        "buffer_size": str(case.buffer_size),
        "checksum_algorithm": case.checksum,
        "checksum_backend": args.checksum_backend,
        "preallocate": case.preallocate,
        "file_io_backend": case.file_io_backend,
        "file_io_buffer_size": str(case.file_io_buffer_size),
        "file_io_queue_depth": str(case.file_io_queue_depth),
        "file_io_batch_size": str(case.file_io_batch_size),
        "file_io_advice": case.file_io_advice,
        "posix_write_strategy": case.posix_write_strategy,
        "posix_write_strategy_effective": "",
        "repeat_index": str(case.repeat_index),
        "elapsed": "",
        "throughput_gbps": "",
        "skipped_bytes": "",
        "resent_bytes": "",
        "verified_bytes": "",
        "manifest_flush_count": "",
        "manifest_flush_policy": case.manifest_flush_policy,
        "manifest_flush_interval_chunks": str(case.manifest_flush_interval_chunks),
        "commit_sync_policy": case.commit_sync_policy,
        "final_verify_policy": case.final_verify_policy,
        "final_verify_policy_effective": "",
        "host_baseline_csv": args.host_baseline_csv or "",
        "storage_bench_csv": args.storage_bench_csv or "",
        "source_sha256": "",
        "dest_sha256": "",
        "result": "fail",
        "server_log": str(server_log),
        "client_log": str(client_log),
        "server_env_before_log": "",
        "server_env_after_log": "",
        "client_env_before_log": "",
        "client_env_after_log": "",
        "server_hostname": env.server_hostname,
        "client_hostname": env.client_hostname,
        "server_kernel": env.server_kernel,
        "client_kernel": env.client_kernel,
        "server_cpu_flags": env.server_cpu_flags,
        "client_cpu_flags": env.client_cpu_flags,
        "server_fs_type": env.server_fs_type,
        "client_fs_type": env.client_fs_type,
        "server_free_bytes": env.server_free_bytes,
        "client_free_bytes": env.client_free_bytes,
        "transfer_id": "",
        "control_port": str(control_port(args, case.index)),
        "data_port_base": str(data_port_base(args, case.index)),
        "temp_root": "",
        "remote_path": "",
        "local_path": "",
        "error": "",
    }
    for field in STAGE_FIELDS + PHASE_ALIAS_FIELDS:
        row[field] = ""
    for prefix in ("sender", "receiver"):
        for field in ROLE_METRIC_FIELDS + STAGE_FIELDS + PHASE_ALIAS_FIELDS:
            row[f"{prefix}_{field}"] = ""
    return row


def fill_metrics(row: dict[str, str], direction: str, server_text: str, client_text: str) -> None:
    receiver_role = UPLOAD_SERVER_ROLE if direction.startswith("stor") else DOWNLOAD_CLIENT_ROLE
    receiver_text = server_text if direction.startswith("stor") else client_text
    receiver_metrics = parse_role_metrics(receiver_text, receiver_role)
    sender_role = UPLOAD_CLIENT_ROLE if direction.startswith("stor") else DOWNLOAD_SENDER_ROLE
    sender_text = client_text if direction.startswith("stor") else server_text
    sender_metrics = parse_role_metrics(sender_text, sender_role)

    metrics = receiver_metrics or sender_metrics
    row["elapsed"] = metrics.get("elapsed_seconds", "")
    row["throughput_gbps"] = metrics.get("throughput_gbps", "")
    row["checksum_backend"] = metrics.get("checksum_backend", row["checksum_backend"])
    row["skipped_bytes"] = metrics.get("skipped_bytes", "")
    row["resent_bytes"] = metrics.get("resent_bytes", "")
    row["verified_bytes"] = metrics.get("verified_bytes", "")
    row["manifest_flush_count"] = metrics.get("manifest_flush_count", "")
    row["manifest_flush_policy"] = metrics.get("manifest_flush_policy", row["manifest_flush_policy"])
    row["manifest_flush_interval_chunks"] = metrics.get(
        "manifest_flush_interval_chunks", row["manifest_flush_interval_chunks"]
    )
    row["commit_sync_policy"] = metrics.get("commit_sync_policy", row["commit_sync_policy"])
    row["final_verify_policy"] = metrics.get("final_verify_policy", row["final_verify_policy"])
    row["final_verify_policy_effective"] = metrics.get("final_verify_policy_effective", "")
    row["preallocate"] = metrics.get("preallocate", row["preallocate"])
    row["file_io_backend"] = metrics.get("file_io_backend", row["file_io_backend"])
    row["file_io_buffer_size"] = metrics.get("file_io_buffer_size", row["file_io_buffer_size"])
    row["file_io_queue_depth"] = metrics.get("file_io_queue_depth", row["file_io_queue_depth"])
    row["file_io_batch_size"] = metrics.get("file_io_batch_size", row["file_io_batch_size"])
    row["file_io_advice"] = metrics.get("file_io_advice", row["file_io_advice"])
    row["posix_write_strategy"] = metrics.get(
        "posix_write_strategy", row["posix_write_strategy"]
    )
    row["posix_write_strategy_effective"] = metrics.get(
        "posix_write_strategy_effective", row["posix_write_strategy_effective"]
    )
    for field in STAGE_FIELDS + PHASE_ALIAS_FIELDS:
        row[field] = metrics.get(field, "")

    role_metrics = {"sender": sender_metrics, "receiver": receiver_metrics}
    for prefix, values in role_metrics.items():
        if not values:
            continue
        role_map = {
            "elapsed": "elapsed_seconds",
            "throughput_gbps": "throughput_gbps",
            "skipped_bytes": "skipped_bytes",
            "resent_bytes": "resent_bytes",
            "verified_bytes": "verified_bytes",
            "manifest_flush_count": "manifest_flush_count",
            "manifest_flush_policy": "manifest_flush_policy",
            "manifest_flush_interval_chunks": "manifest_flush_interval_chunks",
            "final_verify_policy": "final_verify_policy",
            "final_verify_policy_effective": "final_verify_policy_effective",
            "commit_sync_policy": "commit_sync_policy",
            "preallocate": "preallocate",
            "file_io_backend": "file_io_backend",
            "file_io_buffer_size": "file_io_buffer_size",
            "file_io_queue_depth": "file_io_queue_depth",
            "file_io_batch_size": "file_io_batch_size",
            "file_io_advice": "file_io_advice",
            "posix_write_strategy": "posix_write_strategy",
            "posix_write_strategy_effective": "posix_write_strategy_effective",
        }
        for field, source_key in role_map.items():
            row[f"{prefix}_{field}"] = values.get(source_key, "")
        for field in STAGE_FIELDS + PHASE_ALIAS_FIELDS:
            row[f"{prefix}_{field}"] = values.get(field, "")


SUMMARY_GROUP_FIELDS = [
    "mode",
    "direction",
    "bytes",
    "connections",
    "chunk_size",
    "buffer_size",
    "checksum_algorithm",
    "checksum_backend",
    "preallocate",
    "file_io_backend",
    "file_io_buffer_size",
    "file_io_queue_depth",
    "file_io_batch_size",
    "file_io_advice",
    "posix_write_strategy",
    "posix_write_strategy_effective",
    "manifest_flush_policy",
    "manifest_flush_interval_chunks",
    "commit_sync_policy",
    "final_verify_policy",
    "final_verify_policy_effective",
]

SUMMARY_METRIC_FIELDS = [
    "throughput_gbps",
    "elapsed",
    *STAGE_FIELDS,
    *PHASE_ALIAS_FIELDS,
    *[f"sender_{field}" for field in ROLE_METRIC_FIELDS],
    *[f"sender_{field}" for field in STAGE_FIELDS],
    *[f"sender_{field}" for field in PHASE_ALIAS_FIELDS],
    *[f"receiver_{field}" for field in ROLE_METRIC_FIELDS],
    *[f"receiver_{field}" for field in STAGE_FIELDS],
    *[f"receiver_{field}" for field in PHASE_ALIAS_FIELDS],
]

SUMMARY_FIELDS = [
    *SUMMARY_GROUP_FIELDS,
    "repeat_count",
    "pass_count",
    "fail_count",
    "unstable_spread_gt_20pct",
    "unstable_minmax_outlier",
    "stage_throughput_mismatch",
    *[
        f"{field}_{stat}"
        for field in SUMMARY_METRIC_FIELDS
        for stat in ("min", "median", "max", "spread_pct", "p95")
    ],
]


def float_values(rows: list[dict[str, str]], field: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        try:
            values.append(float(row[field]))
        except (KeyError, TypeError, ValueError):
            continue
    return values


def median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def nearest_rank_p95(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int((len(ordered) * 95 + 99) / 100) - 1))
    return ordered[index]


def spread_pct(values: list[float]) -> float | None:
    if not values:
        return None
    center = statistics.median(values)
    if center == 0:
        return 0.0 if max(values) == min(values) else None
    return ((max(values) - min(values)) / abs(center)) * 100.0


def is_valid_write_strategy_case(strategy: str, file_io_buffer_size: int) -> bool:
    return not (strategy == "coalesced" and file_io_buffer_size == 0)


def summarize_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = {}
    for row in rows:
        key = (
            row["mode"],
            row["direction"],
            row["bytes"],
            row["connections"],
            row["chunk_size"],
            row["buffer_size"],
            row["checksum_algorithm"],
            row["checksum_backend"],
            row["preallocate"],
            row["file_io_backend"],
            row["file_io_buffer_size"],
            row["file_io_queue_depth"],
            row["file_io_batch_size"],
            row["file_io_advice"],
            row["posix_write_strategy"],
            row["posix_write_strategy_effective"],
            row["manifest_flush_policy"],
            row["manifest_flush_interval_chunks"],
            row["commit_sync_policy"],
            row["final_verify_policy"],
            row["final_verify_policy_effective"],
        )
        groups.setdefault(key, []).append(row)

    summaries: list[dict[str, str]] = []
    for key, grouped_rows in sorted(groups.items()):
        pass_count = sum(1 for row in grouped_rows if row["result"] == "pass")
        summary = dict(zip(SUMMARY_GROUP_FIELDS, key, strict=True))
        summary.update(
            {
                "repeat_count": str(len(grouped_rows)),
                "pass_count": str(pass_count),
                "fail_count": str(len(grouped_rows) - pass_count),
            }
        )
        throughput_values = float_values(grouped_rows, "throughput_gbps")
        elapsed_values = float_values(grouped_rows, "elapsed")
        throughput_spread = spread_pct(throughput_values) or 0.0
        elapsed_spread = spread_pct(elapsed_values) or 0.0
        stage_probe_fields = [
            "receiver_temp_write_seconds",
            "receiver_download_temp_write_seconds",
            "sender_network_send_seconds",
            "sender_source_read_seconds",
            "temp_write_seconds",
            "download_temp_write_seconds",
            "stage_write_seconds",
            "stage_send_seconds",
        ]
        stage_spreads = [
            spread
            for field in stage_probe_fields
            if (spread := spread_pct(float_values(grouped_rows, field))) is not None
        ]
        max_stage_spread = max(stage_spreads) if stage_spreads else 0.0
        min_throughput = min(throughput_values) if throughput_values else 0.0
        max_throughput = max(throughput_values) if throughput_values else 0.0
        summary["unstable_spread_gt_20pct"] = "1" if throughput_spread > 20.0 else "0"
        summary["unstable_minmax_outlier"] = (
            "1" if min_throughput > 0.0 and (max_throughput / min_throughput) > 1.5 else "0"
        )
        summary["stage_throughput_mismatch"] = (
            "1"
            if (throughput_spread > 20.0 and max_stage_spread < 10.0)
            or (throughput_spread < 10.0 and max_stage_spread > 30.0 and elapsed_spread < 10.0)
            else "0"
        )
        for field in SUMMARY_METRIC_FIELDS:
            values = float_values(grouped_rows, field)
            value_median = median(values)
            value_spread = spread_pct(values)
            value_p95 = nearest_rank_p95(values)
            summary[f"{field}_min"] = f"{min(values):.6f}" if values else ""
            summary[f"{field}_median"] = f"{value_median:.6f}" if value_median is not None else ""
            summary[f"{field}_max"] = f"{max(values):.6f}" if values else ""
            summary[f"{field}_spread_pct"] = f"{value_spread:.6f}" if value_spread is not None else ""
            summary[f"{field}_p95"] = f"{value_p95:.6f}" if value_p95 is not None else ""
        summaries.append(summary)
    return summaries


def local_environment_text(path: Path) -> str:
    command = f"""
set +e
echo "timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "hostname=$(hostname)"
echo "path={shlex.quote(str(path))}"
echo "section=free_m"
free -m 2>&1 || true
echo "section=meminfo"
grep -E '^(Dirty|Writeback|Cached):' /proc/meminfo 2>&1 || true
echo "section=df_h"
df -h {shlex.quote(str(path))} 2>&1 || true
echo "section=iostat"
if command -v iostat >/dev/null 2>&1; then
  iostat -xz 1 1 2>&1 || true
else
  echo "iostat=unavailable"
fi
"""
    completed = subprocess.run(["bash", "-lc", command], text=True, capture_output=True, check=False)
    return completed.stdout + completed.stderr


def remote_environment_text(remote: str, path: str) -> str:
    command = f"""
set +e
echo "timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "hostname=$(hostname)"
echo "path={shlex.quote(path)}"
echo "section=free_m"
free -m 2>&1 || true
echo "section=meminfo"
grep -E '^(Dirty|Writeback|Cached):' /proc/meminfo 2>&1 || true
echo "section=df_h"
df -h {shlex.quote(path)} 2>&1 || true
echo "section=iostat"
if command -v iostat >/dev/null 2>&1; then
  iostat -xz 1 1 2>&1 || true
else
  echo "iostat=unavailable"
fi
"""
    completed = run_remote(remote, command, check=False, timeout=30)
    return completed.stdout + completed.stderr


def capture_environment_sidecars(
    args: argparse.Namespace,
    server_path: Path,
    client_path: str,
    server_log: Path,
    client_log: Path,
    label: str,
) -> tuple[Path, Path]:
    server_env_log = server_log.with_name(f"{server_log.stem}_env_{label}.log")
    client_env_log = client_log.with_name(f"{client_log.stem}_env_{label}.log")
    write_text(server_env_log, local_environment_text(server_path))
    write_text(client_env_log, remote_environment_text(args.remote, client_path))
    return server_env_log, client_env_log


def run_case(args: argparse.Namespace, case: Case, run_root: Path, env: EnvironmentSnapshot) -> dict[str, str]:
    case_id = (
        f"case{case.index:04d}_r{case.repeat_index:02d}_{case.direction}_bytes{case.total_bytes}_"
        f"c{case.connections}_chunk{case.chunk_size}_buf{case.buffer_size}_{case.checksum}_"
        f"pre{case.preallocate}_fv{case.final_verify_policy}_"
        f"mfp{case.manifest_flush_policy}_mfi{case.manifest_flush_interval_chunks}_"
        f"csp{case.commit_sync_policy}_"
        f"fiobuf{case.file_io_buffer_size}_fioqd{case.file_io_queue_depth}_"
        f"fiobs{case.file_io_batch_size}_fioadv{case.file_io_advice}_pws{case.posix_write_strategy}"
    )
    case_root = run_root / case_id
    server_root = case_root / "server-root"
    server_root.mkdir(parents=True)
    server_log = Path(args.output_dir) / f"{compact_timestamp()}_{case_id}_server.log"
    client_log = Path(args.output_dir) / f"{compact_timestamp()}_{case_id}_client.log"
    row = initial_row(args, case, env, server_log, client_log)
    row["repeat_index"] = str(case.repeat_index)
    row["temp_root"] = str(case_root)

    server: subprocess.Popen[str] | None = None
    remote_source = f"/tmp/gridflux_phase4a_{case_id}.src"
    remote_output = f"/tmp/gridflux_phase4a_{case_id}.dst"
    output_name = f"{case_id}.bin"
    local_source = server_root / f"{case_id}.source.bin"
    local_output = server_root / output_name
    env_client_path = remote_source if case.direction.startswith("stor") else str(Path(remote_output).parent)

    try:
        cleanup_remote_paths(args, remote_source, remote_output)
        server = start_control_server(args, case, server_root, server_log)

        if case.direction.startswith("stor"):
            make_remote_file(args.remote, remote_source, case.total_bytes)
            source_sha = remote_sha256(args.remote, remote_source)
            row["remote_path"] = remote_source
            row["local_path"] = str(local_output)
            before_server, before_client = capture_environment_sidecars(
                args, server_root, env_client_path, server_log, client_log, "before"
            )
            row["server_env_before_log"] = str(before_server)
            row["client_env_before_log"] = str(before_client)
            if case.direction == "stor":
                transfer_id, client_text = run_stor_once(args, case, remote_source, output_name)
            else:
                transfer_id, client_text = run_stor_resume(args, case, remote_source, output_name)
            dest_sha = sha256_file(local_output)
        else:
            make_local_file(local_source, case.total_bytes)
            source_sha = sha256_file(local_source)
            row["remote_path"] = remote_output
            row["local_path"] = str(local_source)
            before_server, before_client = capture_environment_sidecars(
                args, server_root, env_client_path, server_log, client_log, "before"
            )
            row["server_env_before_log"] = str(before_server)
            row["client_env_before_log"] = str(before_client)
            if case.direction == "retr":
                transfer_id, client_text = run_retr_once(args, case, local_source.name, remote_output)
            else:
                transfer_id, client_text = run_retr_resume(args, case, local_source.name, remote_output)
            dest_sha = remote_sha256(args.remote, remote_output)

        after_server, after_client = capture_environment_sidecars(
            args, server_root, env_client_path, server_log, client_log, "after"
        )
        row["server_env_after_log"] = str(after_server)
        row["client_env_after_log"] = str(after_client)
        write_text(client_log, client_text)
        server_text = server_log.read_text(encoding="utf-8", errors="replace")
        row["transfer_id"] = transfer_id
        row["source_sha256"] = source_sha
        row["dest_sha256"] = dest_sha
        fill_metrics(row, case.direction, server_text, client_text)
        if source_sha != dest_sha:
            raise RuntimeError(f"sha256 mismatch: {source_sha} != {dest_sha}")
        row["result"] = "pass"
        return row
    except Exception as error:  # noqa: BLE001 - preserve case diagnostics in CSV.
        row["error"] = str(error).replace("\n", " ")[:1000]
        if row["server_env_before_log"] and not row["server_env_after_log"]:
            try:
                after_server, after_client = capture_environment_sidecars(
                    args, server_root, env_client_path, server_log, client_log, "after"
                )
                row["server_env_after_log"] = str(after_server)
                row["client_env_after_log"] = str(after_client)
            except Exception as sidecar_error:  # noqa: BLE001
                row["error"] = (row["error"] + f" env_sidecar_after={sidecar_error}").strip()[:1000]
        if not client_log.exists():
            write_text(client_log, "")
        return row
    finally:
        stop_process(server)
        if row["result"] == "pass" and not args.keep_files:
            shutil.rmtree(case_root, ignore_errors=True)
            cleanup_remote_paths(args, remote_source, remote_output)


def generate_cases(args: argparse.Namespace) -> list[Case]:
    if args.smoke:
        default_directions = "stor,retr"
        default_bytes = "64MiB,128MiB"
        default_connections = "1,4"
        default_chunk_sizes = "1MiB"
        default_buffer_sizes = "64KiB"
        default_checksums = "crc32c,none"
    else:
        default_directions = "stor,retr,stor-resume,retr-resume"
        default_bytes = "256MiB,1GiB"
        default_connections = "1,2,4,8,16"
        default_chunk_sizes = "1MiB,4MiB,16MiB"
        default_buffer_sizes = "64KiB,256KiB,1MiB"
        default_checksums = "crc32c,none"

    directions = parse_choice_list(
        args.directions or default_directions,
        {"stor", "retr", "stor-resume", "retr-resume"},
        "direction",
    )
    byte_values = parse_int_list(args.bytes or default_bytes)
    connections = parse_int_list(args.connections or default_connections)
    chunk_sizes = parse_int_list(args.chunk_sizes or default_chunk_sizes)
    buffer_sizes = parse_int_list(args.buffer_sizes or default_buffer_sizes)
    checksums = parse_choice_list(args.checksums or default_checksums, {"crc32c", "none"}, "checksum")
    preallocates = parse_choice_list(args.preallocates, {"off", "full"}, "preallocate")
    manifest_flush_policies = parse_choice_list(
        args.manifest_flush_policies or args.manifest_flush_policy,
        {"every_n_chunks", "final_only"},
        "manifest flush policy",
    )
    manifest_flush_intervals = parse_int_list(
        args.manifest_flush_interval_chunks_list or str(args.manifest_flush_interval_chunks)
    )
    if any(value <= 0 or value > 65536 for value in manifest_flush_intervals):
        raise SystemExit("--manifest-flush-interval-chunks values must be in range 1..65536")
    commit_sync_policies = parse_choice_list(
        args.commit_sync_policies or args.commit_sync_policy,
        {"none", "fsync_file", "fsync_file_and_dir"},
        "commit sync policy",
    )
    file_io_backends = parse_choice_list(
        args.file_io_backends, {"posix", "io_uring"}, "file IO backend"
    )
    file_io_buffer_sizes = parse_int_list(args.file_io_buffer_sizes)
    file_io_queue_depths = parse_int_list(args.file_io_queue_depths)
    if any(value <= 0 or value > 256 for value in file_io_queue_depths):
        raise SystemExit("--file-io-queue-depths values must be in range 1..256")
    file_io_batch_sizes = parse_int_list(args.file_io_batch_sizes) if args.file_io_batch_sizes else None
    if file_io_batch_sizes is not None and any(value <= 0 or value > 256 for value in file_io_batch_sizes):
        raise SystemExit("--file-io-batch-sizes values must be in range 1..256")
    file_io_advices = parse_choice_list(
        args.file_io_advices,
        {"off", "sequential", "noreuse", "dontneed", "sequential_dontneed"},
        "file IO advice",
    )
    posix_write_strategies = parse_choice_list(
        args.posix_write_strategies,
        {"auto", "direct", "coalesced"},
        "posix write strategy",
    )
    final_verify_policies = parse_choice_list(
        args.final_verify_policies or args.final_verify_policy,
        {"full", "verified_chunks"},
        "final verify policy",
    )

    cases: list[Case] = []
    index = 0
    for direction in directions:
        for total_bytes in byte_values:
            for connection in connections:
                for chunk_size in chunk_sizes:
                    for buffer_size in buffer_sizes:
                        for checksum in checksums:
                            for preallocate in preallocates:
                                for manifest_flush_policy in manifest_flush_policies:
                                    for manifest_flush_interval in manifest_flush_intervals:
                                        for commit_sync_policy in commit_sync_policies:
                                            for file_io_backend in file_io_backends:
                                                for file_io_buffer_size in file_io_buffer_sizes:
                                                    for posix_write_strategy in posix_write_strategies:
                                                        if not is_valid_write_strategy_case(
                                                            posix_write_strategy,
                                                            file_io_buffer_size,
                                                        ):
                                                            continue
                                                        for file_io_queue_depth in file_io_queue_depths:
                                                            batch_sizes = (
                                                                file_io_batch_sizes
                                                                if file_io_batch_sizes is not None
                                                                else [file_io_queue_depth]
                                                            )
                                                            for file_io_batch_size in batch_sizes:
                                                                for file_io_advice in file_io_advices:
                                                                    for final_verify_policy in final_verify_policies:
                                                                        for repeat_index in range(args.repeat):
                                                                            cases.append(
                                                                                Case(
                                                                                    index=index,
                                                                                    direction=direction,
                                                                                    total_bytes=total_bytes,
                                                                                    connections=connection,
                                                                                    chunk_size=chunk_size,
                                                                                    buffer_size=buffer_size,
                                                                                    checksum=checksum,
                                                                                    preallocate=preallocate,
                                                                                    manifest_flush_policy=manifest_flush_policy,
                                                                                    manifest_flush_interval_chunks=manifest_flush_interval,
                                                                                    commit_sync_policy=commit_sync_policy,
                                                                                    final_verify_policy=final_verify_policy,
                                                                                    file_io_backend=file_io_backend,
                                                                                    file_io_buffer_size=file_io_buffer_size,
                                                                                    file_io_queue_depth=file_io_queue_depth,
                                                                                    file_io_batch_size=file_io_batch_size,
                                                                                    file_io_advice=file_io_advice,
                                                                                    posix_write_strategy=posix_write_strategy,
                                                                                    repeat_index=repeat_index,
                                                                                )
                                                                            )
                                                                            index += 1
    return cases


def validate_ports(args: argparse.Namespace, cases: list[Case]) -> None:
    for case in cases:
        if control_port(args, case.index) > 65535:
            raise RuntimeError("control port range exceeds 65535")
        if data_port_base(args, case.index) + case.connections > 65535:
            raise RuntimeError("data port range exceeds 65535")


def process_check(remote: str) -> tuple[str, str]:
    pattern = "'[g]ridflux-gridftp-server|[g]ridflux-file-'"
    local = subprocess.run(
        ["bash", "-lc", f"pgrep -af {pattern} || true"],
        text=True,
        capture_output=True,
        check=False,
    ).stdout.strip()
    remote_output_text = run_remote(remote, f"pgrep -af {pattern} || true", check=False).stdout.strip()
    return local, remote_output_text


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP framed performance matrix.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--smoke", action="store_true", help="run the small default matrix")
    mode.add_argument("--full", action="store_true", help="run the full explicit matrix")
    parser.add_argument("--remote", default="root@<redacted>")
    parser.add_argument("--server-host", default="<redacted>")
    parser.add_argument("--local-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--control-port-base", type=int, default=21210)
    parser.add_argument("--data-port-base", type=int, default=20400)
    parser.add_argument("--port-stride", type=int, default=20)
    parser.add_argument("--directions", help="comma list: stor,retr,stor-resume,retr-resume")
    parser.add_argument("--bytes", help="comma list, supports KiB/MiB/GiB suffixes")
    parser.add_argument("--connections", help="comma list")
    parser.add_argument("--chunk-sizes", help="comma list, supports KiB/MiB/GiB suffixes")
    parser.add_argument("--buffer-sizes", help="comma list, supports KiB/MiB/GiB suffixes")
    parser.add_argument("--checksums", help="comma list: crc32c,none")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument(
        "--manifest-flush-policy",
        choices=["every_n_chunks", "final_only"],
        default="every_n_chunks",
    )
    parser.add_argument("--manifest-flush-policies", help="comma list: every_n_chunks,final_only")
    parser.add_argument("--manifest-flush-interval-chunks", type=int, default=16)
    parser.add_argument("--manifest-flush-interval-chunks-list", help="comma list of chunk counts")
    parser.add_argument("--commit-sync-policy", choices=["none", "fsync_file", "fsync_file_and_dir"], default="none")
    parser.add_argument("--commit-sync-policies", help="comma list: none,fsync_file,fsync_file_and_dir")
    parser.add_argument("--final-verify-policy", choices=["full", "verified_chunks"], default="full")
    parser.add_argument("--final-verify-policies", help="comma list: full,verified_chunks")
    parser.add_argument("--preallocates", default="off", help="comma list: off,full")
    parser.add_argument("--file-io-backends", default="posix", help="comma list: posix,io_uring")
    parser.add_argument("--file-io-buffer-sizes", default="0", help="comma list, 0 disables buffering")
    parser.add_argument("--file-io-queue-depths", default="1", help="comma list, io_uring queue depth")
    parser.add_argument("--file-io-batch-sizes", default="", help="comma list, defaults to queue depths")
    parser.add_argument(
        "--file-io-advices",
        default="off",
        help="comma list: off,sequential,noreuse,dontneed,sequential_dontneed",
    )
    parser.add_argument(
        "--posix-write-strategies",
        default="auto",
        help="comma list: auto,direct,coalesced",
    )
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--host-baseline-csv", default="")
    parser.add_argument("--storage-bench-csv", default="")
    parser.add_argument("--max-chunks", type=int, default=8)
    parser.add_argument("--case-timeout", type=int, default=1800)
    parser.add_argument("--keep-files", action="store_true")
    args = parser.parse_args()
    if args.repeat <= 0:
        raise SystemExit("--repeat must be greater than zero")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = compact_timestamp()
    csv_path = output_dir / f"{run_id}_gridftp-private-matrix-{'full' if args.full else 'smoke'}.csv"
    summary_path = (
        output_dir / f"{run_id}_gridftp-private-matrix-{'full' if args.full else 'smoke'}-summary.csv"
    )
    run_root = Path(tempfile.mkdtemp(prefix=f"gridflux-phase4a-{run_id}."))
    remove_run_root = not args.keep_files

    try:
        cases = generate_cases(args)
        validate_ports(args, cases)
        check_binaries(args)
        env = collect_environment(args.remote)

        rows: list[dict[str, str]] = []
        for case in cases:
            print(
                f"[{case.index + 1}/{len(cases)}] {case.direction} bytes={case.total_bytes} "
                f"connections={case.connections} chunk={case.chunk_size} buffer={case.buffer_size} "
                f"checksum={case.checksum} manifest_flush={case.manifest_flush_policy}/"
                f"{case.manifest_flush_interval_chunks} commit_sync={case.commit_sync_policy}",
                flush=True,
            )
            row = run_case(args, case, run_root, env)
            rows.append(row)
            with csv_path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            with summary_path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=SUMMARY_FIELDS)
                writer.writeheader()
                writer.writerows(summarize_rows(rows))
            print(f"  result={row['result']} throughput_gbps={row['throughput_gbps']}", flush=True)

        local_processes, remote_processes = process_check(args.remote)
        if local_processes or remote_processes:
            print("leftover gridflux process detected", file=sys.stderr)
            if local_processes:
                print("local:\n" + local_processes, file=sys.stderr)
            if remote_processes:
                print("remote:\n" + remote_processes, file=sys.stderr)
            return 1

        failures = [row for row in rows if row["result"] != "pass"]
        if failures:
            remove_run_root = False
        print(f"csv={csv_path}")
        print(f"summary_csv={summary_path}")
        print(f"cases={len(rows)} failures={len(failures)}")
        return 1 if failures else 0
    finally:
        if remove_run_root:
            shutil.rmtree(run_root, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
