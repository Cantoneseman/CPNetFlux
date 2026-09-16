from __future__ import annotations

import hashlib
import json
import platform
import shutil
import socket
import time
from pathlib import Path
from typing import Any


GENERATOR_VERSION = "gridftp_compare_dataset_v1"

DATASET_PROFILES: dict[str, dict[str, object]] = {
    "single_64MiB": {"kind": "single", "sizes": [64 * 1024 * 1024]},
    "single_256MiB": {"kind": "single", "sizes": [256 * 1024 * 1024]},
    "tree_dense_128MiB": {"kind": "tree", "sizes": [1 * 1024 * 1024] * 128},
    "tree_mixed_256MiB": {
        "kind": "tree",
        "sizes": [*(512 * 1024 for _ in range(128)), *(4 * 1024 * 1024 for _ in range(16)), *(32 * 1024 * 1024 for _ in range(4))],
    },
    "single_1GiB": {"kind": "single", "sizes": [1024 * 1024 * 1024]},
}


def dataset_total_bytes(profile: str) -> int:
    if profile not in DATASET_PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    return sum(int(size) for size in DATASET_PROFILES[profile]["sizes"])


def dataset_kind(profile: str) -> str:
    if profile not in DATASET_PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    return str(DATASET_PROFILES[profile]["kind"])


def dataset_specs(profile: str) -> list[dict[str, Any]]:
    if profile not in DATASET_PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    config = DATASET_PROFILES[profile]
    sizes = [int(size) for size in config["sizes"]]
    if config["kind"] == "single":
        return [{"path": "single.bin", "size": sizes[0], "label": f"{profile}/single.bin"}]
    if str(config["kind"]) == "tree":
        root_name = {
            "tree_dense_128MiB": "tree-dense",
            "tree_mixed_256MiB": "tree-mixed",
        }.get(profile, profile.replace("_", "-"))
        return [
            {
                "path": f"{root_name}/file-{index:04d}.bin",
                "size": size,
                "label": f"{profile}/{index:04d}",
            }
            for index, size in enumerate(sizes)
        ]
    raise ValueError(f"unsupported profile layout: {profile}")


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
        if ".cpnetflux." in relative or ".part." in relative:
            continue
        size = path.stat().st_size
        file_count += 1
        total_bytes += size
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(str(size).encode("ascii") + b"\0")
        digest.update(file_sha256(path).encode("ascii") + b"\0")
    return digest.hexdigest(), file_count, total_bytes


def remove_generated_entries(output: Path) -> None:
    for name in ["single.bin", "tree-dense", "tree-mixed", "manifest.json", "dataset_manifest.json"]:
        path = output / name
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()


def materialize_dataset(output: Path, *, profile: str, seed: int = 20260831, clean: bool = True) -> list[dict[str, Any]]:
    if profile not in DATASET_PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    output.mkdir(parents=True, exist_ok=True)
    if clean:
        remove_generated_entries(output)

    files: list[dict[str, Any]] = []
    for spec in dataset_specs(profile):
        path = output / str(spec["path"])
        write_deterministic_file(path, int(spec["size"]), seed=seed, label=str(spec["label"]))
        files.append(
            {
                "path": str(spec["path"]),
                "size": int(spec["size"]),
                "sha256": file_sha256(path),
            }
        )
    return files


def manifest_from_files(
    output: Path,
    *,
    profile: str,
    seed: int,
    files: list[dict[str, Any]],
    materialized: bool,
) -> dict[str, Any]:
    content_hash, file_count, total_bytes = tree_hash(output) if materialized else ("", len(files), sum(int(item["size"]) for item in files))
    single_manifest: dict[str, Any] = {}
    dense_files: list[dict[str, Any]] = []
    mixed_files: list[dict[str, Any]] = []
    dense_hash = dense_count = dense_bytes = 0
    mixed_hash = mixed_count = mixed_bytes = 0
    single = output / "single.bin"
    tree_dense = output / "tree-dense"
    tree_mixed = output / "tree-mixed"
    if profile.startswith("single_") and files:
        single_manifest = {
            "path": str(single),
            "bytes": int(files[0]["size"]),
            "sha256": str(files[0]["sha256"]),
        }
    elif profile == "tree_dense_128MiB":
        dense_files = files
        if materialized:
            dense_hash, dense_count, dense_bytes = tree_hash(tree_dense)
        else:
            dense_count = len(files)
            dense_bytes = sum(int(item["size"]) for item in files)
    elif profile == "tree_mixed_256MiB":
        mixed_files = files
        if materialized:
            mixed_hash, mixed_count, mixed_bytes = tree_hash(tree_mixed)
        else:
            mixed_count = len(files)
            mixed_bytes = sum(int(item["size"]) for item in files)

    manifest: dict[str, Any] = {
        "schema_version": 1,
        "generator": GENERATOR_VERSION,
        "profile": profile,
        "seed": seed,
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "host": {
            "hostname": socket.gethostname(),
            "platform": platform.platform(),
        },
        "output": str(output),
        "kind": dataset_kind(profile),
        "file_count": file_count,
        "total_bytes": total_bytes,
        "tree_hash": content_hash,
        "materialized": materialized,
        "files": files,
        "single": single_manifest,
        "tree_dense": {
            "path": str(tree_dense),
            "file_count": dense_count,
            "bytes": dense_bytes,
            "tree_hash": dense_hash,
            "files": dense_files,
        },
        "tree_mixed": {
            "path": str(tree_mixed),
            "file_count": mixed_count,
            "bytes": mixed_bytes,
            "tree_hash": mixed_hash,
            "files": mixed_files,
        },
    }
    return manifest


def make_dataset(
    output: Path,
    *,
    profile: str,
    seed: int = 20260831,
    write_legacy_manifest: bool = True,
    materialize: bool = True,
) -> dict[str, Any]:
    if materialize:
        files = materialize_dataset(output, profile=profile, seed=seed, clean=True)
    else:
        output.mkdir(parents=True, exist_ok=True)
        remove_generated_entries(output)
        files = [{"path": str(spec["path"]), "size": int(spec["size"]), "sha256": ""} for spec in dataset_specs(profile)]
    manifest = manifest_from_files(output, profile=profile, seed=seed, files=files, materialized=materialize)
    (output / "dataset_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if write_legacy_manifest:
        legacy = {
            key: value
            for key, value in manifest.items()
            if key
            in {
                "profile",
                "seed",
                "generated_at",
                "output",
                "single",
                "tree_dense",
                "tree_mixed",
            }
        }
        (output / "manifest.json").write_text(json.dumps(legacy, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest
