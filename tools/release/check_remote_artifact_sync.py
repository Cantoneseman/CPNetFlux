#!/usr/bin/env python3
"""Check that selected release artifacts are present and identical on a remote tree."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass, asdict
from pathlib import Path

import remote_auth
import sync_remote_artifacts


LOG_FIELD_SUFFIX = "_log"
KNOWN_LOG_FIELDS = {
    "server_log",
    "client_log",
    "server_env_before_log",
    "server_env_after_log",
    "client_env_before_log",
    "client_env_after_log",
}


@dataclass
class ArtifactCheck:
    path: str
    local_exists: bool
    remote_exists: bool
    local_sha256: str
    remote_sha256: str
    status: str


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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalize_artifact_path(value: str, local_root: Path) -> str | None:
    if not value:
        return None
    path = Path(value)
    if path.is_absolute():
        try:
            return sync_remote_artifacts.validate_artifact_path(path.resolve().relative_to(local_root).as_posix())
        except ValueError:
            return None
    if ".." in path.parts:
        return None
    try:
        return sync_remote_artifacts.validate_artifact_path(path.as_posix())
    except ValueError:
        return None


def paths_from_csv(csv_path: Path, local_root: Path) -> set[str]:
    result: set[str] = {str(csv_path.relative_to(local_root)) if csv_path.is_absolute() else str(csv_path)}
    if not csv_path.exists():
        return result
    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            for field, value in row.items():
                if field in KNOWN_LOG_FIELDS or field.endswith(LOG_FIELD_SUFFIX):
                    normalized = normalize_artifact_path(value.strip(), local_root)
                    if normalized:
                        result.add(normalized)
    return result


def collect_artifact_paths(paths: list[str], csvs: list[str], local_root: Path) -> list[str]:
    result: set[str] = set()
    for path in paths:
        normalized = normalize_artifact_path(path, local_root)
        if normalized:
            result.add(normalized)
    for csv_text in csvs:
        normalized = normalize_artifact_path(csv_text, local_root)
        if not normalized:
            continue
        result.update(paths_from_csv(local_root / normalized, local_root))
    return sorted(result)


def remote_sha256(remote: str, remote_root: str, relative_path: str) -> tuple[bool, str]:
    remote_path = f"{remote_root.rstrip('/')}/{relative_path}"
    command = (
        f"if test -f {shlex.quote(remote_path)}; then "
        f"sha256sum {shlex.quote(remote_path)} | awk '{{print $1}}'; "
        "else exit 2; fi"
    )
    completed = run_remote(remote, command)
    if completed.returncode != 0:
        return False, ""
    return True, completed.stdout.strip().splitlines()[0] if completed.stdout.strip() else ""


def check_artifacts(
    *,
    remote: str,
    local_root: Path,
    remote_root: str,
    relative_paths: list[str],
) -> list[ArtifactCheck]:
    checks: list[ArtifactCheck] = []
    remote_batch = sync_remote_artifacts.remote_stats_batch(remote, remote_root, relative_paths)
    for relative_path in relative_paths:
        local_path = local_root / relative_path
        local_exists = local_path.is_file()
        local_hash = sha256_file(local_path) if local_exists else ""
        remote_exists, _remote_size, remote_hash = remote_batch.get(relative_path, (False, 0, ""))
        if not local_exists:
            status = "missing_local"
        elif not remote_exists:
            status = "missing_remote"
        elif local_hash != remote_hash:
            status = "mismatch"
        else:
            status = "match"
        checks.append(
            ArtifactCheck(
                path=relative_path,
                local_exists=local_exists,
                remote_exists=remote_exists,
                local_sha256=local_hash,
                remote_sha256=remote_hash,
                status=status,
            )
        )
    return checks


def checks_from_manifest(
    *,
    manifest_path: Path,
    remote: str,
    local_root: Path,
    remote_root: str,
) -> list[ArtifactCheck]:
    _, artifacts = sync_remote_artifacts.load_manifest(manifest_path, local_root)
    statuses = sync_remote_artifacts.check_artifacts(
        artifacts=artifacts,
        local_root=local_root,
        remote=remote,
        remote_root=remote_root,
    )
    return [
        ArtifactCheck(
            path=status.path,
            local_exists=status.local_exists,
            remote_exists=status.remote_exists,
            local_sha256=status.local_sha256,
            remote_sha256=status.remote_sha256,
            status="match" if status.status == "match" else status.status,
        )
        for status in statuses
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description="Check release artifact sync between local and remote trees.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--local-root", default=".")
    parser.add_argument("--remote-root", default="/root/projects/GridFlux")
    parser.add_argument("--manifest", help="alpha artifact manifest path")
    parser.add_argument("--path", action="append", default=[], help="relative artifact path to verify")
    parser.add_argument("--csv", action="append", default=[], help="CSV path; referenced *_log files are verified too")
    parser.add_argument("--json-output", help="optional machine-readable report path")
    args = parser.parse_args()

    local_root = Path(args.local_root).resolve()
    if args.manifest:
        manifest_path = Path(args.manifest)
        if not manifest_path.is_absolute():
            manifest_path = local_root / manifest_path
        try:
            checks = checks_from_manifest(
                manifest_path=manifest_path.resolve(),
                remote=args.remote,
                local_root=local_root,
                remote_root=args.remote_root,
            )
        except Exception as exc:  # noqa: BLE001 - command-line report.
            report = {
                "remote": args.remote,
                "local_root": str(local_root),
                "remote_root": args.remote_root,
                "checked": 0,
                "missing": 0,
                "mismatch": 0,
                "status": "fail",
                "error": str(exc),
                "artifacts": [],
            }
            if args.json_output:
                output = Path(args.json_output)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            print(f"remote artifact sync check failed: {exc}", file=sys.stderr)
            return 1
    else:
        relative_paths = collect_artifact_paths(args.path, args.csv, local_root)
        checks = check_artifacts(
            remote=args.remote,
            local_root=local_root,
            remote_root=args.remote_root,
            relative_paths=relative_paths,
        )
    failures = [check for check in checks if check.status != "match"]
    missing = sum(1 for check in checks if check.status in {"missing_remote", "missing"})
    mismatch = sum(1 for check in checks if check.status == "mismatch")

    report = {
        "remote": args.remote,
        "local_root": str(local_root),
        "remote_root": args.remote_root,
        "total": len(checks),
        "checked": len(checks),
        "missing": missing,
        "mismatch": mismatch,
        "failures": len(failures),
        "status": "pass" if not failures else "fail",
        "artifacts": [asdict(check) for check in checks],
    }
    if args.json_output:
        output = Path(args.json_output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if failures:
        print("remote artifact sync check failed:", file=sys.stderr)
        for check in failures:
            print(f"  {check.path}: {check.status}", file=sys.stderr)
        return 1
    print(f"remote artifact sync check passed: artifacts={len(checks)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
