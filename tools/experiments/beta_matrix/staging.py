from __future__ import annotations

import gzip
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


DEFAULT_BLOCK_SIZE = 128 * 1024 * 1024


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _write_manifest(staging_root: Path, manifest: dict[str, Any]) -> None:
    staging_root.mkdir(parents=True, exist_ok=True)
    (staging_root / "staging_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def _roundtrip_gzip(raw: bytes, compressed_path: Path, restore_path: Path) -> tuple[int, float, float]:
    start = time.perf_counter()
    compressed_path.write_bytes(gzip.compress(raw))
    compress_time = time.perf_counter() - start
    start = time.perf_counter()
    restore_path.write_bytes(gzip.decompress(compressed_path.read_bytes()))
    decompress_time = time.perf_counter() - start
    return compressed_path.stat().st_size, compress_time, decompress_time


def _roundtrip_lz4(raw: bytes, compressed_path: Path, restore_path: Path) -> tuple[int, float, float]:
    try:
        import lz4.frame  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError("lz4 Python module is not installed") from exc
    start = time.perf_counter()
    compressed_path.write_bytes(lz4.frame.compress(raw))
    compress_time = time.perf_counter() - start
    start = time.perf_counter()
    restore_path.write_bytes(lz4.frame.decompress(compressed_path.read_bytes()))
    decompress_time = time.perf_counter() - start
    return compressed_path.stat().st_size, compress_time, decompress_time


def _run_cpss_command(command: list[str], compressor_root: Path) -> float:
    env = os.environ.copy()
    python_path = str(compressor_root)
    if env.get("PYTHONPATH"):
        python_path += os.pathsep + env["PYTHONPATH"]
    env["PYTHONPATH"] = python_path
    start = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=compressor_root,
        env=env,
        text=True,
        capture_output=True,
        check=False,
        timeout=300,
    )
    elapsed = time.perf_counter() - start
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return elapsed


def _roundtrip_cpss(
    raw: bytes,
    raw_block_path: Path,
    compressed_path: Path,
    restore_path: Path,
    compressor_root: Path | None,
) -> tuple[int, float, float]:
    if compressor_root is None:
        raise RuntimeError("CPSS staging requires --compressor-root")
    if not (compressor_root / "Compressor" / "cli.py").is_file():
        raise RuntimeError("CPSS compressor root must contain Compressor/cli.py")
    raw_block_path.write_bytes(raw)
    compress_time = _run_cpss_command(
        [
            sys.executable,
            "-m",
            "Compressor.cli",
            "compress",
            "--selector",
            "exhaustive",
            str(raw_block_path),
            "-o",
            str(compressed_path),
        ],
        compressor_root,
    )
    decompress_time = _run_cpss_command(
        [
            sys.executable,
            "-m",
            "Compressor.cli",
            "decompress",
            str(compressed_path),
            "-o",
            str(restore_path),
        ],
        compressor_root,
    )
    return compressed_path.stat().st_size, compress_time, decompress_time


def stage_blocks(
    source: Path,
    staging_root: Path,
    *,
    method: str,
    block_size: int = DEFAULT_BLOCK_SIZE,
    compressor_root: Path | None = None,
) -> dict[str, Any]:
    if method not in {"cpss", "gzip", "lz4"}:
        raise ValueError("method must be cpss, gzip, or lz4")
    if block_size <= 0:
        raise ValueError("block_size must be positive")
    source = source.resolve()
    staging_root = staging_root.resolve()
    raw_root = staging_root / "raw_blocks"
    compressed_root = staging_root / "compressed"
    restore_root = staging_root / "restored"
    for path in (raw_root, compressed_root, restore_root):
        path.mkdir(parents=True, exist_ok=True)

    blocks: list[dict[str, Any]] = []
    offset = 0
    index = 0
    suffix = {"cpss": ".cpss", "gzip": ".gz", "lz4": ".lz4"}[method]
    with source.open("rb") as handle:
        while True:
            raw = handle.read(block_size)
            if not raw:
                break
            raw_sha = hashlib.sha256(raw).hexdigest()
            raw_block_path = raw_root / f"{source.name}.block-{index:06d}.raw"
            compressed_path = compressed_root / f"{source.name}.block-{index:06d}{suffix}"
            restore_path = restore_root / f"{source.name}.block-{index:06d}.restored"
            if method == "gzip":
                compressed_size, compress_time, decompress_time = _roundtrip_gzip(
                    raw, compressed_path, restore_path
                )
            elif method == "lz4":
                compressed_size, compress_time, decompress_time = _roundtrip_lz4(
                    raw, compressed_path, restore_path
                )
            else:
                compressed_size, compress_time, decompress_time = _roundtrip_cpss(
                    raw, raw_block_path, compressed_path, restore_path, compressor_root
                )
            restored_sha = sha256_file(restore_path)
            blocks.append(
                {
                    "index": index,
                    "offset": offset,
                    "original_size": len(raw),
                    "original_sha256": raw_sha,
                    "compressed_size": compressed_size,
                    "compress_time_sec": round(compress_time, 6),
                    "decompress_time_sec": round(decompress_time, 6),
                    "compressed_path": str(compressed_path),
                    "restore_path": str(restore_path),
                    "restored_sha256": restored_sha,
                    "sha256_ok": restored_sha == raw_sha,
                }
            )
            offset += len(raw)
            index += 1

    manifest = {
        "schema_version": 1,
        "source_path": str(source),
        "source_size": source.stat().st_size,
        "source_sha256": sha256_file(source),
        "method": method,
        "compression_scope": "per_file_blocks",
        "block_size": block_size,
        "block_count": len(blocks),
        "blocks": blocks,
    }
    _write_manifest(staging_root, manifest)
    return manifest
