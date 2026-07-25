#!/usr/bin/env python3
"""Synchronize and verify alpha release artifacts listed in a manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

import remote_auth


BUILD_LIKE_NAMES = {"build", "out", "dist", ".cache", "Testing", "CMakeFiles", "_deps"}
BUILD_FILES = {
    "CMakeCache.txt",
    "CTestTestfile.cmake",
    "build.ninja",
    "cmake_install.cmake",
    "compile_commands.json",
    "install_manifest.txt",
    ".ninja_deps",
    ".ninja_log",
}
BLOCKED_SUFFIXES = {
    ".o",
    ".a",
    ".so",
    ".d",
    ".pyc",
    ".bin",
    ".exe",
    ".elf",
    ".pem",
    ".key",
    ".p12",
    ".crt",
    ".cer",
}
SAFE_SUFFIXES = {
    ".md",
    ".py",
    ".json",
    ".jsonl",
    ".csv",
    ".log",
    ".txt",
    ".sh",
    ".cmake",
    ".clang-tidy",
    ".gitignore",
}
SENSITIVE_PARTS = {"AGENTS.md", ".env", ".env.local", ".envrc", "id_rsa", "id_dsa", "id_ed25519"}
SENSITIVE_WORDS = {"password", "passwd", "secret", "token", "cookie", "credential"}


@dataclass
class ManifestArtifact:
    path: str
    size: int
    sha256: str
    type: str
    required: bool


@dataclass
class ArtifactStatus:
    path: str
    required: bool
    local_exists: bool
    remote_exists: bool
    local_size: int
    remote_size: int
    local_sha256: str
    remote_sha256: str
    status: str
    synced: bool = False
    reason: str = ""


def is_build_like_part(part: str) -> bool:
    return part in BUILD_LIKE_NAMES or part.startswith("build-") or part.startswith("cmake-build-")


def validate_artifact_path(path_text: str) -> str:
    if not path_text:
        raise ValueError("empty artifact path")
    path = Path(path_text)
    if path.is_absolute():
        raise ValueError(f"absolute artifact path is not allowed: {path_text}")
    if ".." in path.parts:
        raise ValueError(f"path traversal is not allowed: {path_text}")
    for part in path.parts:
        if part in SENSITIVE_PARTS:
            raise ValueError(f"sensitive artifact path is not allowed: {path_text}")
        if is_build_like_part(part):
            raise ValueError(f"build-like artifact path is not allowed: {path_text}")
        lowered = part.lower()
        if any(word in lowered for word in SENSITIVE_WORDS):
            raise ValueError(f"sensitive artifact path is not allowed: {path_text}")
    if path.name in BUILD_FILES:
        raise ValueError(f"build artifact file is not allowed: {path_text}")
    if path.suffix in BLOCKED_SUFFIXES:
        raise ValueError(f"binary or credential artifact suffix is not allowed: {path_text}")
    return path.as_posix()


def is_probably_binary(path: Path) -> bool:
    try:
        chunk = path.read_bytes()[:8192]
    except OSError:
        return False
    return b"\0" in chunk


def validate_local_artifact_file(path: Path, relative_path: str) -> None:
    if not path.exists():
        return
    if not path.is_file():
        raise ValueError(f"artifact is not a regular file: {relative_path}")
    if path.suffix and path.suffix not in SAFE_SUFFIXES:
        raise ValueError(f"unknown artifact suffix is not allowed: {relative_path}")
    if is_probably_binary(path):
        raise ValueError(f"unknown binary artifact is not allowed: {relative_path}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact_type_for(path: str) -> str:
    if path.endswith("_alpha-artifacts.json"):
        return "artifact_manifest"
    if path.endswith("_alpha-release-gate.json"):
        return "gate_json"
    if path.endswith(".jsonl"):
        return "event_log"
    if path.startswith("docs/release/"):
        return "release_doc"
    if path.startswith("docs/"):
        return "doc"
    if path.startswith("tools/release/"):
        return "release_tool"
    if path.endswith("-summary.csv"):
        return "matrix_summary_csv"
    if path.endswith(".csv"):
        return "matrix_raw_csv"
    if path.endswith(".log"):
        return "sidecar_log"
    return "other"


def manifest_entry_for(root: Path, relative_path: str, *, required: bool = True) -> ManifestArtifact:
    normalized = validate_artifact_path(relative_path)
    path = root / normalized
    validate_local_artifact_file(path, normalized)
    if not path.is_file():
        raise FileNotFoundError(f"required artifact does not exist: {normalized}")
    return ManifestArtifact(
        path=normalized,
        size=path.stat().st_size,
        sha256=sha256_file(path),
        type=artifact_type_for(normalized),
        required=required,
    )


def load_manifest(manifest_path: Path, local_root: Path) -> tuple[dict[str, object], list[ManifestArtifact]]:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    artifacts: list[ManifestArtifact] = []
    for item in data.get("artifacts", []):
        if not isinstance(item, dict):
            raise ValueError("manifest artifact must be an object")
        relative = validate_artifact_path(str(item.get("path", "")))
        local_path = local_root / relative
        validate_local_artifact_file(local_path, relative)
        artifacts.append(
            ManifestArtifact(
                path=relative,
                size=int(item.get("size", -1)),
                sha256=str(item.get("sha256", "")),
                type=str(item.get("type", artifact_type_for(relative))),
                required=bool(item.get("required", True)),
            )
        )
    manifest_relative = validate_artifact_path(manifest_path.resolve().relative_to(local_root).as_posix())
    manifest_entry = manifest_entry_for(local_root, manifest_relative, required=True)
    if all(artifact.path != manifest_entry.path for artifact in artifacts):
        artifacts.append(manifest_entry)
    return data, artifacts


def ssh_prefix(remote: str) -> list[str]:
    return remote_auth.ssh_prefix(remote)


def run_remote(remote: str, command: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ssh_prefix(remote) + [command],
        text=True,
        capture_output=True,
        check=False,
        env=remote_auth.command_env(remote),
    )


def run_remote_with_input(remote: str, command: str, payload: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ssh_prefix(remote) + [command],
        input=payload,
        text=True,
        capture_output=True,
        check=False,
        env=remote_auth.command_env(remote),
    )


def remote_stats_batch(remote: str, remote_root: str, relative_paths: list[str]) -> dict[str, tuple[bool, int, str]]:
    if not relative_paths:
        return {}
    script = r"""
