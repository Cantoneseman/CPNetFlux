#!/usr/bin/env python3
"""Exercise durable V3 resume after the server process restarts mid-transfer."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tree_smoke_common import free_port, start_server, tree_hash
from gridftp_port_window import clamp_passive_data_port_base

FILE_SIZE_MIB = 512
CHUNK_SIZE = 1024 * 1024
TEST_TOKEN = "v3-test-only"


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
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    return fields


def chunk_count(fields: dict[str, str]) -> int:
    return len([item for item in fields.get("verified_chunks", "").split(",") if item])


def manifest_local(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    return parse_manifest(path.read_text(encoding="utf-8"))


def client_command(build_dir: Path, direction: str, host: str, port: int,
                   source: str, destination: str, summary: Path,
                   resume: bool) -> list[str]:
    command = [
        str(build_dir / f"cpnetflux-tree-{direction}-client"),
        "--host", host, "--port", str(port),
        "--source-dir", source, "--dest-dir", destination,
        "--connections", "1", "--file-parallelism", "1",
        "--chunk-size", str(CHUNK_SIZE), "--buffer-size", "65536",
        "--checksum", "crc32c", "--compression", "off",
        "--data-session-reuse", "tree", "--file-scheduling", "dynamic",
        "--control-reuse", "worker", "--transfer-timeout-ms", "120000",
        "--phase-timing", "on", "--json-summary", str(summary),
    ]
    if resume:
        command.append("--resume")
    return command


class Service:
    def __init__(self, build_dir: Path, run_dir: Path, *, remote_alias: str | None,
                 remote_build_dir: Path | None, remote_root: Path | None):
        self.remote_alias = remote_alias
        self.remote_build_dir = remote_build_dir
        self.remote_root = remote_root
        self.local_root = run_dir / "server-root"
        self.control_port = 24600 if remote_alias else free_port()
        self.data_port = clamp_passive_data_port_base(24610 if remote_alias else free_port())
        self.host = "47.116.174.181" if remote_alias else "127.0.0.1"
        self.build_dir = build_dir
        self.run_dir = run_dir
        self.binary = (remote_build_dir / "cpnetflux-gridftp-server"
                       if remote_alias and remote_build_dir
                       else build_dir / "cpnetflux-gridftp-server")
        self.pid: int | None = None
        self.generation = 0
        self.proxy_pid: int | None = None
        self.remote_case_dir = remote_root
        self.remote_server_root = remote_root / "server-root" if remote_root else None
        self.remote_bin_dir = remote_root / "bin" if remote_root else None

    def prepare(self) -> None:
        if not self.remote_alias:
            self.local_root.mkdir(parents=True, exist_ok=False)
            return
        if not self.remote_build_dir or not self.remote_root:
            raise ValueError("remote mode requires --remote-build-dir and --remote-root")
        from run_cpnetflux_lane_recovery_interop import (
            SSH_OPTIONS, scp_to, ssh_run, SHANGHAI_BIND,
        )
        if not str(self.remote_root).startswith(
                "/tmp/cpnetflux-runs/V3-RELEASE-STABILITY-01/slice-b/"):
            raise ValueError(f"unsafe remote test root: {self.remote_root}")
        exists = ssh_run(self.remote_alias, ["test", "!", "-e", str(self.remote_root)])
        if exists.returncode:
            raise RuntimeError(f"remote test path already exists: {self.remote_root}")
        made = ssh_run(self.remote_alias, ["mkdir", "-p", str(self.remote_server_root),
                                           str(self.remote_bin_dir)])
        if made.returncode:
            raise RuntimeError(f"cannot create isolated remote test paths: {made.stderr}")
        proxy_script = Path(__file__).with_name("cpnetflux_fault_proxy.py")
        scp_to(self.remote_alias, proxy_script, str(self.remote_bin_dir / proxy_script.name))
        proxy_log = self.remote_root / "proxy.jsonl"
        self.proxy_pid = self._remote_start([
            "python3", str(self.remote_bin_dir / "cpnetflux_fault_proxy.py"),
            "--listen-host", SHANGHAI_BIND, "--backend-host", "127.0.0.1",
            "--control-port", str(self.control_port),
            "--data-base", str(self.data_port), "--count", "8",
            "--delay", "0", "--drops", "0", "--log", str(proxy_log),
        ], self.remote_root / "proxy.log")
        time.sleep(0.2)

    def _remote_start(self, argv: list[str], log: Path) -> int:
        from run_cpnetflux_lane_recovery_interop import remote_start
        return remote_start(self.remote_alias, argv, str(log))

    def start(self) -> None:
        self.generation += 1
        log = self.run_dir / f"server-generation-{self.generation}.log"
        if not self.remote_alias:
            self.local_root.mkdir(parents=True, exist_ok=True)
            self.process = start_server(
                self.build_dir, self.local_root, self.control_port, self.data_port,
                log, extra_env={"CPNETFLUX_TEST_TOKEN": TEST_TOKEN},
            )
            self.pid = self.process.pid
            return
        root = self.remote_server_root
        if self.generation == 1:
            mkdir = self._ssh(["mkdir", "-p", str(root)])
            if mkdir.returncode:
                raise RuntimeError(f"remote server root creation failed: {mkdir.stderr}")
        argv = [
            "env", f"CPNETFLUX_TEST_TOKEN={TEST_TOKEN}", str(self.binary),
            "--host", "127.0.0.1", "--port", str(self.control_port),
            "--root", str(root), "--data-port-base", str(self.data_port),
            "--connections", "1", "--chunk-size", str(CHUNK_SIZE),
            "--buffer-size", "65536", "--checksum", "crc32c",
            "--checksum-backend", "auto",
        ]
        self.pid = self._remote_start(argv, self.remote_root / f"server-generation-{self.generation}.log")
        from run_cpnetflux_lane_recovery_interop import wait_for_tcp
        wait_for_tcp(self.host, self.control_port, timeout=15)

    def _ssh(self, argv: list[str]) -> subprocess.CompletedProcess[str]:
        from run_cpnetflux_lane_recovery_interop import ssh_run
        return ssh_run(self.remote_alias, argv, timeout=30)

    def stop(self) -> None:
        if self.pid is None:
            return
        pid = self.pid
        if not self.remote_alias:
            process = self.process
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    self.pid = None
                    raise RuntimeError("temporary local service did not stop after SIGTERM")
            self.pid = None
            if process.returncode not in (0, -signal.SIGTERM):
                raise RuntimeError(f"temporary local service exit={process.returncode}")
            return
        root = self.remote_server_root
        binary = self.binary
        check = (
            "import pathlib,sys; p=pathlib.Path('/proc')/sys.argv[1]/'cmdline'; "
            "data=p.read_bytes().replace(bytes([0]),b' ').decode(errors='replace'); "
            "assert str(pathlib.Path(sys.argv[2])) in data and str(pathlib.Path(sys.argv[3])) in data, data"
        )
        validated = self._ssh(["python3", "-c", check, str(pid), str(binary), str(root)])
        if validated.returncode:
            self.pid = None
            raise RuntimeError(f"refusing to stop unexpected remote PID {pid}: {validated.stderr}")
        stopped = self._ssh(["kill", "-TERM", str(pid)])
        if stopped.returncode:
            self.pid = None
            raise RuntimeError(f"SIGTERM failed for temporary server PID {pid}: {stopped.stderr}")
        poll = (
            "import pathlib,sys,time; p=pathlib.Path('/proc')/sys.argv[1]/'stat'; "
            "end=time.monotonic()+10; "
            "\nwhile time.monotonic()<end:\n"
            " try:\n  s=p.read_text().split()\n"
            "  if len(s)>2 and s[2]=='Z': raise SystemExit(0)\n"
            " except FileNotFoundError: raise SystemExit(0)\n"
            " time.sleep(.1)\n"
            "raise SystemExit(1)"
        )
        exited = self._ssh(["python3", "-c", poll, str(pid)])
        self.pid = None
        if exited.returncode:
            raise RuntimeError(f"temporary remote server PID {pid} did not exit after SIGTERM")

    def stop_proxy(self) -> None:
        if not self.remote_alias or self.proxy_pid is None:
            return
        pid = self.proxy_pid
        proxy = self.remote_bin_dir / "cpnetflux_fault_proxy.py"
        check = (
            "import pathlib,sys; p=pathlib.Path('/proc')/sys.argv[1]/'cmdline'; "
            "data=p.read_bytes().replace(bytes([0]),b' ').decode(errors='replace'); "
            "assert str(pathlib.Path(sys.argv[2])) in data, data"
        )
        validated = self._ssh(["python3", "-c", check, str(pid), str(proxy)])
        if validated.returncode:
            self.proxy_pid = None
            raise RuntimeError(f"refusing to stop unexpected proxy PID {pid}: {validated.stderr}")
        stopped = self._ssh(["kill", "-TERM", str(pid)])
        self.proxy_pid = None
        if stopped.returncode:
            raise RuntimeError(f"SIGTERM failed for temporary proxy PID {pid}: {stopped.stderr}")

    def manifest(self, local_path: Path, remote_path: Path | None = None) -> dict[str, str]:
        if remote_path is None:
            return manifest_local(local_path)
        result = self._ssh(["cat", str(remote_path)])
        if result.returncode:
            return {}
        return parse_manifest(result.stdout)

    def remote_sha256(self, path: Path) -> str:
        result = self._ssh(["sha256sum", str(path)])
        if result.returncode:
            raise RuntimeError(f"remote sha256sum failed for {path}: {result.stderr}")
        return result.stdout.split()[0]

    def remote_tree_hash(self, root: Path) -> str:
        code = """import hashlib,pathlib,sys
