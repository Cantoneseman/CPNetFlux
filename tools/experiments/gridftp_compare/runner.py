from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

from tools.release import remote_auth
from tools.test.gridftp_port_window import MAX_PASSIVE_DATA_PORT_BASE, passive_data_port_window_end

from .analyze import classify_evidence, classify_transfer_result, summarize_rows
from .dataset import (
    dataset_kind,
    dataset_specs,
    dataset_total_bytes,
    file_sha256,
    make_dataset,
    materialize_dataset,
    tree_hash,
)
from .preflight import (
    build_globus_partial_get_command,
    build_globus_transfer_command,
    command_text,
    file_url,
    gridftp_url,
    run_gridftp_preflight,
    run_remote_capture,
)
from .schemas import (
    CommandAudit,
    ExperimentCase,
    RESULT_FIELDS,
    STATUS_BLOCKED_EXTERNAL_GRIDFTP,
    STATUS_BLOCKED_IO_URING,
    STATUS_BLOCKED_REMOTE_AUTH,
    STATUS_BLOCKED_RESOURCE,
    STATUS_DRY_RUN,
    STATUS_FAIL_CORRECTNESS,
    STATUS_FAIL_RUNTIME,
    STATUS_PASS,
    SUMMARY_FIELDS,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_REMOTE = "root@47.116.174.181"
DEFAULT_CONTROL_HOST = "47.116.174.181"
DEFAULT_SEED = 20260831
DEFAULT_CHUNK_SIZE = 1024 * 1024
DEFAULT_BUFFER_SIZE = 64 * 1024
DEFAULT_GRIDFTP_GSI_HOME_DIR = "/srv/cpnetflux-gsi"
STAGES = {"preflight", "smoke", "core", "scheduler", "io", "resume", "all"}


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def utc_timestamp() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def parse_csv_list(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def append_jsonl(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(payload, ensure_ascii=False, sort_keys=True) + "\n")


def write_csv(path: Path, fields: list[str], rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def configure_ssh_password_file(args: argparse.Namespace) -> None:
    password_file = args.ssh_password_file or os.environ.get("CPNETFLUX_SSH_PASSWORD_FILE", "")
    if not password_file:
        return
    path = Path(password_file).expanduser()
    if not path.is_file():
        raise SystemExit(f"--ssh-password-file does not exist: {path}")
    mode = path.stat().st_mode
    if mode & 0o077:
        raise SystemExit(f"--ssh-password-file must be readable only by owner; run: chmod 600 {path}")
    password = path.read_text(encoding="utf-8").rstrip("\r\n")
    if not password:
        raise SystemExit(f"--ssh-password-file is empty: {path}")
    os.environ["CPNETFLUX_SSH_PASSWORD"] = password


def safe_id(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")


def case_id(
    *,
    stage: str,
    system: str,
    direction: str,
    dataset: str,
    file_parallelism: int,
    connections: int,
    repeat_index: int,
    scheduler: str = "off",
    scheduler_policy: str = "fixed",
    file_io_backend: str = "posix",
    queue_depth: int = 1,
    batch_size: int = 1,
    resume: bool = False,
) -> str:
    pieces = [
        stage,
        system,
        direction,
        dataset,
        f"fp{file_parallelism}",
        f"n{connections}",
        f"sched{scheduler}-{scheduler_policy}",
        f"io{file_io_backend}-qd{queue_depth}-bs{batch_size}",
        "resume" if resume else "fresh",
        f"r{repeat_index}",
    ]
    return safe_id("_".join(pieces))


def make_case(
    *,
    stage: str,
    system: str,
    direction: str,
    dataset: str,
    file_parallelism: int,
    connections: int,
    repeat_index: int,
    scheduler: str = "off",
    scheduler_policy: str = "fixed",
    file_io_backend: str = "posix",
    queue_depth: int = 1,
    batch_size: int = 1,
    resume: bool = False,
    control_reuse: str = "off",
) -> ExperimentCase:
    return ExperimentCase(
        case_id=case_id(
            stage=stage,
            system=system,
            direction=direction,
            dataset=dataset,
            file_parallelism=file_parallelism,
            connections=connections,
            repeat_index=repeat_index,
            scheduler=scheduler,
            scheduler_policy=scheduler_policy,
            file_io_backend=file_io_backend,
            queue_depth=queue_depth,
            batch_size=batch_size,
            resume=resume,
        ),
        stage=stage,
        system=system,
        direction=direction,
        dataset=dataset,
        file_parallelism=file_parallelism,
        per_file_connections=connections,
        repeat_index=repeat_index,
        scheduler=scheduler,
        scheduler_policy=scheduler_policy,
        file_io_backend=file_io_backend,
        queue_depth=queue_depth,
        batch_size=batch_size,
        control_reuse=control_reuse,
        resume=resume,
    )


def stage_names(stage: str) -> list[str]:
    if stage == "all":
        return ["smoke", "core", "scheduler", "io", "resume"]
    return [stage]


def stage_profiles(stage: str) -> list[str]:
    profiles: list[str] = []
    for name in stage_names(stage):
        if name in {"preflight", "smoke"}:
            profiles.append("single_64MiB")
        elif name == "core":
            profiles.extend(["single_256MiB", "tree_dense_128MiB", "tree_mixed_256MiB"])
        elif name == "scheduler":
            profiles.append("tree_mixed_256MiB")
        elif name == "io":
            profiles.append("single_1GiB")
        elif name == "resume":
            profiles.append("tree_mixed_256MiB")
    return sorted(set(profiles))


def build_cases(
    *,
    stage: str,
    systems: list[str],
    directions: list[str],
    repeat: int,
    scheduler_repeat: int,
    io_repeat: int,
    control_reuse: str,
    max_cases: int = 0,
) -> list[ExperimentCase]:
    cases: list[ExperimentCase] = []
    if stage in {"smoke", "all"}:
        for system in systems:
            for direction in directions:
                cases.append(
                    make_case(
                        stage="smoke",
                        system=system,
                        direction=direction,
                        dataset="single_64MiB",
                        file_parallelism=1,
                        connections=1,
                        repeat_index=0,
                        control_reuse=control_reuse,
                    )
                )
    if stage in {"core", "all"}:
        for system in systems:
            for direction in directions:
                for repeat_index in range(repeat):
                    for connections in [1, 2, 4, 8]:
                        cases.append(
                            make_case(
                                stage="core",
                                system=system,
                                direction=direction,
                                dataset="single_256MiB",
                                file_parallelism=1,
                                connections=connections,
                                repeat_index=repeat_index,
                                control_reuse=control_reuse,
                            )
                        )
                    for file_parallelism in [1, 2, 4, 8]:
                        cases.append(
                            make_case(
                                stage="core",
                                system=system,
                                direction=direction,
                                dataset="tree_dense_128MiB",
                                file_parallelism=file_parallelism,
                                connections=1,
                                repeat_index=repeat_index,
                                control_reuse=control_reuse,
                            )
                        )
                    for file_parallelism, connections in [(1, 1), (1, 2), (2, 2), (4, 2)]:
                        cases.append(
                            make_case(
                                stage="core",
                                system=system,
                                direction=direction,
                                dataset="tree_mixed_256MiB",
                                file_parallelism=file_parallelism,
                                connections=connections,
                                repeat_index=repeat_index,
                                control_reuse=control_reuse,
                            )
                        )
    if stage in {"scheduler", "all"} and "cpnetflux" in systems:
        for direction in directions:
            for repeat_index in range(scheduler_repeat):
                for scheduler, policy in [("off", "fixed"), ("global", "fixed"), ("global", "adaptive")]:
                    cases.append(
                        make_case(
                            stage="scheduler",
                            system="cpnetflux",
                            direction=direction,
                            dataset="tree_mixed_256MiB",
                            file_parallelism=4,
                            connections=8,
                            repeat_index=repeat_index,
                            scheduler=scheduler,
                            scheduler_policy=policy,
                            control_reuse=control_reuse,
                        )
                    )
    if stage in {"io", "all"} and "cpnetflux" in systems:
        for direction in directions:
            for repeat_index in range(io_repeat):
                for backend in ["posix", "io_uring"]:
                    for queue_depth, batch_size in [(1, 1), (4, 4), (8, 8)]:
                        cases.append(
                            make_case(
                                stage="io",
                                system="cpnetflux",
                                direction=direction,
                                dataset="single_1GiB",
                                file_parallelism=1,
                                connections=8,
                                repeat_index=repeat_index,
                                file_io_backend=backend,
                                queue_depth=queue_depth,
                                batch_size=batch_size,
                                control_reuse=control_reuse,
                            )
                        )
    if stage in {"resume", "all"}:
        for system in systems:
            for direction in directions:
                cases.append(
                    make_case(
                        stage="resume",
                        system=system,
                        direction=direction,
                        dataset="tree_mixed_256MiB",
                        file_parallelism=4,
                        connections=2,
                        repeat_index=0,
                        resume=True,
                        control_reuse=control_reuse,
                    )
                )
    if max_cases > 0:
        return cases[:max_cases]
    return cases


def output_dir_from_args(args: argparse.Namespace) -> Path:
    if args.resume_run:
        return Path(args.resume_run).resolve()
    if args.output_dir:
        return Path(args.output_dir).resolve()
    return (REPO_ROOT / "tools" / "experiments" / "gridftp_compare" / "results" / f"manual_{compact_timestamp()}").resolve()


def command_audit_path(output_dir: Path) -> Path:
    return output_dir / "command.jsonl"


def append_command_audit(output_dir: Path, audit: CommandAudit) -> None:
    append_jsonl(command_audit_path(output_dir), audit.to_dict())


def run_logged_command(
    *,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    command_id: str,
    command: list[str],
    stdout_log: Path,
    stderr_log: Path,
    cwd: Path | None = None,
    timeout: int | None = None,
    env: dict[str, str] | None = None,
) -> tuple[int, bool, str]:
    started_at = utc_timestamp()
    start = time.monotonic()
    timed_out = False
    error = ""
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    stderr_log.parent.mkdir(parents=True, exist_ok=True)
    try:
        with stdout_log.open("w", encoding="utf-8") as out, stderr_log.open("w", encoding="utf-8") as err:
            completed = subprocess.run(
                command,
                cwd=str(cwd) if cwd else None,
                text=True,
                stdout=out,
                stderr=err,
                check=False,
                timeout=timeout,
                env=env,
            )
            exit_code = completed.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        exit_code = 124
        error = f"timeout after {exc.timeout}s"
        with stderr_log.open("a", encoding="utf-8") as err:
            err.write(error + "\n")
    except OSError as exc:
        exit_code = 127
        error = str(exc)
        with stderr_log.open("a", encoding="utf-8") as err:
            err.write(error + "\n")
    ended_at = utc_timestamp()
    append_command_audit(
        output_dir,
        CommandAudit(
            timestamp=ended_at,
            run_id=run_id,
            case_id=case.case_id,
            stage=case.stage,
            system=case.system,
            command_id=command_id,
            command=command_text(command),
            cwd=str(cwd or ""),
            started_at=started_at,
            ended_at=ended_at,
            duration_seconds=f"{time.monotonic() - start:.6f}",
            exit_code=str(exit_code),
            stdout_log=str(stdout_log),
            stderr_log=str(stderr_log),
            result=STATUS_FAIL_RUNTIME if exit_code != 0 else STATUS_PASS,
            dry_run=False,
            error=error,
        ),
    )
    return exit_code, timed_out, error


def record_dry_run_command(output_dir: Path, run_id: str, case: ExperimentCase, command_id: str, command: list[str]) -> None:
    append_command_audit(
        output_dir,
        CommandAudit(
            timestamp=utc_timestamp(),
            run_id=run_id,
            case_id=case.case_id,
            stage=case.stage,
            system=case.system,
            command_id=command_id,
            command=command_text(command),
            result=STATUS_DRY_RUN,
            dry_run=True,
        ),
    )


def run_remote_command(
    *,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    command_id: str,
    remote: str,
    command: str,
    timeout: int = 60,
) -> subprocess.CompletedProcess[str]:
    started_at = utc_timestamp()
    start = time.monotonic()
    log_stem = safe_id(f"{case.case_id}_{command_id}")
    stdout_log = output_dir / "command_logs" / f"{log_stem}.stdout.log"
    stderr_log = output_dir / "command_logs" / f"{log_stem}.stderr.log"
    try:
        completed = run_remote_capture(remote, command, timeout=timeout)
        error = ""
    except subprocess.TimeoutExpired as exc:
        completed = subprocess.CompletedProcess(args=command, returncode=124, stdout="", stderr=f"timeout after {exc.timeout}s")
        error = completed.stderr
    stdout_log.parent.mkdir(parents=True, exist_ok=True)
    stdout_log.write_text(completed.stdout, encoding="utf-8")
    stderr_log.write_text(completed.stderr, encoding="utf-8")
    ended_at = utc_timestamp()
    append_command_audit(
        output_dir,
        CommandAudit(
            timestamp=ended_at,
            run_id=run_id,
            case_id=case.case_id,
            stage=case.stage,
            system=case.system,
            command_id=command_id,
            command=f"ssh {remote} {command}",
            started_at=started_at,
            ended_at=ended_at,
            duration_seconds=f"{time.monotonic() - start:.6f}",
            exit_code=str(completed.returncode),
            stdout_log=str(stdout_log),
            stderr_log=str(stderr_log),
            result=STATUS_FAIL_RUNTIME if completed.returncode != 0 else STATUS_PASS,
            dry_run=False,
            error=error or completed.stderr.strip()[:1000],
        ),
    )
    return completed


def probe_remote_access(remote: str) -> tuple[bool, str]:
    try:
        completed = run_remote_capture(remote, "true", timeout=10)
    except Exception as exc:  # noqa: BLE001
        return False, str(exc)
    if completed.returncode == 0:
        return True, ""
    return False, (completed.stdout + completed.stderr).strip()


def run_link_baseline(args: argparse.Namespace, output_dir: Path, run_id: str) -> dict[str, Any]:
    baseline: dict[str, Any] = {
        "status": "blocked_remote_auth",
        "tool": "iperf3",
        "duration_seconds": args.iperf_duration,
        "tests": [],
    }
    path = output_dir / "link_baseline.json"
    if args.dry_run:
        baseline["status"] = "dry_run"
        write_json(path, baseline)
        return baseline
    if not getattr(args, "remote_access_ok", False):
        baseline["error"] = "remote auth unavailable; iperf3 baseline was not executed"
        write_json(path, baseline)
        return baseline
    if shutil.which("iperf3") is None:
        baseline["status"] = "blocked_link_baseline"
        baseline["error"] = "local iperf3 not found"
        write_json(path, baseline)
        return baseline
    remote_iperf = run_remote_capture(args.remote, "command -v iperf3", timeout=20)
    if remote_iperf.returncode != 0:
        baseline["status"] = "blocked_link_baseline"
        baseline["error"] = "remote iperf3 not found"
        write_json(path, baseline)
        return baseline

    baseline_dir = output_dir / "link_baseline"
    baseline_dir.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    for direction, reverse in [("shenzhen_to_shanghai", False), ("shanghai_to_shenzhen", True)]:
        for parallelism in [1, 8]:
            label = f"{direction}_p{parallelism}"
            remote_log = f"{args.remote_work_root.rstrip('/')}/{run_id}-iperf-{label}.log"
            start = run_remote_capture(
                args.remote,
                (
                    f"rm -f {shlex.quote(remote_log)}; "
                    f"nohup iperf3 -s -1 -p {args.iperf_port} > {shlex.quote(remote_log)} 2>&1 < /dev/null & "
                    "printf '%s\\n' \"$!\""
                ),
                timeout=20,
            )
            server_pid = start.stdout.strip().splitlines()[-1] if start.stdout.strip() else ""
            client_command = [
                "iperf3",
                "-c",
                args.control_host,
                "-p",
                str(args.iperf_port),
                "-t",
                str(args.iperf_duration),
                "-P",
                str(parallelism),
                "-J",
            ]
            if reverse:
                client_command.append("-R")
            started_at = time.monotonic()
            try:
                client = subprocess.run(
                    client_command,
                    text=True,
                    capture_output=True,
                    check=False,
                    timeout=args.iperf_duration + 45,
                )
            except subprocess.TimeoutExpired as exc:
                client = subprocess.CompletedProcess(
                    client_command,
                    124,
                    stdout=exc.stdout or "",
                    stderr=str(exc),
                )
            elapsed = time.monotonic() - started_at
            if server_pid.isdigit():
                run_remote_capture(args.remote, f"kill -TERM {shlex.quote(server_pid)} 2>/dev/null || true", timeout=20)
            server = run_remote_capture(args.remote, f"cat {shlex.quote(remote_log)}", timeout=20)
            client_json_path = baseline_dir / f"{label}.client.json"
            server_log_path = baseline_dir / f"{label}.server.log"
            client_json_path.write_text(client.stdout or "", encoding="utf-8")
            server_log_path.write_text(server.stdout + server.stderr, encoding="utf-8")
            test: dict[str, Any] = {
                "label": label,
                "direction": direction,
                "reverse": reverse,
                "parallelism": parallelism,
                "server_start_exit_code": start.returncode,
                "client_command": command_text(client_command),
                "client_exit_code": client.returncode,
                "client_json": str(client_json_path),
                "server_log": str(server_log_path),
                "elapsed_seconds": elapsed,
                "receiver_bitrate_bps": None,
                "sender_bitrate_bps": None,
                "retransmits": None,
                "rtt_ms": None,
            }
            try:
                data = json.loads(client.stdout)
                end = data["end"]
                received = end.get("sum_received", {})
                sent = end.get("sum_sent", {})
                test["receiver_bitrate_bps"] = received.get("bits_per_second")
                test["sender_bitrate_bps"] = sent.get("bits_per_second")
                test["retransmits"] = sent.get("retransmits")
                rtts = [
                    stream.get("sender", {}).get("mean_rtt")
                    for stream in end.get("streams", [])
                    if stream.get("sender", {}).get("mean_rtt") is not None
                ]
                if rtts:
                    test["rtt_ms"] = sum(rtts) / len(rtts) / 1000.0
                test["result"] = "pass" if client.returncode == 0 and start.returncode == 0 else "fail"
            except (json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
                test["result"] = "fail"
                test["error"] = str(exc)
            if test["result"] != "pass":
                failures.append(label)
            baseline["tests"].append(test)
    baseline["status"] = "pass" if not failures and len(baseline["tests"]) == 4 else "blocked_link_baseline"
    if failures:
        baseline["failed_tests"] = failures
    write_json(path, baseline)
    return baseline


def wait_tcp(host: str, port: int, *, timeout: float = 20.0) -> None:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=2.0):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(0.2)
    raise RuntimeError(f"control port {host}:{port} did not become reachable: {last_error}")


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


class ControlConnection:
    def __init__(self, host: str, port: int) -> None:
        self.sock = socket.create_connection((host, port), timeout=10.0)
        self.buffer = bytearray()
        greeting = self.read_reply()
        if reply_code(greeting) != 220:
            raise RuntimeError(f"unexpected control greeting: {greeting!r}")

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
        user = self.send("USER cpnetflux")
        if reply_code(user) != 331:
            raise RuntimeError(f"USER failed: {user!r}")
        password = self.send("PASS cpnetflux")
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


class SocketSampler:
    def __init__(self, path: Path, *, case_id: str, control_port: int, data_port_base: int, interval: float) -> None:
        self.path = path
        self.case_id = case_id
        self.control_port = control_port
        self.data_port_base = data_port_base
        self.data_port_end = passive_data_port_window_end(data_port_base)
        self.interval = interval
        self.max_data_streams = 0
        self.peak_established_data_streams = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def __enter__(self) -> "SocketSampler":
        self._thread.start()
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self._stop.set()
        self._thread.join(timeout=max(1.0, self.interval * 2.0))

    def _run(self) -> None:
        while not self._stop.is_set():
            self.sample_once()
            self._stop.wait(self.interval)

    def sample_once(self) -> None:
        try:
            established = subprocess.run(
                ["ss", "-Htan", "state", "established"],
                text=True,
                capture_output=True,
                check=False,
                timeout=5,
            )
            time_wait = subprocess.run(
                ["ss", "-Htan", "state", "time-wait"],
                text=True,
                capture_output=True,
                check=False,
                timeout=5,
            )
            established_data_streams = 0
            control_streams = 0
            for line in established.stdout.splitlines():
                ports = [int(match) for match in re.findall(r":(\d+)\b", line)]
                if self.control_port in ports:
                    control_streams += 1
                if any(self.data_port_base <= port <= self.data_port_end for port in ports):
                    established_data_streams += 1
            time_wait_data_sockets = sum(
                1
                for line in time_wait.stdout.splitlines()
                if any(
                    self.data_port_base <= port <= self.data_port_end
                    for port in [int(match) for match in re.findall(r":(\d+)\b", line)]
                )
            )
            self.peak_established_data_streams = max(
                self.peak_established_data_streams,
                established_data_streams,
            )
            self.max_data_streams = self.peak_established_data_streams
            payload = {
                "timestamp": utc_timestamp(),
                "case_id": self.case_id,
                "control_port": self.control_port,
                "data_port_range": f"{self.data_port_base}..{self.data_port_end}",
                "control_streams": control_streams,
                "established_data_streams": established_data_streams,
                "peak_established_data_streams": self.peak_established_data_streams,
                "time_wait_data_sockets": time_wait_data_sockets,
                "ss_exit_code": established.returncode,
                "time_wait_ss_exit_code": time_wait.returncode,
            }
        except Exception as exc:  # noqa: BLE001
            payload = {
                "timestamp": utc_timestamp(),
                "case_id": self.case_id,
                "control_port": self.control_port,
                "data_port_range": f"{self.data_port_base}..{self.data_port_end}",
                "error": str(exc),
            }
        append_jsonl(self.path, payload)


def control_port(args: argparse.Namespace, case_index: int) -> int:
    return args.cpnetflux_control_port_base + case_index * args.port_stride


def data_port_base(args: argparse.Namespace, case_index: int) -> int:
    return args.cpnetflux_data_port_base + case_index * args.port_stride


def validate_case_ports(args: argparse.Namespace, cases: list[ExperimentCase]) -> None:
    for index, _case in enumerate(cases):
        if control_port(args, index) > 65535:
            raise SystemExit("control port range exceeds 65535")
        if data_port_base(args, index) > MAX_PASSIVE_DATA_PORT_BASE:
            raise SystemExit("data port window exceeds 65535")


REMOTE_DATASET_SCRIPT = r"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

root = Path(sys.argv[1])
specs = json.loads(sys.argv[2])
seed = int(sys.argv[3])
if root.exists():
    shutil.rmtree(root)
root.mkdir(parents=True)

def deterministic_block(seed_value, label, counter):
    digest = hashlib.sha256(f"{seed_value}:{label}:{counter}".encode("utf-8")).digest()
    return (digest * (4096 // len(digest) + 1))[:4096]

def write_file(path, size, label):
    path.parent.mkdir(parents=True, exist_ok=True)
    remaining = int(size)
    counter = 0
    with path.open("wb") as handle:
        while remaining > 0:
            block = deterministic_block(seed, label, counter)
            chunk = block[:min(remaining, len(block))]
            handle.write(chunk)
            remaining -= len(chunk)
            counter += 1

for spec in specs:
    write_file(root / spec["path"], int(spec["size"]), spec["label"])

digest = hashlib.sha256()
file_count = 0
total_bytes = 0
for path in sorted(item for item in root.rglob("*") if item.is_file()):
    rel = path.relative_to(root).as_posix()
    if ".cpnetflux." in rel or ".part." in rel:
        continue
    file_digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            file_digest.update(block)
    size = path.stat().st_size
    file_count += 1
    total_bytes += size
    digest.update(rel.encode("utf-8") + b"\0")
    digest.update(str(size).encode("ascii") + b"\0")
    digest.update(file_digest.hexdigest().encode("ascii") + b"\0")
print(json.dumps({"tree_hash": digest.hexdigest(), "file_count": file_count, "total_bytes": total_bytes}))
"""


REMOTE_TREE_HASH_SCRIPT = r"""
import hashlib
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
digest = hashlib.sha256()
file_count = 0
total_bytes = 0
for path in sorted(item for item in root.rglob("*") if item.is_file()):
    rel = path.relative_to(root).as_posix()
    if ".cpnetflux." in rel or ".part." in rel:
        continue
    file_digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            file_digest.update(block)
    size = path.stat().st_size
    file_count += 1
    total_bytes += size
    digest.update(rel.encode("utf-8") + b"\0")
    digest.update(str(size).encode("ascii") + b"\0")
    digest.update(file_digest.hexdigest().encode("ascii") + b"\0")
print(json.dumps({"tree_hash": digest.hexdigest(), "file_count": file_count, "total_bytes": total_bytes}))
"""


def remote_python_command(script: str, args: list[str]) -> str:
    return "python3 - " + " ".join(shlex.quote(arg) for arg in args) + " <<'PY'\n" + script + "\nPY"


def materialize_remote_dataset(
    *,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    remote: str,
    root: str,
    seed: int,
    timeout: int,
) -> dict[str, Any]:
    command = remote_python_command(REMOTE_DATASET_SCRIPT, [root, json.dumps(dataset_specs(case.dataset)), str(seed)])
    completed = run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="remote_materialize_dataset",
        remote=remote,
        command=command,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return json.loads(completed.stdout.strip())


def remote_tree_hash(
    *,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    remote: str,
    root: str,
    timeout: int,
) -> dict[str, Any]:
    completed = run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="remote_tree_hash",
        remote=remote,
        command=remote_python_command(REMOTE_TREE_HASH_SCRIPT, [root]),
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return json.loads(completed.stdout.strip())


def remote_single_sha256(
    *,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    remote: str,
    path: str,
    timeout: int,
) -> str:
    command = (
        "python3 - "
        + shlex.quote(path)
        + " <<'PY'\n"
        + "import hashlib, sys\n"
        + "digest = hashlib.sha256()\n"
        + "with open(sys.argv[1], 'rb') as handle:\n"
        + "    for block in iter(lambda: handle.read(1024 * 1024), b''):\n"
        + "        digest.update(block)\n"
        + "print(digest.hexdigest())\n"
        + "PY"
    )
    completed = run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="remote_sha256",
        remote=remote,
        command=command,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return completed.stdout.strip()


def materialize_local_payload(root: Path, case: ExperimentCase, seed: int) -> dict[str, Any]:
    materialize_dataset(root, profile=case.dataset, seed=seed, clean=True)
    hash_value, count, total = tree_hash(root)
    return {"tree_hash": hash_value, "file_count": count, "total_bytes": total}


def remote_case_root(args: argparse.Namespace, run_id: str, case: ExperimentCase) -> str:
    if case.system == "gridftp" and args.gridftp_auth_mode in {"anonymous", "gsi"}:
        home_dir = getattr(args, "gridftp_home_dir", "")
        if home_dir:
            return f"{home_dir.rstrip('/')}/{case.case_id}"
    return f"{args.remote_work_root.rstrip('/')}/{run_id}/{case.case_id}"


def prepare_gridftp_destination_command(remote_root: str, remote_directory: str, auth_mode: str) -> str:
    command = f"rm -rf {shlex.quote(remote_root)} && mkdir -p {shlex.quote(remote_directory)}"
    if auth_mode == "anonymous":
        command += (
            f" && chown -R nobody:nogroup {shlex.quote(remote_root)}"
            f" && chmod -R u+rwX,g+rwX,o-rwx {shlex.quote(remote_root)}"
        )
    elif auth_mode == "gsi":
        command += f" && chmod 0777 {shlex.quote(remote_directory)}"
    return command


def prepare_gridftp_source_permissions_command(remote_dataset_root: str, auth_mode: str) -> str:
    if auth_mode == "anonymous":
        return (
            f"chown -R nobody:nogroup {shlex.quote(remote_dataset_root)} && "
            f"chmod -R u+rwX,g+rwX,o-rwx {shlex.quote(remote_dataset_root)}"
        )
    if auth_mode == "gsi":
        return f"chmod -R a+rX {shlex.quote(remote_dataset_root)}"
    raise ValueError(f"unsupported GridFTP auth mode: {auth_mode}")


def gridftp_service_paths(args: argparse.Namespace, run_id: str) -> dict[str, str]:
    service_root = f"{args.remote_work_root.rstrip('/')}/{run_id}"
    home_dir = getattr(args, "gridftp_home_dir", "") or f"{service_root}/server-root"
    return {
        "service_root": service_root,
        "home_dir": home_dir,
        "pidfile": f"{service_root}/gridftp-server.pid",
        "server_log": f"{service_root}/gridftp-server.log",
        "transfer_log": f"{service_root}/gridftp-transfer.log",
    }


def gridftp_service_case() -> ExperimentCase:
    return make_case(
        stage="preflight",
        system="gridftp",
        direction="local_to_remote",
        dataset="single_64MiB",
        file_parallelism=1,
        connections=1,
        repeat_index=0,
    )


def remote_gridftp_service_probe_command(paths: dict[str, str], port: int) -> str:
    pidfile = shlex.quote(paths["pidfile"])
    home_dir = shlex.quote(paths["home_dir"])
    return (
        f"pid=''\n"
        f"if test -r {pidfile}; then pid=$(tr -cd '0-9' < {pidfile}); fi\n"
        "owned=false\n"
        "args=''\n"
        "if test -n \"$pid\" && kill -0 \"$pid\" 2>/dev/null; then\n"
        "  args=$(ps -p \"$pid\" -o args= 2>/dev/null || true)\n"
        f"  if printf '%s' \"$args\" | grep -F -- {home_dir} >/dev/null 2>&1; then owned=true; fi\n"
        "fi\n"
        f"listening=$(ss -H -ltn 2>/dev/null | awk '$4 ~ /:{port}$/ {{count++}} END {{print count+0}}')\n"
        "printf 'owned=%s\\npid=%s\\nlistening=%s\\n' \"$owned\" \"$pid\" \"$listening\"\n"
        "printf '%s\\n' '-- globus-gridftp-server processes --'\n"
        "ps -eo pid=,args= | grep '[g]lobus-gridftp-server' || true\n"
        "printf '%s\\n' '-- control listeners --'\n"
        f"ss -ltnp 2>&1 | grep -E ':{port}([[:space:]]|$)' || true"
    )


def remote_gridftp_service_owner_command(paths: dict[str, str]) -> str:
    pidfile = shlex.quote(paths["pidfile"])
    home_dir = shlex.quote(paths["home_dir"])
    return (
        f"test -r {pidfile} || exit 1\n"
        f"pid=$(tr -cd '0-9' < {pidfile})\n"
        "test -n \"$pid\" && kill -0 \"$pid\" 2>/dev/null || exit 1\n"
        "args=$(ps -p \"$pid\" -o args= 2>/dev/null || true)\n"
        f"printf '%s' \"$args\" | grep -F -- {home_dir} >/dev/null 2>&1 || exit 1\n"
        "printf '%s\\n' \"$pid\"\n"
    )


def stop_managed_gridftp_server(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    paths: dict[str, str],
) -> None:
    case = gridftp_service_case()
    owned = run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="verify_gridftp_service_before_stop",
        remote=args.remote,
        command=remote_gridftp_service_owner_command(paths),
        timeout=30,
    )
    pid = owned.stdout.strip()
    if owned.returncode != 0 or not pid.isdigit():
        return
    run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="stop_gridftp_service",
        remote=args.remote,
        command=f"kill -TERM {shlex.quote(pid)} 2>/dev/null || true",
        timeout=30,
    )


def ensure_remote_gridftp_server(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    systems: list[str],
) -> dict[str, Any]:
    if args.dry_run or "gridftp" not in systems:
        return {"ok": True, "managed": False, "skipped": True}
    if args.gridftp_auth_mode != "anonymous":
        return {"ok": True, "managed": False, "skipped": True, "reason": "GSI mode uses its configured server"}
    if not getattr(args, "remote_access_ok", False):
        return {"ok": False, "managed": False, "error": "remote auth unavailable; standalone GridFTP server was not started"}

    paths = gridftp_service_paths(args, run_id)
    case = gridftp_service_case()
    probe = run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="gridftp_service_inspect",
        remote=args.remote,
        command=remote_gridftp_service_probe_command(paths, args.control_port),
        timeout=30,
    )
    detail: dict[str, Any] = {
        "ok": False,
        "managed": True,
        "paths": paths,
        "inspection_exit_code": probe.returncode,
        "inspection": probe.stdout,
    }
    owned = re.search(r"^owned=true$", probe.stdout, flags=re.MULTILINE) is not None
    listening_match = re.search(r"^listening=(\d+)$", probe.stdout, flags=re.MULTILINE)
    listening = int(listening_match.group(1)) if listening_match else 0
    if probe.returncode != 0:
        detail["error"] = "could not inspect the remote GridFTP service"
    elif owned and listening:
        detail.update(started=False, reused_owned_service=True, owner_pid=re.search(r"^pid=(\d+)$", probe.stdout, re.MULTILINE).group(1))
    elif listening:
        detail["error"] = f"control port {args.control_port} is occupied by a non-owned process; the harness will not stop it"
    else:
        if owned:
            stop_managed_gridftp_server(args=args, output_dir=output_dir, run_id=run_id, paths=paths)
        server_command = [
            args.globus_gridftp_server,
            "-S",
            "-p",
            str(args.control_port),
            "-aa",
            "-anonymous-user",
            "nobody",
            "-anonymous-group",
            "nogroup",
            "-home-dir",
            paths["home_dir"],
            "-port-range",
            f"{args.data_port_base},{passive_data_port_window_end(args.data_port_base)}",
            "-l",
            paths["server_log"],
            "-Z",
            paths["transfer_log"],
            "-d",
            "INFO",
            "-pidfile",
            paths["pidfile"],
        ]
        setup = (
            f"mkdir -p {shlex.quote(paths['home_dir'])} && "
            f"chown -R nobody:nogroup {shlex.quote(paths['home_dir'])} && "
            f"chmod -R u+rwX,g+rwX,o-rwx {shlex.quote(paths['home_dir'])} && "
            f"rm -f {shlex.quote(paths['pidfile'])} && {command_text(server_command)}"
        )
        started = run_remote_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id="start_gridftp_service",
            remote=args.remote,
            command=setup,
            timeout=30,
        )
        detail["start_exit_code"] = started.returncode
        if started.returncode == 0:
            verified = run_remote_command(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                command_id="verify_gridftp_service_owner",
                remote=args.remote,
                command=remote_gridftp_service_owner_command(paths),
                timeout=30,
            )
            try:
                wait_tcp(args.control_host, args.control_port, timeout=args.server_start_timeout)
                reachable = True
                reachability_error = ""
            except RuntimeError as exc:
                reachable = False
                reachability_error = str(exc)
            detail.update(
                started=True,
                owner_pid=verified.stdout.strip(),
                verify_exit_code=verified.returncode,
                reachable=reachable,
                reachability_error=reachability_error,
            )
            detail["ok"] = verified.returncode == 0 and reachable
        else:
            detail["error"] = "standalone GridFTP server start command failed"
    write_json(output_dir / "gridftp_service.json", detail)
    return detail


def start_remote_cpnetflux_server(
    *,
    output_dir: Path,
    run_id: str,
    args: argparse.Namespace,
    case: ExperimentCase,
    case_index: int,
    remote_root: str,
) -> tuple[str, str, str]:
    server_log = f"{remote_root}/server.log"
    server_event_log = f"{remote_root}/server_events.jsonl"
    server_bin = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-gridftp-server"
    command = [
        server_bin,
        "--root",
        f"{remote_root}/server-root",
        "--host",
        "0.0.0.0",
        "--port",
        str(control_port(args, case_index)),
        "--data-port-base",
        str(data_port_base(args, case_index)),
        "--connections",
        str(case.per_file_connections),
        "--chunk-size",
        str(args.chunk_size),
        "--buffer-size",
        str(args.buffer_size),
        "--checksum",
        case.checksum,
        "--checksum-backend",
        args.checksum_backend,
        "--manifest-flush-policy",
        "every_n_chunks",
        "--manifest-flush-interval-chunks",
        str(args.manifest_flush_interval_chunks),
        "--commit-sync-policy",
        args.commit_sync_policy,
        "--final-verify-policy",
        args.final_verify_policy,
        "--preallocate",
        args.preallocate,
        "--file-io-backend",
        case.file_io_backend,
        "--file-io-buffer-size",
        str(args.file_io_buffer_size),
        "--file-io-queue-depth",
        str(case.queue_depth),
        "--file-io-batch-size",
        str(case.batch_size),
        "--file-io-advice",
        args.file_io_advice,
        "--posix-write-strategy",
        args.posix_write_strategy,
        "--auth-mode",
        args.auth_mode,
        "--event-log",
        server_event_log,
    ]
    setup = (
        f"mkdir -p {shlex.quote(remote_root + '/server-root')} && "
        f"(nohup {command_text(command)} > {shlex.quote(server_log)} 2>&1 < /dev/null & printf '%s\\n' \"$!\")"
    )
    completed = run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="start_cpnetflux_server",
        remote=args.remote,
        command=setup,
        timeout=30,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    pid = completed.stdout.strip().splitlines()[-1]
    if not pid.isdigit():
        raise RuntimeError(f"remote server pid was not numeric: {pid!r}")
    wait_tcp(args.control_host, control_port(args, case_index), timeout=args.server_start_timeout)
    return pid, server_log, server_event_log


def stop_remote_cpnetflux_server(
    *,
    output_dir: Path,
    run_id: str,
    args: argparse.Namespace,
    case: ExperimentCase,
    pid: str,
) -> None:
    if not pid:
        return
    command = (
        f"kill -TERM -- -{shlex.quote(pid)} 2>/dev/null || kill -TERM {shlex.quote(pid)} 2>/dev/null || true; "
        "sleep 1; "
        f"kill -KILL -- -{shlex.quote(pid)} 2>/dev/null || true"
    )
    run_remote_command(
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        command_id="stop_cpnetflux_server",
        remote=args.remote,
        command=command,
        timeout=30,
    )


def fetch_remote_text(remote: str, path: str, local_path: Path) -> None:
    local_path.parent.mkdir(parents=True, exist_ok=True)
    completed = run_remote_capture(remote, f"test -f {shlex.quote(path)} && cat {shlex.quote(path)}", timeout=60)
    local_path.write_text(completed.stdout + completed.stderr, encoding="utf-8")


def fetch_remote_manifests(remote: str, root: str, local_dir: Path) -> list[str]:
    parent = str(Path(root).parent)
    listing = run_remote_capture(
        remote,
        f"find {shlex.quote(parent)} -maxdepth 2 -type f -name '*.cpnetflux*.manifest' -print",
        timeout=60,
    )
    if listing.returncode != 0:
        return []
    saved: list[str] = []
    for remote_path in listing.stdout.splitlines():
        remote_path = remote_path.strip()
        if not remote_path:
            continue
        relative = safe_id(str(Path(remote_path).relative_to(Path(parent))))
        local_path = local_dir / "manifests" / relative
        content = run_remote_capture(
            remote,
            f"cat {shlex.quote(remote_path)}",
            timeout=60,
        )
        if content.returncode == 0:
            local_path.parent.mkdir(parents=True, exist_ok=True)
            local_path.write_text(content.stdout, encoding="utf-8")
            saved.append(str(local_path))
    return saved


def fetch_case_remote_manifests(args: argparse.Namespace, root: str, case_dir: Path) -> None:
    if not getattr(args, "remote_access_ok", False):
        return
    fetch_remote_manifests(args.remote, root, case_dir)


def manifest_verified_chunk_count(path: Path) -> int:
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("verified_chunks="):
            value = line.partition("=")[2]
            return len([item for item in value.split(",") if item])
    return 0


def build_tree_client_command(
    *,
    args: argparse.Namespace,
    case: ExperimentCase,
    case_dir: Path,
    source_dir: str,
    dest_dir: str,
) -> list[str]:
    binary = "cpnetflux-tree-upload-client" if case.direction == "local_to_remote" else "cpnetflux-tree-download-client"
    command = [
        str(Path(args.local_build_dir) / binary),
        "--host",
        args.control_host,
        "--port",
        str(args.active_control_port),
        "--source-dir",
        source_dir,
        "--dest-dir",
        dest_dir,
        "--connections",
        str(case.per_file_connections),
        "--file-parallelism",
        str(case.file_parallelism),
        "--chunk-size",
        str(args.chunk_size),
        "--buffer-size",
        str(args.buffer_size),
        "--checksum",
        case.checksum,
        "--compression",
        case.compression,
        "--checksum-backend",
        args.checksum_backend,
        "--control-reuse",
        case.control_reuse,
        "--auth-mode",
        args.auth_mode,
        "--json-summary",
        str(case_dir / "client_summary.json"),
        "--event-log",
        str(case_dir / "client_events.jsonl"),
    ]
    if case.resume:
        command.append("--resume")
    if case.scheduler == "global":
        metrics_dir = case_dir / "scheduler_metrics"
        command.extend(
            [
                "--scheduler",
                "global",
                "--scheduler-policy",
                case.scheduler_policy,
                "--scheduler-metrics-dir",
                str(metrics_dir),
                "--scheduler-link-id",
                args.scheduler_link_id,
                "--scheduler-capacity-gbps",
                str(args.scheduler_capacity_gbps),
                "--scheduler-workitem-min-bytes",
                str(args.scheduler_workitem_min_bytes),
                "--scheduler-workitem-max-bytes",
                str(args.scheduler_workitem_max_bytes),
                "--scheduler-default-rtt-ms",
                str(args.scheduler_default_rtt_ms),
                "--scheduler-min-compress-gbps",
                str(args.scheduler_min_compress_gbps),
            ]
        )
    return command


def build_file_client_command(
    *,
    args: argparse.Namespace,
    case: ExperimentCase,
    case_dir: Path,
    data_port: int,
    transfer_id: str,
    source_or_output: Path,
    upload: bool,
    resume: bool = False,
    max_chunks: int = 0,
) -> list[str]:
    if upload:
        command = [
            str(Path(args.local_build_dir) / "cpnetflux-file-client"),
            "--host",
            args.control_host,
            "--port",
            str(data_port),
            "--input",
            str(source_or_output),
            "--connections",
            str(case.per_file_connections),
            "--chunk-size",
            str(args.chunk_size),
            "--buffer-size",
            str(args.buffer_size),
            "--checksum",
            case.checksum,
            "--checksum-backend",
            args.checksum_backend,
            "--transfer-id",
            transfer_id,
        ]
    else:
        command = [
            str(Path(args.local_build_dir) / "cpnetflux-file-download-client"),
            "--host",
            args.control_host,
            "--port",
            str(data_port),
            "--output",
            str(source_or_output),
            "--connections",
            str(case.per_file_connections),
            "--buffer-size",
            str(args.buffer_size),
            "--checksum",
            case.checksum,
            "--checksum-backend",
            args.checksum_backend,
            "--transfer-id",
            transfer_id,
            "--manifest-flush-policy",
            "every_n_chunks",
            "--manifest-flush-interval-chunks",
            str(args.manifest_flush_interval_chunks),
            "--commit-sync-policy",
            args.commit_sync_policy,
            "--final-verify-policy",
            args.final_verify_policy,
            "--preallocate",
            args.preallocate,
        ]
    command.extend(
        [
            "--file-io-backend",
            case.file_io_backend,
            "--file-io-buffer-size",
            str(args.file_io_buffer_size),
            "--file-io-queue-depth",
            str(case.queue_depth),
            "--file-io-batch-size",
            str(case.batch_size),
            "--file-io-advice",
            args.file_io_advice,
            "--posix-write-strategy",
            args.posix_write_strategy,
            "--event-log",
            str(case_dir / "client_events.jsonl"),
        ]
    )
    if resume:
        command.append("--resume")
    if max_chunks > 0:
        command.extend(["--max-chunks", str(max_chunks)])
    return command


def open_data_port(args: argparse.Namespace, case_index: int) -> tuple[ControlConnection, int]:
    control = ControlConnection(args.control_host, control_port(args, case_index))
    control.login_type_i()
    epsv = control.send("EPSV")
    if reply_code(epsv) != 229:
        control.close()
        raise RuntimeError(f"EPSV failed: {epsv!r}")
    port = parse_epsv_port(epsv)
    base = data_port_base(args, case_index)
    end = passive_data_port_window_end(base)
    if port < base or port > end:
        control.close()
        raise RuntimeError(f"EPSV port {port} outside data window {base}..{end}")
    return control, port


def run_cpnetflux_file_case(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    case_index: int,
    case_dir: Path,
    remote_root: str,
) -> dict[str, Any]:
    server_root = f"{remote_root}/server-root"
    local_source_root = case_dir / "source_payload"
    local_dest_root = case_dir / "dest_payload"
    local_source_file = local_source_root / "single.bin"
    local_dest_file = local_dest_root / "single.bin"
    remote_source_file = f"{server_root}/single.bin"
    remote_dest_file = f"{server_root}/single.bin"
    server_pid = ""
    server_remote_log = ""
    server_remote_event = ""
    client_stdout = case_dir / "client.stdout.log"
    client_stderr = case_dir / "client.stderr.log"
    start = time.monotonic()
    source_hash = ""
    destination_hash = ""
    source_count = destination_count = 1
    exit_code = 1
    timed_out = False
    error = ""
    try:
        prep = run_remote_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id="remote_case_prepare",
            remote=args.remote,
            command=f"rm -rf {shlex.quote(remote_root)} && mkdir -p {shlex.quote(server_root)}",
            timeout=60,
        )
        if prep.returncode != 0:
            raise RuntimeError(prep.stdout + prep.stderr)
        if case.direction == "local_to_remote":
            materialize_local_payload(local_source_root, case, args.seed)
        else:
            materialize_remote_dataset(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                remote=args.remote,
                root=server_root,
                seed=args.seed,
                timeout=args.dataset_timeout,
            )
        server_pid, server_remote_log, server_remote_event = start_remote_cpnetflux_server(
            output_dir=output_dir,
            run_id=run_id,
            args=args,
            case=case,
            case_index=case_index,
            remote_root=remote_root,
        )
        if case.direction == "local_to_remote":
            source_hash = file_sha256(local_source_file)
            control, data_port = open_data_port(args, case_index)
            try:
                stor = control.send("STOR single.bin")
                if reply_code(stor) != 150:
                    raise RuntimeError(f"STOR failed: {stor!r}")
                transfer_id = parse_transfer_id(stor)
                command = build_file_client_command(
                    args=args,
                    case=case,
                    case_dir=case_dir,
                    data_port=data_port,
                    transfer_id=transfer_id,
                    source_or_output=local_source_file,
                    upload=True,
                )
                with SocketSampler(
                    output_dir / "socket_samples.jsonl",
                    case_id=case.case_id,
                    control_port=control_port(args, case_index),
                    data_port_base=data_port_base(args, case_index),
                    interval=args.socket_sample_interval,
                ) as sampler:
                    exit_code, timed_out, error = run_logged_command(
                        output_dir=output_dir,
                        run_id=run_id,
                        case=case,
                        command_id="cpnetflux_file_upload",
                        command=command,
                        stdout_log=client_stdout,
                        stderr_log=client_stderr,
                        cwd=REPO_ROOT,
                        timeout=args.case_timeout,
                    )
                final = control.read_reply()
                if reply_code(final) != 226:
                    raise RuntimeError(f"STOR completion failed: {final!r}")
                max_streams = sampler.max_data_streams
            finally:
                control.close()
            destination_hash = remote_single_sha256(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                remote=args.remote,
                path=remote_dest_file,
                timeout=args.case_timeout,
            )
            fetch_case_remote_manifests(args, server_root, case_dir)
        else:
            source_hash = remote_single_sha256(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                remote=args.remote,
                path=remote_source_file,
                timeout=args.case_timeout,
            )
            local_dest_root.mkdir(parents=True, exist_ok=True)
            control, data_port = open_data_port(args, case_index)
            try:
                retr = control.send("RETR single.bin")
                if reply_code(retr) != 150:
                    raise RuntimeError(f"RETR failed: {retr!r}")
                transfer_id = parse_transfer_id(retr)
                command = build_file_client_command(
                    args=args,
                    case=case,
                    case_dir=case_dir,
                    data_port=data_port,
                    transfer_id=transfer_id,
                    source_or_output=local_dest_file,
                    upload=False,
                )
                with SocketSampler(
                    output_dir / "socket_samples.jsonl",
                    case_id=case.case_id,
                    control_port=control_port(args, case_index),
                    data_port_base=data_port_base(args, case_index),
                    interval=args.socket_sample_interval,
                ) as sampler:
                    exit_code, timed_out, error = run_logged_command(
                        output_dir=output_dir,
                        run_id=run_id,
                        case=case,
                        command_id="cpnetflux_file_download",
                        command=command,
                        stdout_log=client_stdout,
                        stderr_log=client_stderr,
                        cwd=REPO_ROOT,
                        timeout=args.case_timeout,
                    )
                final = control.read_reply()
                if reply_code(final) != 226:
                    raise RuntimeError(f"RETR completion failed: {final!r}")
                max_streams = sampler.max_data_streams
            finally:
                control.close()
            destination_hash = file_sha256(local_dest_file)
            fetch_case_remote_manifests(args, server_root, case_dir)
    except Exception as exc:  # noqa: BLE001
        error = (error + " " + str(exc)).strip()
        max_streams = 0
    finally:
        stop_remote_cpnetflux_server(
            output_dir=output_dir,
            run_id=run_id,
            args=args,
            case=case,
            pid=server_pid,
        )
        if server_remote_log:
            fetch_remote_text(args.remote, server_remote_log, case_dir / "server.log")
        if server_remote_event:
            fetch_remote_text(args.remote, server_remote_event, case_dir / "server_events.jsonl")

    elapsed = time.monotonic() - start
    hash_match = bool(source_hash and destination_hash and source_hash == destination_hash)
    result = classify_transfer_result(exit_code=exit_code, hash_match=hash_match, timed_out=timed_out)
    logical_bytes = dataset_total_bytes(case.dataset)
    return result_row(
        args=args,
        run_id=run_id,
        case=case,
        case_index=case_index,
        logical_bytes=logical_bytes,
        file_count=1,
        elapsed=elapsed,
        max_streams=max_streams,
        source_hash=source_hash,
        dest_hash=destination_hash,
        source_count=source_count,
        dest_count=destination_count,
        exit_code=exit_code,
        result=result,
        case_dir=case_dir,
        error=error,
    )


def run_cpnetflux_tree_case(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    case_index: int,
    case_dir: Path,
    remote_root: str,
) -> dict[str, Any]:
    server_root = f"{remote_root}/server-root"
    remote_dataset_root = f"{server_root}/dataset"
    local_source_root = case_dir / "source_payload"
    local_dest_root = case_dir / "dest_payload"
    server_pid = ""
    server_remote_log = ""
    server_remote_event = ""
    client_stdout = case_dir / "client.stdout.log"
    client_stderr = case_dir / "client.stderr.log"
    start = time.monotonic()
    exit_code = 1
    timed_out = False
    error = ""
    source_meta: dict[str, Any] = {}
    dest_meta: dict[str, Any] = {}
    max_streams = 0
    try:
        prep = run_remote_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id="remote_case_prepare",
            remote=args.remote,
            command=f"rm -rf {shlex.quote(remote_root)} && mkdir -p {shlex.quote(server_root)}",
            timeout=60,
        )
        if prep.returncode != 0:
            raise RuntimeError(prep.stdout + prep.stderr)
        if case.direction == "local_to_remote":
            source_meta = materialize_local_payload(local_source_root, case, args.seed)
        else:
            source_meta = materialize_remote_dataset(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                remote=args.remote,
                root=remote_dataset_root,
                seed=args.seed,
                timeout=args.dataset_timeout,
            )
        server_pid, server_remote_log, server_remote_event = start_remote_cpnetflux_server(
            output_dir=output_dir,
            run_id=run_id,
            args=args,
            case=case,
            case_index=case_index,
            remote_root=remote_root,
        )
        args.active_control_port = control_port(args, case_index)
        if case.direction == "local_to_remote":
            command = build_tree_client_command(
                args=args,
                case=case,
                case_dir=case_dir,
                source_dir=str(local_source_root),
                dest_dir="dataset",
            )
        else:
            local_dest_root.mkdir(parents=True, exist_ok=True)
            command = build_tree_client_command(
                args=args,
                case=case,
                case_dir=case_dir,
                source_dir="dataset",
                dest_dir=str(local_dest_root),
            )
        if case.resume:
            partial = [part for part in command if part != "--resume"]
            partial.extend(["--max-files", str(args.resume_max_files)])
            partial_stdout = case_dir / "client.partial.stdout.log"
            partial_stderr = case_dir / "client.partial.stderr.log"
            run_logged_command(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                command_id="cpnetflux_tree_partial",
                command=partial,
                stdout_log=partial_stdout,
                stderr_log=partial_stderr,
                cwd=REPO_ROOT,
                timeout=args.case_timeout,
            )
        with SocketSampler(
            output_dir / "socket_samples.jsonl",
            case_id=case.case_id,
            control_port=control_port(args, case_index),
            data_port_base=data_port_base(args, case_index),
            interval=args.socket_sample_interval,
        ) as sampler:
            exit_code, timed_out, error = run_logged_command(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                command_id="cpnetflux_tree_transfer",
                command=command,
                stdout_log=client_stdout,
                stderr_log=client_stderr,
                cwd=REPO_ROOT,
                timeout=args.case_timeout,
            )
        max_streams = sampler.max_data_streams
        if case.direction == "local_to_remote":
            dest_meta = remote_tree_hash(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                remote=args.remote,
                root=remote_dataset_root,
                timeout=args.case_timeout,
            )
            fetch_case_remote_manifests(args, remote_dataset_root, case_dir)
        else:
            hash_value, count, total = tree_hash(local_dest_root)
            dest_meta = {"tree_hash": hash_value, "file_count": count, "total_bytes": total}
    except Exception as exc:  # noqa: BLE001
        error = (error + " " + str(exc)).strip()
    finally:
        stop_remote_cpnetflux_server(
            output_dir=output_dir,
            run_id=run_id,
            args=args,
            case=case,
            pid=server_pid,
        )
        if server_remote_log:
            fetch_remote_text(args.remote, server_remote_log, case_dir / "server.log")
        if server_remote_event:
            fetch_remote_text(args.remote, server_remote_event, case_dir / "server_events.jsonl")

    elapsed = time.monotonic() - start
    source_hash = str(source_meta.get("tree_hash", ""))
    dest_hash = str(dest_meta.get("tree_hash", ""))
    hash_match = bool(source_hash and dest_hash and source_hash == dest_hash)
    result = classify_transfer_result(exit_code=exit_code, hash_match=hash_match, timed_out=timed_out)
    return result_row(
        args=args,
        run_id=run_id,
        case=case,
        case_index=case_index,
        logical_bytes=int(source_meta.get("total_bytes", dataset_total_bytes(case.dataset))),
        file_count=int(source_meta.get("file_count", len(dataset_specs(case.dataset)))),
        elapsed=elapsed,
        max_streams=max_streams,
        source_hash=source_hash,
        dest_hash=dest_hash,
        source_count=int(source_meta.get("file_count", 0)),
        dest_count=int(dest_meta.get("file_count", 0)),
        exit_code=exit_code,
        result=result,
        case_dir=case_dir,
        error=error,
    )


def build_gridftp_case_command(
    *,
    args: argparse.Namespace,
    case: ExperimentCase,
    local_source_root: Path,
    local_dest_root: Path,
    remote_dataset_root: str,
    remote_dest_root: str,
) -> list[str]:
    recursive = dataset_kind(case.dataset) == "tree"
    if case.direction == "local_to_remote":
        if recursive:
            source = file_url(local_source_root, directory=True)
            dest = gridftp_url(
                args.control_host,
                args.control_port,
                remote_dest_root,
                directory=True,
                auth_mode=args.gridftp_auth_mode,
                home_dir=getattr(args, "gridftp_home_dir", None),
            )
        else:
            source = file_url(local_source_root / "single.bin")
            dest = gridftp_url(
                args.control_host,
                args.control_port,
                f"{remote_dest_root.rstrip('/')}/single.bin",
                auth_mode=args.gridftp_auth_mode,
                home_dir=getattr(args, "gridftp_home_dir", None),
            )
    else:
        if recursive:
            source = gridftp_url(
                args.control_host,
                args.control_port,
                remote_dataset_root,
                directory=True,
                auth_mode=args.gridftp_auth_mode,
                home_dir=getattr(args, "gridftp_home_dir", None),
            )
            dest = file_url(local_dest_root, directory=True)
        else:
            source = gridftp_url(
                args.control_host,
                args.control_port,
                f"{remote_dataset_root.rstrip('/')}/single.bin",
                auth_mode=args.gridftp_auth_mode,
                home_dir=getattr(args, "gridftp_home_dir", None),
            )
            dest = file_url(local_dest_root / "single.bin")
    return build_globus_transfer_command(
        globus_url_copy=args.globus_url_copy,
        source_url=source,
        dest_url=dest,
        parallelism=(
            None
            if args.gridftp_auth_mode == "gsi" and case.direction == "remote_to_local"
            else case.per_file_connections
        ),
        concurrency=case.file_parallelism,
        recursive=recursive,
        restart=case.resume,
        auth_mode=args.gridftp_auth_mode,
        fast=not (args.gridftp_auth_mode == "gsi" and case.direction == "remote_to_local"),
    )


def run_gridftp_segmented_single_case(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    case_dir: Path,
    remote_dataset_root: str,
    local_dest_root: Path,
    source_meta: dict[str, Any],
    transfer_env: dict[str, str],
) -> tuple[int, bool, str]:
    source_url = gridftp_url(
        args.control_host,
        args.control_port,
        f"{remote_dataset_root.rstrip('/')}/single.bin",
        auth_mode=args.gridftp_auth_mode,
        home_dir=getattr(args, "gridftp_home_dir", None),
    )
    destination = local_dest_root / "single.bin"
    total_bytes = int(source_meta["total_bytes"])
    segment_count = min(case.per_file_connections, total_bytes)
    segments: list[tuple[int, int, int, Path, list[str]]] = []
    for index in range(segment_count):
        offset = total_bytes * index // segment_count
        end = total_bytes * (index + 1) // segment_count
        length = end - offset
        part_path = case_dir / f"single.bin.part.{index:04d}"
        command = build_globus_partial_get_command(
            globus_url_copy=args.globus_url_copy,
            source_url=source_url,
            dest_url=file_url(part_path),
            offset=offset,
            length=length,
            auth_mode=args.gridftp_auth_mode,
        )
        segments.append((index, offset, length, part_path, command))

    def run_segment(segment: tuple[int, int, int, Path, list[str]]) -> tuple[int, bool, str]:
        index, _, _, _, command = segment
        return run_logged_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id=f"gridftp_partial_get_{index}",
            command=command,
            stdout_log=case_dir / f"globus.partial.{index:04d}.stdout.log",
            stderr_log=case_dir / f"globus.partial.{index:04d}.stderr.log",
            cwd=REPO_ROOT,
            timeout=args.case_timeout,
            env=transfer_env,
        )

    with ThreadPoolExecutor(max_workers=segment_count) as executor:
        outcomes = list(executor.map(run_segment, segments))
    stdout_lines = ["segmented_gridftp_get=true", f"segment_count={segment_count}"]
    stdout_lines.extend("$ " + command_text(segment[4]) for segment in segments)
    (case_dir / "globus.stdout.log").write_text("\n".join(stdout_lines) + "\n", encoding="utf-8")
    failed = [outcome for outcome in outcomes if outcome[0] != 0 or outcome[1]]
    if failed:
        exit_code, timed_out, error = failed[0]
        return exit_code, timed_out, error or "one or more GridFTP partial GETs failed"

    local_dest_root.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as output:
        for _, offset, expected_length, part_path, _ in segments:
            if not part_path.is_file() or part_path.stat().st_size < offset + expected_length:
                return 1, False, f"partial GET size mismatch: {part_path}"
            with part_path.open("rb") as part:
                part.seek(offset)
                remaining = expected_length
                while remaining > 0:
                    chunk = part.read(min(1024 * 1024, remaining))
                    if not chunk:
                        return 1, False, f"partial GET ended early: {part_path}"
                    output.write(chunk)
                    remaining -= len(chunk)
            part_path.unlink()
    return 0, False, ""


def run_gridftp_case(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    case_index: int,
    case_dir: Path,
) -> dict[str, Any]:
    remote_root = remote_case_root(args, run_id, case)
    remote_dataset_root = f"{remote_root}/source_payload"
    remote_dest_root = f"{remote_root}/dest_payload"
    local_source_root = case_dir / "source_payload"
    local_dest_root = case_dir / "dest_payload"
    stdout_log = case_dir / "globus.stdout.log"
    stderr_log = case_dir / "globus.stderr.log"
    start = time.monotonic()
    source_meta: dict[str, Any]
    dest_meta: dict[str, Any] = {}
    max_streams = 0
    if case.direction == "local_to_remote":
        source_meta = materialize_local_payload(local_source_root, case, args.seed)
        mkdir = run_remote_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id="remote_gridftp_dest_prepare",
            remote=args.remote,
            command=prepare_gridftp_destination_command(remote_root, remote_dest_root, args.gridftp_auth_mode),
            timeout=60,
        )
        if mkdir.returncode != 0:
            raise RuntimeError(mkdir.stdout + mkdir.stderr)
    else:
        source_meta = materialize_remote_dataset(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            remote=args.remote,
            root=remote_dataset_root,
            seed=args.seed,
            timeout=args.dataset_timeout,
        )
        readable = run_remote_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id="remote_gridftp_source_permissions",
            remote=args.remote,
            command=prepare_gridftp_source_permissions_command(remote_dataset_root, args.gridftp_auth_mode),
            timeout=60,
        )
        if readable.returncode != 0:
            raise RuntimeError(readable.stdout + readable.stderr)
        local_dest_root.mkdir(parents=True, exist_ok=True)
    command = build_gridftp_case_command(
        args=args,
        case=case,
        local_source_root=local_source_root,
        local_dest_root=local_dest_root,
        remote_dataset_root=remote_dataset_root,
        remote_dest_root=remote_dest_root,
    )
    transfer_env = os.environ.copy()
    if args.gridftp_auth_mode == "gsi" and case.direction == "remote_to_local":
        # Shenzhen is behind NAT; let the source endpoint open the data channel.
        transfer_env["GLOBUS_FTP_CLIENT_SOURCE_PASV"] = "1"
    if case.resume:
        partial_command = ["timeout", str(args.resume_interrupt_after_s), *[part for part in command if part != "-rst"]]
        run_logged_command(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            command_id="gridftp_partial_timeout",
            command=partial_command,
            stdout_log=case_dir / "globus.partial.stdout.log",
            stderr_log=case_dir / "globus.partial.stderr.log",
            cwd=REPO_ROOT,
            timeout=args.resume_interrupt_after_s + 30,
            env=transfer_env,
        )
    with SocketSampler(
        output_dir / "socket_samples.jsonl",
        case_id=case.case_id,
        control_port=args.control_port,
        data_port_base=args.data_port_base,
        interval=args.socket_sample_interval,
    ) as sampler:
        segmented_single = (
            args.gridftp_auth_mode == "gsi"
            and case.direction == "remote_to_local"
            and dataset_kind(case.dataset) == "single"
            and case.per_file_connections > 1
            and not case.resume
        )
        if segmented_single:
            exit_code, timed_out, error = run_gridftp_segmented_single_case(
                args=args,
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                case_dir=case_dir,
                remote_dataset_root=remote_dataset_root,
                local_dest_root=local_dest_root,
                source_meta=source_meta,
                transfer_env=transfer_env,
            )
        else:
            exit_code, timed_out, error = run_logged_command(
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                command_id="gridftp_transfer",
                command=command,
                stdout_log=stdout_log,
                stderr_log=stderr_log,
                cwd=REPO_ROOT,
                timeout=args.case_timeout,
                env=transfer_env,
            )
    max_streams = sampler.max_data_streams
    if case.direction == "local_to_remote":
        dest_meta = remote_tree_hash(
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            remote=args.remote,
            root=remote_dest_root,
            timeout=args.case_timeout,
        )
        fetch_case_remote_manifests(args, remote_dest_root, case_dir)
    else:
        hash_value, count, total = tree_hash(local_dest_root)
        dest_meta = {"tree_hash": hash_value, "file_count": count, "total_bytes": total}
    elapsed = time.monotonic() - start
    source_hash = str(source_meta.get("tree_hash", ""))
    dest_hash = str(dest_meta.get("tree_hash", ""))
    hash_match = bool(source_hash and dest_hash and source_hash == dest_hash)
    result = classify_transfer_result(exit_code=exit_code, hash_match=hash_match, timed_out=timed_out)
    return result_row(
        args=args,
        run_id=run_id,
        case=case,
        case_index=case_index,
        logical_bytes=int(source_meta.get("total_bytes", dataset_total_bytes(case.dataset))),
        file_count=int(source_meta.get("file_count", len(dataset_specs(case.dataset)))),
        elapsed=elapsed,
        max_streams=max_streams,
        source_hash=source_hash,
        dest_hash=dest_hash,
        source_count=int(source_meta.get("file_count", 0)),
        dest_count=int(dest_meta.get("file_count", 0)),
        exit_code=exit_code,
        result=result,
        case_dir=case_dir,
        error=error,
    )


def result_row(
    *,
    args: argparse.Namespace,
    run_id: str,
    case: ExperimentCase,
    case_index: int,
    logical_bytes: int,
    file_count: int,
    elapsed: float,
    max_streams: int,
    source_hash: str,
    dest_hash: str,
    source_count: int,
    dest_count: int,
    exit_code: int | str,
    result: str,
    case_dir: Path,
    error: str = "",
) -> dict[str, Any]:
    client_log_path = case_dir / ("client.stdout.log" if case.system == "cpnetflux" else "globus.stdout.log")
    server_log_path = case_dir / "server.log"
    metrics: dict[str, list[int]] = {}
    client_metrics: dict[str, list[int]] = {}
    server_metrics: dict[str, list[int]] = {}
    for path, target in [(client_log_path, client_metrics), (server_log_path, server_metrics)]:
        if not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            for key, value in re.findall(r"\b(wire_bytes|verified_chunks|loaded_verified_chunks|missing_chunks)=(\d+)", line):
                target.setdefault(key, []).append(int(value))
    metrics = client_metrics or server_metrics
    wire_values = metrics.get("wire_bytes", [])
    wire_bytes = str(sum(wire_values)) if wire_values else ""
    summary_path = case_dir / "client_summary.json"
    if summary_path.is_file():
        try:
            summary_wire_bytes = json.loads(summary_path.read_text(encoding="utf-8")).get("wire_bytes")
            if isinstance(summary_wire_bytes, int):
                wire_bytes = str(summary_wire_bytes)
        except (OSError, json.JSONDecodeError):
            pass
    wire_matches = (
        "true"
        if wire_bytes and int(wire_bytes) == logical_bytes
        else "false"
        if wire_bytes
        else "not_applicable"
    )
    manifest_evidence = sorted(
        str(path)
        for path in case_dir.rglob("*.cpnetflux*.manifest")
        if path.is_file()
    )
    tree_manifest_evidence = [path for path in manifest_evidence if ".cpnetflux.tree." in path]
    verified_chunk_count = sum(manifest_verified_chunk_count(Path(path)) for path in manifest_evidence)
    verified_chunks = str(verified_chunk_count) if verified_chunk_count else ""
    evidence = classify_evidence(
        system=case.system,
        dataset_kind=dataset_kind(case.dataset),
        checksum=case.checksum,
        logical_bytes=logical_bytes,
        wire_bytes=wire_bytes,
        manifest_evidence=manifest_evidence,
        tree_manifest_evidence=tree_manifest_evidence,
        verified_chunks=verified_chunks,
    )
    goodput = logical_bytes * 8.0 / elapsed / 1_000_000.0 if elapsed > 0 and result == STATUS_PASS else ""
    integrity_status = "pass" if source_hash and dest_hash and source_hash == dest_hash else "fail" if source_hash or dest_hash else "unknown"
    if evidence["evidence_errors"] and not error:
        error = evidence["evidence_errors"]
    row = {
        "run_id": run_id,
        "case_id": case.case_id,
        "stage": case.stage,
        "system": case.system,
        "tool": "cpnetflux" if case.system == "cpnetflux" else "globus-url-copy",
        "tool_version": "",
        "direction": case.direction,
        "dataset": case.dataset,
        "file_count": str(file_count),
        "logical_bytes": str(logical_bytes),
        "profile": case.dataset,
        "file_parallelism": str(case.file_parallelism),
        "per_file_connections": str(case.per_file_connections),
        "total_stream_budget": str(case.total_stream_budget),
        "max_observed_data_streams": str(max_streams),
        "chunk_or_block_size": str(args.chunk_size),
        "buffer_size": str(args.buffer_size),
        "checksum": case.checksum,
        "compression": case.compression,
        "file_io_backend": case.file_io_backend,
        "queue_depth": str(case.queue_depth),
        "batch_size": str(case.batch_size),
        "control_reuse": case.control_reuse,
        "scheduler": case.scheduler,
        "scheduler_policy": case.scheduler_policy,
        "repeat_index": str(case.repeat_index),
        "elapsed_seconds": f"{elapsed:.6f}" if elapsed > 0 else "",
        "logical_goodput_mbps": f"{goodput:.6f}" if isinstance(goodput, float) else "",
        "wire_goodput_mbps": "",
        "wire_bytes": wire_bytes,
        "wire_bytes_equal_logical": wire_matches,
        "wire_accounting_status": evidence["wire_accounting_status"],
        "verified_chunks": verified_chunks,
        "manifest_evidence": json.dumps(manifest_evidence, ensure_ascii=False),
        "tree_manifest_evidence": json.dumps(tree_manifest_evidence, ensure_ascii=False),
        "evidence_status": evidence["evidence_status"],
        "evidence_errors": evidence["evidence_errors"],
        "source_tree_hash": source_hash,
        "destination_tree_hash": dest_hash,
        "hash_match": "true" if source_hash and dest_hash and source_hash == dest_hash else "false",
        "integrity_status": integrity_status,
        "source_file_count": str(source_count),
        "destination_file_count": str(dest_count),
        "exit_code": str(exit_code),
        "result": result,
        "spread_percent": "",
        "unstable": "",
        "server_log": str(server_log_path) if case.system == "cpnetflux" else "",
        "client_log": str(client_log_path),
        "environment_json": str(args.environment_json),
        "socket_sample_jsonl": str(args.socket_sample_jsonl),
        "command_id": case.case_id,
        "error": error.replace("\n", " ")[:1000],
    }
    return {field: row.get(field, "") for field in RESULT_FIELDS}


def blocked_row(
    *,
    args: argparse.Namespace,
    run_id: str,
    case: ExperimentCase,
    case_index: int,
    case_dir: Path,
    status: str,
    error: str,
) -> dict[str, Any]:
    return result_row(
        args=args,
        run_id=run_id,
        case=case,
        case_index=case_index,
        logical_bytes=dataset_total_bytes(case.dataset),
        file_count=len(dataset_specs(case.dataset)),
        elapsed=0.0,
        max_streams=0,
        source_hash="",
        dest_hash="",
        source_count=0,
        dest_count=0,
        exit_code="",
        result=status,
        case_dir=case_dir,
        error=error,
    )


def disk_budget_error(args: argparse.Namespace, output_dir: Path) -> str:
    minimum_bytes = int(float(args.min_free_gib) * 1024**3)
    local_free = shutil.disk_usage(output_dir).free
    if local_free < minimum_bytes:
        return f"local free space below budget: {local_free / 1024**3:.2f} GiB"
    if not getattr(args, "remote_access_ok", False):
        return ""
    completed = run_remote_capture(args.remote, "df -Pk /tmp | tail -1", timeout=20)
    if completed.returncode != 0:
        return f"remote disk budget probe failed: {completed.stderr.strip()}"
    fields = completed.stdout.split()
    if len(fields) < 4:
        return f"remote disk budget probe returned unexpected output: {completed.stdout.strip()}"
    remote_free = int(fields[3]) * 1024
    if remote_free < minimum_bytes:
        return f"remote free space below budget: {remote_free / 1024**3:.2f} GiB"
    return ""


def cleanup_case_resources(args: argparse.Namespace, run_id: str, case: ExperimentCase, case_dir: Path) -> None:
    if getattr(args, "retain_payloads", False):
        return
    for name in ("source_payload", "dest_payload"):
        shutil.rmtree(case_dir / name, ignore_errors=True)
    if getattr(args, "remote_access_ok", False):
        remote_root = remote_case_root(args, run_id, case)
        run_remote_capture(args.remote, f"rm -rf {shlex.quote(remote_root)}", timeout=60)


def collect_environment(args: argparse.Namespace, output_dir: Path, run_id: str) -> dict[str, Any]:
    def local(command: list[str]) -> str:
        try:
            completed = subprocess.run(command, text=True, capture_output=True, check=False, timeout=10)
        except FileNotFoundError:
            return f"unavailable: {command[0]}"
        return (completed.stdout + completed.stderr).strip()

    environment: dict[str, Any] = {
        "run_id": run_id,
        "generated_at": utc_timestamp(),
        "repo_root": str(REPO_ROOT),
        "local": {
            "hostname": local(["hostname"]),
            "kernel": local(["uname", "-a"]),
            "python": sys.version.split()[0],
            "git_revision": local(["git", "rev-parse", "HEAD"]),
            "git_status_short": local(["git", "status", "--short"]),
            "globus_url_copy": shutil.which(args.globus_url_copy) or "",
        },
        "remote": {
            "remote": args.remote,
            "auth": remote_auth.status(args.remote, REPO_ROOT),
        },
        "config": {
            "stage": args.stage,
            "systems": args.systems,
            "directions": args.directions,
            "control_host": args.control_host,
            "gridftp_control_port": args.control_port,
            "gridftp_data_port_base": args.data_port_base,
            "gridftp_data_port_range": f"{args.data_port_base}..{passive_data_port_window_end(args.data_port_base)}",
            "gridftp_auth_mode": args.gridftp_auth_mode,
            "gridftp_home_dir": getattr(args, "gridftp_home_dir", ""),
            "gridftp_gsi_case_directory_mode": "0777" if args.gridftp_auth_mode == "gsi" else "not_applicable",
            "cpnetflux_control_port_base": args.cpnetflux_control_port_base,
            "cpnetflux_data_port_base": args.cpnetflux_data_port_base,
            "cpnetflux_data_port_range_base": f"{args.cpnetflux_data_port_base}..{passive_data_port_window_end(args.cpnetflux_data_port_base)}",
            "remote_work_root": args.remote_work_root,
            "local_build_dir": args.local_build_dir,
            "remote_build_dir": args.remote_build_dir,
            "min_free_gib": args.min_free_gib,
            "retain_payloads": args.retain_payloads,
        },
    }
    if args.dry_run:
        remote_ok, remote_error = False, "skipped by dry_run"
    else:
        remote_ok, remote_error = probe_remote_access(args.remote)
    environment["remote"]["access_probe"] = {"ok": remote_ok, "error": remote_error}
    args.remote_access_ok = remote_ok
    if remote_ok:
        for key, command in {
            "hostname": "hostname",
            "kernel": "uname -a",
            "cpnetflux_server": f"test -x {shlex.quote(args.remote_build_dir.rstrip('/') + '/cpnetflux-gridftp-server')} && echo present || echo missing",
            "globus_gridftp_server": f"command -v {shlex.quote(args.globus_gridftp_server)} || true",
        }.items():
            completed = run_remote_capture(args.remote, command, timeout=20)
            environment["remote"][key] = (completed.stdout + completed.stderr).strip()
    write_json(output_dir / "environment.json", environment)
    return environment


def prepare_dataset_catalog(args: argparse.Namespace, output_dir: Path, profiles: list[str]) -> dict[str, Any]:
    dataset_root = output_dir / "datasets"
    entries: list[dict[str, Any]] = []
    for profile in profiles:
        total = dataset_total_bytes(profile)
        materialize = (not args.dry_run and args.materialize_catalog) or (
            args.dry_run and total <= args.dry_run_materialize_limit
        )
        manifest = make_dataset(
            dataset_root / profile,
            profile=profile,
            seed=args.seed,
            materialize=materialize,
        )
        entries.append(manifest)
    catalog = {
        "schema_version": 1,
        "generated_at": utc_timestamp(),
        "seed": args.seed,
        "materialize_catalog": args.materialize_catalog,
        "profiles": entries,
    }
    write_json(output_dir / "dataset_manifest.json", catalog)
    return catalog


def existing_completed_case_ids(results_path: Path) -> set[str]:
    if not results_path.is_file():
        return set()
    with results_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    done_statuses = {
        STATUS_PASS,
        STATUS_BLOCKED_EXTERNAL_GRIDFTP,
        STATUS_BLOCKED_REMOTE_AUTH,
        STATUS_BLOCKED_IO_URING,
    }
    return {row["case_id"] for row in rows if row.get("result") in done_statuses}


def command_plan_for_case(args: argparse.Namespace, run_id: str, case: ExperimentCase, case_index: int, case_dir: Path) -> list[str]:
    if case.system == "gridftp":
        remote_root = remote_case_root(args, run_id, case)
        return build_gridftp_case_command(
            args=args,
            case=case,
            local_source_root=case_dir / "source_payload",
            local_dest_root=case_dir / "dest_payload",
            remote_dataset_root=f"{remote_root}/source_payload",
            remote_dest_root=f"{remote_root}/dest_payload",
        )
    if dataset_kind(case.dataset) == "single":
        if case.direction == "local_to_remote":
            return [
                str(Path(args.local_build_dir) / "cpnetflux-file-client"),
                "--host",
                args.control_host,
                "--port",
                "<epsv-data-port>",
                "--input",
                str(case_dir / "source_payload" / "single.bin"),
                "--connections",
                str(case.per_file_connections),
                "--chunk-size",
                str(args.chunk_size),
                "--buffer-size",
                str(args.buffer_size),
                "--checksum",
                case.checksum,
                "--transfer-id",
                "<GFID>",
            ]
        return [
            str(Path(args.local_build_dir) / "cpnetflux-file-download-client"),
            "--host",
            args.control_host,
            "--port",
            "<epsv-data-port>",
            "--output",
            str(case_dir / "dest_payload" / "single.bin"),
            "--connections",
            str(case.per_file_connections),
            "--buffer-size",
            str(args.buffer_size),
            "--checksum",
            case.checksum,
            "--transfer-id",
            "<GFID>",
        ]
    args.active_control_port = control_port(args, case_index)
    return build_tree_client_command(
        args=args,
        case=case,
        case_dir=case_dir,
        source_dir=str(case_dir / "source_payload") if case.direction == "local_to_remote" else "dataset",
        dest_dir="dataset" if case.direction == "local_to_remote" else str(case_dir / "dest_payload"),
    )


def run_case(
    *,
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    case: ExperimentCase,
    case_index: int,
    gridftp_preflight: dict[str, Any],
) -> dict[str, Any]:
    case_dir = output_dir / "cases" / case.case_id
    case_dir.mkdir(parents=True, exist_ok=True)
    if args.dry_run:
        record_dry_run_command(
            output_dir,
            run_id,
            case,
            "planned_transfer",
            command_plan_for_case(args, run_id, case, case_index, case_dir),
        )
        return blocked_row(
            args=args,
            run_id=run_id,
            case=case,
            case_index=case_index,
            case_dir=case_dir,
            status=STATUS_DRY_RUN,
            error="planned only; no transfer executed",
        )
    if not getattr(args, "remote_access_ok", False):
        return blocked_row(
            args=args,
            run_id=run_id,
            case=case,
            case_index=case_index,
            case_dir=case_dir,
            status=STATUS_BLOCKED_REMOTE_AUTH,
            error="remote auth unavailable; run tools/release/remote_auth.py --remote <remote> --status",
        )
    if case.system == "gridftp" and gridftp_preflight.get("status") != STATUS_PASS:
        return blocked_row(
            args=args,
            run_id=run_id,
            case=case,
            case_index=case_index,
            case_dir=case_dir,
            status=STATUS_BLOCKED_EXTERNAL_GRIDFTP,
            error=str(gridftp_preflight.get("error", "external GridFTP preflight did not pass")),
        )
    if case.file_io_backend == "io_uring" and not args.allow_io_uring_stub and not io_uring_available(args):
        return blocked_row(
            args=args,
            run_id=run_id,
            case=case,
            case_index=case_index,
            case_dir=case_dir,
            status=STATUS_BLOCKED_IO_URING,
            error="real io_uring backend not detected by ldd; use --allow-io-uring-stub only for fallback observation",
        )
    if case.system == "gridftp":
        return run_gridftp_case(
            args=args,
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            case_index=case_index,
            case_dir=case_dir,
        )
    remote_root = remote_case_root(args, run_id, case)
    if dataset_kind(case.dataset) == "single":
        return run_cpnetflux_file_case(
            args=args,
            output_dir=output_dir,
            run_id=run_id,
            case=case,
            case_index=case_index,
            case_dir=case_dir,
            remote_root=remote_root,
        )
    return run_cpnetflux_tree_case(
        args=args,
        output_dir=output_dir,
        run_id=run_id,
        case=case,
        case_index=case_index,
        case_dir=case_dir,
        remote_root=remote_root,
    )


def io_uring_available(args: argparse.Namespace) -> bool:
    local_bins = [Path(args.local_build_dir) / "cpnetflux-file-client", Path(args.local_build_dir) / "cpnetflux-file-download-client"]
    local_ok = True
    for binary in local_bins:
        if not binary.exists():
            return False
        completed = subprocess.run(["ldd", str(binary)], text=True, capture_output=True, check=False, timeout=10)
        local_ok = local_ok and "liburing" in (completed.stdout + completed.stderr)
    if not getattr(args, "remote_access_ok", False):
        return False
    remote_bin = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-gridftp-server"
    completed = run_remote_capture(args.remote, f"ldd {shlex.quote(remote_bin)} 2>&1 | grep -i liburing", timeout=20)
    return local_ok and completed.returncode == 0


def write_final_summary(output_dir: Path, rows: list[dict[str, Any]], preflight: dict[str, Any]) -> None:
    pass_count = sum(1 for row in rows if row.get("result") == STATUS_PASS)
    blocked_count = sum(1 for row in rows if str(row.get("result", "")).startswith("blocked"))
    fail_count = sum(1 for row in rows if str(row.get("result", "")).startswith("fail"))
    text = f"""# GridFTP 对比实验手动运行汇总

- 生成时间：{utc_timestamp()}
- GridFTP preflight：{preflight.get("status", "")}
- 已记录 case：{len(rows)}
- pass：{pass_count}
- blocked：{blocked_count}
- fail：{fail_count}
- 结果目录：{output_dir}

说明：

- 本脚本只记录真实执行或明确 blocked 的结果；不会用 scp、rsync 或 SSH tunnel 替代 GridFTP。
- raw 对比固定为 scheduler=off、compression=off、checksum=none、POSIX 默认路径。
- scheduler 事件和 sidecar 只是运行时观测；最终完成事实仍以独立 hash/tree hash 与进程退出码为准。
- 公网 100M 实验只能支持本次链路下的 correctness、resume、backpressure、指标完整性和稳定性结论，不能外推为 100G readiness。
"""
    (output_dir / "final_summary_zh.md").write_text(text, encoding="utf-8")


def load_or_create_run_id(output_dir: Path, args: argparse.Namespace) -> str:
    state_path = output_dir / "experiment_state.json"
    if args.resume_run and state_path.is_file():
        data = json.loads(state_path.read_text(encoding="utf-8"))
        return str(data["run_id"])
    run_id = compact_timestamp()
    write_json(
        state_path,
        {
            "run_id": run_id,
            "created_at": utc_timestamp(),
            "stage": args.stage,
            "resume_run": bool(args.resume_run),
        },
    )
    return run_id


def run_experiment(args: argparse.Namespace) -> dict[str, Any]:
    configure_ssh_password_file(args)
    output_dir = output_dir_from_args(args)
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = load_or_create_run_id(output_dir, args)
    args.run_id = run_id
    if not args.gridftp_home_dir:
        if args.gridftp_auth_mode == "gsi":
            args.gridftp_home_dir = DEFAULT_GRIDFTP_GSI_HOME_DIR
        else:
            args.gridftp_home_dir = f"{args.remote_work_root.rstrip('/')}/{run_id}/server-root"
    args.environment_json = output_dir / "environment.json"
    args.socket_sample_jsonl = output_dir / "socket_samples.jsonl"

    systems = parse_csv_list(args.systems)
    directions = parse_csv_list(args.directions)
    cases = build_cases(
        stage=args.stage,
        systems=systems,
        directions=directions,
        repeat=args.repeat,
        scheduler_repeat=args.scheduler_repeat,
        io_repeat=args.io_repeat,
        control_reuse=args.control_reuse,
        max_cases=args.max_cases,
    )
    validate_case_ports(args, cases)
    environment = collect_environment(args, output_dir, run_id)
    link_baseline = run_link_baseline(args, output_dir, run_id)
    if not args.dry_run and not args.remote_access_ok:
        gridftp_preflight = {
            "status": STATUS_BLOCKED_REMOTE_AUTH,
            "smoke_hash_match": False,
            "error": "remote auth unavailable; formal matrix was not started",
        }
        write_json(output_dir / "preflight.json", gridftp_preflight)
        write_json(
            output_dir / "case_plan.json",
            {
                "run_id": run_id,
                "stage": args.stage,
                "dry_run": False,
                "case_count": len(cases),
                "cases": [case.to_dict() for case in cases],
                "experiment_status": STATUS_BLOCKED_REMOTE_AUTH,
            },
        )
        write_csv(output_dir / "results.csv", RESULT_FIELDS, [])
        write_csv(output_dir / "summary.csv", SUMMARY_FIELDS, [])
        write_final_summary(output_dir, [], gridftp_preflight)
        return {
            "experiment_status": STATUS_BLOCKED_REMOTE_AUTH,
            "run_id": run_id,
            "output_dir": str(output_dir),
            "environment": environment,
            "link_baseline": link_baseline,
            "preflight": gridftp_preflight,
            "case_count": len(cases),
            "recorded_rows": 0,
            "results_csv": str(output_dir / "results.csv"),
            "summary_csv": str(output_dir / "summary.csv"),
            "final_summary_zh": str(output_dir / "final_summary_zh.md"),
        }
    gridftp_service = ensure_remote_gridftp_server(
        args=args,
        output_dir=output_dir,
        run_id=run_id,
        systems=systems,
    )
    profiles = parse_csv_list(args.dataset_profile) if args.dataset_profile else stage_profiles(args.stage)
    prepare_dataset_catalog(args, output_dir, profiles)
    preflight_needs_smoke = (
        args.run_gridftp_smoke
        or (not args.dry_run and "gridftp" in systems and args.stage in {"smoke", "core", "resume", "all"})
    )
    gridftp_preflight = run_gridftp_preflight(
        output_dir=output_dir,
        control_host=args.control_host,
        control_port=args.control_port,
        data_port_base=args.data_port_base,
        remote=args.remote,
        auth_mode=args.gridftp_auth_mode,
        run_smoke=preflight_needs_smoke,
        dry_run=args.dry_run,
        globus_url_copy=args.globus_url_copy,
        globus_gridftp_server=args.globus_gridftp_server,
        remote_path_prefix=args.gridftp_home_dir,
        gridftp_home_dir=args.gridftp_home_dir,
        server_check=gridftp_service,
        timeout=args.case_timeout,
    )
    plan = {
        "run_id": run_id,
        "stage": args.stage,
        "dry_run": args.dry_run,
        "case_count": len(cases),
        "cases": [case.to_dict() for case in cases],
    }
    write_json(output_dir / "case_plan.json", plan)

    results_path = output_dir / "results.csv"
    previous_rows: list[dict[str, Any]] = []
    if args.resume_run and results_path.is_file():
        with results_path.open(newline="", encoding="utf-8") as handle:
            previous_rows = list(csv.DictReader(handle))
    completed_case_ids = existing_completed_case_ids(results_path) if args.resume_run else set()
    rows = previous_rows
    for index, case in enumerate(cases):
        if case.case_id in completed_case_ids:
            continue
        budget_error = disk_budget_error(args, output_dir)
        if budget_error:
            row = blocked_row(
                args=args,
                run_id=run_id,
                case=case,
                case_index=index,
                case_dir=output_dir / "cases" / case.case_id,
                status=STATUS_BLOCKED_RESOURCE,
                error=budget_error,
            )
        else:
            row = run_case(
                args=args,
                output_dir=output_dir,
                run_id=run_id,
                case=case,
                case_index=index,
                gridftp_preflight=gridftp_preflight,
            )
        rows.append(row)
        cleanup_case_resources(args, run_id, case, output_dir / "cases" / case.case_id)
        write_csv(results_path, RESULT_FIELDS, rows)
        write_csv(output_dir / "summary.csv", SUMMARY_FIELDS, summarize_rows(rows))
        write_final_summary(output_dir, rows, gridftp_preflight)

    write_csv(results_path, RESULT_FIELDS, rows)
    summary_rows = summarize_rows(rows)
    write_csv(output_dir / "summary.csv", SUMMARY_FIELDS, summary_rows)
    write_final_summary(output_dir, rows, gridftp_preflight)
    if gridftp_service.get("started"):
        stop_managed_gridftp_server(
            args=args,
            output_dir=output_dir,
            run_id=run_id,
            paths=gridftp_service["paths"],
        )
    return {
        "run_id": run_id,
        "output_dir": str(output_dir),
        "environment": environment,
        "link_baseline": link_baseline,
        "preflight": gridftp_preflight,
        "case_count": len(cases),
        "recorded_rows": len(rows),
        "results_csv": str(results_path),
        "summary_csv": str(output_dir / "summary.csv"),
        "final_summary_zh": str(output_dir / "final_summary_zh.md"),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the CPNetFlux vs real GridFTP comparison experiment harness.")
    parser.add_argument("--stage", choices=sorted(STAGES), required=True)
    parser.add_argument("--dry-run", action="store_true", help="write plan/audit files without executing transfer cases")
    parser.add_argument("--resume-run", default="", help="resume an existing result directory")
    parser.add_argument("--output-dir", default="")
    parser.add_argument("--remote", default=DEFAULT_REMOTE)
    parser.add_argument("--ssh-password-file", default="", help="read SSH password at runtime; file must be chmod 600")
    parser.add_argument("--control-host", default=DEFAULT_CONTROL_HOST)
    parser.add_argument("--control-port", type=int, default=2811, help="external GridFTP control port")
    parser.add_argument("--data-port-base", type=int, default=32000, help="external GridFTP passive window base")
    parser.add_argument("--cpnetflux-control-port-base", type=int, default=21210)
    parser.add_argument("--cpnetflux-data-port-base", type=int, default=33000)
    parser.add_argument("--port-stride", type=int, default=1)
    parser.add_argument("--remote-work-root", default="/tmp/cpnetflux-gridftp-compare")
    parser.add_argument("--local-build-dir", default=str(REPO_ROOT / "build"))
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux-Beta/build")
    parser.add_argument("--systems", default="cpnetflux,gridftp", help="comma list: cpnetflux,gridftp")
    parser.add_argument("--directions", default="local_to_remote,remote_to_local")
    parser.add_argument("--dataset-profile", default="", help="comma list for catalog generation; cases still follow the selected stage matrix")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--scheduler-repeat", type=int, default=5)
    parser.add_argument("--io-repeat", type=int, default=3)
    parser.add_argument("--max-cases", type=int, default=0)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--chunk-size", type=int, default=DEFAULT_CHUNK_SIZE)
    parser.add_argument("--buffer-size", type=int, default=DEFAULT_BUFFER_SIZE)
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--manifest-flush-interval-chunks", type=int, default=16)
    parser.add_argument("--commit-sync-policy", choices=["none", "fsync_file", "fsync_file_and_dir"], default="none")
    parser.add_argument("--final-verify-policy", choices=["full", "verified_chunks"], default="full")
    parser.add_argument("--preallocate", choices=["off", "full"], default="off")
    parser.add_argument("--file-io-buffer-size", type=int, default=0)
    parser.add_argument("--file-io-advice", default="off")
    parser.add_argument("--posix-write-strategy", default="auto")
    parser.add_argument("--control-reuse", choices=["off", "worker"], default="off")
    parser.add_argument("--auth-mode", choices=["anonymous", "token"], default="anonymous")
    parser.add_argument(
        "--gridftp-auth-mode",
        choices=["anonymous", "gsi"],
        default="anonymous",
        help="authentication mode used by the external GridFTP baseline",
    )
    parser.add_argument(
        "--gridftp-home-dir",
        default="",
        help="anonymous GridFTP home directory; defaults to a run-scoped remote path",
    )
    parser.add_argument("--globus-url-copy", default="globus-url-copy")
    parser.add_argument("--globus-gridftp-server", default="globus-gridftp-server")
    parser.add_argument("--run-gridftp-smoke", action="store_true")
    parser.add_argument("--allow-io-uring-stub", action="store_true")
    parser.add_argument("--materialize-catalog", action="store_true")
    parser.add_argument("--dry-run-materialize-limit", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--server-start-timeout", type=float, default=20.0)
    parser.add_argument("--iperf-port", type=int, default=28120)
    parser.add_argument("--iperf-duration", type=int, default=15)
    parser.add_argument("--dataset-timeout", type=int, default=1800)
    parser.add_argument("--case-timeout", type=int, default=3600)
    parser.add_argument("--min-free-gib", type=float, default=10.0, help="minimum free space required on local and remote /tmp before each case")
    parser.add_argument("--retain-payloads", action="store_true", help="retain source/destination payloads for forensic inspection")
    parser.add_argument("--socket-sample-interval", type=float, default=0.5)
    parser.add_argument("--resume-max-files", type=int, default=1)
    parser.add_argument("--resume-interrupt-after-s", type=int, default=10)
    parser.add_argument("--scheduler-link-id", default="link0")
    parser.add_argument("--scheduler-capacity-gbps", type=float, default=0.1)
    parser.add_argument("--scheduler-workitem-min-bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--scheduler-workitem-max-bytes", type=int, default=256 * 1024 * 1024)
    parser.add_argument("--scheduler-default-rtt-ms", type=int, default=10)
    parser.add_argument("--scheduler-min-compress-gbps", type=float, default=1.0)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.repeat <= 0 or args.scheduler_repeat <= 0 or args.io_repeat <= 0:
        parser.error("repeat counts must be greater than zero")
    for system in parse_csv_list(args.systems):
        if system not in {"cpnetflux", "gridftp"}:
            parser.error("--systems must contain only cpnetflux and/or gridftp")
    for direction in parse_csv_list(args.directions):
        if direction not in {"local_to_remote", "remote_to_local"}:
            parser.error("--directions must contain only local_to_remote and/or remote_to_local")
    if args.data_port_base < 1 or args.data_port_base > MAX_PASSIVE_DATA_PORT_BASE:
        parser.error(f"--data-port-base must be in range 1..{MAX_PASSIVE_DATA_PORT_BASE}")
    if args.cpnetflux_data_port_base < 1 or args.cpnetflux_data_port_base > MAX_PASSIVE_DATA_PORT_BASE:
        parser.error(f"--cpnetflux-data-port-base must be in range 1..{MAX_PASSIVE_DATA_PORT_BASE}")
    try:
        result = run_experiment(args)
    except KeyboardInterrupt:
        print("interrupted; partial results remain in the output directory", file=sys.stderr)
        return 130
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0
