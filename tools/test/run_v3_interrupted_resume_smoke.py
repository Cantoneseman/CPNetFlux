#!/usr/bin/env python3
"""Prove that a partially checkpointed V3 tree transfer survives client interruption."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tree_smoke_common import free_port, start_server, stop_server, tree_hash
from gridftp_port_window import clamp_passive_data_port_base

FILE_SIZE = 512 * 1024 * 1024
CHUNK_SIZE = 1024 * 1024


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def create_payload(path: Path) -> None:
    block = bytes(((index * 131 + 17) % 256) for index in range(CHUNK_SIZE))
    with path.open("wb") as handle:
        for _ in range(FILE_SIZE // CHUNK_SIZE):
            handle.write(block)


def manifest_fields(path: Path) -> dict[str, str]:
    fields = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    return fields


def wait_for_partial_checkpoint(process: subprocess.Popen, checkpoint: Path,
                                label: str, log: Path) -> tuple[dict[str, str], int]:
    total_chunks = math.ceil(FILE_SIZE / CHUNK_SIZE)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"{label}: client exited before a partial checkpoint; "
                               f"exit={process.returncode}; log={log.read_text(errors='replace')}")
        if checkpoint.is_file():
            fields = manifest_fields(checkpoint)
            chunks = [item for item in fields.get("verified_chunks", "").split(",") if item]
            if (fields.get("state") != "committed" and
                    0 < len(chunks) < total_chunks):
                return fields, len(chunks)
        time.sleep(0.002)
    raise RuntimeError(f"{label}: timed out waiting for a non-empty partial checkpoint; "
                       f"log={log.read_text(errors='replace')}")


def client_command(build_dir: Path, direction: str, port: int, source: str,
                   destination: str, summary: Path, resume: bool) -> list[str]:
    command = [
        str(build_dir / f"cpnetflux-tree-{direction}-client"),
        "--host", "127.0.0.1", "--port", str(port),
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


def interrupt_and_resume(build_dir: Path, root: Path,
                         port: int, direction: str, source: str,
                         destination: str, checkpoint: Path) -> dict:
    label = direction
    interrupted_log = root / f"{label}-interrupted.log"
    interrupted_summary = root / f"{label}-interrupted.json"
    command = client_command(build_dir, direction, port, source, destination,
                             interrupted_summary, False)
    with interrupted_log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        fields, verified_count = wait_for_partial_checkpoint(
            process, checkpoint, label, interrupted_log)
        if process.poll() is not None:
            raise RuntimeError(f"{label}: client finished before interruption")
        process.send_signal(signal.SIGTERM)
        try:
            return_code = process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            raise RuntimeError(f"{label}: client did not stop after SIGTERM")
    if return_code != -signal.SIGTERM:
        raise RuntimeError(f"{label}: expected SIGTERM exit, got {return_code}")
    if interrupted_summary.exists():
        raise RuntimeError(f"{label}: interrupted transfer unexpectedly wrote a success summary")
    persisted = manifest_fields(checkpoint)
    persisted_chunks = [item for item in persisted.get("verified_chunks", "").split(",") if item]
    if not persisted_chunks or persisted.get("state") == "committed":
        raise RuntimeError(f"{label}: partial checkpoint did not survive client interruption: "
                           f"{persisted}")

    resume_summary = root / f"{label}-resume.json"
    resume_log = root / f"{label}-resume.log"
    resume_command = client_command(build_dir, direction, port, source, destination,
                                    resume_summary, True)
    with resume_log.open("w", encoding="utf-8") as output:
        resumed = subprocess.run(resume_command, stdout=output, stderr=subprocess.STDOUT,
                                 text=True, timeout=300, check=False)
    if resumed.returncode != 0:
        raise RuntimeError(f"{label}: resume failed ({resumed.returncode}): "
                           f"{resume_log.read_text(errors='replace')}")
    summary = json.loads(resume_summary.read_text(encoding="utf-8"))
    for key, expected in (("result", "pass"), ("actual_mode", "persistent_tree"),
                          ("data_session_reuse_mode", "tree"), ("actual_transport", "v3")):
        if summary.get(key) != expected:
            raise RuntimeError(f"{label}: expected {key}={expected}, got {summary}")
    if summary.get("fallback_reason") != "":
        raise RuntimeError(f"{label}: V3 resume reported fallback: {summary}")
    if summary.get("resume_skipped_bytes", 0) <= 0:
        raise RuntimeError(f"{label}: resume did not report already-checkpointed bytes: {summary}")
    if summary.get("resume_skipped_bytes", 0) >= FILE_SIZE:
        raise RuntimeError(f"{label}: resume skipped the entire file unexpectedly: {summary}")
    after = manifest_fields(checkpoint)
    if after.get("state") != "committed":
        raise RuntimeError(f"{label}: final checkpoint is not committed: {after}")
    return {
        "direction": direction,
        "checkpoint_chunks_before_interrupt": verified_count,
        "checkpoint_chunks_after_interrupt": len(persisted_chunks),
        "checkpoint_state_after_interrupt": persisted.get("state"),
        "checkpoint_state_after_resume": after.get("state"),
        "summary": summary,
        "interrupted_exit_code": return_code,
        "interrupted_log": interrupted_log.read_text(errors="replace"),
        "resume_log": resume_log.read_text(errors="replace"),
    }


def run(build_dir: Path, evidence_dir: Path) -> dict:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-v3-interrupted-resume.") as temp_text:
        root = Path(temp_text)
        source = root / "source"
        source.mkdir()
        source_file = source / "payload.bin"
        create_payload(source_file)
        source_hash = sha256_file(source_file)
        source_tree_hash = tree_hash(source)
        server_root = root / "server"
        server_root.mkdir()
        port = free_port()
        server_log = root / "server.log"
        server = start_server(
            build_dir, server_root, port, clamp_passive_data_port_base(free_port()),
            server_log, extra_env={"CPNETFLUX_TEST_TOKEN": "v3-test-only"},
        )
        try:
            upload_checkpoint = server_root / "uploaded" / "payload.bin.cpnetflux.manifest"
            upload = interrupt_and_resume(build_dir, root, port, "upload",
                                          str(source), "uploaded", upload_checkpoint)
            uploaded_file = server_root / "uploaded" / "payload.bin"
            if sha256_file(uploaded_file) != source_hash:
                raise RuntimeError("upload final file SHA-256 mismatch")
            if tree_hash(server_root / "uploaded") != source_tree_hash:
                raise RuntimeError("upload final tree SHA-256 mismatch")

            download_root = root / "download"
            download_checkpoint = download_root / "payload.bin.cpnetflux.download.manifest"
            download = interrupt_and_resume(build_dir, root, port, "download",
                                            "uploaded", str(download_root), download_checkpoint)
            downloaded_file = download_root / "payload.bin"
            if sha256_file(downloaded_file) != source_hash:
                raise RuntimeError("download final file SHA-256 mismatch")
            if tree_hash(download_root) != source_tree_hash:
                raise RuntimeError("download final tree SHA-256 mismatch")
        finally:
            stop_server(server, server_log)

        result = {
            "result": "pass",
            "file_size": FILE_SIZE,
            "chunk_size": CHUNK_SIZE,
            "source_sha256": source_hash,
            "source_tree_sha256": source_tree_hash,
            "upload": upload,
            "download": download,
        }
        (evidence_dir / "summary.json").write_text(
            json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({
            "result": "pass",
            "source_sha256": source_hash,
            "source_tree_sha256": source_tree_hash,
            "upload_checkpoint_chunks": upload["checkpoint_chunks_before_interrupt"],
            "upload_resume_skipped_bytes": upload["summary"]["resume_skipped_bytes"],
            "download_checkpoint_chunks": download["checkpoint_chunks_before_interrupt"],
            "download_resume_skipped_bytes": download["summary"]["resume_skipped_bytes"],
            "evidence": str(evidence_dir / "summary.json"),
        }, sort_keys=True))
        return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    args = parser.parse_args()
    run(args.build_dir.resolve(), args.evidence_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
