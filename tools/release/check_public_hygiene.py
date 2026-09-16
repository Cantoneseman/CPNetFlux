#!/usr/bin/env python3
"""Scan a CPNetFlux tree for obvious private release hygiene problems."""

from __future__ import annotations

import argparse
import os
import re
import sys
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

SKIP_DIRS = {
    ".git",
    "__pycache__",
    ".pytest_cache",
}

SKIP_PARTS = {
    ("tools", "perf", "results"),
}

BINARY_SUFFIXES = {
    ".a",
    ".bin",
    ".d",
    ".dst",
    ".gz",
    ".jpg",
    ".jpeg",
    ".o",
    ".png",
    ".so",
    ".src",
    ".tar",
    ".xz",
    ".zip",
}

BUILD_ARTIFACT_NAMES = {
    "CMakeCache.txt",
    "CTestTestfile.cmake",
    "build.ninja",
    "cmake_install.cmake",
    "compile_commands.json",
    "install_manifest.txt",
    ".ninja_deps",
    ".ninja_log",
}

ALLOWED_BINARY_SUFFIXES = {
    ".jpg",
    ".jpeg",
    ".png",
}

PLACEHOLDER_RE = re.compile(r"(<[^>\n]+>|\*{3,}|REDACTED|CHANGEME|placeholder)", re.IGNORECASE)

PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("private key block", re.compile(r"-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----")),
    ("sshpass inline password", re.compile(r"\bsshpass\s+-p\s+\S+")),
    ("CPNETFLUX_SSH_PASSWORD assignment", re.compile(r"\bCPNETFLUX_SSH_PASSWORD\s*=\s*['\"]?[^'\"\s]+")),
    ("SSHPASS direct assignment", re.compile(r"\bSSHPASS\s*=\s*['\"]?(?!\$CPNETFLUX_SSH_PASSWORD\b)[^'\"\s]+")),
    ("password assignment", re.compile(r"\b(password|passwd|pwd)\s*[:=]\s*['\"](?!cpnetflux\b)(?!\*\*\*)(?!<)[^'\"\s|]+['\"]?", re.IGNORECASE)),
    ("secret/token assignment", re.compile(r"\b(api[_-]?key|access[_-]?token|secret[_-]?key)\s*[:=]\s*['\"]?(?!\*\*\*)(?!<)[^'\"\s]+", re.IGNORECASE)),
    ("auth token literal", re.compile(r"\b(auth[_-]?token|token)\s*[:=]\s*['\"]?(?!\*\*\*)(?!<)(?!REDACTED\b)(?!CHANGEME\b)[A-Za-z0-9._+/=-]{20,}", re.IGNORECASE)),
    ("known private password", re.compile("609" + "@scst")),
    ("known public IP", re.compile(r"\b(120\.79\.11\.149|119\.23\.73\.206)\b")),
    ("known private IP", re.compile(r"\b192\.168\.10\.[12]\b")),
    ("server login table", re.compile(r"\|\s*<redacted>[一二]\s*\|")),
]

BINARY_SECRET_PATTERNS: list[tuple[str, bytes]] = [
    ("known private password", b"609" + b"@scst"),
    ("known public IP", b"<redacted>"),
    ("known public IP", b"<redacted>"),
    ("known private IP", b"<redacted>"),
    ("known private IP", b"<redacted>"),
]


def is_build_dir_name(name: str) -> bool:
    return (
        name in BUILD_DIR_NAMES
        or name.startswith("build-")
        or name.startswith("cmake-build-")
    )


def is_elf_file(path: Path) -> bool:
    try:
        with path.open("rb") as file:
            return file.read(4) == b"\x7fELF"
    except OSError:
        return False


def is_build_artifact(path: Path) -> bool:
    return path.name in BUILD_ARTIFACT_NAMES


def should_skip(path: Path, root: Path, strict: bool) -> bool:
    relative = path.relative_to(root)
    if any(part in SKIP_DIRS for part in relative.parts):
        return True
    if not strict and any(is_build_dir_name(part) for part in relative.parts):
        return True
    for skip in SKIP_PARTS:
        if relative.parts[: len(skip)] == skip:
            return True
    return not strict and path.suffix.lower() in BINARY_SUFFIXES


def is_placeholder(line: str) -> bool:
    return bool(PLACEHOLDER_RE.search(line))


def scan_file(path: Path, root: Path, strict: bool) -> list[str]:
    relative = path.relative_to(root)
    if relative == Path("AGENTS.md"):
        return [f"{relative}: private AGENTS.md must not be in public export"]
    if strict and is_build_artifact(path):
        return [f"{relative}: build artifact present in strict export"]
    if strict and path.suffix.lower() in BINARY_SUFFIXES and path.suffix.lower() not in ALLOWED_BINARY_SUFFIXES:
        return [f"{relative}: binary/build artifact present in strict export"]
    if strict and is_elf_file(path):
        return [f"{relative}: ELF executable/library present in strict export"]
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        if not strict:
            return []
        try:
            data = path.read_bytes()
        except OSError as exc:
            return [f"{relative}: unreadable binary file in strict export: {exc}"]
        findings = [f"{relative}: unknown binary file present in strict export"]
        for name, needle in BINARY_SECRET_PATTERNS:
            if needle in data:
                findings.append(f"{relative}: {name} embedded in binary")
        return findings

    findings: list[str] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if is_placeholder(line):
            continue
        for name, pattern in PATTERNS:
            if pattern.search(line):
                findings.append(f"{relative}:{line_number}: {name}")
    if strict and relative.parts[:2] == ("tools", "perf") and "results" in relative.parts:
        findings.append(f"{relative}: perf result artifact present in strict export")
    return findings


def scan(root: Path, strict: bool) -> list[str]:
    findings: list[str] = []
    for current_root, dirs, files in os.walk(root):
        current = Path(current_root)
        kept_dirs = []
        for directory in dirs:
            path = current / directory
            relative = path.relative_to(root)
            if directory in SKIP_DIRS:
                continue
            if is_build_dir_name(directory):
                if strict:
                    findings.append(f"{relative}: build-like directory present in strict export")
                else:
                    continue
            kept_dirs.append(directory)
        dirs[:] = kept_dirs
        for file_name in files:
            path = current / file_name
            if should_skip(path, root, strict):
                continue
            findings.extend(scan_file(path, root, strict))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description="Check CPNetFlux public release hygiene.")
    parser.add_argument("--path", default=".", help="tree to scan")
    parser.add_argument("--strict", action="store_true", help="fail on public-export-only issues")
    args = parser.parse_args()

    root = Path(args.path).resolve()
    findings = scan(root, args.strict)
    if findings:
        print("public hygiene check failed:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1
    print(f"public hygiene check passed: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
