#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

PROFILES = {
    "single_256MiB": [("single.bin", 256 * 1024 * 1024)],
    "tree_dense_128MiB": [(f"tree-dense/file-{i:04d}.bin", 1024 * 1024) for i in range(128)],
    "tree_mixed_256MiB": [
        *[(f"tree-mixed/file-{i:04d}.bin", 512 * 1024) for i in range(128)],
        *[(f"tree-mixed/file-{128+i:04d}.bin", 4 * 1024 * 1024) for i in range(16)],
        *[(f"tree-mixed/file-{144+i:04d}.bin", 32 * 1024 * 1024) for i in range(4)],
    ],
}
CP_BIN = "/tmp/cpnetflux-v3-worker-state-01c/bin"
CRED = "/tmp/cpnetflux-v3-worker-state-01c/credentials"
CP_SERVER_ROOT = "/tmp/cpnetflux-v3-worker-state-01c/server-root"
GRID_ROOT = "/tmp/cpnetflux-gridftp-gsi-compare-20261014/server-root"
HOST = "192.168.100.2"
CP_PORT = "22622"
REMOTE_GRID = "nodeb.grid:22522"
OUT = Path("/tmp/cpnetflux-v3-worker-state-01c/results")
R750_SSH = ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "Su@192.168.100.2"]


def run(cmd, *, env=None, check=True):
    t0 = time.monotonic()
    p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    elapsed = time.monotonic() - t0
    if check and p.returncode:
        raise RuntimeError(f"command failed rc={p.returncode}: {' '.join(cmd)}\n{p.stdout[-4000:]}")
    return p.stdout, elapsed


def ssh(command):
    return run(R750_SSH + [command])[0]


def remove_remote(root, relative):
    ssh(f"rm -rf -- '{root}/{relative}'")


def tree_hash(root):
    root = Path(root)
    digest = hashlib.sha256()
    count = total = 0
    for path in sorted(p for p in root.rglob("*") if p.is_file() and ".cpnetflux." not in p.name):
        file_digest = hashlib.sha256()
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                file_digest.update(block)
        size = path.stat().st_size
        rel = path.relative_to(root).as_posix()
        digest.update(rel.encode() + b"\0" + str(size).encode() + b"\0" + file_digest.hexdigest().encode() + b"\0")
        count += 1
        total += size
    return digest.hexdigest(), count, total


