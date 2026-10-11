#!/usr/bin/env python3
"""Create a deterministic, hygiene-checked CPNetFlux source-only tarball."""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime, timezone
from urllib.parse import urlsplit

MAX_ARCHIVE_MEMBERS = 100_000
MAX_ARCHIVE_BYTES = 2 * 1024 * 1024 * 1024


def run_git(root: Path, *args: str, check: bool = True) -> str:
    completed = subprocess.run(["git", "-C", str(root), *args], text=True, capture_output=True, check=False)
    if check and completed.returncode:
        raise RuntimeError(completed.stderr.strip() or f"git {' '.join(args)} failed")
    return completed.stdout.strip()


def safe_member_name(name: str, expected_root: str) -> tuple[str, ...]:
    path = PurePosixPath(name)
    parts = tuple(part for part in path.parts if part not in ("", "."))
    if path.is_absolute() or not parts or any(part == ".." for part in path.parts):
        raise RuntimeError(f"unsafe path in Git archive: {name!r}")
    if parts[0] != expected_root:
        raise RuntimeError(f"unexpected archive root: {name!r}")
    return parts[1:]


def extract_git_archive(archive: Path, destination: Path, expected_root: str) -> Path:
    with tarfile.open(archive, mode="r:") as tar:
        members = tar.getmembers()
        if len(members) > MAX_ARCHIVE_MEMBERS:
            raise RuntimeError("Git archive exceeds the member-count budget")
        parsed = []
        paths: set[tuple[str, ...]] = set()
        total = 0
        for member in members:
            parts = safe_member_name(member.name, expected_root)
            if not parts:
                parsed.append((member, ()))
                continue
            if parts in paths:
                raise RuntimeError(f"duplicate archive path: {member.name}")
            paths.add(parts)
            if not (member.isdir() or member.isfile() or member.issym()):
                raise RuntimeError(f"unsupported file type in Git archive: {member.name}")
            if member.isfile():
                total += member.size
                if total > MAX_ARCHIVE_BYTES:
                    raise RuntimeError("Git archive exceeds the unpacked-size budget")
            if member.issym():
                raise RuntimeError(f"symlinks are not allowed in source release input: {member.name}")
            parsed.append((member, parts))
        source_root = destination / expected_root
        source_root.mkdir(parents=True)
        for member, parts in parsed:
            if not parts or not member.isdir():
                continue
            (source_root / Path(*parts)).mkdir(parents=True, exist_ok=True)
        for member, parts in parsed:
            if not parts or member.isdir():
                continue
            target = source_root / Path(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            mode = 0o755 if member.mode & 0o111 else 0o644
            source = tar.extractfile(member)
            if source is None:
                raise RuntimeError(f"cannot read archived file: {member.name}")
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(mode)
        return source_root


def tree_digest(root: Path) -> tuple[str, int, list[str]]:
    digest = hashlib.sha256()
    count = 0
    paths: list[str] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        paths.append(relative)
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode):
            kind, data, mode = b"L", os.readlink(path).encode(), 0o777
        elif stat.S_ISDIR(info.st_mode):
            kind, data, mode = b"D", b"", 0o755
        elif stat.S_ISREG(info.st_mode):
            kind, data = b"F", path.read_bytes()
            mode = 0o755 if info.st_mode & 0o111 else 0o644
        else:
            raise RuntimeError(f"unsupported entry in public source tree: {relative}")
        digest.update(kind + b"\0" + relative.encode("utf-8") + b"\0" + oct(mode).encode() + b"\0")
        digest.update(hashlib.sha256(data).digest())
        digest.update(b"\n")
        if kind != b"D":
            count += 1
    return digest.hexdigest(), count, paths


