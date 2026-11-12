#!/usr/bin/env python3
"""Cross-cloud V3 corrupted-checkpoint safety acceptance runner."""
from __future__ import annotations

import argparse
import json
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_v3_timeout_resume_smoke import (  # noqa: E402
    CHUNK_SIZE,
    TestEndpoint,
    client_command,
    copy_to_remote,
    create_payload,
    manifest_chunks,
    parse_manifest,
    remote_run,
    sha256_file,
    wait_for_stall,
)

FILE_SIZE_MIB = 64


def signature(endpoint: TestEndpoint, path: Path, *, remote: bool) -> dict[str, int | str]:
    if remote:
        return {"size": endpoint.remote_size(path), "sha256": endpoint.remote_sha256(path)}
    return {"size": path.stat().st_size, "sha256": sha256_file(path)}


def corrupt_timestamp(text: str) -> str:
    marker = "updated_at_unix_ns="
    lines = text.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith(marker):
            value = line[len(marker):].rstrip("\r\n")
            if not value.isdigit():
                raise RuntimeError("checkpoint timestamp is not numeric")
            ending = line[len(line.rstrip("\r\n")):]
            lines[index] = marker + str(int(value) + 1) + ending
            corrupted = "".join(lines)
            if corrupted == text:
                raise RuntimeError("checkpoint corruption did not change content")
            return corrupted
    raise RuntimeError("checkpoint timestamp field is missing")


def run_direction(build: Path, remote_build: Path, remote_alias: str,
                  remote_base: Path, run_dir: Path, source: Path,
                  direction: str) -> dict:
    case_dir = run_dir / direction
    case_dir.mkdir()
    remote_root = remote_base / direction
    endpoint = TestEndpoint(build, case_dir, remote_alias, remote_build, remote_root)
    local_process: subprocess.Popen | None = None
    partial_path: Path | None = None
    remote_partial = False
    try:
        endpoint.prepare()
        if direction == "download":
            created = remote_run(remote_alias, [
                "mkdir", "-p", str(endpoint.remote_server_root / "uploaded")
            ])
            if created.returncode:
                raise RuntimeError(f"cannot prepare remote download source: {created.stderr}")
            copy_to_remote(remote_alias, source,
                           str(endpoint.remote_server_root / "uploaded" / "payload.bin"))
        endpoint.start_server()
        endpoint.start_proxy()

        if direction == "upload":
            source_arg = str(source.parent)
            destination_arg = "uploaded"
            checkpoint = endpoint.remote_server_root / "uploaded" / "payload.bin.cpnetflux.manifest"
            remote_checkpoint = checkpoint
        else:
            destination = case_dir / "download"
            destination.mkdir()
            source_arg = "uploaded"
            destination_arg = str(destination)
            checkpoint = destination / "payload.bin.cpnetflux.download.manifest"
            remote_checkpoint = None

        initial_log = case_dir / "initial.log"
        initial_summary = case_dir / "initial.json"
        initial_command = client_command(
            build, direction, endpoint.host, endpoint.control_port,
            source_arg, destination_arg, initial_summary, False,
        )
        with initial_log.open("w", encoding="utf-8") as output:
            local_process = subprocess.Popen(
                initial_command, stdout=output, stderr=subprocess.STDOUT
            )
            fields, partial_chunks = wait_for_stall(
                endpoint, local_process, checkpoint, remote_checkpoint,
                direction, initial_log, FILE_SIZE_MIB,
            )
            local_process.send_signal(signal.SIGTERM)
            interrupted_code = local_process.wait(timeout=15)
        if interrupted_code != -signal.SIGTERM:
            raise RuntimeError(f"{direction}: expected SIGTERM, got {interrupted_code}")

        checkpoint_text = (
            remote_run(remote_alias, ["cat", str(checkpoint)]).stdout
            if remote_checkpoint else checkpoint.read_text(encoding="utf-8")
        )
        if partial_chunks <= 0 or partial_chunks >= FILE_SIZE_MIB:
            raise RuntimeError(f"{direction}: checkpoint was not a non-empty partial transfer")
        if parse_manifest(checkpoint_text).get("state") == "committed":
            raise RuntimeError(f"{direction}: checkpoint was already committed")
        path_hex = parse_manifest(checkpoint_text).get("temp_path_hex")
        if not path_hex:
            raise RuntimeError(f"{direction}: checkpoint has no partial-file path")
        partial_path = Path(bytes.fromhex(path_hex).decode("utf-8"))
        remote_partial = direction == "upload"
        before = signature(endpoint, partial_path, remote=remote_partial)

        corrupted = corrupt_timestamp(checkpoint_text)
        if remote_checkpoint:
            write_script = (
                "from pathlib import Path; import sys; "
                "Path(sys.argv[1]).write_text(sys.argv[2], encoding='utf-8')"
            )
            changed = remote_run(remote_alias, [
                "python3", "-c", write_script, str(checkpoint), corrupted
            ])
            if changed.returncode:
                raise RuntimeError(f"{direction}: remote checkpoint corruption failed: {changed.stderr}")
        else:
            checkpoint.write_text(corrupted, encoding="utf-8")

        endpoint.remove_pause()
        endpoint.restart_proxy()
        resume_log = case_dir / "corrupt-resume.log"
        resume_summary_path = case_dir / "corrupt-resume.json"
        resume_command = client_command(
            build, direction, endpoint.host, endpoint.control_port,
            source_arg, destination_arg, resume_summary_path, True,
        )
        with resume_log.open("w", encoding="utf-8") as output:
            resumed = subprocess.run(
                resume_command, stdout=output, stderr=subprocess.STDOUT,
                text=True, timeout=60, check=False,
            )
        if resumed.returncode == 0:
            raise RuntimeError(f"{direction}: V3 accepted the corrupted checkpoint")
        failure_summary = (
            json.loads(resume_summary_path.read_text(encoding="utf-8"))
            if resume_summary_path.is_file() else {}
        )
        diagnostic = resume_log.read_text(encoding="utf-8", errors="replace")
        diagnostic += json.dumps(failure_summary, sort_keys=True)
        if "manifest" not in diagnostic.lower() and "checksum" not in diagnostic.lower():
            raise RuntimeError(f"{direction}: no checkpoint integrity diagnostic was recorded")
        after_text = (
            remote_run(remote_alias, ["cat", str(checkpoint)]).stdout
            if remote_checkpoint else checkpoint.read_text(encoding="utf-8")
        )
        if parse_manifest(after_text).get("state") == "committed":
            raise RuntimeError(f"{direction}: corrupt checkpoint was marked committed")
        after = signature(endpoint, partial_path, remote=remote_partial)
        if before != after:
            raise RuntimeError(f"{direction}: partial destination changed: {before} -> {after}")

        final_target = (
            endpoint.remote_server_root / "uploaded" / "payload.bin"
            if direction == "upload" else case_dir / "download" / "payload.bin"
        )
        final_exists = (
            remote_run(remote_alias, ["test", "-e", str(final_target)]).returncode == 0
            if direction == "upload" else final_target.exists()
        )
        if final_exists:
            raise RuntimeError(f"{direction}: corrupted data produced a final target")

        return {
            "direction": direction,
            "verified_chunks_before_interruption": partial_chunks,
            "initial_exit_code": interrupted_code,
            "resume_exit_code": resumed.returncode,
            "resume_summary": failure_summary,
            "checkpoint_state_before": fields.get("state"),
            "checkpoint_state_after": parse_manifest(after_text).get("state"),
            "partial_file_before": before,
            "partial_file_after": after,
            "final_target_exists": final_exists,
            "resume_log": str(resume_log),
        }
    finally:
        if local_process is not None and local_process.poll() is None:
            local_process.kill()
            local_process.wait(timeout=5)
        try:
            endpoint.stop()
        finally:
            if partial_path is not None:
                if remote_partial:
                    remote_run(remote_alias, ["rm", "-f", str(partial_path)])
                else:
                    partial_path.unlink(missing_ok=True)
            endpoint.cleanup_payload()


