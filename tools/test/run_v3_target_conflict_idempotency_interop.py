#!/usr/bin/env python3
"""Cross-cloud V3 target-conflict and repeated-resume acceptance."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_v3_target_conflict_idempotency_smoke import run_client, assert_conflict
from run_v3_timeout_resume_smoke import (
    CHUNK_SIZE, TestEndpoint, copy_to_remote, create_payload,
    parse_manifest, remote_run, sha256_file,
)


def signature(endpoint: TestEndpoint, path: Path, remote: bool) -> dict:
    if not remote:
        stat = path.stat()
        return {"sha256": sha256_file(path), "size": stat.st_size, "inode": stat.st_ino}
    result = remote_run(endpoint.remote_alias, ["stat", "-c", "%s %i", str(path)])
    if result.returncode:
        raise RuntimeError(f"remote target stat failed: {result.stderr}")
    size, inode = result.stdout.split()
    return {"sha256": endpoint.remote_sha256(path), "size": int(size), "inode": int(inode)}


def manifest(endpoint: TestEndpoint, path: Path, remote: bool) -> dict:
    if not remote:
        return parse_manifest(path.read_text(encoding="utf-8"))
    result = remote_run(endpoint.remote_alias, ["cat", str(path)])
    if result.returncode:
        raise RuntimeError(f"remote manifest read failed: {result.stderr}")
    return parse_manifest(result.stdout)


def run_case(build: Path, remote_build: Path, alias: str, remote_base: Path,
             run_dir: Path, source: Path, direction: str, case: str) -> dict:
    case_name = f"{direction}-{case}"
    root = run_dir / case_name
    root.mkdir()
    endpoint = TestEndpoint(build, root, alias, remote_build, remote_base / case_name)
    remote_target = direction == "upload"
    target = None
    try:
        endpoint.prepare()
        remote_payload = endpoint.remote_server_root / "uploaded" / "payload.bin"
        made = remote_run(alias, ["mkdir", "-p", str(remote_payload.parent)])
        if made.returncode:
            raise RuntimeError(made.stderr)
        if direction == "download":
            copy_to_remote(alias, source, str(remote_payload))
            target_dir = root / "download"
            target_dir.mkdir()
            target = target_dir / "payload.bin"
            source_arg, destination_arg = "uploaded", str(target_dir)
        else:
            target = remote_payload
            source_arg, destination_arg = str(source.parent), "uploaded"
        if case == "conflict":
            sentinel = root / "sentinel.txt"
            sentinel.write_bytes(b"keep existing user content\n")
            if remote_target:
                copy_to_remote(alias, sentinel, str(target))
            else:
                target.write_bytes(sentinel.read_bytes())
        endpoint.start_server()
        endpoint.start_proxy()

        if case == "conflict":
            before = signature(endpoint, target, remote_target)
            result, summary, log = run_client(
                endpoint, build, root, direction, source_arg, destination_arg,
                resume=False, label="conflict",
            )
            after = signature(endpoint, target, remote_target)
            assert_conflict(result, summary, log, before["sha256"], after["sha256"], case_name)
            if before != after:
                raise RuntimeError(f"{case_name}: target size/inode changed")
            return {"case": case_name, "exit_code": result.returncode,
                    "before": before, "after": after, "target_unchanged": True,
                    "summary": summary, "log": str(log)}

        initial, first, first_log = run_client(
            endpoint, build, root, direction, source_arg, destination_arg,
            resume=False, label="initial-submit",
        )
        if initial.returncode or first.get("result") != "pass":
            raise RuntimeError(f"{case_name}: initial transfer failed: {first}")
        before = signature(endpoint, target, remote_target)
        if before["sha256"] != sha256_file(source):
            raise RuntimeError(f"{case_name}: target differs from source")
        checkpoint = Path(str(target) + (".cpnetflux.manifest" if remote_target
                                        else ".cpnetflux.download.manifest"))
        initial_manifest = manifest(endpoint, checkpoint, remote_target)
        repeated, summary, log = run_client(
            endpoint, build, root, direction, source_arg, destination_arg,
            resume=True, label="repeated-resume",
        )
        if repeated.returncode or summary.get("result") != "pass":
            raise RuntimeError(f"{case_name}: repeated resume failed: {summary}")
        after = signature(endpoint, target, remote_target)
        final_manifest = manifest(endpoint, checkpoint, remote_target)
        if before != after:
            raise RuntimeError(f"{case_name}: repeated resume changed/replaced target")
        if final_manifest.get("state") != "committed" or initial_manifest.get("state") != "committed":
            raise RuntimeError(f"{case_name}: committed state changed")
        transfer_id = initial_manifest.get("transfer_id")
        if not transfer_id or transfer_id != final_manifest.get("transfer_id"):
            raise RuntimeError(f"{case_name}: transfer identity changed or missing")
        if summary.get("actual_transport") != "v3" or summary.get("fallback_reason", ""):
            raise RuntimeError(f"{case_name}: invalid V3 runtime summary")
        if int(summary.get("bytes_transferred", -1)) != 0:
            raise RuntimeError(f"{case_name}: repeated resume retransmitted payload")
        if int(summary.get("completed_files", -1)) > 1:
            raise RuntimeError(f"{case_name}: repeated file completion was overcounted")
        return {"case": case_name, "initial_exit_code": initial.returncode,
                "repeat_exit_code": repeated.returncode, "before": before, "after": after,
                "target_unchanged": True, "transfer_id_unchanged": True,
                "checkpoint_state": final_manifest.get("state"), "summary": summary,
                "first_log": str(first_log), "repeat_log": str(log)}
    finally:
        endpoint.stop()
        endpoint.cleanup_payload()
        if target is not None and not remote_target:
            target.unlink(missing_ok=True)


def run(build: Path, evidence: Path, alias: str, remote_build: Path,
        remote_root: Path) -> dict:
    evidence.mkdir(parents=True, exist_ok=True)
    root = evidence / time.strftime("%Y%m%dT%H%M%S", time.gmtime())
    root.mkdir(exist_ok=False)
    source_dir = root / "source"
    source_dir.mkdir()
    source = source_dir / "payload.bin"
    create_payload(source, 8)
    try:
        results = [run_case(build, remote_build, alias, remote_root,
                            root, source, direction, case)
                   for case in ("conflict", "repeat")
                   for direction in ("upload", "download")]
        result = {"result": "pass", "payload_bytes": 8 * CHUNK_SIZE, "cases": results,
                  "formal_service_modified": False, "evidence_dir": str(root)}
        summary = root / "summary.json"
        summary.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
        print(json.dumps({"result": "pass", "cases": len(results),
                          "evidence": str(summary)}, sort_keys=True))
        return result
    except Exception as error:
        (root / "failure.txt").write_text(str(error) + "\n", encoding="utf-8")
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
    args = parser.parse_args()
    run(args.build_dir.resolve(), args.evidence_dir.resolve(), args.remote_alias,
        args.remote_build_dir.resolve(), args.remote_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
