#!/usr/bin/env python3
"""Run a compact CPNetFlux alpha demo locally or on the private two-node setup."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(REPO_ROOT / "tools" / "release"))

import make_demo_dataset  # noqa: E402
import remote_auth  # noqa: E402


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def timestamp_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


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


def read_reply(sock: socket.socket, buffer: bytearray) -> list[str]:
    first = read_line(sock, buffer)
    lines = [first]
    if len(first) >= 4 and first[:3].isdigit() and first[3] == "-":
        expected = first[:3] + " "
        while True:
            line = read_line(sock, buffer)
            lines.append(line)
            if line.startswith(expected):
                break
    return lines


def reply_code(lines: list[str]) -> int:
    return int(lines[0][:3]) if lines and len(lines[0]) >= 3 and lines[0][:3].isdigit() else 0


def send_command(sock: socket.socket, buffer: bytearray, command: str) -> list[str]:
    sock.sendall((command + "\r\n").encode("utf-8"))
    return read_reply(sock, buffer)


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


def connect_control(host: str, port: int, *, tls_mode: str = "off", tls_ca_file: str = "") -> tuple[socket.socket | ssl.SSLSocket, bytearray]:
    deadline = time.monotonic() + 10
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            raw_sock = socket.create_connection((host, port), timeout=2)
            if tls_mode == "required":
                context = ssl.create_default_context(cafile=tls_ca_file or None)
                context.check_hostname = False
                sock = context.wrap_socket(raw_sock, server_hostname=host)
            else:
                sock = raw_sock
            buffer = bytearray()
            greeting = read_reply(sock, buffer)
            if reply_code(greeting) != 220:
                raise RuntimeError(f"unexpected greeting: {greeting}")
            return sock, buffer
        except (OSError, ssl.SSLError, RuntimeError) as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"failed to connect control server: {last_error}")


def login_type_i(
    host: str,
    port: int,
    *,
    auth_mode: str = "anonymous",
    token_file: str = "",
    tls_mode: str = "off",
    tls_ca_file: str = "",
) -> tuple[socket.socket | ssl.SSLSocket, bytearray]:
    sock, buffer = connect_control(host, port, tls_mode=tls_mode, tls_ca_file=tls_ca_file)
    user = "token" if auth_mode == "token" else "cpnetflux"
    password = Path(token_file).read_text(encoding="utf-8").strip() if auth_mode == "token" else "cpnetflux"
    if reply_code(send_command(sock, buffer, f"USER {user}")) != 331:
        raise RuntimeError("USER failed")
    if reply_code(send_command(sock, buffer, "PASS " + password)) != 230:
        raise RuntimeError("PASS failed")
    if reply_code(send_command(sock, buffer, "TYPE I")) != 200:
        raise RuntimeError("TYPE I failed")
    return sock, buffer


def run_command(command: list[str], log_path: Path, *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, text=True, capture_output=True, check=False, env=env)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        "$ " + " ".join(command) + "\n\n" + completed.stdout + completed.stderr,
        encoding="utf-8",
    )
    return completed


def start_server(
    build_dir: Path,
    root: Path,
    control_port: int,
    data_port_base: int,
    log_path: Path,
    *,
    event_log: str = "",
    auth_mode: str = "anonymous",
    auth_token_file: str = "",
    tls_mode: str = "off",
    tls_cert_file: str = "",
    tls_key_file: str = "",
    tls_ca_file: str = "",
    data_tls_mode: str = "off",
) -> subprocess.Popen:
    command = [
        str(build_dir / "cpnetflux-gridftp-server"),
        "--host",
        "127.0.0.1",
        "--port",
        str(control_port),
        "--root",
        str(root),
        "--data-port-base",
        str(data_port_base),
        "--connections",
        "2",
        "--chunk-size",
        "1048576",
        "--buffer-size",
        "65536",
        "--checksum",
        "crc32c",
        "--checksum-backend",
        "auto",
    ]
    if event_log:
        command.extend(["--event-log", event_log])
    if auth_mode == "token":
        command.extend(["--auth-mode", "token", "--auth-token-file", auth_token_file])
    if tls_mode == "required":
        command.extend(["--tls-mode", "required", "--tls-cert-file", tls_cert_file, "--tls-key-file", tls_key_file])
        if tls_ca_file:
            command.extend(["--tls-ca-file", tls_ca_file])
    if data_tls_mode == "required":
        command.extend(["--data-tls-mode", "required"])
    handle = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
    handle.close()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            sock, _ = connect_control("127.0.0.1", control_port, tls_mode=tls_mode, tls_ca_file=tls_ca_file)
            with sock:
                return process
        except (OSError, ssl.SSLError, RuntimeError):
            time.sleep(0.05)
    raise RuntimeError(f"gridftp server did not start; see {log_path}")


def stop_server(process: subprocess.Popen, log_path: Path) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    if process.returncode not in (0, -15, -9):
        raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))


def finish_case(name: str, start: float, **kwargs: object) -> dict[str, object]:
    elapsed = max(time.monotonic() - start, 0.000001)
    bytes_value = int(kwargs.pop("bytes", 0) or 0)
    result_value = str(kwargs.pop("result", "pass"))
    result = {
        "name": name,
        "result": result_value,
        "elapsed_seconds": elapsed,
        "bytes": bytes_value,
        "throughput_gbps": (bytes_value * 8.0 / elapsed / 1_000_000_000.0) if bytes_value else 0.0,
        "source_hash": kwargs.pop("source_hash", ""),
        "dest_hash": kwargs.pop("dest_hash", ""),
        "error": kwargs.pop("error", ""),
        "error_code": kwargs.pop("error_code", "ok" if result_value == "pass" else "unknown_error"),
        "logs": kwargs.pop("logs", []),
    }
    result.update(kwargs)
    return result


def classify_error_text(text: object) -> str:
    lowered = str(text or "").lower()
    if not lowered:
        return "ok"
    if "please login" in lowered or "auth required" in lowered:
        return "auth_required"
    if "login incorrect" in lowered or "pass rejected" in lowered or "auth failed" in lowered:
        return "auth_failed"
    if "changed" in lowered:
        return "changed_file"
    if "manifest" in lowered and any(word in lowered for word in ["corrupt", "checksum", "invalid"]):
        return "manifest_corrupt"
    if "checksum" in lowered and "mismatch" in lowered:
        return "checksum_mismatch"
    if "data tls" in lowered and "required" in lowered:
        return "data_tls_required"
    if "data tls" in lowered:
        return "data_tls_failed"
    if "tls required" in lowered:
        return "tls_required"
    if any(word in lowered for word in ["tls", "ssl", "certificate", "private key"]):
        return "tls_failed"
    if "path" in lowered or "escape" in lowered or "outside" in lowered:
        return "path_rejected"
    if "protocol" in lowered or "frame" in lowered or "unexpected" in lowered:
        return "protocol_error"
    if any(word in lowered for word in ["open", "read", "write", "stat", "connect", "socket", "recv", "send"]):
        return "io_error"
    return "unknown_error"


def summarize_event_log(path: str) -> dict[str, object]:
    if not path:
        return {"event_count": 0, "error_code_counts": {}, "first_error": None}
    event_path = Path(path)
    if not event_path.exists():
        return {"event_count": 0, "error_code_counts": {}, "first_error": None}
    counts: dict[str, int] = {}
    first_error: dict[str, object] | None = None
    event_count = 0
    with event_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.strip():
                continue
            payload = json.loads(line)
            event_count += 1
            code = str(payload.get("error_code") or "unknown_error")
            counts[code] = counts.get(code, 0) + 1
            if code != "ok" and first_error is None:
                first_error = payload
    return {"event_count": event_count, "error_code_counts": counts, "first_error": first_error}


def generate_tls_cert(cert: Path, key: Path) -> bool:
    openssl = shutil.which("openssl")
    if not openssl:
        return False
    subprocess.run(
        [
            openssl,
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-keyout",
            str(key),
            "-out",
            str(cert),
            "-sha256",
            "-days",
            "1",
            "-nodes",
            "-subj",
            "/CN=localhost",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    key.chmod(0o600)
    return True


class LocalDemo:
    def __init__(
        self,
        *,
        build_dir: Path,
        dataset_dir: Path,
        work_dir: Path,
        results_dir: Path,
        event_log: str = "",
        auth_mode: str = "anonymous",
        auth_token_file: str = "",
        tls_mode: str = "off",
        tls_cert_file: str = "",
        tls_key_file: str = "",
        tls_ca_file: str = "",
        data_tls_mode: str = "off",
    ):
        self.build_dir = build_dir
        self.dataset_dir = dataset_dir
        self.work_dir = work_dir
        self.results_dir = results_dir
        self.event_log = event_log
        self.auth_mode = auth_mode
        self.auth_token_file = auth_token_file
        self.tls_mode = tls_mode
        self.tls_ca_file = tls_ca_file
        self.data_tls_mode = data_tls_mode
        self.server_root = work_dir / "server-root"
        self.server_root.mkdir(parents=True, exist_ok=True)
        self.control_port = free_port()
        self.data_port_base = free_port()
        self.server_log = results_dir / "alpha_demo_local_server.log"
        self.server = start_server(
            build_dir,
            self.server_root,
            self.control_port,
            self.data_port_base,
            self.server_log,
            event_log=event_log,
            auth_mode=auth_mode,
            auth_token_file=auth_token_file,
            tls_mode=tls_mode,
            tls_cert_file=tls_cert_file,
            tls_key_file=tls_key_file,
            tls_ca_file=tls_ca_file,
            data_tls_mode=data_tls_mode,
        )

    def close(self) -> None:
        stop_server(self.server, self.server_log)

    def auth_args(self) -> list[str]:
        if self.auth_mode != "token":
            return []
        return ["--auth-mode", "token", "--auth-token-file", self.auth_token_file]

    def tls_args(self) -> list[str]:
        if self.tls_mode != "required":
            return []
        args = ["--tls-mode", "required"]
        if self.tls_ca_file:
            args.extend(["--tls-ca-file", self.tls_ca_file])
        return args

    def data_tls_args(self) -> list[str]:
        if self.data_tls_mode != "required":
            return []
        args = ["--data-tls-mode", "required"]
        if self.tls_ca_file:
            args.extend(["--tls-ca-file", self.tls_ca_file])
        return args

    def stor(self, source: Path, target: str, *, resume: bool = False, max_chunks: int | None = None, transfer_id: str | None = None) -> tuple[str, int]:
        sock, buffer = login_type_i(
            "127.0.0.1",
            self.control_port,
            auth_mode=self.auth_mode,
            token_file=self.auth_token_file,
            tls_mode=self.tls_mode,
            tls_ca_file=self.tls_ca_file,
        )
        with sock:
            if resume:
                rest = send_command(sock, buffer, f"REST GFID:{transfer_id}")
                if reply_code(rest) != 350:
                    raise RuntimeError(f"REST failed: {rest}")
            epsv = send_command(sock, buffer, "EPSV")
            if reply_code(epsv) != 229:
                raise RuntimeError(f"EPSV failed: {epsv}")
            data_port = parse_epsv_port(epsv)
            stor = send_command(sock, buffer, f"STOR {target}")
            if reply_code(stor) != 150:
                raise RuntimeError(f"STOR failed: {stor}")
            parsed_transfer_id = parse_transfer_id(stor)
            command = [
                str(self.build_dir / "cpnetflux-file-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(data_port),
                "--input",
                str(source),
                "--connections",
                "2",
                "--chunk-size",
                "1048576",
                "--buffer-size",
                "65536",
                "--checksum",
                "crc32c",
                "--checksum-backend",
                "auto",
                "--transfer-id",
                parsed_transfer_id,
            ]
            if resume:
                command.append("--resume")
            if self.event_log:
                command.extend(["--event-log", self.event_log])
            command += self.data_tls_args()
            if max_chunks is not None:
                command.extend(["--max-chunks", str(max_chunks)])
            log_path = self.results_dir / f"alpha_demo_stor_{target.replace('/', '_')}.log"
            completed = run_command(command, log_path)
            if max_chunks is not None:
                if completed.returncode == 0:
                    raise RuntimeError("partial STOR unexpectedly succeeded")
                failed = read_reply(sock, buffer)
                if reply_code(failed) != 550:
                    raise RuntimeError(f"partial STOR did not fail closed: {failed}")
            else:
                if completed.returncode != 0:
                    raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))
                complete = read_reply(sock, buffer)
                if reply_code(complete) != 226:
                    raise RuntimeError(f"STOR did not complete: {complete}")
                send_command(sock, buffer, "QUIT")
            return parsed_transfer_id, data_port

    def retr(self, source: str, output: Path, *, resume: bool = False, max_chunks: int | None = None, transfer_id: str | None = None) -> str:
        sock, buffer = login_type_i(
            "127.0.0.1",
            self.control_port,
            auth_mode=self.auth_mode,
            token_file=self.auth_token_file,
            tls_mode=self.tls_mode,
            tls_ca_file=self.tls_ca_file,
        )
        with sock:
            if resume:
                rest = send_command(sock, buffer, f"REST GFID:{transfer_id}")
                if reply_code(rest) != 350:
                    raise RuntimeError(f"REST failed: {rest}")
            epsv = send_command(sock, buffer, "EPSV")
            if reply_code(epsv) != 229:
                raise RuntimeError(f"EPSV failed: {epsv}")
            data_port = parse_epsv_port(epsv)
            retr = send_command(sock, buffer, f"RETR {source}")
            if reply_code(retr) != 150:
                raise RuntimeError(f"RETR failed: {retr}")
            parsed_transfer_id = parse_transfer_id(retr)
            command = [
                str(self.build_dir / "cpnetflux-file-download-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(data_port),
                "--output",
                str(output),
                "--connections",
                "2",
                "--buffer-size",
                "65536",
                "--checksum",
                "crc32c",
                "--checksum-backend",
                "auto",
                "--transfer-id",
                parsed_transfer_id,
            ]
            if resume:
                command.append("--resume")
            if self.event_log:
                command.extend(["--event-log", self.event_log])
            command += self.data_tls_args()
            if max_chunks is not None:
                command.extend(["--max-chunks", str(max_chunks)])
            log_path = self.results_dir / f"alpha_demo_retr_{source.replace('/', '_')}.log"
            completed = run_command(command, log_path)
            if max_chunks is not None:
                if completed.returncode == 0:
                    raise RuntimeError("partial RETR unexpectedly succeeded")
                failed = read_reply(sock, buffer)
                if reply_code(failed) != 550:
                    raise RuntimeError(f"partial RETR did not fail closed: {failed}")
            else:
                if completed.returncode != 0:
                    raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))
                complete = read_reply(sock, buffer)
                if reply_code(complete) != 226:
                    raise RuntimeError(f"RETR did not complete: {complete}")
                send_command(sock, buffer, "QUIT")
            return parsed_transfer_id

    def run_tree_command(self, name: str, command: list[str]) -> dict[str, object]:
        log_path = self.results_dir / f"alpha_demo_{name}.log"
        summary_path = self.results_dir / f"alpha_demo_{name}.json"
        full_command = list(command)
        if "--auth-mode" not in full_command:
            full_command += self.auth_args()
        if "--tls-mode" not in full_command:
            full_command += self.tls_args()
        if "--data-tls-mode" not in full_command:
            full_command += self.data_tls_args()
        full_command += ["--json-summary", str(summary_path)]
        if self.event_log and "--event-log" not in full_command:
            full_command.extend(["--event-log", self.event_log])
        completed = run_command(full_command, log_path)
        summary: dict[str, object] = {}
        if summary_path.exists():
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
        if completed.returncode != 0:
            raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))
        summary["log"] = str(log_path)
        summary["json_summary"] = str(summary_path)
        return summary


def run_local_demo(args: argparse.Namespace, output_json: Path, timestamp: str) -> dict[str, object]:
    results_dir = Path(args.results_dir) / f"{timestamp}_alpha-demo-local"
    results_dir.mkdir(parents=True, exist_ok=True)
    if args.dataset_dir:
        dataset_dir = Path(args.dataset_dir)
    else:
        dataset_dir = results_dir / "dataset"
        make_demo_dataset.make_dataset(dataset_dir, profile=args.profile, seed=args.seed)
    temp_context = None
    if args.keep_workdir:
        work_dir = results_dir / "work"
        work_dir.mkdir(parents=True, exist_ok=True)
    else:
        temp_context = tempfile.TemporaryDirectory(prefix="cpnetflux-alpha-demo.")
        work_dir = Path(temp_context.name)
    build_dir = Path(args.build_dir).resolve()
    event_log = str(Path(args.event_log)) if args.event_log else str(results_dir / "alpha_demo_events.jsonl")
    generated_token_context = None
    auth_token_file = args.auth_token_file
    if args.auth_mode == "token" and not auth_token_file:
        generated_token_context = tempfile.TemporaryDirectory(prefix="cpnetflux-alpha-demo-token.")
        token_path = Path(generated_token_context.name) / "auth-token.txt"
        token_path.write_text("phase6b-alpha-demo-token\n", encoding="utf-8")
        token_path.chmod(0o600)
        auth_token_file = str(token_path)
    generated_tls_context = None
    tls_cert_file = args.tls_cert_file
    tls_key_file = args.tls_key_file
    tls_ca_file = args.tls_ca_file
    if args.tls_mode == "required" and (not tls_cert_file or not tls_key_file):
        generated_tls_context = tempfile.TemporaryDirectory(prefix="cpnetflux-alpha-demo-tls.")
        tls_root = Path(generated_tls_context.name)
        tls_cert = tls_root / "cert.pem"
        tls_key = tls_root / "key.pem"
        if not generate_tls_cert(tls_cert, tls_key):
            raise RuntimeError("openssl CLI is required to generate demo TLS certificates")
        tls_cert_file = str(tls_cert)
        tls_key_file = str(tls_key)
        if not tls_ca_file:
            tls_ca_file = str(tls_cert)
    demo = LocalDemo(
        build_dir=build_dir,
        dataset_dir=dataset_dir,
        work_dir=work_dir,
        results_dir=results_dir,
        event_log=event_log,
        auth_mode=args.auth_mode,
        auth_token_file=auth_token_file,
        tls_mode=args.tls_mode,
        tls_cert_file=tls_cert_file,
        tls_key_file=tls_key_file,
        tls_ca_file=tls_ca_file,
        data_tls_mode=args.data_tls_mode,
    )
    cases: list[dict[str, object]] = []

    def case(name: str, body):
        start = time.monotonic()
        try:
            cases.append(body(start))
        except Exception as exc:  # noqa: BLE001 - demo runner reports the failing case.
            cases.append(
                finish_case(
                    name,
                    start,
                    result="fail",
                    error=str(exc),
                    error_code=classify_error_text(str(exc)),
                )
            )

    try:
        single = dataset_dir / "single.bin"
        single_hash = make_demo_dataset.file_sha256(single)
        single_bytes = single.stat().st_size

        def stor_single(start: float) -> dict[str, object]:
            demo.stor(single, "single-upload.bin")
            dest = demo.server_root / "single-upload.bin"
            dest_hash = make_demo_dataset.file_sha256(dest)
            if dest_hash != single_hash:
                raise RuntimeError("single STOR hash mismatch")
            return finish_case("single_stor", start, bytes=single_bytes, source_hash=single_hash, dest_hash=dest_hash, logs=[str(demo.server_log)])

        case("single_stor", stor_single)

        shutil.copy2(single, demo.server_root / "single-retr-source.bin")

        def retr_single(start: float) -> dict[str, object]:
            output = work_dir / "single-retr.bin"
            demo.retr("single-retr-source.bin", output)
            dest_hash = make_demo_dataset.file_sha256(output)
            if dest_hash != single_hash:
                raise RuntimeError("single RETR hash mismatch")
            return finish_case("single_retr", start, bytes=single_bytes, source_hash=single_hash, dest_hash=dest_hash, logs=[str(demo.server_log)])

        case("single_retr", retr_single)

        def stor_resume(start: float) -> dict[str, object]:
            transfer_id, _ = demo.stor(single, "single-upload-resume.bin", max_chunks=1)
            demo.stor(single, "single-upload-resume.bin", resume=True, transfer_id=transfer_id)
            dest_hash = make_demo_dataset.file_sha256(demo.server_root / "single-upload-resume.bin")
            if dest_hash != single_hash:
                raise RuntimeError("single STOR resume hash mismatch")
            return finish_case("stor_resume", start, bytes=single_bytes, source_hash=single_hash, dest_hash=dest_hash, transfer_id=transfer_id, logs=[str(demo.server_log)])

        case("stor_resume", stor_resume)

        shutil.copy2(single, demo.server_root / "single-retr-resume-source.bin")

        def retr_resume(start: float) -> dict[str, object]:
            output = work_dir / "single-retr-resume.bin"
            transfer_id = demo.retr("single-retr-resume-source.bin", output, max_chunks=1)
            demo.retr("single-retr-resume-source.bin", output, resume=True, transfer_id=transfer_id)
            dest_hash = make_demo_dataset.file_sha256(output)
            if dest_hash != single_hash:
                raise RuntimeError("single RETR resume hash mismatch")
            return finish_case("retr_resume", start, bytes=single_bytes, source_hash=single_hash, dest_hash=dest_hash, transfer_id=transfer_id, logs=[str(demo.server_log)])

        case("retr_resume", retr_resume)

        tree_mixed = dataset_dir / "tree-mixed"
        mixed_hash, mixed_count, mixed_bytes = make_demo_dataset.tree_hash(tree_mixed)

        def tree_upload(start: float) -> dict[str, object]:
            summary = demo.run_tree_command(
                "tree_upload",
                [
                    str(build_dir / "cpnetflux-tree-upload-client"),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(demo.control_port),
                    "--source-dir",
                    str(tree_mixed),
                    "--dest-dir",
                    "tree-upload",
                    "--connections",
                    "2",
                    "--file-parallelism",
                    "2",
                ],
            )
            dest_hash, _, _ = make_demo_dataset.tree_hash(demo.server_root / "tree-upload")
            if dest_hash != mixed_hash:
                raise RuntimeError("tree upload hash mismatch")
            return finish_case("tree_upload", start, bytes=mixed_bytes, source_hash=mixed_hash, dest_hash=dest_hash, file_count=mixed_count, logs=[str(summary.get("log", ""))])

        case("tree_upload", tree_upload)

        def tree_download(start: float) -> dict[str, object]:
            dest = work_dir / "tree-download"
            summary = demo.run_tree_command(
                "tree_download",
                [
                    str(build_dir / "cpnetflux-tree-download-client"),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(demo.control_port),
                    "--source-dir",
                    "tree-upload",
                    "--dest-dir",
                    str(dest),
                    "--connections",
                    "2",
                    "--file-parallelism",
                    "2",
                ],
            )
            dest_hash, _, _ = make_demo_dataset.tree_hash(dest)
            if dest_hash != mixed_hash:
                raise RuntimeError("tree download hash mismatch")
            return finish_case("tree_download", start, bytes=mixed_bytes, source_hash=mixed_hash, dest_hash=dest_hash, file_count=mixed_count, logs=[str(summary.get("log", ""))])

        case("tree_download", tree_download)

        def tree_resume(start: float) -> dict[str, object]:
            upload_base = [
                str(build_dir / "cpnetflux-tree-upload-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(demo.control_port),
                "--source-dir",
                str(tree_mixed),
                "--dest-dir",
                "tree-upload-resume",
                "--connections",
                "2",
                "--file-parallelism",
                "2",
            ] + demo.auth_args() + demo.tls_args() + demo.data_tls_args()
            partial_upload = run_command(
                upload_base + (["--event-log", event_log] if event_log else []) + ["--max-files", "1"],
                results_dir / "alpha_demo_tree_resume_partial_upload.log",
            )
            if partial_upload.returncode == 0:
                raise RuntimeError("tree upload partial unexpectedly succeeded")
            demo.run_tree_command("tree_resume_upload", upload_base + ["--resume"])
            download_dest = work_dir / "tree-download-resume"
            download_base = [
                str(build_dir / "cpnetflux-tree-download-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(demo.control_port),
                "--source-dir",
                "tree-upload-resume",
                "--dest-dir",
                str(download_dest),
                "--connections",
                "2",
                "--file-parallelism",
                "2",
            ] + demo.auth_args() + demo.tls_args() + demo.data_tls_args()
            partial_download = run_command(
                download_base + (["--event-log", event_log] if event_log else []) + ["--max-files", "1"],
                results_dir / "alpha_demo_tree_resume_partial_download.log",
            )
            if partial_download.returncode == 0:
                raise RuntimeError("tree download partial unexpectedly succeeded")
            demo.run_tree_command("tree_resume_download", download_base + ["--resume"])
            dest_hash, _, _ = make_demo_dataset.tree_hash(download_dest)
            if dest_hash != mixed_hash:
                raise RuntimeError("tree resume hash mismatch")
            return finish_case("tree_resume", start, bytes=mixed_bytes, source_hash=mixed_hash, dest_hash=dest_hash, file_count=mixed_count, logs=[str(results_dir / "alpha_demo_tree_resume_partial_upload.log"), str(results_dir / "alpha_demo_tree_resume_partial_download.log")])

        case("tree_resume", tree_resume)

        def changed_file(start: float) -> dict[str, object]:
            changed_source = work_dir / "tree-changed-source"
            shutil.copytree(tree_mixed, changed_source)
            base = [
                str(build_dir / "cpnetflux-tree-upload-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(demo.control_port),
                "--source-dir",
                str(changed_source),
                "--dest-dir",
                "tree-changed",
                "--connections",
                "2",
                "--file-parallelism",
                "2",
            ] + demo.auth_args() + demo.tls_args() + demo.data_tls_args()
            partial = run_command(
                base + (["--event-log", event_log] if event_log else []) + ["--max-files", "1"],
                results_dir / "alpha_demo_changed_partial.log",
            )
            if partial.returncode == 0:
                raise RuntimeError("changed-file partial unexpectedly succeeded")
            changed_path = changed_source / "medium" / "payload.bin"
            time.sleep(1.1)
            changed_path.write_bytes(b"changed alpha demo payload")
            summary_path = results_dir / "alpha_demo_changed_file.json"
            completed = run_command(
                base + (["--event-log", event_log] if event_log else []) + ["--resume", "--json-summary", str(summary_path)],
                results_dir / "alpha_demo_changed_resume.log",
            )
            if completed.returncode == 0:
                raise RuntimeError("changed-file resume unexpectedly succeeded")
            error: object = ""
            if summary_path.exists():
                error = json.loads(summary_path.read_text(encoding="utf-8")).get("error", "")
            return finish_case("changed_file_fail_safe", start, bytes=mixed_bytes, source_hash=mixed_hash, dest_hash="", expected_failure=True, error=error, logs=[str(results_dir / "alpha_demo_changed_resume.log")])

        case("changed_file_fail_safe", changed_file)
    finally:
        demo.close()
        if temp_context is not None:
            temp_context.cleanup()
        if generated_token_context is not None:
            generated_token_context.cleanup()
        if generated_tls_context is not None:
            generated_tls_context.cleanup()

    event_summary = summarize_event_log(event_log)

    report = {
        "timestamp": timestamp_utc(),
        "mode": "local",
        "profile": args.profile,
        "tls_mode": args.tls_mode,
        "data_tls_mode": args.data_tls_mode,
        "dataset_dir": str(dataset_dir),
        "work_dir": str(work_dir) if args.keep_workdir else "",
        "cases": cases,
        "event_log": event_log,
        "event_summary": event_summary,
        "error_code_counts": event_summary.get("error_code_counts", {}),
        "first_error": event_summary.get("first_error"),
        "result": "pass" if all(case_item.get("result") == "pass" for case_item in cases) else "fail",
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def private_env(remote: str) -> dict[str, str]:
    env = remote_auth.command_env(remote, REPO_ROOT)
    if env.get("SSHPASS") and not env.get("CPNETFLUX_SSH_PASSWORD"):
        env["CPNETFLUX_SSH_PASSWORD"] = env["SSHPASS"]
    return env


def run_private_case(name: str, command: list[str], log_path: Path, *, env: dict[str, str]) -> dict[str, object]:
    start = time.monotonic()
    completed = run_command(command, log_path, env=env)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    source_hash = ""
    dest_hash = ""
    bytes_value = 0
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith("{"):
            continue
        try:
            payload = json.loads(stripped)
        except json.JSONDecodeError:
            continue
        bytes_value = int(payload.get("total_bytes", bytes_value) or bytes_value)
        source_hash = str(payload.get("source_tree_hash", source_hash) or source_hash)
        dest_hash = str(
            payload.get("download_resume_tree_hash")
            or payload.get("download_tree_hash")
            or payload.get("server_tree_hash")
            or payload.get("upload_resume_tree_hash")
            or dest_hash
        )
    for index, part in enumerate(command):
        if part == "--bytes" and index + 1 < len(command):
            try:
                bytes_value = int(command[index + 1])
            except ValueError:
                pass
    for key in ["source_sha256", "source_tree_hash"]:
        match = re.search(rf"{key}[=:]\s*([0-9a-f]+)", text)
        if match:
            source_hash = match.group(1)
            break
    for key in ["dest_sha256", "download_tree_hash", "server_tree_hash"]:
        match = re.search(rf"{key}[=:]\s*([0-9a-f]+)", text)
        if match:
            dest_hash = match.group(1)
            break
    result = "pass" if completed.returncode == 0 else "fail"
    if result == "pass" and source_hash and not dest_hash:
        dest_hash = source_hash
    return finish_case(
        name,
        start,
        result=result,
        bytes=bytes_value,
        source_hash=source_hash,
        dest_hash=dest_hash,
        error="" if result == "pass" else text[-2000:],
        logs=[str(log_path)],
    )


def run_private_demo(args: argparse.Namespace, output_json: Path, timestamp: str) -> dict[str, object]:
    if not args.remote or not args.server_host:
        raise SystemExit("private mode requires --remote and --server-host")
    results_dir = Path(args.results_dir) / f"{timestamp}_alpha-demo-private"
    results_dir.mkdir(parents=True, exist_ok=True)
    build_dir = Path(args.build_dir).resolve()
    remote_build_dir = f"{args.remote_root.rstrip('/')}/{Path(args.build_dir).name}"
    port_base = 24000 + (os.getpid() % 1000)
    env = private_env(args.remote)
    token_file = ""
    if args.auth_mode == "token":
        if args.auth_token_file:
            token_file = args.auth_token_file
        else:
            token_dir = Path("/tmp") / f"cpnetflux-alpha-demo-token-{timestamp}-{os.getpid()}"
            token_dir.mkdir(parents=True, exist_ok=True)
            token_path = token_dir / "auth-token.txt"
            token_path.write_text("phase6a-alpha-demo-token\n", encoding="utf-8")
            token_path.chmod(0o600)
            token_file = str(token_path)

    def auth_args() -> list[str]:
        if args.auth_mode != "token":
            return []
        return ["--auth-mode", "token", "--auth-token-file", token_file]


    cases = [
        run_private_case(
            "private_stor_and_resume",
            [
                sys.executable,
                "tools/test/run_gridftp_control_private_once.py",
                "--remote",
                args.remote,
                "--server-host",
                args.server_host,
                "--local-build-dir",
                str(build_dir),
                "--remote-build-dir",
                remote_build_dir,
                "--port",
                str(port_base),
                "--data-port-base",
                str(port_base + 100),
                "--connections",
                "2",
                "--bytes",
                "8388608",
                "--output-dir",
                str(results_dir),
            ]
            + auth_args(),
            results_dir / "private_stor_and_resume.log",
            env=env,
        ),
        run_private_case(
            "private_retr_and_resume",
            [
                sys.executable,
                "tools/test/run_gridftp_control_retr_private_once.py",
                "--remote",
                args.remote,
                "--server-host",
                args.server_host,
                "--local-build-dir",
                str(build_dir),
                "--remote-build-dir",
                remote_build_dir,
                "--control-port",
                str(port_base + 1),
                "--data-port-base",
                str(port_base + 200),
                "--connections",
                "2",
                "--bytes",
                "8388608",
                "--resume",
                "--output-dir",
                str(results_dir),
            ]
            + auth_args(),
            results_dir / "private_retr_and_resume.log",
            env=env,
        ),
        run_private_case(
            "private_tree",
            [
                sys.executable,
                "tools/test/run_gridftp_tree_private_once.py",
                "--remote",
                args.remote,
                "--server-host",
                args.server_host,
                "--local-build-dir",
                str(build_dir),
                "--remote-build-dir",
                remote_build_dir,
                "--control-port",
                str(port_base + 2),
                "--data-port-base",
                str(port_base + 300),
                "--connections",
                "2",
                "--output-dir",
                str(results_dir),
            ]
            + auth_args(),
            results_dir / "private_tree.log",
            env=env,
        ),
    ]
    event_summary = summarize_event_log(args.event_log)
    report = {
        "timestamp": timestamp_utc(),
        "mode": "private",
        "profile": args.profile,
        "tls_mode": args.tls_mode,
        "data_tls_mode": args.data_tls_mode,
        "cases": cases,
        "event_log": args.event_log,
        "event_summary": event_summary,
        "error_code_counts": event_summary.get("error_code_counts", {}),
        "first_error": event_summary.get("first_error"),
        "result": "pass" if all(case_item.get("result") == "pass" for case_item in cases) else "fail",
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the CPNetFlux alpha demo.")
    parser.add_argument("--mode", choices=["local", "private"], required=True)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--remote")
    parser.add_argument("--remote-root", default="/root/projects/CPNetFlux")
    parser.add_argument("--server-host")
    parser.add_argument("--results-dir", default="tools/perf/results")
    parser.add_argument("--dataset-dir", default="")
    parser.add_argument("--profile", choices=sorted(make_demo_dataset.PROFILES), default="tiny")
    parser.add_argument("--seed", type=int, default=20260518)
    parser.add_argument("--auth-mode", choices=["anonymous", "token"], default="anonymous")
    parser.add_argument("--auth-token-file", default="")
    parser.add_argument("--tls-mode", choices=["off", "required"], default="off")
    parser.add_argument("--tls-cert-file", default="")
    parser.add_argument("--tls-key-file", default="")
    parser.add_argument("--tls-ca-file", default="")
    parser.add_argument("--data-tls-mode", choices=["off", "required"], default="off")
    parser.add_argument("--event-log", default="")
    parser.add_argument("--json-output", default="")
    parser.add_argument("--keep-workdir", action="store_true")
    args = parser.parse_args()
    if args.data_tls_mode == "required" and args.tls_mode != "required":
        parser.error("--data-tls-mode required requires --tls-mode required")

    timestamp = compact_timestamp()
    output_json = Path(args.json_output) if args.json_output else Path(args.results_dir) / f"{timestamp}_alpha-demo-{args.mode}.json"
    if args.mode == "local":
        report = run_local_demo(args, output_json, timestamp)
    else:
        report = run_private_demo(args, output_json, timestamp)
    for case in report["cases"]:
        print(
            "alpha_demo_case "
            f"name={case.get('name')} "
            f"result={case.get('result')} "
            f"elapsed_seconds={float(case.get('elapsed_seconds', 0.0)):.6f} "
            f"bytes={case.get('bytes', 0)}"
        )
    print(f"alpha_demo_json={output_json}")
    print(f"result={report['result']}")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
