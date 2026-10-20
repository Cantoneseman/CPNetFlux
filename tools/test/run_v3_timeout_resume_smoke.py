#!/usr/bin/env python3
"""Exercise V3 transfer deadline behavior when a data relay stalls, then resume."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control

CHUNK_SIZE = 1024 * 1024
TEST_TOKEN = "v3-test-only"
TRANSFER_TIMEOUT_MS = 5000
TIMEOUT_TOLERANCE_SECONDS = 2.0
PAUSE_AFTER_BYTES = 32 * CHUNK_SIZE
REMOTE_SERVER_PUBLIC = "47.116.174.181"
REMOTE_SERVER_BIND = "192.168.10.138"
REMOTE_PORT = 24600
REMOTE_DATA_BASE = 24610


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def create_payload(path: Path, size_mib: int) -> None:
    block = bytes(((index * 131 + 17) % 256) for index in range(CHUNK_SIZE))
    with path.open("wb") as handle:
        for _ in range(size_mib):
            handle.write(block)


def parse_manifest(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def manifest_chunks(fields: dict[str, str]) -> list[str]:
    return [item for item in fields.get("verified_chunks", "").split(",") if item]


def one_file_tree_hash(name: str, size: int, file_sha256: str) -> str:
    digest = hashlib.sha256()
    digest.update(name.encode("utf-8") + b"\0")
    digest.update(str(size).encode("ascii") + b"\0")
    digest.update(file_sha256.encode("ascii") + b"\0")
    return digest.hexdigest()


def client_command(build_dir: Path, direction: str, host: str, control_port: int,
                   source: str, destination: str, summary: Path, resume: bool) -> list[str]:
    command = [
        str(build_dir / f"cpnetflux-tree-{direction}-client"),
        "--host", host, "--port", str(control_port),
        "--source-dir", source, "--dest-dir", destination,
        "--connections", "1", "--file-parallelism", "1",
        "--chunk-size", str(CHUNK_SIZE), "--buffer-size", "65536",
        "--checksum", "crc32c", "--compression", "off",
        "--data-session-reuse", "tree", "--file-scheduling", "dynamic",
        "--control-reuse", "worker",
        "--transfer-timeout-ms", str(TRANSFER_TIMEOUT_MS),
        "--phase-timing", "on", "--json-summary", str(summary),
    ]
    if resume:
        command.append("--resume")
    return command


def remote_run(alias: str, argv: list[str], timeout: int = 40) -> subprocess.CompletedProcess[str]:
    from run_cpnetflux_lane_recovery_interop import ssh_run
    return ssh_run(alias, argv, timeout=timeout)


def remote_start(alias: str, argv: list[str], log: str) -> int:
    from run_cpnetflux_lane_recovery_interop import remote_start as start
    return start(alias, argv, log)


def copy_to_remote(alias: str, source: Path, destination: str) -> None:
    from run_cpnetflux_lane_recovery_interop import scp_to
    scp_to(alias, source, destination)


def wait_for_remote_loopback(alias: str, port: int) -> None:
    probe = """import socket, sys, time
port = int(sys.argv[1])
end = time.monotonic() + 10
while time.monotonic() < end:
    try:
        connection = socket.create_connection(("127.0.0.1", port), timeout=1)
        connection.close()
        raise SystemExit(0)
    except OSError:
        time.sleep(0.05)