def deterministic_tar(root: Path, output: Path, archive_root: str, timestamp: int) -> None:
    with output.open("xb") as raw:
        with gzip.GzipFile(filename="", mode="wb", compresslevel=9, fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as tar:
                entries = [root, *sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix())]
                for path in entries:
                    relative = "" if path == root else path.relative_to(root).as_posix()
                    info_fs = path.lstat()
                    info = tarfile.TarInfo(archive_root if not relative else f"{archive_root}/{relative}")
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = timestamp
                    info.pax_headers = {}
                    if stat.S_ISDIR(info_fs.st_mode):
                        info.type = tarfile.DIRTYPE
                        info.mode = 0o755
                        tar.addfile(info)
                    elif stat.S_ISLNK(info_fs.st_mode):
                        info.type = tarfile.SYMTYPE
                        info.mode = 0o777
                        info.linkname = os.readlink(path)
                        tar.addfile(info)
                    elif stat.S_ISREG(info_fs.st_mode):
                        info.type = tarfile.REGTYPE
                        info.mode = 0o755 if info_fs.st_mode & 0o111 else 0o644
                        info.size = info_fs.st_size
                        with path.open("rb") as source:
                            tar.addfile(info, source)
                    else:
                        raise RuntimeError(f"unsupported entry in public source tree: {relative}")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def create(args: argparse.Namespace) -> tuple[Path, Path, dict]:
    source = Path(args.source).resolve() if args.source else Path(__file__).resolve().parents[2]
    top = Path(run_git(source, "rev-parse", "--show-toplevel"))
    head = run_git(top, "rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", args.commit):
        raise RuntimeError("--commit must be a full 40/64-character object SHA")
    commit = run_git(top, "rev-parse", "--verify", f"{args.commit}^{{commit}}")
    if commit != head:
        raise RuntimeError(f"HEAD ({head}) does not equal requested commit ({commit})")
    dirty = run_git(top, "status", "--porcelain=v1", "--untracked-files=all")
    if dirty:
        raise RuntimeError("source worktree is dirty; commit or remove task-owned changes before packaging")
    branch = run_git(top, "branch", "--show-current")
    ref = args.remote_ref if args.remote_ref.startswith("refs/") else f"refs/heads/{args.remote_ref}"
    valid_ref = subprocess.run(
        ["git", "-C", str(top), "check-ref-format", ref],
        text=True,
        capture_output=True,
        check=False,
    )
    if any(ch.isspace() for ch in ref) or valid_ref.returncode != 0:
        raise RuntimeError(f"invalid remote ref: {ref}")

    def verified_remote_sha() -> str:
        checked = subprocess.run(
            ["git", "-C", str(top), "ls-remote", "--exit-code", "--refs", args.remote, ref],
            text=True,
            capture_output=True,
            check=False,
        )
        lines = checked.stdout.splitlines()
        if checked.returncode != 0 or len(lines) != 1:
            return "missing"
        fields = lines[0].split()
        if len(fields) != 2 or fields[1] != ref:
            return "missing"
        return fields[0]

    remote_sha = verified_remote_sha()
    if remote_sha != commit:
        raise RuntimeError(f"remote {args.remote} {ref} SHA {remote_sha} does not match commit {commit}")
    remote_url = run_git(top, "remote", "get-url", args.remote)
    parsed_remote = urlsplit(remote_url)
    if parsed_remote.scheme in {"http", "https"} and (
        parsed_remote.username or parsed_remote.password or parsed_remote.query or parsed_remote.fragment
    ):
        raise RuntimeError("remote URL contains userinfo or query data; refusing to write it into the manifest")
    output = Path(args.output).resolve()
    manifest = output.with_name(output.name + ".manifest.json")
    if output.suffixes[-2:] != [".tar", ".gz"]:
        raise RuntimeError("--output must end in .tar.gz")
    if output == top or top in output.parents:
        raise RuntimeError("source package output must be outside the Git worktree")
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or manifest.exists():
        raise RuntimeError("output archive or manifest already exists; refusing to overwrite")
    lock = output.with_name(output.name + ".lock")
    try:
        lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as exc:
        raise RuntimeError(f"release output is locked: {lock}") from exc
    try:
        with tempfile.TemporaryDirectory(prefix=".cpnetflux-source-", dir=output.parent) as temporary:
            work = Path(temporary)
            root_name = f"CPNetFlux-source-{commit[:12]}"
            archive = work / "git-tree.tar"
            with archive.open("wb") as sink:
                completed = subprocess.run(["git", "-C", str(top), "archive", "--format=tar", f"--prefix={root_name}/", commit], stdout=sink, stderr=subprocess.PIPE, check=False)
            if completed.returncode:
                raise RuntimeError(completed.stderr.decode(errors="replace").strip() or "git archive failed")
            snapshot_parent = work / "snapshot"
            snapshot_parent.mkdir()
            snapshot = extract_git_archive(archive, snapshot_parent, root_name)
            public_tree = work / "public-tree"
            exporter = Path(__file__).with_name("export_public_repo.py")
            exported = subprocess.run([sys.executable, str(exporter), "--source", str(snapshot), "--output", str(public_tree)], text=True, capture_output=True, check=False)
            if exported.returncode:
                raise RuntimeError((exported.stdout + exported.stderr).strip() or "public source export failed")
            checker = Path(__file__).with_name("check_public_hygiene.py")
            checked = subprocess.run([sys.executable, str(checker), "--path", str(public_tree), "--strict"], text=True, capture_output=True, check=False)
            if checked.returncode:
                raise RuntimeError((checked.stdout + checked.stderr).strip() or "strict public hygiene failed")
            tree_hash, file_count, paths = tree_digest(public_tree)
            package_temp = work / output.name
            timestamp = int(run_git(top, "show", "-s", "--format=%ct", commit))
            deterministic_tar(public_tree, package_temp, root_name, timestamp)
            archive_hash = sha256_file(package_temp)
            license_files = [
                name for name in paths
                if "/" not in name and Path(name).name.lower() in {
                    "license", "license.txt", "license.md", "copying", "copying.txt"
                }
            ]
            if verified_remote_sha() != commit:
                raise RuntimeError(f"remote {args.remote} {ref} moved while packaging; refusing to publish artifact")

            manifest_data = {
                "manifest_version": 1,
                "project": "CPNetFlux",
                "package_kind": "source-only-candidate",
                "source_commit": commit,
                "source_branch": branch,
                "remote_name": args.remote,
                "remote_url": remote_url,
                "remote_ref": ref,
                "remote_sha_verified": remote_sha,
                "archive_file": output.name,
                "archive_sha256": archive_hash,
                "source_tree_sha256": tree_hash,
                "source_file_count": file_count,
                "license_files": license_files,
                "redistribution_status": "no-root-license-found" if not license_files else "license-present-review-required",
                "public_release_ready": False,
                "hygiene": "strict-passed",
                "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            }
            manifest_temp = work / manifest.name
            manifest_temp.write_text(json.dumps(manifest_data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            os.link(package_temp, output)
            try:
                os.link(manifest_temp, manifest)
            except Exception:
                output.unlink()
                raise
            output.chmod(0o644)
            manifest.chmod(0o644)
        return output, manifest, manifest_data
    finally:
        os.close(lock_fd)
        lock.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a clean, deterministic CPNetFlux source-only candidate archive.")
    parser.add_argument("--commit", required=True, help="full commit SHA; must equal clean local HEAD")
    parser.add_argument("--remote", default="origin", help="verified Git remote name")
    parser.add_argument("--remote-ref", required=True, help="branch or full ref whose remote SHA must equal --commit")
    parser.add_argument("--output", required=True, help="new .tar.gz output path outside the source worktree")
    parser.add_argument("--source", help="source repository; defaults to this script's repository")
    args = parser.parse_args()
    try:
        output, manifest, data = create(args)
    except (OSError, RuntimeError, ValueError, tarfile.TarError) as exc:
        print(f"source release refused: {exc}", file=sys.stderr)
        return 1
    print(f"source archive: {output}")
    print(f"manifest: {manifest}")
    print(f"archive SHA-256: {data['archive_sha256']}")
    print(f"source tree SHA-256: {data['source_tree_sha256']}")
    print(f"redistribution status: {data['redistribution_status']}; public_release_ready=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
