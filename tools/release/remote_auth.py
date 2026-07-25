#!/usr/bin/env python3
"""Private remote auth helper.

The helper never stores or prints secrets. It uses GRIDFLUX_SSH_PASSWORD/SSHPASS
when present, and otherwise may read the local private AGENTS.md topology table.
AGENTS.md is excluded from public export.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class RemoteAuth:
    password: str
    source: str


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def parse_remote(remote: str) -> tuple[str, str]:
    if "@" in remote:
        user, host = remote.rsplit("@", 1)
        return user, host
    return "", remote


def markdown_cells(line: str) -> list[str]:
    if "|" not in line:
        return []
    cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
    if not cells or all(set(cell) <= {"-"} for cell in cells if cell):
        return []
    return cells


def password_from_agents(remote: str, root: Path | None = None) -> RemoteAuth | None:
    root = root or repo_root()
    agents = root / "AGENTS.md"
    if not agents.is_file():
        return None
    user, host = parse_remote(remote)
    try:
        text = agents.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None
    for line in text.splitlines():
        cells = markdown_cells(line)
        if len(cells) < 5:
            continue
        header = "".join(cells[:5]).lower()
        if "password" in header or "密码" in header or "<redacted>" in header and "用户" in header:
            continue
        machine, public_ip, private_ip, row_user, password = cells[:5]
        del machine
        if not password:
            continue
        if host not in {public_ip, private_ip}:
            continue
        if user and row_user and user != row_user:
            continue
        return RemoteAuth(password=password, source="AGENTS.md")
    return None


def resolve_auth(remote: str, root: Path | None = None) -> RemoteAuth | None:
    if os.environ.get("GRIDFLUX_SSH_PASSWORD"):
        return RemoteAuth(password=os.environ["GRIDFLUX_SSH_PASSWORD"], source="GRIDFLUX_SSH_PASSWORD")
    if os.environ.get("SSHPASS"):
        return RemoteAuth(password=os.environ["SSHPASS"], source="SSHPASS")
    return password_from_agents(remote, root)


def command_env(remote: str, root: Path | None = None) -> dict[str, str]:
    env = os.environ.copy()
    auth = resolve_auth(remote, root)
    if auth:
        env["SSHPASS"] = auth.password
    return env


def has_password(remote: str, root: Path | None = None) -> bool:
    return resolve_auth(remote, root) is not None


def require_sshpass_if_needed(remote: str, root: Path | None = None) -> None:
    if has_password(remote, root) and shutil.which("sshpass") is None:
        raise RuntimeError("sshpass is required for password-based remote access")


def ssh_prefix(remote: str, *, root: Path | None = None, connect_timeout: int = 10) -> list[str]:
    base = [
        "ssh",
        "-o",
        "StrictHostKeyChecking=no",
        "-o",
        f"ConnectTimeout={connect_timeout}",
        remote,
    ]
    if has_password(remote, root):
        require_sshpass_if_needed(remote, root)
        return ["sshpass", "-e", *base]
    return ["ssh", "-o", "StrictHostKeyChecking=no", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={connect_timeout}", remote]


def wrap_with_sshpass(remote: str, command: list[str], *, root: Path | None = None) -> tuple[list[str], dict[str, str]]:
    env = command_env(remote, root)
    if has_password(remote, root):
        require_sshpass_if_needed(remote, root)
        return ["sshpass", "-e", *command], env
    return command, env


def status(remote: str, root: Path | None = None) -> dict[str, str]:
    auth = resolve_auth(remote, root)
    return {
        "remote": remote,
        "auth": "password" if auth else "none",
        "source": auth.source if auth else "",
        "sshpass": "available" if shutil.which("sshpass") else "missing",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a command with GridFlux private remote auth if available.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--repo-root", default="")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--sshpass-prefix", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    root = Path(args.repo_root).resolve() if args.repo_root else repo_root()
    if args.status:
        info = status(args.remote, root)
        print("remote_auth " + " ".join(f"{key}={value}" for key, value in info.items()))
        return 0 if info["auth"] != "none" or args.command else 0

    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("command is required unless --status is used")

    try:
        if args.sshpass_prefix:
            command, env = wrap_with_sshpass(args.remote, command, root=root)
        else:
            env = command_env(args.remote, root)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    completed = subprocess.run(command, env=env, check=False)
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