raise SystemExit(1)
"""
    result = remote_run(alias, ["python3", "-c", probe, str(port)])
    if result.returncode:
        raise RuntimeError(f"remote temporary server port {port} did not start: {result.stderr}")


def wait_for_remote_tcp(host: str, port: int, timeout: float = 15.0) -> None:
    from run_cpnetflux_lane_recovery_interop import wait_for_tcp
    wait_for_tcp(host, port, timeout=timeout)


class TestEndpoint:
    def __init__(self, build_dir: Path, run_dir: Path, remote_alias: str | None,
                 remote_build_dir: Path | None, remote_root: Path | None):
        self.build_dir = build_dir
        self.run_dir = run_dir
        self.remote_alias = remote_alias
        self.remote_build_dir = remote_build_dir
        self.remote_root = remote_root
        self.local_server_root = run_dir / "server-root"
        self.remote_server_root = remote_root / "server-root" if remote_root else None
        self.remote_bin = remote_root / "bin" if remote_root else None
        self.control_port = REMOTE_PORT if remote_alias else free_port()
        self.data_port = (REMOTE_DATA_BASE if remote_alias
                          else clamp_passive_data_port_base(free_port()))
        self.host = REMOTE_SERVER_PUBLIC if remote_alias else "127.0.0.2"
        self.proxy_script = Path(__file__).with_name("cpnetflux_fault_proxy.py")
        self.proxy_log = run_dir / "proxy.jsonl"
        self.pause_file = run_dir / "pause.gate"
        self.local_server_log = run_dir / "server.log"
        self.proxy_process: subprocess.Popen | None = None
        self.proxy_output = None
        self.server_process: subprocess.Popen | None = None
        self.remote_proxy_pid: int | None = None
        self.remote_server_pid: int | None = None
        self.proxy_generation = 0
        self.proxy_events_path = (remote_root / "proxy.jsonl" if remote_root else self.proxy_log)
        self.remote_pause_file = remote_root / "pause.gate" if remote_root else None

    def prepare(self) -> None:
        if self.remote_alias:
            if not self.remote_build_dir or not self.remote_root:
                raise ValueError("remote mode requires --remote-build-dir and --remote-root")
            safe_prefix = "/tmp/cpnetflux-runs/V3-RELEASE-STABILITY-01/slice-c-remote/"
            if not str(self.remote_root).startswith(safe_prefix):
                raise ValueError(f"unsafe remote test root: {self.remote_root}")
            absent = remote_run(self.remote_alias, ["test", "!", "-e", str(self.remote_root)])
            if absent.returncode:
                raise RuntimeError(f"remote task path already exists: {self.remote_root}")
            made = remote_run(self.remote_alias, [
                "mkdir", "-p", str(self.remote_server_root), str(self.remote_bin)
            ])
            if made.returncode:
                raise RuntimeError(f"cannot create isolated remote test paths: {made.stderr}")
            copy_to_remote(
                self.remote_alias, self.proxy_script,
                str(self.remote_bin / "cpnetflux_fault_proxy.py"),
            )
            self.proxy_events_path = self.remote_root / "proxy.jsonl"
            self.remote_pause_file = self.remote_root / "pause.gate"
        else:
            self.local_server_root.mkdir(parents=True, exist_ok=False)

    def start_server(self) -> None:
        if not self.remote_alias:
            command = [
                str(self.build_dir / "cpnetflux-gridftp-server"),
                "--host", "127.0.0.1", "--port", str(self.control_port),
                "--root", str(self.local_server_root),
                "--data-port-base", str(self.data_port),
                "--connections", "2", "--chunk-size", str(CHUNK_SIZE),
                "--buffer-size", "65536", "--checksum", "crc32c",
                "--checksum-backend", "auto",
            ]
            env = os.environ.copy()
            env["CPNETFLUX_TEST_TOKEN"] = TEST_TOKEN
            with self.local_server_log.open("w", encoding="utf-8") as output:
                self.server_process = subprocess.Popen(
                    command, stdout=output, stderr=subprocess.STDOUT, env=env
                )
            wait_for_control(self.control_port)
            return

        command = [
            "env", f"CPNETFLUX_TEST_TOKEN={TEST_TOKEN}",
            str(self.remote_build_dir / "cpnetflux-gridftp-server"),
            "--host", "127.0.0.1", "--port", str(self.control_port),
            "--root", str(self.remote_server_root),
            "--data-port-base", str(self.data_port),
            "--connections", "2", "--chunk-size", str(CHUNK_SIZE),
            "--buffer-size", "65536", "--checksum", "crc32c",
            "--checksum-backend", "auto",
        ]
        self.remote_server_pid = remote_start(
            self.remote_alias, command, str(self.remote_root / "server.log")
        )
        wait_for_remote_loopback(self.remote_alias, self.control_port)

    def start_proxy(self) -> None:
        self.proxy_generation += 1
        self.proxy_log.touch(exist_ok=True)
        self.pause_file.unlink(missing_ok=True)
        if not self.remote_alias:
            command = [
                sys.executable, str(self.proxy_script),
                "--listen-host", "127.0.0.2", "--backend-host", "127.0.0.1",
                "--control-port", str(self.control_port),
                "--data-base", str(self.data_port), "--count", "8",
                "--delay", "0", "--drops", "0",
                "--pause-file", str(self.pause_file),
                "--pause-after-bytes", str(PAUSE_AFTER_BYTES),
                "--log", str(self.proxy_log),
            ]
            self.proxy_output = self.proxy_log.open("a", encoding="utf-8")
            self.proxy_process = subprocess.Popen(
                command, stdout=self.proxy_output, stderr=subprocess.STDOUT
            )
            time.sleep(0.25)
            if self.proxy_process.poll() is not None:
                raise RuntimeError(f"local proxy exited: {self.proxy_log.read_text(errors='replace')}")
            return

        self.remote_pause_file = self.remote_root / "pause.gate"
        log = self.remote_root / f"proxy-generation-{self.proxy_generation}.log"
        command = [
            "python3", str(self.remote_bin / "cpnetflux_fault_proxy.py"),
            "--listen-host", REMOTE_SERVER_BIND, "--backend-host", "127.0.0.1",
            "--control-port", str(self.control_port),
            "--data-base", str(self.data_port), "--count", "8",
            "--delay", "0", "--drops", "0",
            "--pause-file", str(self.remote_pause_file),
            "--pause-after-bytes", str(PAUSE_AFTER_BYTES),
            "--log", str(self.proxy_events_path),
        ]
        self.remote_proxy_pid = remote_start(self.remote_alias, command, str(log))
        wait_for_remote_tcp(REMOTE_SERVER_PUBLIC, self.control_port)

    def _stop_local_proxy(self) -> None:
        if self.proxy_process and self.proxy_process.poll() is None:
            self.proxy_process.send_signal(signal.SIGTERM)
            self.proxy_process.wait(timeout=5)
        self.proxy_process = None
        if self.proxy_output:
            self.proxy_output.close()
            self.proxy_output = None

    def _stop_remote_pid(self, pid: int, expected: list[str]) -> None:
        script = """import os, pathlib, signal, sys, time
