#!/usr/bin/env python3
import argparse
import json
import os
import shlex
import subprocess
import sys
import time
from pathlib import Path

from tree_smoke_common import file_sha256, tree_hash


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("CPNETFLUX_SSH_PASSWORD") or os.environ.get("SSHPASS"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_local(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if check and completed.returncode != 0:
        raise RuntimeError("$ " + " ".join(command) + "\n" + completed.stdout + completed.stderr)
    return completed


def run_remote(remote: str, command: str, *, check: bool = True) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("CPNETFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["CPNETFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        ssh_prefix(remote) + [command],
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    if check and completed.returncode != 0:
        raise RuntimeError("$ " + command + "\n" + completed.stdout + completed.stderr)
    return completed


def make_remote_tree(remote: str, root: str) -> None:
    script = f"""
set -e
rm -rf {shlex.quote(root)}
mkdir -p {shlex.quote(root)}/nested/deeper
printf alpha > {shlex.quote(root)}/alpha.txt
: > {shlex.quote(root)}/empty.bin
python3 - <<'PY'
from pathlib import Path
root = Path({root!r})
(root / 'nested' / 'beta.bin').write_bytes(bytes(i % 251 for i in range(131072)))
(root / 'nested' / 'deeper' / 'gamma.bin').write_bytes(bytes((i * 17) % 251 for i in range(1048593)))
PY
"""
    run_remote(remote, script)


def remote_tree_hash(remote: str, root: str) -> tuple[str, int, int]:
    script = f"""
python3 - <<'PY'
import hashlib, json
from pathlib import Path
root = Path({root!r})
digest = hashlib.sha256()
count = 0
total = 0
for path in sorted(p for p in root.rglob('*') if p.is_file()):
    rel = path.relative_to(root).as_posix()
    if '.cpnetflux.' in rel or '.part.' in rel:
        continue
    h = hashlib.sha256(path.read_bytes()).hexdigest()
    size = path.stat().st_size
    count += 1
    total += size
    digest.update(rel.encode() + b'\\0')
    digest.update(str(size).encode() + b'\\0')
    digest.update(h.encode() + b'\\0')
print(json.dumps({{'hash': digest.hexdigest(), 'count': count, 'total': total}}))
PY
"""
    completed = run_remote(remote, script)
    data = json.loads(completed.stdout.strip())
    return data["hash"], int(data["count"]), int(data["total"])


def start_server(args: argparse.Namespace, root: str, log: Path) -> subprocess.Popen:
    Path(root).mkdir(parents=True, exist_ok=True)
    command = [
        str(Path(args.local_build_dir) / "cpnetflux-gridftp-server"),
        "--host",
        args.server_host,
        "--port",
        str(args.control_port),
        "--root",
        root,
        "--data-port-base",
        str(args.data_port_base),
        "--connections",
        str(args.connections),
        "--chunk-size",
        str(args.chunk_size),
        "--buffer-size",
        str(args.buffer_size),
        "--checksum",
        args.checksum,
        "--checksum-backend",
        args.checksum_backend,
    ]
    if args.auth_mode == "token":
        command.extend(["--auth-mode", "token", "--auth-token-file", args.auth_token_file])
    handle = log.open("w", encoding="utf-8")
    process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
    handle.close()
    time.sleep(1.0)
    return process


def stop_server(process: subprocess.Popen, log: Path) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    if process.returncode not in (0, -15, -9):
        raise RuntimeError(log.read_text(encoding="utf-8", errors="replace"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private CPNetFlux tree upload/download smoke.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--server-host", required=True)
    parser.add_argument("--local-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--connections", type=int, default=2)
    parser.add_argument("--chunk-size", type=int, default=1048576)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--auth-mode", choices=["anonymous", "token"], default="anonymous")
    parser.add_argument("--auth-token-file", default="")
    parser.add_argument("--output-dir", default="tools/perf/results")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    local_root = f"/tmp/cpnetflux-tree-private-root-{timestamp}"
    local_resume_root = f"/tmp/cpnetflux-tree-private-root-resume-{timestamp}"
    remote_source = f"/tmp/cpnetflux-tree-private-source-{timestamp}"
    remote_download = f"/tmp/cpnetflux-tree-private-download-{timestamp}"
    remote_resume_download = f"/tmp/cpnetflux-tree-private-download-resume-{timestamp}"
    server_log = output_dir / f"{timestamp}_gridftp-tree-private-server.log"
    result_json = output_dir / f"{timestamp}_gridftp-tree-private.json"

    make_remote_tree(args.remote, remote_source)
    process = start_server(args, local_root, server_log)
    try:
        upload_cmd = [
            f"{args.remote_build_dir}/cpnetflux-tree-upload-client",
            "--host",
            args.server_host,
            "--port",
            str(args.control_port),
            "--source-dir",
            remote_source,
            "--dest-dir",
            "dataset",
            "--connections",
            str(args.connections),
            "--checksum",
            args.checksum,
            "--checksum-backend",
            args.checksum_backend,
        ]
        if args.auth_mode == "token":
            upload_cmd.extend(["--auth-mode", "token", "--auth-token-file", args.auth_token_file])
        run_remote(args.remote, " ".join(shlex.quote(part) for part in upload_cmd))
        remote_hash, count, total = remote_tree_hash(args.remote, remote_source)
        local_hash, _, _ = tree_hash(Path(local_root) / "dataset")
        if remote_hash != local_hash:
            raise RuntimeError("private tree upload hash mismatch")

        download_cmd = [
            f"{args.remote_build_dir}/cpnetflux-tree-download-client",
            "--host",
            args.server_host,
            "--port",
            str(args.control_port),
            "--source-dir",
            "dataset",
            "--dest-dir",
            remote_download,
            "--connections",
            str(args.connections),
            "--checksum",
            args.checksum,
            "--checksum-backend",
            args.checksum_backend,
        ]
        if args.auth_mode == "token":
            download_cmd.extend(["--auth-mode", "token", "--auth-token-file", args.auth_token_file])
        run_remote(args.remote, " ".join(shlex.quote(part) for part in download_cmd))
        downloaded_hash, _, _ = remote_tree_hash(args.remote, remote_download)
        if downloaded_hash != local_hash:
            raise RuntimeError("private tree download hash mismatch")

        resume_upload_cmd = [
            f"{args.remote_build_dir}/cpnetflux-tree-upload-client",
            "--host",
            args.server_host,
            "--port",
            str(args.control_port),
            "--source-dir",
            remote_source,
            "--dest-dir",
            "dataset-resume",
            "--connections",
            str(args.connections),
            "--checksum",
            args.checksum,
            "--checksum-backend",
            args.checksum_backend,
            "--max-files",
            "1",
        ]
        if args.auth_mode == "token":
            resume_upload_cmd.extend(["--auth-mode", "token", "--auth-token-file", args.auth_token_file])
        run_remote(args.remote, " ".join(shlex.quote(part) for part in resume_upload_cmd), check=False)
        resume_upload_cmd.remove("--max-files")
        resume_upload_cmd.remove("1")
        resume_upload_cmd.append("--resume")
        run_remote(args.remote, " ".join(shlex.quote(part) for part in resume_upload_cmd))
        local_resume_hash, _, _ = tree_hash(Path(local_root) / "dataset-resume")
        if local_resume_hash != remote_hash:
            raise RuntimeError("private tree upload resume hash mismatch")

        resume_download_cmd = [
            f"{args.remote_build_dir}/cpnetflux-tree-download-client",
            "--host",
            args.server_host,
            "--port",
            str(args.control_port),
            "--source-dir",
            "dataset",
            "--dest-dir",
            remote_resume_download,
            "--connections",
            str(args.connections),
            "--checksum",
            args.checksum,
            "--checksum-backend",
            args.checksum_backend,
            "--max-files",
            "1",
        ]
        if args.auth_mode == "token":
            resume_download_cmd.extend(["--auth-mode", "token", "--auth-token-file", args.auth_token_file])
        run_remote(args.remote, " ".join(shlex.quote(part) for part in resume_download_cmd), check=False)
        resume_download_cmd.remove("--max-files")
        resume_download_cmd.remove("1")
        resume_download_cmd.append("--resume")
        run_remote(args.remote, " ".join(shlex.quote(part) for part in resume_download_cmd))
        resume_download_hash, _, _ = remote_tree_hash(args.remote, remote_resume_download)
        if resume_download_hash != local_hash:
            raise RuntimeError("private tree download resume hash mismatch")

        result = {
            "result": "pass",
            "file_count": count,
            "total_bytes": total,
            "source_tree_hash": remote_hash,
            "server_tree_hash": local_hash,
            "download_tree_hash": downloaded_hash,
            "upload_resume_tree_hash": local_resume_hash,
            "download_resume_tree_hash": resume_download_hash,
            "server_log": str(server_log),
        }
        result_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(json.dumps(result, sort_keys=True))
    finally:
        stop_server(process, server_log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