import hashlib
import json
import os
import sys

payload = json.load(sys.stdin)
root = payload["root"]
out = {}
for relative in payload["paths"]:
    path = os.path.join(root, relative)
    if not os.path.isfile(path):
        out[relative] = {"exists": False, "size": 0, "sha256": ""}
        continue
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    out[relative] = {
        "exists": True,
        "size": os.path.getsize(path),
        "sha256": digest.hexdigest(),
    }
print(json.dumps(out, sort_keys=True))
"""
    command = "python3 -c " + shlex.quote(script)
    payload = json.dumps({"root": remote_root.rstrip("/"), "paths": relative_paths})
    completed = run_remote_with_input(remote, command, payload)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "remote artifact batch stat failed")
    data = json.loads(completed.stdout or "{}")
    result: dict[str, tuple[bool, int, str]] = {}
    for relative in relative_paths:
        item = data.get(relative, {})
        result[relative] = (
            bool(item.get("exists", False)),
            int(item.get("size", 0)),
            str(item.get("sha256", "")),
        )
    return result


def remote_stat(remote: str | None, remote_root: str, relative_path: str, remote_local_root: Path | None = None) -> tuple[bool, int, str]:
    if remote_local_root is not None:
        path = remote_local_root / relative_path
        if not path.is_file():
            return False, 0, ""
        return True, path.stat().st_size, sha256_file(path)
    if remote is None:
        raise ValueError("remote is required when remote_local_root is not used")
    remote_path = f"{remote_root.rstrip('/')}/{relative_path}"
    command = (
        f"if test -f {shlex.quote(remote_path)}; then "
        f"stat -c '%s' {shlex.quote(remote_path)} && "
        f"sha256sum {shlex.quote(remote_path)} | awk '{{print $1}}'; "
        "else exit 2; fi"
    )
    completed = run_remote(remote, command)
    if completed.returncode != 0:
        return False, 0, ""
    lines = completed.stdout.strip().splitlines()
    if len(lines) < 2:
        return False, 0, ""
    return True, int(lines[0]), lines[1].strip()


def copy_local_artifacts(local_root: Path, remote_local_root: Path, paths: list[str]) -> int:
    count = 0
    for relative in paths:
        source = local_root / relative
        target = remote_local_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
        count += 1
    return count


def rsync_artifacts(remote: str, remote_root: str, local_root: Path, paths: list[str]) -> tuple[int, str]:
    if not paths:
        return 0, "no artifacts to sync\n"
    command = [
        "rsync",
        "-az",
        "-e",
        "ssh -o StrictHostKeyChecking=no",
        "--relative",
        *paths,
        f"{remote}:{remote_root.rstrip('/')}/",
    ]
    command, env = remote_auth.wrap_with_sshpass(remote, command, root=local_root)
    completed = subprocess.run(
        command,
        cwd=local_root,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    return completed.returncode, "$ " + " ".join(command) + "\n\n" + completed.stdout + completed.stderr


def check_artifacts(
    *,
    artifacts: list[ManifestArtifact],
    local_root: Path,
    remote: str | None,
    remote_root: str,
    remote_local_root: Path | None = None,
) -> list[ArtifactStatus]:
    checks: list[ArtifactStatus] = []
    remote_batch: dict[str, tuple[bool, int, str]] = {}
    if remote_local_root is None and remote is not None:
        remote_batch = remote_stats_batch(remote, remote_root, [artifact.path for artifact in artifacts])
    for artifact in artifacts:
        local_path = local_root / artifact.path
        local_exists = local_path.is_file()
        local_size = local_path.stat().st_size if local_exists else 0
        local_hash = sha256_file(local_path) if local_exists else ""
        if remote_local_root is not None:
            remote_exists, remote_size, remote_hash = remote_stat(remote, remote_root, artifact.path, remote_local_root)
        else:
            remote_exists, remote_size, remote_hash = remote_batch.get(artifact.path, (False, 0, ""))
        if not local_exists:
            status = "missing_local"
        elif not remote_exists:
            status = "missing"
        elif local_size != remote_size or local_hash != remote_hash or artifact.sha256 != local_hash:
            status = "mismatch"
        else:
            status = "match"
        checks.append(
            ArtifactStatus(
                path=artifact.path,
                required=artifact.required,
                local_exists=local_exists,
                remote_exists=remote_exists,
                local_size=local_size,
                remote_size=remote_size,
                local_sha256=local_hash,
                remote_sha256=remote_hash,
                status=status,
            )
        )
    return checks


def summarize(statuses: list[ArtifactStatus], *, mode: str, synced: int = 0, rsync_log: str = "") -> dict[str, object]:
    missing = sum(1 for status in statuses if status.status in {"missing", "missing_local"})
    mismatch = sum(1 for status in statuses if status.status == "mismatch")
    skipped = sum(1 for status in statuses if not status.required)
    ok = missing == 0 and mismatch == 0
    summary = {
        "mode": mode,
        "checked": len(statuses),
        "synced": synced,
        "missing": missing,
        "mismatch": mismatch,
        "skipped": skipped,
        "status": "pass" if ok else ("would_sync" if mode == "dry-run" else "fail"),
        "artifacts": [asdict(status) for status in statuses],
    }
    if rsync_log:
        summary["rsync_log"] = rsync_log
    return summary


def count_missing(statuses: list[ArtifactStatus]) -> int:
    return sum(1 for status in statuses if status.status in {"missing", "missing_local"})


def count_mismatch(statuses: list[ArtifactStatus]) -> int:
    return sum(1 for status in statuses if status.status == "mismatch")


def add_pre_post_summary(
    summary: dict[str, object],
    *,
    initial: list[ArtifactStatus],
    final: list[ArtifactStatus],
) -> dict[str, object]:
    post_missing = count_missing(final)
    post_mismatch = count_mismatch(final)
    summary["pre_sync_missing"] = count_missing(initial)
    summary["pre_sync_mismatch"] = count_mismatch(initial)
    summary["post_sync_missing"] = post_missing
    summary["post_sync_mismatch"] = post_mismatch
    summary["post_sync_status"] = "pass" if post_missing == 0 and post_mismatch == 0 else "fail"
    return summary


def sync_from_manifest(
    *,
    manifest_path: Path,
    remote: str | None,
    local_root: Path,
    remote_root: str,
    mode: str,
    remote_local_root: Path | None = None,
) -> dict[str, object]:
    _, artifacts = load_manifest(manifest_path, local_root)
    initial = check_artifacts(
        artifacts=artifacts,
        local_root=local_root,
        remote=remote,
        remote_root=remote_root,
        remote_local_root=remote_local_root,
    )
    to_sync = [
        status.path
        for status in initial
        if status.required and status.local_exists and status.status in {"missing", "mismatch"}
    ]
    if mode == "verify-only":
        return add_pre_post_summary(summarize(initial, mode=mode), initial=initial, final=initial)
    if mode == "dry-run":
        return add_pre_post_summary(
            summarize(initial, mode=mode, synced=len(to_sync)), initial=initial, final=initial
        )
    if mode != "sync":
        raise ValueError(f"unsupported mode: {mode}")
    rsync_log = ""
    if remote_local_root is not None:
        synced = copy_local_artifacts(local_root, remote_local_root, to_sync)
    else:
        if remote is None:
            raise ValueError("remote is required for sync")
        returncode, rsync_log = rsync_artifacts(remote, remote_root, local_root, to_sync)
        if returncode != 0:
            summary = summarize(initial, mode=mode, synced=0, rsync_log=rsync_log)
            summary["status"] = "fail"
            return add_pre_post_summary(summary, initial=initial, final=initial)
        synced = len(to_sync)
    final = check_artifacts(
        artifacts=artifacts,
        local_root=local_root,
        remote=remote,
        remote_root=remote_root,
        remote_local_root=remote_local_root,
    )
    for status in final:
        status.synced = status.path in set(to_sync)
    return add_pre_post_summary(
        summarize(final, mode=mode, synced=synced, rsync_log=rsync_log),
        initial=initial,
        final=final,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Synchronize release artifacts listed in an alpha manifest.")
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--remote", required=True)
    parser.add_argument("--local-root", default=".")
    parser.add_argument("--remote-root", default="/root/projects/GridFlux")
    parser.add_argument("--json-output")
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--verify-only", action="store_true")
    modes.add_argument("--sync", action="store_true")
    modes.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    local_root = Path(args.local_root).resolve()
    manifest = Path(args.manifest)
    if not manifest.is_absolute():
        manifest = local_root / manifest
    mode = "sync" if args.sync else "dry-run" if args.dry_run else "verify-only"
    try:
        summary = sync_from_manifest(
            manifest_path=manifest.resolve(),
            remote=args.remote,
            local_root=local_root,
            remote_root=args.remote_root,
            mode=mode,
        )
    except Exception as exc:  # noqa: BLE001 - command-line tool should report all validation failures.
        summary = {
            "mode": mode,
            "checked": 0,
            "synced": 0,
            "missing": 0,
            "mismatch": 0,
            "skipped": 0,
            "status": "fail",
            "error": str(exc),
            "artifacts": [],
        }

    if args.json_output:
        output = Path(args.json_output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "artifact_sync "
        f"mode={summary.get('mode')} "
        f"checked={summary.get('checked')} "
        f"synced={summary.get('synced')} "
        f"missing={summary.get('missing')} "
        f"mismatch={summary.get('mismatch')} "
        f"pre_sync_missing={summary.get('pre_sync_missing')} "
        f"pre_sync_mismatch={summary.get('pre_sync_mismatch')} "
        f"post_sync_missing={summary.get('post_sync_missing')} "
        f"post_sync_mismatch={summary.get('post_sync_mismatch')} "
        f"post_sync_status={summary.get('post_sync_status')} "
        f"status={summary.get('status')}"
    )
    if summary.get("status") not in {"pass", "would_sync"}:
        if summary.get("error"):
            print(summary["error"], file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
