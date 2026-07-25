#!/usr/bin/env python3
"""Generate a deterministic GridFlux alpha demo dataset."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path


PROFILES = {
    "tiny": {
        "single": 2 * 1024 * 1024,
        "small_count": 8,
        "small_size": 4 * 1024,
        "medium": 512 * 1024,
        "large": 2 * 1024 * 1024,
    },
    "small": {
        "single": 8 * 1024 * 1024,
        "small_count": 32,
        "small_size": 8 * 1024,
        "medium": 2 * 1024 * 1024,
        "large": 8 * 1024 * 1024,
    },
    "mixed": {
        "single": 16 * 1024 * 1024,
        "small_count": 64,
        "small_size": 16 * 1024,
        "medium": 4 * 1024 * 1024,
        "large": 16 * 1024 * 1024,
    },
}


def deterministic_block(seed: int, label: str, counter: int) -> bytes:
    digest = hashlib.sha256(f"{seed}:{label}:{counter}".encode("utf-8")).digest()
    return (digest * (4096 // len(digest) + 1))[:4096]


def write_deterministic_file(path: Path, size: int, *, seed: int, label: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    remaining = size
    counter = 0
    with path.open("wb") as handle:
        while remaining > 0:
            block = deterministic_block(seed, label, counter)
            chunk = block[: min(remaining, len(block))]
            handle.write(chunk)
            remaining -= len(chunk)
            counter += 1


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_hash(root: Path) -> tuple[str, int, int]:
    digest = hashlib.sha256()
    file_count = 0
    total_bytes = 0
    if not root.exists():
        return digest.hexdigest(), 0, 0
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if ".gridflux." in relative or ".part." in relative:
            continue
        size = path.stat().st_size
        file_count += 1
        total_bytes += size
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(str(size).encode("ascii") + b"\0")
        digest.update(file_sha256(path).encode("ascii") + b"\0")
    return digest.hexdigest(), file_count, total_bytes


def remove_generated_entries(output: Path) -> None:
    for name in ["single.bin", "tree-mixed", "tree-small", "manifest.json"]:
        path = output / name
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()


def make_dataset(output: Path, *, profile: str, seed: int) -> dict[str, object]:
    if profile not in PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    config = PROFILES[profile]
    output.mkdir(parents=True, exist_ok=True)
    remove_generated_entries(output)

    single = output / "single.bin"
    tree_small = output / "tree-small"
    tree_mixed = output / "tree-mixed"

    write_deterministic_file(single, int(config["single"]), seed=seed, label="single.bin")

    for index in range(int(config["small_count"])):
        name = f"small-{index:03d}.bin"
        write_deterministic_file(
            tree_small / "files" / name,
            int(config["small_size"]),
            seed=seed,
            label=f"tree-small/{name}",
        )

    (tree_mixed / "empty.bin").parent.mkdir(parents=True, exist_ok=True)
    (tree_mixed / "empty.bin").write_bytes(b"")
    (tree_mixed / "notes with spaces.txt").write_text("GridFlux alpha demo\n", encoding="utf-8")
    write_deterministic_file(
        tree_mixed / "special_chars" / "alpha+beta,@demo.txt",
        12 * 1024,
        seed=seed,
        label="tree-mixed/special",
    )
    write_deterministic_file(
        tree_mixed / "deep" / "level1" / "level2" / "level3" / "deep.bin",
        96 * 1024,
        seed=seed,
        label="tree-mixed/deep",
    )
    write_deterministic_file(
        tree_mixed / "medium" / "payload.bin",
        int(config["medium"]),
        seed=seed,
        label="tree-mixed/medium",
    )
    write_deterministic_file(
        tree_mixed / "large" / "large.bin",
        int(config["large"]),
        seed=seed,
        label="tree-mixed/large",
    )
    # Empty directories are intentionally generated so demos can show that
    # directory transfer does not preserve them.
    (tree_mixed / "empty-dir").mkdir(parents=True, exist_ok=True)

    single_hash = file_sha256(single)
    mixed_hash, mixed_count, mixed_bytes = tree_hash(tree_mixed)
    small_hash, small_count, small_bytes = tree_hash(tree_small)
    manifest = {
        "profile": profile,
        "seed": seed,
        "single": {
            "path": str(single),
            "bytes": single.stat().st_size,
            "sha256": single_hash,
        },
        "tree_mixed": {
            "path": str(tree_mixed),
            "file_count": mixed_count,
            "bytes": mixed_bytes,
            "tree_hash": mixed_hash,
        },
        "tree_small": {
            "path": str(tree_small),
            "file_count": small_count,
            "bytes": small_bytes,
            "tree_hash": small_hash,
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic GridFlux alpha demo data.")
    parser.add_argument("--output", required=True)
    parser.add_argument("--seed", type=int, default=20260518)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="small")
    args = parser.parse_args()
    manifest = make_dataset(Path(args.output), profile=args.profile, seed=args.seed)
    print(
        "demo_dataset "
        f"profile={manifest['profile']} "
        f"seed={manifest['seed']} "
        f"single_bytes={manifest['single']['bytes']} "
        f"tree_mixed_files={manifest['tree_mixed']['file_count']} "
        f"tree_mixed_bytes={manifest['tree_mixed']['bytes']} "
        f"tree_small_files={manifest['tree_small']['file_count']} "
        f"tree_small_bytes={manifest['tree_small']['bytes']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
