#!/usr/bin/env python3
"""Exercise clean-commit source export, hygiene filtering and deterministic packaging."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

SCRIPT = Path(__file__).with_name("create_source_release.py")


def run(command: list[str], *, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    if check and result.returncode:
        raise AssertionError(f"command failed: {command}\n{result.stdout}\n{result.stderr}")
    return result


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="cpnetflux-source-release-test-") as temp:
        base = Path(temp)
        repo = base / "repo"
        remote = base / "origin.git"
        repo.mkdir()
        run(["git", "init", "-q", str(repo)])
        run(["git", "-C", str(repo), "config", "user.name", "Release Test"])
        run(["git", "-C", str(repo), "config", "user.email", "release-test@example.invalid"])
        files = {
            "README.md": "CPNetFlux release fixture\n",
            "LICENSE": "Synthetic test-only license marker\n",
            "AGENTS.md": "private IPv4 198.51.100.42 and synthetic-password\n",
            "src/main.cpp": "int main() { return 0; }\n",
            "build/CMakeCache.txt": "PRIVATE=never-export\n",
            "tools/perf/results/payload.dat": "large synthetic payload\n",
            "tools/test/run_token_smoke.py": "def main(): return 0\n",
            "credentials/token.txt": "synthetic token material\n",
            "certs/test.pem": "-----BEGIN " + "PRIVATE KEY-----\nfixture\n-----END PRIVATE KEY-----\n",
        }
        for relative, contents in files.items():
            path = repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
        run(["git", "-C", str(repo), "add", "--all"])
        run(["git", "-C", str(repo), "commit", "-qm", "release fixture"])
        commit = run(["git", "-C", str(repo), "rev-parse", "HEAD"]).stdout.strip()
        run(["git", "init", "--bare", "-q", str(remote)])
        run(["git", "-C", str(repo), "remote", "add", "origin", str(remote)])
        run(["git", "-C", str(repo), "push", "-q", "origin", "HEAD:refs/heads/main"])

        first = base / "one" / f"cpnetflux-source-{commit[:12]}.tar.gz"
        second = base / "two" / f"cpnetflux-source-{commit[:12]}.tar.gz"
        for output in (first, second):
            result = run([sys.executable, str(SCRIPT), "--source", str(repo), "--commit", commit,
                          "--remote", "origin", "--remote-ref", "main", "--output", str(output)])
            assert "public_release_ready=false" in result.stdout
        assert sha(first) == sha(second), "same commit must produce byte-identical archives"
        manifest = json.loads(first.with_name(first.name + ".manifest.json").read_text(encoding="utf-8"))
        assert manifest["source_commit"] == commit
        assert manifest["remote_sha_verified"] == commit
        assert manifest["redistribution_status"] == "license-present-review-required"
        assert manifest["public_release_ready"] is False
        with tarfile.open(first, "r:gz") as archive:
            names = set(archive.getnames())
        assert any(name.endswith("/README.md") for name in names)
        assert any(name.endswith("/LICENSE") for name in names)
        assert not any(name.endswith("/AGENTS.md") for name in names)
        assert not any("/build/" in name for name in names)
        assert not any("tools/perf/results" in name for name in names)
        assert not any(name.endswith("test.pem") for name in names)
        assert not any(name.endswith("/credentials/token.txt") for name in names)
        assert any(name.endswith("/tools/test/run_token_smoke.py") for name in names)

        dirty = repo / "untracked.txt"
        dirty.write_text("must block package", encoding="utf-8")
        rejected = run([sys.executable, str(SCRIPT), "--source", str(repo), "--commit", commit,
                        "--remote", "origin", "--remote-ref", "main", "--output", str(base / "dirty" / "bad.tar.gz")], check=False)
        assert rejected.returncode != 0 and "worktree is dirty" in rejected.stderr
        dirty.unlink()

        wrong_ref = run([sys.executable, str(SCRIPT), "--source", str(repo), "--commit", commit,
                         "--remote", "origin", "--remote-ref", "missing", "--output", str(base / "wrong" / "bad.tar.gz")], check=False)
        assert wrong_ref.returncode != 0 and "does not match commit" in wrong_ref.stderr

        invalid_ref = run([sys.executable, str(SCRIPT), "--source", str(repo), "--commit", commit,
                           "--remote", "origin", "--remote-ref", "bad ref", "--output", str(base / "invalid-ref" / "bad.tar.gz")], check=False)
        assert invalid_ref.returncode != 0 and "invalid remote ref" in invalid_ref.stderr

        overwrite = run([sys.executable, str(SCRIPT), "--source", str(repo), "--commit", commit,
                         "--remote", "origin", "--remote-ref", "main", "--output", str(first)], check=False)
        assert overwrite.returncode != 0 and "already exists" in overwrite.stderr

        linked_repo = base / "linked-repo"
        linked_remote = base / "linked-origin.git"
        linked_repo.mkdir()
        run(["git", "init", "-q", str(linked_repo)])
        run(["git", "-C", str(linked_repo), "config", "user.name", "Release Test"])
        run(["git", "-C", str(linked_repo), "config", "user.email", "release-test@example.invalid"])
        (base / "outside.txt").write_text("private external target", encoding="utf-8")
        os.symlink("../outside.txt", linked_repo / "leak.txt")
        run(["git", "-C", str(linked_repo), "add", "--all"])
        run(["git", "-C", str(linked_repo), "commit", "-qm", "symlink fixture"])
        linked_commit = run(["git", "-C", str(linked_repo), "rev-parse", "HEAD"]).stdout.strip()
        run(["git", "init", "--bare", "-q", str(linked_remote)])
        run(["git", "-C", str(linked_repo), "remote", "add", "origin", str(linked_remote)])
        run(["git", "-C", str(linked_repo), "push", "-q", "origin", "HEAD:refs/heads/main"])
        symlink_rejected = run([sys.executable, str(SCRIPT), "--source", str(linked_repo), "--commit", linked_commit,
                                "--remote", "origin", "--remote-ref", "main",
                                "--output", str(base / "symlink" / "bad.tar.gz")], check=False)
        assert symlink_rejected.returncode != 0 and "symlinks are not allowed" in symlink_rejected.stderr
    print("source release regression test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