def run(build: Path, evidence_dir: Path, remote_alias: str,
        remote_build: Path, remote_root: Path, file_size_mib: int) -> dict:
    if file_size_mib != FILE_SIZE_MIB:
        raise ValueError(f"acceptance requires exactly {FILE_SIZE_MIB} MiB")
    evidence_dir.mkdir(parents=True, exist_ok=True)
    run_id = time.strftime("%Y%m%dT%H%M%S", time.gmtime())
    run_dir = evidence_dir / run_id
    run_dir.mkdir(exist_ok=False)
    source_dir = run_dir / "source"
    source_dir.mkdir()
    source = source_dir / "payload.bin"
    create_payload(source, file_size_mib)
    try:
        results = [
            run_direction(build, remote_build, remote_alias, remote_root,
                          run_dir, source, direction)
            for direction in ("upload", "download")
        ]
        result = {
            "result": "pass",
            "topology": f"Shenzhen V3 client to {remote_alias} temporary V3 service",
            "payload_size_bytes": file_size_mib * CHUNK_SIZE,
            "directions": results,
            "formal_service_modified": False,
            "evidence_dir": str(run_dir),
        }
        summary_path = run_dir / "summary.json"
        summary_path.write_text(
            json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8"
        )
        print(json.dumps({"result": "pass", "evidence": str(summary_path),
                          "upload_resume_exit_code": results[0]["resume_exit_code"],
                          "download_resume_exit_code": results[1]["resume_exit_code"]},
                         sort_keys=True))
        return result
    except Exception as error:
        (run_dir / "failure.txt").write_text(str(error) + "\n", encoding="utf-8")
        raise
    finally:
        source.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--remote-alias", required=True)
    parser.add_argument("--remote-build-dir", type=Path, required=True)
    parser.add_argument("--remote-root", type=Path, required=True)
    parser.add_argument("--file-size-mib", type=int, default=FILE_SIZE_MIB)
    args = parser.parse_args()
    run(args.build_dir.resolve(), args.evidence_dir.resolve(), args.remote_alias,
        args.remote_build_dir.resolve(), args.remote_root, args.file_size_mib)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
