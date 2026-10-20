#!/usr/bin/env python3
"""Verify V3 target conflicts and repeated committed resume remain safe."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_v3_timeout_resume_smoke import (  # noqa: E402
    CHUNK_SIZE,
    TestEndpoint,
    client_command,
    create_payload,
    sha256_file,
    parse_manifest,
)

FILE_SIZE_MIB = 8


def run_client(endpoint: TestEndpoint, build: Path, root: Path, direction: str,
               source: str, destination: str, *, resume: bool,
               label: str) -> tuple[subprocess.CompletedProcess[str], dict, Path]:
    summary = root / f"{label}.json"
    log = root / f"{label}.log"
    command = client_command(
        build, direction, endpoint.host, endpoint.control_port,
        source, destination, summary, resume,
    )
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.run(
            command, stdout=output, stderr=subprocess.STDOUT,
            text=True, timeout=120, check=False,
        )
    data = json.loads(summary.read_text(encoding="utf-8")) if summary.is_file() else {}
    return process, data, log


def assert_conflict(result: subprocess.CompletedProcess[str], summary: dict,
                    log: Path, before_sha: str, after_sha: str, label: str) -> None:
    output = log.read_text(encoding="utf-8", errors="replace")
    combined = output + json.dumps(summary, sort_keys=True)
    if result.returncode == 0 or summary.get("result") != "fail":
        raise RuntimeError(f"{label}: existing target was not rejected: {summary}; {output}")
    if before_sha != after_sha:
        raise RuntimeError(f"{label}: existing target changed: {before_sha} -> {after_sha}")
    if "already exists" not in combined.lower():
        raise RuntimeError(f"{label}: conflict error was not clear: {combined}")


def start_endpoint(build: Path, root: Path) -> TestEndpoint:
    endpoint = TestEndpoint(build, root, None, None, None)
    endpoint.prepare()
    endpoint.start_server()
    endpoint.start_proxy()
    return endpoint


def conflict_case(build: Path, root: Path, source_file: Path,
                  direction: str) -> dict:
    endpoint = start_endpoint(build, root)
    target: Path
    try:
        if direction == "upload":
            target = endpoint.local_server_root / "uploaded" / "payload.bin"
            target.parent.mkdir(parents=True)
            target.write_bytes(b"pre-existing server target\n")
            source, destination = str(source_file.parent), "uploaded"
        else:
            server_source = endpoint.local_server_root / "uploaded" / "payload.bin"
            server_source.parent.mkdir(parents=True)
            shutil.copyfile(source_file, server_source)
            target = root / "download" / "payload.bin"
            target.parent.mkdir()
            target.write_bytes(b"pre-existing client target\n")
            source, destination = "uploaded", str(target.parent)
        before = sha256_file(target)
        result, summary, log = run_client(
            endpoint, build, root, direction, source, destination,
            resume=False, label=f"{direction}-target-conflict",
        )
        after = sha256_file(target)
        assert_conflict(result, summary, log, before, after, f"{direction} conflict")
        return {
            "direction": direction,
            "exit_code": result.returncode,
            "result": summary.get("result"),
            "error_code": summary.get("error_code"),
            "before_sha256": before,
            "after_sha256": after,
            "target_unchanged": before == after,
            "log": str(log),
        }
    finally:
        endpoint.stop()
        if direction == "upload":
            (endpoint.local_server_root / "uploaded" / "payload.bin").unlink(missing_ok=True)
        else:
            (endpoint.local_server_root / "uploaded" / "payload.bin").unlink(missing_ok=True)
            (root / "download" / "payload.bin").unlink(missing_ok=True)


def idempotent_case(build: Path, root: Path, source_file: Path,
                    direction: str) -> dict:
    endpoint = start_endpoint(build, root)
    try:
        if direction == "upload":
            source, destination = str(source_file.parent), "uploaded"
            target = endpoint.local_server_root / "uploaded" / "payload.bin"
        else:
            server_source = endpoint.local_server_root / "uploaded" / "payload.bin"
            server_source.parent.mkdir(parents=True)
            shutil.copyfile(source_file, server_source)
            target = root / "download" / "payload.bin"
            target.parent.mkdir()
            source, destination = "uploaded", str(target.parent)

        first, first_summary, first_log = run_client(
            endpoint, build, root, direction, source, destination,
            resume=False, label=f"{direction}-first-submit",
        )
        if first.returncode != 0 or first_summary.get("result") != "pass":
            raise RuntimeError(f"{direction}: initial transfer failed: {first_summary}; {first_log}")
        initial_sha = sha256_file(target)
        if initial_sha != sha256_file(source_file):
            raise RuntimeError(f"{direction}: initial target differs from source")
        initial_stat = target.stat()
        checkpoint = Path(str(target) + (".cpnetflux.manifest" if direction == "upload"
                                        else ".cpnetflux.download.manifest"))
        first_checkpoint = parse_manifest(checkpoint.read_text(encoding="utf-8"))
        if first_checkpoint.get("state") != "committed":
            raise RuntimeError(f"{direction}: initial checkpoint was not committed")

        repeated, repeated_summary, repeated_log = run_client(
            endpoint, build, root, direction, source, destination,
            resume=True, label=f"{direction}-repeat-resume",
        )
        if repeated.returncode != 0 or repeated_summary.get("result") != "pass":
            raise RuntimeError(
                f"{direction}: repeated resume failed: {repeated_summary}; "
                f"{repeated_log.read_text(errors='replace')}"
            )
        repeated_sha = sha256_file(target)
        repeated_stat = target.stat()
        repeated_checkpoint = parse_manifest(checkpoint.read_text(encoding="utf-8"))
        if repeated_checkpoint.get("state") != "committed":
            raise RuntimeError(f"{direction}: repeated checkpoint lost committed state")
        if repeated_checkpoint.get("transfer_id") != first_checkpoint.get("transfer_id"):
            raise RuntimeError(f"{direction}: repeated resume changed transfer identity")
        if repeated_stat.st_ino != initial_stat.st_ino:
            raise RuntimeError(f"{direction}: repeated resume replaced the committed target")
        if int(repeated_summary.get("bytes_transferred", -1)) != 0:
            raise RuntimeError(f"{direction}: repeated resume retransmitted payload")
        if int(repeated_summary.get("completed_files", -1)) > 1:
            raise RuntimeError(f"{direction}: repeated resume counted a file more than once")
        if initial_sha != repeated_sha:
            raise RuntimeError(f"{direction}: repeated resume changed final target")
        if repeated_summary.get("actual_transport") != "v3":
            raise RuntimeError(f"{direction}: repeated resume did not report V3: {repeated_summary}")
        if repeated_summary.get("fallback_reason", ""):
            raise RuntimeError(f"{direction}: repeated resume used fallback: {repeated_summary}")
        return {
            "direction": direction,
            "first_exit_code": first.returncode,
            "repeat_resume_exit_code": repeated.returncode,
            "first_result": first_summary.get("result"),
            "repeat_result": repeated_summary.get("result"),
            "repeat_completed_files": repeated_summary.get("completed_files"),
            "repeat_skipped_files": repeated_summary.get("skipped_files"),
            "repeat_resume_skipped_bytes": repeated_summary.get("resume_skipped_bytes"),
            "actual_transport": repeated_summary.get("actual_transport"),
            "actual_mode": repeated_summary.get("actual_mode"),
            "fallback_reason": repeated_summary.get("fallback_reason", ""),
            "sha256_before_repeat": initial_sha,
            "sha256_after_repeat": repeated_sha,
            "target_unchanged": initial_sha == repeated_sha,
            "target_inode_unchanged": initial_stat.st_ino == repeated_stat.st_ino,
            "repeat_payload_bytes": repeated_summary.get("bytes_transferred"),
            "checkpoint_state": repeated_checkpoint.get("state"),
            "transfer_id_unchanged": repeated_checkpoint.get("transfer_id") == first_checkpoint.get("transfer_id"),
            "first_log": str(first_log),
            "repeat_log": str(repeated_log),
        }
    finally:
        endpoint.stop()
        if direction == "upload":
            (endpoint.local_server_root / "uploaded" / "payload.bin").unlink(missing_ok=True)
        else:
            (endpoint.local_server_root / "uploaded" / "payload.bin").unlink(missing_ok=True)
            (root / "download" / "payload.bin").unlink(missing_ok=True)


def run(build: Path, evidence_dir: Path, file_size_mib: int) -> dict:
    if file_size_mib != FILE_SIZE_MIB:
        raise ValueError(f"Slice E requires --file-size-mib {FILE_SIZE_MIB}")
    evidence_dir.mkdir(parents=True, exist_ok=True)
    run_id = time.strftime("%Y%m%dT%H%M%S", time.gmtime()) + f"-{__import__('os').getpid()}"
    run_dir = evidence_dir / run_id
    run_dir.mkdir(exist_ok=False)
    payload_dir = run_dir / "payloads"
    payload_dir.mkdir()
    source_file = payload_dir / "payload.bin"
    create_payload(source_file, file_size_mib)
    cases = run_dir / "cases"
    cases.mkdir()
    try:
        results = {
            "target_conflict": [
                conflict_case(build, cases / "upload-conflict", source_file, "upload"),
                conflict_case(build, cases / "download-conflict", source_file, "download"),
            ],
            "repeated_resume": [
                idempotent_case(build, cases / "upload-repeat", source_file, "upload"),
                idempotent_case(build, cases / "download-repeat", source_file, "download"),
            ],
        }
        result = {
            "result": "pass",
            "payload_size_bytes": file_size_mib * CHUNK_SIZE,
            **results,
            "formal_service_modified": False,
            "evidence_dir": str(run_dir),
        }
        summary = run_dir / "summary.json"
        summary.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n",
                           encoding="utf-8")
        shutil.rmtree(payload_dir)
        for payload in cases.rglob("payload.bin"):
            payload.unlink(missing_ok=True)
        print(json.dumps({
            "result": "pass",
            "conflict_cases": len(results["target_conflict"]),
            "repeat_cases": len(results["repeated_resume"]),
            "evidence": str(summary),
        }, sort_keys=True))
        return result
    except Exception as error:
        (run_dir / "failure.txt").write_text(str(error) + "\n", encoding="utf-8")
        raise


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
