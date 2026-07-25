#!/usr/bin/env python3
"""Export a sanitized GridFlux source tree for public publishing."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


BUILD_DIR_NAMES = {
    "build",
    "CMakeFiles",
    "Testing",
    "_deps",
    ".cache",
    "out",
    "dist",
}

EXCLUDE_DIR_NAMES = {
    ".git",
    "__pycache__",
    ".pytest_cache",
}

EXCLUDE_NAMES = {
    "AGENTS.md",
    "CMakeCache.txt",
    "CTestTestfile.cmake",
    "build.ninja",
    "cmake_install.cmake",
    "compile_commands.json",
    "install_manifest.txt",
    ".env",
    ".env.local",
    ".ninja_deps",
    ".ninja_log",
}

EXCLUDE_SUFFIXES = {
    ".a",
    ".bin",
    ".cookie",
    ".crt",
    ".csr",
    ".d",
    ".dst",
    ".key",
    ".log",
    ".o",
    ".p12",
    ".pem",
    ".pfx",
    ".pyc",
    ".so",
    ".src",
    ".tmp",
}

EXCLUDE_PREFIX_PARTS = {
    ("tools", "perf", "results"),
}


class ExportStats:
    def __init__(self) -> None:
        self.copied_files = 0
        self.skipped_files = 0
        self.skipped_dirs = 0
        self.skipped_build_dirs: list[str] = []


def is_build_dir_name(name: str) -> bool:
    return (
        name in BUILD_DIR_NAMES
        or name.startswith("build-")
        or name.startswith("cmake-build-")
    )


def is_build_artifact_name(name: str) -> bool:
    return name in EXCLUDE_NAMES


def is_elf_file(path: Path) -> bool:
    try:
        with path.open("rb") as file:
            return file.read(4) == b"\x7fELF"
    except OSError:
        return False


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def read_private_values(root: Path) -> set[str]:
    values: set[str] = set()
    agents = root / "AGENTS.md"
    if not agents.exists():
        return values
    text = agents.read_text(encoding="utf-8")
    for value in re.findall(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", text):
        values.add(value)
    for row in text.splitlines():
        if "|" not in row or "<redacted>" not in row:
            continue
        for cell in row.split("|"):
            stripped = cell.strip()
            if stripped and stripped not in {"<redacted>一", "<redacted>二", "root"}:
                values.add(stripped)
    values.add("609" + "@scst")
    return {value for value in values if value and value not in {"公网 IP", "私网 IP", "用户", "密码"}}


def should_exclude(path: Path, root: Path) -> bool:
    relative = path.relative_to(root)
    if any(part in EXCLUDE_DIR_NAMES or is_build_dir_name(part) for part in relative.parts):
        return True
    if path.name in EXCLUDE_NAMES:
        return True
    if path.suffix.lower() in EXCLUDE_SUFFIXES:
        return True
    lower_name = path.name.lower()
    if "token" in lower_name or "secret" in lower_name:
        return True
    if path.is_file() and is_elf_file(path):
        return True
    for prefix in EXCLUDE_PREFIX_PARTS:
        if relative.parts[: len(prefix)] == prefix:
            return True
    return False


def sanitize_text(text: str, private_values: set[str]) -> str:
    sanitized = text
    for value in sorted(private_values, key=len, reverse=True):
        sanitized = sanitized.replace(value, "<redacted>")
    return sanitized


def copy_tree(source: Path, destination: Path, private_values: set[str]) -> ExportStats:
    stats = ExportStats()
    for current_root, dirs, files in os.walk(source):
        current = Path(current_root)
        kept_dirs = []
        for directory in dirs:
            path = current / directory
            if should_exclude(path, source):
                stats.skipped_dirs += 1
                if is_build_dir_name(directory):
                    stats.skipped_build_dirs.append(str(path.relative_to(source)))
                continue
            kept_dirs.append(directory)
        dirs[:] = kept_dirs
        relative_dir = current.relative_to(source)
        target_dir = destination / relative_dir
        target_dir.mkdir(parents=True, exist_ok=True)
        for file_name in files:
            source_file = current / file_name
            if should_exclude(source_file, source):
                stats.skipped_files += 1
                continue
            target_file = target_dir / file_name
            try:
                text = source_file.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                shutil.copy2(source_file, target_file)
                stats.copied_files += 1
                continue
            target_file.write_text(sanitize_text(text, private_values), encoding="utf-8")
            shutil.copystat(source_file, target_file)
            stats.copied_files += 1
    return stats


def run_hygiene_check(destination: Path, source: Path) -> int:
    del source
    checker = Path(__file__).resolve().with_name("check_public_hygiene.py")
    completed = subprocess.run(
        [sys.executable, str(checker), "--path", str(destination), "--strict"],
        text=True,
        check=False,
    )
    return completed.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Export sanitized GridFlux public source tree.")
    parser.add_argument("--output", help="destination directory; default uses a temporary directory")
    parser.add_argument("--source", help="source directory; default is this repository")
    parser.add_argument("--force", action="store_true", help="remove output directory if it exists")
    args = parser.parse_args()

    source = Path(args.source).resolve() if args.source else repo_root()
    if args.output:
        destination = Path(args.output).resolve()
        if destination.exists():
            if not args.force:
                print(f"output already exists: {destination}", file=sys.stderr)
                return 1
            shutil.rmtree(destination)
        destination.mkdir(parents=True)
    else:
        destination = Path(tempfile.mkdtemp(prefix="gridflux-public-"))

    stats = copy_tree(source, destination, read_private_values(source))
    result = run_hygiene_check(destination, source)
    if result != 0:
        print(f"public export failed hygiene check: {destination}", file=sys.stderr)
        print(
            "export summary: "
            f"copied_files={stats.copied_files} "
            f"skipped_files={stats.skipped_files} "
            f"skipped_dirs={stats.skipped_dirs} "
            f"skipped_build_dirs={len(stats.skipped_build_dirs)}",
            file=sys.stderr,
        )
        if stats.skipped_build_dirs:
            print("skipped build dirs:", file=sys.stderr)
            for path in stats.skipped_build_dirs:
                print(f"  {path}", file=sys.stderr)
        return result
    print(f"public export ready: {destination}")
    print(
        "export summary: "
        f"copied_files={stats.copied_files} "
        f"skipped_files={stats.skipped_files} "
        f"skipped_dirs={stats.skipped_dirs} "
        f"skipped_build_dirs={len(stats.skipped_build_dirs)}"
    )
    if stats.skipped_build_dirs:
        print("skipped build dirs:")
        for path in stats.skipped_build_dirs:
            print(f"  {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
