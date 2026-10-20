#!/usr/bin/env python3
"""Verify V3 rejects a corrupted partial checkpoint without committing data."""
from __future__ import annotations

import argparse
import json
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_v3_timeout_resume_smoke import (  # noqa: E402
    CHUNK_SIZE,
    TestEndpoint,
    client_command,
    create_payload,
    manifest_chunks,
    sha256_file,
    wait_for_stall,
)

FILE_SIZE_MIB = 64


def parse_manifest(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    return fields


def raw_checkpoint(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def file_signature(path: Path) -> dict[str, int | str]:
    return {"size": path.stat().st_size, "sha256": sha256_file(path)}


def corrupt_checkpoint(path: Path) -> tuple[str, str]:
    original = raw_checkpoint(path)
    marker = "updated_at_unix_ns="
    lines = original.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith(marker):
            value = line[len(marker):].rstrip("\r\n")
            if not value.isdigit():
                raise RuntimeError("checkpoint updated_at_unix_ns is not numeric")
            replacement = str(int(value) + 1)
            ending = line[len(line.rstrip("\r\n")):]
            lines[index] = marker + replacement + ending
            break
    else:
        raise RuntimeError("checkpoint has no timestamp field to corrupt")
    corrupted = "".join(lines)
    if corrupted == original:
        raise RuntimeError("checkpoint corruption did not change content")
    path.write_text(corrupted, encoding="utf-8")
    return original, corrupted


def run_direction(build_dir: Path, root: Path, source_file: Path,
                  direction: str) -> dict:
    endpoint = TestEndpoint(build_dir, root, None, None, None)
    endpoint.prepare()
    if direction == "download":
        source_dir = endpoint.local_server_root / "uploaded"
        source_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_file, source_dir / "payload.bin")
    endpoint.start_server()
    endpoint.start_proxy()
    checkpoint = (endpoint.local_server_root / "uploaded" /
                  "payload.bin.cpnetflux.manifest"
                  if direction == "upload" else
                  root / "download" / "payload.bin.cpnetflux.download.manifest")
    target_dir = root / "download"
    if direction == "download":
        target_dir.mkdir()
        source_arg, destination_arg = "uploaded", str(target_dir)
    else:
        source_arg, destination_arg = str(source_file.parent), "uploaded"
    first_log = root / f"{direction}-initial.log"
    first_summary = root / f"{direction}-initial.json"
    command = client_command(
        build_dir, direction, endpoint.host, endpoint.control_port,
        source_arg, destination_arg, first_summary, False,
    )
    process = None
    temp_path = None
    try:
        with first_log.open("w", encoding="utf-8") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
            fields, count = wait_for_stall(
                endpoint, process, checkpoint, None,
                direction, first_log, FILE_SIZE_MIB,
            )
            process.send_signal(signal.SIGTERM)
            exit_code = process.wait(timeout=15)
        if exit_code != -signal.SIGTERM:
            raise RuntimeError(f"{direction}: expected SIGTERM, got {exit_code}")
        before_corruption = raw_checkpoint(checkpoint)
        if count <= 0 or count >= FILE_SIZE_MIB or "state=committed" in before_corruption:
            raise RuntimeError(f"{direction}: checkpoint was not partial: {fields}")
        checkpoint_target = parse_manifest(before_corruption).get("temp_path_hex")
        if not checkpoint_target:
            raise RuntimeError(f"{direction}: checkpoint has no temp path")
        temp_path = Path(bytes.fromhex(checkpoint_target).decode("utf-8"))
        temp_before = file_signature(temp_path)
        original_manifest, corrupted_manifest = corrupt_checkpoint(checkpoint)
        if "manifest_body_crc32c=" not in original_manifest:
            raise RuntimeError(f"{direction}: checkpoint lacks body checksum")
        endpoint.remove_pause()
        endpoint.restart_proxy()
        resume_log = root / f"{direction}-corrupt-resume.log"
        resume_summary = root / f"{direction}-corrupt-resume.json"
        resume_command = client_command(
            build_dir, direction, endpoint.host, endpoint.control_port,
            source_arg, destination_arg, resume_summary, True,
        )
        with resume_log.open("w", encoding="utf-8") as output:
            resumed = subprocess.run(
                resume_command, stdout=output, stderr=subprocess.STDOUT,
                text=True, timeout=60, check=False,
            )
        if resumed.returncode == 0:
            raise RuntimeError(f"{direction}: corrupted checkpoint was accepted")
        output = resume_log.read_text(encoding="utf-8", errors="replace")
        summary = json.loads(resume_summary.read_text(encoding="utf-8")) \
            if resume_summary.is_file() else {}
        combined = output + json.dumps(summary, sort_keys=True)
        if "manifest" not in combined.lower() and "checksum" not in combined.lower():
            raise RuntimeError(f"{direction}: failure did not identify checkpoint corruption: {combined}")
        after_manifest = raw_checkpoint(checkpoint)
        if "state=committed" in after_manifest:
            raise RuntimeError(f"{direction}: corrupt checkpoint became committed")
        temp_after = file_signature(temp_path)
        if temp_before != temp_after:
            raise RuntimeError(f"{direction}: partial destination changed: {temp_before} -> {temp_after}")
        final_target = (endpoint.local_server_root / "uploaded" / "payload.bin"
                        if direction == "upload" else target_dir / "payload.bin")
        if final_target.exists():
            raise RuntimeError(f"{direction}: corrupt checkpoint produced a committed target")
        return {
            "direction": direction,
            "initial_exit_code": exit_code,
            "verified_chunks_before_corruption": count,
            "checkpoint_state_before_corruption": fields.get("state"),
            "checkpoint_state_after_resume": parse_manifest(after_manifest).get("state"),
            "resume_exit_code": resumed.returncode,
            "resume_summary": summary,
            "corruption_changed_manifest": original_manifest != corrupted_manifest,
            "temp_signature_before": temp_before,
            "temp_signature_after": temp_after,
            "final_target_exists": False,
            "resume_log": str(resume_log),
        }
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        try:
            endpoint.stop()
        finally:
            if temp_path is not None:
                temp_path.unlink(missing_ok=True)


def run(build_dir: Path, evidence_dir: Path, file_size_mib: int) -> dict:
    if file_size_mib != FILE_SIZE_MIB:
        raise ValueError(f"Slice D requires --file-size-mib {FILE_SIZE_MIB}")
    evidence_dir.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="v3-corrupt-checkpoint.", dir=evidence_dir))
    source = run_dir / "source"
    source.mkdir()
    source_file = source / "payload.bin"
    create_payload(source_file, file_size_mib)
    try:
        upload = run_direction(build_dir, run_dir / "upload", source_file, "upload")
        download = run_direction(build_dir, run_dir / "download-case", source_file, "download")
        result = {
            "result": "pass",
            "file_size_bytes": file_size_mib * CHUNK_SIZE,
            "chunk_size_bytes": CHUNK_SIZE,
            "upload": upload,
            "download": download,
            "production_code_changed": False,
            "evidence_dir": str(run_dir),
        }
        (run_dir / "summary.json").write_text(
            json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps({
            "result": "pass",
            "upload_verified_chunks": upload["verified_chunks_before_corruption"],
            "download_verified_chunks": download["verified_chunks_before_corruption"],
            "upload_resume_exit_code": upload["resume_exit_code"],
            "download_resume_exit_code": download["resume_exit_code"],
            "evidence": str(run_dir / "summary.json"),
        }, sort_keys=True))
        return result
    except Exception as error:
        (run_dir / "failure.txt").write_text(str(error) + "\n", encoding="utf-8")
        raise
    finally:
        source_file.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--file-size-mib", type=int, default=FILE_SIZE_MIB)
    args = parser.parse_args()
    run(args.build_dir.resolve(), args.evidence_dir.resolve(), args.file_size_mib)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