pid = int(sys.argv[1])
expected = sys.argv[2:]
cmdline = pathlib.Path("/proc") / str(pid) / "cmdline"
data = cmdline.read_bytes().replace(bytes([0]), b" ").decode(errors="replace")
assert all(item in data for item in expected), data
os.kill(pid, signal.SIGTERM)
deadline = time.monotonic() + 10
while time.monotonic() < deadline:
    try:
        stat = (pathlib.Path("/proc") / str(pid) / "stat").read_text().split()
        if len(stat) > 2 and stat[2] == "Z":
            raise SystemExit(0)
    except FileNotFoundError:
        raise SystemExit(0)
    time.sleep(0.1)
raise SystemExit(1)
"""
        result = remote_run(self.remote_alias, ["python3", "-c", script, str(pid), *expected])
        if result.returncode:
            raise RuntimeError(f"safe stop failed for remote PID {pid}: {result.stderr}")

    def stop_proxy(self) -> None:
        if self.remote_alias:
            if self.remote_proxy_pid is not None:
                self._stop_remote_pid(
                    self.remote_proxy_pid, [str(self.remote_bin / "cpnetflux_fault_proxy.py")]
                )
                self.remote_proxy_pid = None
        else:
            self._stop_local_proxy()

    def stop(self) -> None:
        try:
            self.stop_proxy()
        finally:
            if self.remote_alias:
                if self.remote_server_pid is not None:
                    self._stop_remote_pid(
                        self.remote_server_pid,
                        [str(self.remote_build_dir / "cpnetflux-gridftp-server"),
                         str(self.remote_server_root)],
                    )
                    self.remote_server_pid = None
            elif self.server_process is not None:
                stop_server(self.server_process, self.local_server_log)
                self.server_process = None

    def restart_proxy(self) -> None:
        if self.remote_alias:
            self.stop_proxy()
            clear = remote_run(self.remote_alias, ["rm", "-f", str(self.proxy_events_path)])
            if clear.returncode:
                raise RuntimeError(f"cannot reset remote proxy log: {clear.stderr}")
        else:
            self._stop_local_proxy()
            self.proxy_log.write_text("", encoding="utf-8")
        self.start_proxy()

    def manifest(self, path: Path, remote_path: Path | None = None) -> dict[str, str]:
        if remote_path is None:
            return parse_manifest(path.read_text(encoding="utf-8")) if path.is_file() else {}
        result = remote_run(self.remote_alias, ["cat", str(remote_path)])
        return parse_manifest(result.stdout) if result.returncode == 0 else {}

    def events(self) -> list[dict]:
        if self.remote_alias:
            result = remote_run(self.remote_alias, ["cat", str(self.proxy_events_path)])
            source = result.stdout if result.returncode == 0 else ""
        else:
            source = self.proxy_log.read_text(encoding="utf-8", errors="replace")
        rows = []
        for line in source.splitlines():
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
        return rows

    def remove_pause(self) -> None:
        if self.remote_alias:
            removed = remote_run(self.remote_alias, ["rm", "-f", str(self.remote_pause_file)])
            if removed.returncode:
                raise RuntimeError(f"cannot remove remote pause gate: {removed.stderr}")
        else:
            self.pause_file.unlink(missing_ok=True)

    def remote_sha256(self, path: Path) -> str:
        result = remote_run(self.remote_alias, ["sha256sum", str(path)])
        if result.returncode:
            raise RuntimeError(f"remote checksum failed for {path}: {result.stderr}")
        return result.stdout.split()[0]

    def remote_size(self, path: Path) -> int:
        result = remote_run(self.remote_alias, ["stat", "-c", "%s", str(path)])
        if result.returncode:
            raise RuntimeError(f"remote stat failed for {path}: {result.stderr}")
        return int(result.stdout.strip())

    def cleanup_payload(self) -> None:
        if not self.remote_alias:
            return
        script = (
            "import pathlib,sys; root=pathlib.Path(sys.argv[1]).resolve(); "
            "path=pathlib.Path(sys.argv[2]).resolve(); path.relative_to(root); "
            "path.unlink(missing_ok=True)"
        )
        result = remote_run(
            self.remote_alias, ["python3", "-c", script, str(self.remote_server_root),
                                str(self.remote_server_root / "uploaded" / "payload.bin")]
        )
        if result.returncode:
            raise RuntimeError(f"cannot remove remote test payload: {result.stderr}")


def wait_for_stall(endpoint: TestEndpoint, process: subprocess.Popen,
                   local_checkpoint: Path, remote_checkpoint: Path | None,
                   label: str, log_path: Path, chunk_total: int) -> tuple[dict[str, str], int]:
    deadline = time.monotonic() + 90
    last_fields: dict[str, str] = {}
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"{label}: client exited before stall exit={process.returncode}; "
                f"log={log_path.read_text(errors='replace')}"
            )
        last_fields = endpoint.manifest(local_checkpoint, remote_checkpoint)
        stall_count = sum(event.get("event") == "data_stalled" for event in endpoint.events())
        count = len(manifest_chunks(last_fields))
        if stall_count and last_fields.get("state") != "committed" and 0 < count < chunk_total:
            return last_fields, count
        time.sleep(0.08)
    raise RuntimeError(
        f"{label}: no proxy stall plus non-empty checkpoint; checkpoint={last_fields}; "
        f"events={endpoint.events()[-8:]}"
    )


def transfer(endpoint: TestEndpoint, build_dir: Path, run_dir: Path, source_file: Path,
             direction: str, file_size: int) -> dict:
    total_chunks = math.ceil(file_size / CHUNK_SIZE)
    if direction == "upload":
        source_arg, destination_arg = str(source_file.parent), "uploaded"
        local_checkpoint = run_dir / "no-local-upload-checkpoint"
        remote_checkpoint = (endpoint.remote_server_root / "uploaded" /
                             "payload.bin.cpnetflux.manifest") if endpoint.remote_alias else None
        if not endpoint.remote_alias:
            local_checkpoint = endpoint.local_server_root / "uploaded" / "payload.bin.cpnetflux.manifest"
    else:
        download_root = run_dir / "download"
        download_root.mkdir(exist_ok=True)
        source_arg, destination_arg = "uploaded", str(download_root)
        local_checkpoint = download_root / "payload.bin.cpnetflux.download.manifest"
        remote_checkpoint = None

    first_log = run_dir / f"{direction}-timeout.log"
    first_summary = run_dir / f"{direction}-timeout.json"
    first_command = client_command(
        build_dir, direction, endpoint.host, endpoint.control_port,
        source_arg, destination_arg, first_summary, False,
    )
    with first_log.open("w", encoding="utf-8") as output:
        client = subprocess.Popen(first_command, stdout=output, stderr=subprocess.STDOUT)
        stalled_fields, stalled_count = wait_for_stall(
            endpoint, client, local_checkpoint, remote_checkpoint,
            direction, first_log, total_chunks,
        )
        observed_stall_at = time.monotonic()
        try:
            first_exit = client.wait(timeout=20)
        except subprocess.TimeoutExpired:
            client.kill()
            client.wait(timeout=5)
            raise RuntimeError(f"{direction}: client exceeded 20 seconds after observed stall")
    timeout_seconds = time.monotonic() - observed_stall_at
    if first_exit == 0:
        raise RuntimeError(f"{direction}: client succeeded while data forwarding was stalled")
    first_log_text = first_log.read_text(encoding="utf-8", errors="replace")
    if "transfer total deadline exceeded" not in first_log_text:
        raise RuntimeError(f"{direction}: explicit transfer timeout missing: {first_log_text}")
    failure_summary = json.loads(first_summary.read_text(encoding="utf-8"))
    for key, expected in (("result", "fail"), ("actual_transport", "v3"),
                          ("actual_mode", "persistent_tree"), ("fallback_reason", "")):
        if failure_summary.get(key) != expected:
            raise RuntimeError(f"{direction}: timeout summary {key} mismatch: {failure_summary}")

    after_timeout = endpoint.manifest(local_checkpoint, remote_checkpoint)
    after_timeout_count = len(manifest_chunks(after_timeout))
    if (after_timeout_count < stalled_count or after_timeout_count <= 0
            or after_timeout.get("state") == "committed"):
        raise RuntimeError(
            f"{direction}: verified checkpoint was lost/committed during timeout; "
            f"before={stalled_fields}, after={after_timeout}"
        )
    maximum_timeout = TRANSFER_TIMEOUT_MS / 1000 + TIMEOUT_TOLERANCE_SECONDS
    if not 0 < timeout_seconds <= maximum_timeout:
        raise RuntimeError(
            f"{direction}: timeout took {timeout_seconds:.3f}s after observed stall "
            f"(limit {maximum_timeout:.3f}s)"
        )

    endpoint.remove_pause()
    time.sleep(0.15)
    resume_summary = run_dir / f"{direction}-resume.json"
    resume_log = run_dir / f"{direction}-resume.log"
    resume_command = client_command(
        build_dir, direction, endpoint.host, endpoint.control_port,
        source_arg, destination_arg, resume_summary, True,
    )
    with resume_log.open("w", encoding="utf-8") as output:
        resumed = subprocess.run(
            resume_command, stdout=output, stderr=subprocess.STDOUT,
            text=True, timeout=360, check=False,
        )
    if resumed.returncode:
        raise RuntimeError(
            f"{direction}: resume failed ({resumed.returncode}): "
            f"{resume_log.read_text(encoding='utf-8', errors='replace')}"
        )
    summary = json.loads(resume_summary.read_text(encoding="utf-8"))
    for key, expected in (("result", "pass"), ("actual_transport", "v3"),
                          ("actual_mode", "persistent_tree"),
                          ("data_session_reuse_mode", "tree"), ("fallback_reason", "")):
        if summary.get(key) != expected:
            raise RuntimeError(f"{direction}: resume summary {key} mismatch: {summary}")
    skipped = int(summary.get("resume_skipped_bytes", 0))
    if not 0 < skipped < file_size:
        raise RuntimeError(f"{direction}: invalid resume_skipped_bytes={skipped}")
    final_checkpoint = endpoint.manifest(local_checkpoint, remote_checkpoint)
    if (final_checkpoint.get("state") != "committed"
            or len(manifest_chunks(final_checkpoint)) != total_chunks):
        raise RuntimeError(f"{direction}: final checkpoint incomplete: {final_checkpoint}")

    if direction == "upload":
        target_file = (endpoint.remote_server_root / "uploaded" / "payload.bin"
                       if endpoint.remote_alias
                       else endpoint.local_server_root / "uploaded" / "payload.bin")
        if endpoint.remote_alias:
            target_sha = endpoint.remote_sha256(target_file)
            target_size = endpoint.remote_size(target_file)
        else:
            target_sha = sha256_file(target_file)
            target_size = target_file.stat().st_size
        target_tree = one_file_tree_hash("payload.bin", target_size, target_sha)
    else:
        target_file = run_dir / "download" / "payload.bin"
        target_sha = sha256_file(target_file)
        target_size = target_file.stat().st_size
        target_tree = tree_hash(run_dir / "download")[0]
    source_sha = sha256_file(source_file)
    expected_tree = tree_hash(source_file.parent)[0]
    if target_sha != source_sha or target_tree != expected_tree:
        raise RuntimeError(
            f"{direction}: final SHA mismatch source={source_sha} target={target_sha} "
            f"tree={target_tree} expected_tree={expected_tree}"
        )
    return {
        "direction": direction,
        "proxy_stall_events": sum(e.get("event") == "data_stalled" for e in endpoint.events()),
        "checkpoint_verified_chunks_before_stall": stalled_count,
        "checkpoint_verified_chunks_after_timeout": after_timeout_count,
        "checkpoint_state_before_stall": stalled_fields.get("state"),
        "checkpoint_state_after_timeout": after_timeout.get("state"),
        "checkpoint_state_after_resume": final_checkpoint.get("state"),
        "timeout_exit_code": first_exit,
        "timeout_configured_ms": TRANSFER_TIMEOUT_MS,
        "timeout_seconds_after_observed_stall": round(timeout_seconds, 3),
        "timeout_acceptance_bound_seconds": maximum_timeout,
        "failure_summary": failure_summary,
        "resume_skipped_bytes": skipped,
        "resume_summary": summary,
        "source_sha256": source_sha,
        "target_sha256": target_sha,
        "tree_sha256": expected_tree,
        "timeout_log": str(first_log),
        "resume_log": str(resume_log),
    }


def run(build_dir: Path, evidence_dir: Path, file_size_mib: int,
        remote_alias: str | None = None, remote_build_dir: Path | None = None,
        remote_root: Path | None = None) -> dict:
    if file_size_mib <= 0:
        raise ValueError("--file-size-mib must be positive")
    if remote_alias and (not remote_build_dir or not remote_root):
        raise ValueError("remote mode needs --remote-build-dir and --remote-root")
    evidence_dir.mkdir(parents=True, exist_ok=True)
    run_id = time.strftime("%Y%m%dT%H%M%S", time.gmtime()) + f"-{os.getpid()}"
    run_dir = evidence_dir / run_id
    run_dir.mkdir(exist_ok=False)
    source_dir = run_dir / "source"
    source_dir.mkdir()
    source_file = source_dir / "payload.bin"
    create_payload(source_file, file_size_mib)
    file_size = file_size_mib * CHUNK_SIZE
    endpoint = TestEndpoint(build_dir, run_dir, remote_alias, remote_build_dir, remote_root)
    try:
        endpoint.prepare()
        endpoint.start_server()
        endpoint.start_proxy()
        upload = transfer(endpoint, build_dir, run_dir, source_file, "upload", file_size)
        endpoint.restart_proxy()
        download = transfer(endpoint, build_dir, run_dir, source_file, "download", file_size)
        result = {
            "result": "pass",
            "run_id": run_id,
            "file_size_bytes": file_size,
            "chunk_size_bytes": CHUNK_SIZE,
            "transfer_timeout_ms": TRANSFER_TIMEOUT_MS,
            "source_sha256": sha256_file(source_file),
            "source_tree_sha256": tree_hash(source_dir)[0],
            "topology": ("Shenzhen V3 client to Shanghai temporary V3 service via controllable proxy"
                         if remote_alias else "loopback V3 client to temporary V3 service via controllable proxy"),
            "upload": upload,
            "download": download,
            "formal_service_modified": False,
            "evidence_dir": str(run_dir),
        }
        (run_dir / "summary.json").write_text(
            json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8"
        )
        endpoint.cleanup_payload()
        source_file.unlink(missing_ok=True)
        (run_dir / "download" / "payload.bin").unlink(missing_ok=True)
        local_upload = endpoint.local_server_root / "uploaded" / "payload.bin"
        local_upload.unlink(missing_ok=True)
        print(json.dumps({
            "result": "pass",
            "upload_timeout_seconds": upload["timeout_seconds_after_observed_stall"],
            "download_timeout_seconds": download["timeout_seconds_after_observed_stall"],
            "upload_verified_chunks": upload["checkpoint_verified_chunks_before_stall"],
            "download_verified_chunks": download["checkpoint_verified_chunks_before_stall"],
            "upload_resume_skipped_bytes": upload["resume_skipped_bytes"],
            "download_resume_skipped_bytes": download["resume_skipped_bytes"],
            "source_sha256": result["source_sha256"],
            "source_tree_sha256": result["source_tree_sha256"],
            "evidence": str(run_dir / "summary.json"),
        }, sort_keys=True))
        return result
    except Exception as error:
        (run_dir / "failure.txt").write_text(str(error) + "\n", encoding="utf-8")
        raise
    finally:
        endpoint.stop()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--file-size-mib", type=int, default=64)
    parser.add_argument("--remote-alias")
    parser.add_argument("--remote-build-dir", type=Path)
    parser.add_argument("--remote-root", type=Path)
    args = parser.parse_args()
    run(
        args.build_dir.resolve(), args.evidence_dir.resolve(), args.file_size_mib,
        args.remote_alias,
        args.remote_build_dir.resolve() if args.remote_build_dir else None,
        args.remote_root,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
