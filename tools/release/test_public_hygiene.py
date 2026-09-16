#!/usr/bin/env python3
"""Regression tests for the public export and hygiene gate."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def script_dir() -> Path:
    return Path(__file__).resolve().parent


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if check and completed.returncode != 0:
        print("command failed:", " ".join(command), file=sys.stderr)
        print(completed.stdout, file=sys.stderr)
        print(completed.stderr, file=sys.stderr)
        raise SystemExit(completed.returncode)
    return completed


def write_fixture(root: Path) -> None:
    (root / "tools" / "release").mkdir(parents=True)
    (root / "src").mkdir()
    (root / "build-verify-20260515T163633Z").mkdir()
    (root / "cmake-build-debug" / "CMakeFiles").mkdir(parents=True)

    (root / "AGENTS.md").write_text(
        "| private-machine | 203.0.113.10 | 10.0.0.10 | root | synthetic-private-password |\n",
        encoding="utf-8",
    )
    (root / "AGENTS.example.md").write_text(
        "| machine-one | <public-ip> | <private-ip> | <user> | <password> |\n",
        encoding="utf-8",
    )
    (root / "src" / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
    (root / "src" / "token_fixture.txt").write_text(
        "auth_" + "tok" + "en=super-secret-token-value-123456\n",
        encoding="utf-8",
    )
    (root / "src" / "leaked-key.pem").write_text(
        "-----BEGIN PRIVATE KEY-----\nredacted fixture\n-----END PRIVATE KEY-----\n",
        encoding="utf-8",
    )
    (root / "build-verify-20260515T163633Z" / "cpnetflux_unit_tests").write_bytes(
        b"\x7fELF fake binary with 10.0.0.10"
    )
    (root / "cmake-build-debug" / "CMakeCache.txt").write_text(
        "PRIVATE_IP=10.0.0.11\n",
        encoding="utf-8",
    )
    (root / "build.ninja").write_text("rule cc\n", encoding="utf-8")


def assert_not_exists(path: Path) -> None:
    if path.exists():
        raise AssertionError(f"unexpected path exists: {path}")


def assert_exists(path: Path) -> None:
    if not path.exists():
        raise AssertionError(f"expected path missing: {path}")


def main() -> int:
    export_script = script_dir() / "export_public_repo.py"
    check_script = script_dir() / "check_public_hygiene.py"
    with tempfile.TemporaryDirectory(prefix="cpnetflux-release-test-") as temp:
        temp_root = Path(temp)
        private_repo = temp_root / "private"
        public_repo = temp_root / "public"
        private_repo.mkdir()
        write_fixture(private_repo)

        private_check = run(
            [sys.executable, str(check_script), "--path", str(private_repo), "--strict"],
            check=False,
        )
        if private_check.returncode == 0:
            print("private fixture strict hygiene unexpectedly passed", file=sys.stderr)
            return 1

        run(
            [
                sys.executable,
                str(export_script),
                "--source",
                str(private_repo),
                "--output",
                str(public_repo),
            ]
        )
        run([sys.executable, str(check_script), "--path", str(public_repo), "--strict"])

        assert_not_exists(public_repo / "AGENTS.md")
        assert_exists(public_repo / "AGENTS.example.md")
        assert_not_exists(public_repo / "build-verify-20260515T163633Z")
        assert_not_exists(public_repo / "cmake-build-debug")
        assert_not_exists(public_repo / "build.ninja")
        assert_not_exists(public_repo / "src" / "leaked-key.pem")
        if any(path.name.startswith("build") for path in public_repo.rglob("*") if path.is_dir()):
            raise AssertionError("public export contains a build-like directory")
    print("public hygiene regression test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