def make_dataset(root, profile, seed):
    root = Path(root)
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    for relative, size in PROFILES[profile]:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        remaining = size
        counter = 0
        with path.open("wb") as handle:
            while remaining:
                block = hashlib.sha256(f"{seed}:{profile}:{relative}:{counter}".encode()).digest()
                block = (block * ((4096 // len(block)) + 1))[:4096]
                chunk = block[: min(remaining, len(block))]
                handle.write(chunk)
                remaining -= len(chunk)
                counter += 1
    return tree_hash(root)


def cp_args(binary, source, destination, summary):
    return [
        f"{CP_BIN}/{binary}", "--host", HOST, "--port", CP_PORT,
        "--source-dir", str(source), "--dest-dir", destination,
        "--connections", "4", "--file-parallelism", "4",
        "--chunk-size", "1048576", "--buffer-size", "1048576",
        "--checksum", "crc32c", "--checksum-backend", "auto",
        "--control-reuse", "worker", "--data-session-reuse", "tree",
        "--file-scheduling", "dynamic", "--data-pending-window", "2",
        "--compression", "auto", "--control-pipeline-depth", "2", "--scheduler", "off",
        "--auth-mode", "token", "--auth-token-file", f"{CRED}/token",
        "--phase-timing", "on", "--tls-mode", "required", "--tls-server-name", "r750",
        "--tls-ca-file", f"{CRED}/ca.pem", "--tls-cert-file", f"{CRED}/r730.pem",
        "--tls-key-file", f"{CRED}/r730.key", "--data-tls-mode", "required",
        "--json-summary", str(summary),
    ]


def cp_transfer(direction, profile, rep, source, destination, case):
    summary = OUT / f"{case}.json"
    binary = "cpnetflux-tree-upload-client" if direction == "upload" else "cpnetflux-tree-download-client"
    stdout, elapsed = run(cp_args(binary, source, destination, summary))
    document = json.loads(summary.read_text())
    if document.get("result") != "pass":
        raise RuntimeError(f"CPNetFlux did not pass: {document}\n{stdout[-2000:]}")
    return {
        "system": "cpnetflux-v3-01c", "direction": direction, "profile": profile, "rep": rep,
        "case": case, "bytes": document.get("bytes_total"), "files": document.get("file_count"),
        "elapsed_seconds": document.get("elapsed_seconds", elapsed),
        "throughput_gbps": document.get("throughput_gbps"), "summary": str(summary),
        "actual_mode": document.get("actual_mode"), "actual_file_scheduling": document.get("actual_file_scheduling"),
        "control_reuse": document.get("control_reuse_mode"), "pipeline": document.get("control_pipeline_actual_mode"),
        "checksum": document.get("checksum_algorithm"), "phase_timing": document.get("phase_timing_schema"),
    }


def grid_transfer(direction, profile, rep, source, destination, case):
    env = os.environ.copy()
    env["X509_USER_PROXY"] = "/tmp/x509up_u1002"
    url = f"gsiftp://{REMOTE_GRID}/{case}/"
    if direction == "upload":
        command = ["globus-url-copy", "-fast", "-dcpriv", "-cd", "-rp", "-r", "-p", "8", "-cc", "8", "-tcp-bs", "1048576", f"file://{source}/", url]
        measured_root = Path(source)
    else:
        command = ["globus-url-copy", "-fast", "-dcpriv", "-cd", "-rp", "-r", "-p", "8", "-cc", "8", "-tcp-bs", "1048576", url, f"file://{destination}/"]
        measured_root = Path(destination)
    output, elapsed = run(command, env=env)
    digest, count, total = tree_hash(measured_root)
    return {
        "system": "gridftp-gsi", "direction": direction, "profile": profile, "rep": rep,
        "case": case, "bytes": total, "files": count, "tree_hash": digest,
        "elapsed_seconds": elapsed, "throughput_gbps": total * 8 / elapsed / 1e9,
        "command_output": output[-2000:],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    profiles = ["single_256MiB"] if args.smoke else list(PROFILES)
    repetitions = 1 if args.smoke else args.repetitions
    rows = []
    dataset_root = Path("/tmp/cpnetflux-v3-worker-state-01c/datasets")
    for profile in profiles:
        for rep in range(1, repetitions + 1):
            source = dataset_root / f"{profile}-r{rep}"
            _, _, total = make_dataset(source, profile, 20261015 + rep)
            for direction in ("upload", "download"):
                case = f"{profile}-r{rep}-{direction}"
                cp_remote = f"v3-worker-state-01c/{case}/cp"
                grid_remote = f"v3-worker-state-01c/{case}/grid"
                cp_dest = Path(f"/tmp/cpnetflux-v3-worker-state-01c/downloads/{case}/cp")
                grid_dest = Path(f"/tmp/cpnetflux-v3-worker-state-01c/downloads/{case}/grid")
                for path in (cp_dest, grid_dest):
                    shutil.rmtree(path, ignore_errors=True)
                    path.mkdir(parents=True)
                remove_remote(CP_SERVER_ROOT, cp_remote)
                remove_remote(GRID_ROOT, f"v3-worker-state-01c/{case}")
                if direction == "upload":
                    rows.append(cp_transfer("upload", profile, rep, source, cp_remote, case + "-cp-up"))
                    rows.append(grid_transfer("upload", profile, rep, source, grid_remote, case + "-grid-up"))
                else:
                    prep_summary = OUT / f"{case}-cp-prep.json"
                    run(cp_args("cpnetflux-tree-upload-client", source, cp_remote, prep_summary))
                    env = os.environ.copy()
                    env["X509_USER_PROXY"] = "/tmp/x509up_u1002"
                    run(["globus-url-copy", "-fast", "-dcpriv", "-cd", "-rp", "-r", "-p", "8", "-cc", "8", "-tcp-bs", "1048576", f"file://{source}/", f"gsiftp://{REMOTE_GRID}/{grid_remote}/"], env=env)
                    rows.append(cp_transfer("download", profile, rep, cp_remote, cp_dest, case + "-cp-down"))
                    rows.append(grid_transfer("download", profile, rep, Path("/tmp/unused"), grid_dest, grid_remote))
                if direction == "download" and (tree_hash(cp_dest)[2] != total or tree_hash(grid_dest)[2] != total):
                    raise RuntimeError(f"download size mismatch for {profile} repetition {rep}")
                remove_remote(CP_SERVER_ROOT, cp_remote)
                remove_remote(GRID_ROOT, f"v3-worker-state-01c/{case}")
                shutil.rmtree(cp_dest, ignore_errors=True)
                shutil.rmtree(grid_dest, ignore_errors=True)
            shutil.rmtree(source, ignore_errors=True)
    (OUT / "raw.jsonl").write_text("\n".join(json.dumps(row, ensure_ascii=False) for row in rows) + "\n")
    print(json.dumps({"samples": len(rows), "profiles": profiles, "repetitions": repetitions, "result": "pass", "output": str(OUT / "raw.jsonl")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