root=pathlib.Path(sys.argv[1])
digest=hashlib.sha256()
for path in sorted(item for item in root.rglob("*") if item.is_file()):
    relative=path.relative_to(root).as_posix()
    if ".cpnetflux." in relative or ".part." in relative:
        continue
    size=path.stat().st_size
    digest.update(relative.encode("utf-8")+bytes([0]))
    digest.update(str(size).encode("ascii")+bytes([0]))
    file_digest=hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1048576), b""):
            file_digest.update(block)
    digest.update(file_digest.hexdigest().encode("ascii")+bytes([0]))
print(digest.hexdigest())
"""
        encoded = base64.b64encode(code.encode("utf-8")).decode("ascii")
        result = self._ssh([
            "python3", "-c", "import base64,sys;exec(base64.b64decode(sys.argv[1]))",
            encoded, str(root),
        ])
        if result.returncode:
            raise RuntimeError(f"remote tree hash failed for {root}: {result.stderr}")
        return result.stdout.strip().splitlines()[-1]

    def cleanup_payload(self) -> None:
        if not self.remote_alias or not self.remote_server_root:
            return
        payload = self.remote_server_root / "uploaded" / "payload.bin"
        safe_unlink = (
            "import pathlib,sys; root=pathlib.Path(sys.argv[1]).resolve(); "
            "p=pathlib.Path(sys.argv[2]).resolve(); p.relative_to(root); "
            "p.unlink(missing_ok=True)"
        )
        result = self._ssh(["python3", "-c", safe_unlink,
                            str(self.remote_server_root), str(payload)])
        if result.returncode:
            raise RuntimeError(f"cannot remove test payload {payload}: {result.stderr}")


def stop_proxy_best_effort(service: Service) -> str | None:
    try:
        service.stop_proxy()
        return None
    except Exception as error:
        return str(error)


def wait_for_partial_checkpoint(service: Service, process: subprocess.Popen,
                                local_checkpoint: Path, remote_checkpoint: Path | None,
                                label: str, log: Path, total_chunks: int) -> tuple[dict[str, str], int]:
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"{label}: client exited before the service restart; "
                               f"exit={process.returncode}; log={log.read_text(errors='replace')}")
        fields = service.manifest(local_checkpoint, remote_checkpoint)
        count = chunk_count(fields)
        if fields.get("state") != "committed" and 0 < count < total_chunks:
            return fields, count
        time.sleep(0.02)
    raise RuntimeError(f"{label}: no partial checkpoint before timeout; "
                       f"last_fields={fields}; log={log.read_text(errors='replace')}")


def run_transfer(service: Service, build_dir: Path, run_dir: Path, source_file: Path,
                 direction: str, file_size: int) -> dict:
    total_chunks = math.ceil(file_size / CHUNK_SIZE)
    label = direction
    local_checkpoint = (service.local_root / "uploaded" / "payload.bin.cpnetflux.manifest"
                        if direction == "upload" and not service.remote_alias
                        else run_dir / "download" / "payload.bin.cpnetflux.download.manifest"
                        if direction == "download" else run_dir / "unused-upload-checkpoint")
    remote_checkpoint = (service.remote_server_root / "uploaded" /
                         "payload.bin.cpnetflux.manifest"
                         if direction == "upload" and service.remote_alias else None)
    if direction == "download":
        local_destination = run_dir / "download"
        local_destination.mkdir(exist_ok=True)
        source_arg, destination_arg = "uploaded", str(local_destination)
    else:
        source_arg, destination_arg = str(source_file.parent), "uploaded"

    def make_command(summary: Path, resume: bool) -> list[str]:
        return client_command(build_dir, direction, service.host, service.control_port,
                              source_arg, destination_arg, summary, resume)

    first_log = run_dir / f"{label}-before-service-restart.log"
    first_summary = run_dir / f"{label}-before-service-restart.json"
    command = make_command(first_summary, False)
    with first_log.open("w", encoding="utf-8") as output:
        first = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        try:
            before, before_count = wait_for_partial_checkpoint(
                service, first, local_checkpoint, remote_checkpoint, label, first_log, total_chunks)
            service.stop()
            try:
                first_exit = first.wait(timeout=30)
            except subprocess.TimeoutExpired:
                raise RuntimeError(f"{label}: client did not fail after service termination")
        except BaseException:
            if first.poll() is None:
                first.send_signal(signal.SIGTERM)
                try:
                    first.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    first.kill()
                    first.wait(timeout=5)
            raise
    if first_exit == 0:
        raise RuntimeError(f"{label}: client unexpectedly succeeded while server was stopped")
    persisted = service.manifest(local_checkpoint, remote_checkpoint)
    after_count = chunk_count(persisted)
    if (persisted.get("state") == "committed" or after_count < before_count or
            after_count <= 0):
        raise RuntimeError(f"{label}: checkpoint was lost or committed after server stop: "
                           f"before={before}, after={persisted}")
    service.start()

    resume_log = run_dir / f"{label}-resume-after-service-restart.log"
    resume_summary = run_dir / f"{label}-resume-after-service-restart.json"
    with resume_log.open("w", encoding="utf-8") as output:
        resumed = subprocess.run(make_command(resume_summary, True), stdout=output,
                                 stderr=subprocess.STDOUT, text=True, timeout=360, check=False)
    if resumed.returncode:
        raise RuntimeError(f"{label}: V3 resume failed after server restart "
                           f"({resumed.returncode}): {resume_log.read_text(errors='replace')}")
    summary = json.loads(resume_summary.read_text(encoding="utf-8"))
    for key, expected in (("result", "pass"), ("actual_transport", "v3"),
                          ("actual_mode", "persistent_tree"),
                          ("data_session_reuse_mode", "tree")):
        if summary.get(key) != expected:
            raise RuntimeError(f"{label}: expected {key}={expected}, got {summary}")
    if summary.get("fallback_reason") != "":
        raise RuntimeError(f"{label}: unexpected fallback: {summary}")
    skipped = int(summary.get("resume_skipped_bytes", 0))
    if not 0 < skipped < file_size:
        raise RuntimeError(f"{label}: invalid resume_skipped_bytes={skipped}")
    final = service.manifest(local_checkpoint, remote_checkpoint)
    if final.get("state") != "committed" or chunk_count(final) != total_chunks:
        raise RuntimeError(f"{label}: final checkpoint is incomplete: {final}")
    target_file = ((service.remote_server_root / "uploaded" / "payload.bin")
                   if direction == "upload" and service.remote_alias
                   else service.local_root / "uploaded" / "payload.bin"
                   if direction == "upload"
                   else run_dir / "download" / "payload.bin")
    target_hash = (service.remote_sha256(target_file) if service.remote_alias and direction == "upload"
                   else sha256_file(target_file))
    source_hash = sha256_file(source_file)
    if target_hash != source_hash:
        raise RuntimeError(f"{label}: SHA-256 mismatch source={source_hash}, target={target_hash}")
    target_tree_hash = tree_hash(run_dir / "source")[0]
    target_root = (service.remote_server_root / "uploaded"
                   if direction == "upload" and service.remote_alias
                   else service.local_root / "uploaded" if direction == "upload"
                   else run_dir / "download")
    actual_tree_hash = (service.remote_tree_hash(target_root)
                        if service.remote_alias and direction == "upload"
                        else tree_hash(target_root)[0])
    if actual_tree_hash != target_tree_hash:
        raise RuntimeError(f"{label}: final tree SHA-256 mismatch "
                           f"expected={target_tree_hash}, actual={actual_tree_hash}")
    return {
        "direction": direction,
        "checkpoint_verified_chunks_before_restart": before_count,
        "checkpoint_verified_chunks_after_restart": after_count,
        "checkpoint_state_before_restart": before.get("state"),
        "checkpoint_state_after_restart": persisted.get("state"),
        "checkpoint_state_after_resume": final.get("state"),
        "client_exit_after_server_restart": first_exit,
        "resume_skipped_bytes": skipped,
        "summary": summary,
        "source_sha256": source_hash,
        "target_sha256": target_hash,
        "tree_sha256": target_tree_hash,
        "actual_tree_sha256": actual_tree_hash,
        "first_client_log": str(first_log),
        "resume_client_log": str(resume_log),
        "server_generation_logs": [
            str(service.run_dir / f"server-generation-{n}.log")
            for n in range(1, service.generation + 1)
        ],
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
    service = Service(build_dir, run_dir, remote_alias=remote_alias,
                      remote_build_dir=remote_build_dir, remote_root=remote_root)
    proxy_error: str | None = None
    try:
        service.prepare()
        service.start()
        upload = run_transfer(service, build_dir, run_dir, source_file, "upload",
                              file_size_mib * 1024 * 1024)
        download = run_transfer(service, build_dir, run_dir, source_file, "download",
                                file_size_mib * 1024 * 1024)
        result = {
            "result": "pass",
            "run_id": run_id,
            "file_size_bytes": file_size_mib * 1024 * 1024,
            "chunk_size_bytes": CHUNK_SIZE,
            "source_sha256": sha256_file(source_file),
            "source_tree_sha256": tree_hash(source_dir)[0],
            "topology": ("Shenzhen client to Shanghai temporary service"
                         if remote_alias else "local client and temporary service"),
            "upload": upload,
            "download": download,
            "production_service_modified": False,
            "evidence_dir": str(run_dir),
        }
        (run_dir / "summary.json").write_text(
            json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        service.cleanup_payload()
        source_file.unlink()
        downloaded = run_dir / "download" / "payload.bin"
        downloaded.unlink(missing_ok=True)
        local_uploaded = service.local_root / "uploaded" / "payload.bin"
        local_uploaded.unlink(missing_ok=True)
        print(json.dumps({
            "result": "pass", "run_id": run_id,
            "upload_checkpoint_chunks": upload["checkpoint_verified_chunks_before_restart"],
            "upload_resume_skipped_bytes": upload["resume_skipped_bytes"],
            "download_checkpoint_chunks": download["checkpoint_verified_chunks_before_restart"],
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
        try:
            service.stop()
        finally:
            proxy_error = stop_proxy_best_effort(service)
            if proxy_error:
                (run_dir / "cleanup-warning.txt").write_text(
                    proxy_error + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--file-size-mib", type=int, default=FILE_SIZE_MIB)
    parser.add_argument("--remote-alias")
    parser.add_argument("--remote-build-dir", type=Path)
    parser.add_argument("--remote-root", type=Path)
    args = parser.parse_args()
    run(args.build_dir.resolve(), args.evidence_dir.resolve(), args.file_size_mib,
        args.remote_alias,
        args.remote_build_dir.resolve() if args.remote_build_dir else None,
        args.remote_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
