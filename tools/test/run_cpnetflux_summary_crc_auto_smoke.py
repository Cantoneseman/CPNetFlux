#!/usr/bin/env python3
"""Check summary tree-hash backend selection, timing, and byte-for-byte compatibility."""
import argparse
import json
import platform
import subprocess
import tempfile
from pathlib import Path

from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, stop_server, wait_for_control


def reference_tree_hash(root: Path) -> str:
    value = 0xFFFFFFFF
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if ".cpnetflux." in relative or ".part." in relative:
            continue
        size = path.stat().st_size
        payload = relative.encode("utf-8") + b"\0" + str(size).encode("ascii") + b"\0"
        with path.open("rb") as stream:
            payload += stream.read()
        payload += b"\0"
        for byte in payload:
            value ^= byte
            for _ in range(8):
                value = (value >> 1) ^ (0x82F63B78 if value & 1 else 0)
    return f"crc32c:{value ^ 0xFFFFFFFF:08x}"


def tree_sha256_map(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): __import__("hashlib").sha256(path.read_bytes()).hexdigest()
        for path in sorted(item for item in root.rglob("*") if item.is_file())
        if ".cpnetflux." not in path.relative_to(root).as_posix()
        and ".part." not in path.relative_to(root).as_posix()
    }


def hardware_crc32c_available() -> bool:
    machine = platform.machine().lower()
    try:
        cpuinfo = Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="ignore").lower()
    except OSError:
        return False
    feature_lines = [line.split() for line in cpuinfo.splitlines()
                     if line.startswith(("flags", "features"))]
    if machine in ("x86_64", "amd64", "i386", "i686"):
        return any("sse4_2" in tokens for tokens in feature_lines)
    if machine in ("aarch64", "arm64"):
        return any("crc32" in tokens for tokens in feature_lines)
    return False


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.absolute()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-summary-crc-") as temporary:
        root = Path(temporary)
        source = root / "source"
        server_root = root / "server-root"
        (source / "nested" / "深层").mkdir(parents=True)
        server_root.mkdir()
        (source / "empty.bin").write_bytes(b"")
        (source / "nested" / "深层" / "数据.bin").write_bytes(
            bytes((i * 29) % 251 for i in range(2 * 1024 * 1024 + 37)))
        (source / "readme.txt").write_bytes("CPNetFlux tree hash\n".encode())
        (source / "reuse-threshold.bin").write_bytes(
            bytes((i * 7) % 251 for i in range(128 * 1024 * 1024)))
        expected_hash = reference_tree_hash(source)
        expected_sha256 = tree_sha256_map(source)

        control = free_port()
        data_base = clamp_passive_data_port_base(free_port())
        server_log = root / "server.log"
        with server_log.open("w", encoding="utf-8") as output:
            server = subprocess.Popen([
                str(build / "cpnetflux-gridftp-server"), "--host", "127.0.0.1",
                "--port", str(control), "--data-port-base", str(data_base),
                "--root", str(server_root), "--connections", "1", "--checksum", "crc32c", "--checksum-backend", "auto",
            ], stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for_control(control)
            requested_backends = ["software", "auto"]
            hardware_available = hardware_crc32c_available()
            if hardware_available:
                requested_backends.append("hardware")
            observed_hashes = set()
            results = []
            for backend in requested_backends:
                summary_path = root / f"summary-{backend}.json"
                command = [
                    str(build / "cpnetflux-tree-upload-client"),
                    "--host", "127.0.0.1", "--port", str(control),
                    "--source-dir", str(source), "--dest-dir", f"upload-{backend}",
                    "--connections", "1", "--file-parallelism", "1",
                    "--control-reuse", "worker", "--data-session-reuse", "tree",
                    "--data-pending-window", "1", "--control-pipeline-depth", "1",
                    "--checksum", "crc32c", "--checksum-backend", backend,
                    "--json-summary", str(summary_path),
                ]
                result = subprocess.run(command, text=True, capture_output=True, timeout=90)
                assert result.returncode == 0, result.stdout + result.stderr
                summary = json.loads(summary_path.read_text(encoding="utf-8"))
                assert summary["result"] == "pass", summary
                assert summary["tree_hash"] == expected_hash, (backend, summary["tree_hash"], expected_hash)
                assert summary["summary_tree_hash_source"] == "verified_chunks", summary
                upload_output = server_root / f"upload-{backend}"
                assert tree_sha256_map(upload_output) == expected_sha256
                actual_backend = summary["summary_tree_hash_backend"]
                assert actual_backend in ("software", "hardware"), (backend, actual_backend)
                if backend == "software":
                    assert actual_backend == "software", actual_backend
                elif backend == "hardware":
                    assert actual_backend == "hardware", actual_backend
                else:
                    expected_auto_backend = "hardware" if hardware_available else "software"
                    assert actual_backend == expected_auto_backend, (actual_backend, expected_auto_backend)
                hash_seconds = summary["summary_tree_hash_seconds"]
                assert isinstance(hash_seconds, (float, int)) and hash_seconds >= 0, hash_seconds
                observed_hashes.add(summary["tree_hash"])
                results.append({"direction": "upload", "requested_backend": backend,
                                "actual_backend": actual_backend,
                                "summary_tree_hash_source": summary["summary_tree_hash_source"],
                                "summary_tree_hash_seconds": hash_seconds, "tree_hash": summary["tree_hash"]})

                resumed_summary_path = root / f"summary-resume-{backend}.json"
                resumed = subprocess.run(command + ["--resume", "--json-summary", str(resumed_summary_path)],
                                         text=True, capture_output=True, timeout=90)
                assert resumed.returncode == 0, resumed.stdout + resumed.stderr
                resumed_summary = json.loads(resumed_summary_path.read_text(encoding="utf-8"))
                assert resumed_summary["tree_hash"] == expected_hash, resumed_summary
                assert resumed_summary["summary_tree_hash_source"] == "filesystem_read", resumed_summary

                destination = root / f"download-{backend}"
                download_summary_path = root / f"summary-download-{backend}.json"
                download_command = [
                    str(build / "cpnetflux-tree-download-client"),
                    "--host", "127.0.0.1", "--port", str(control),
                    "--source-dir", f"upload-{backend}", "--dest-dir", str(destination),
                    "--connections", "1", "--file-parallelism", "1",
                    "--control-reuse", "worker", "--data-session-reuse", "tree",
                    "--data-pending-window", "1", "--control-pipeline-depth", "1",
                    "--checksum", "crc32c", "--checksum-backend", backend,
                    "--json-summary", str(download_summary_path),
                ]
                downloaded = subprocess.run(download_command, text=True, capture_output=True, timeout=90)
                assert downloaded.returncode == 0, downloaded.stdout + downloaded.stderr
                download_summary = json.loads(download_summary_path.read_text(encoding="utf-8"))
                assert download_summary["tree_hash"] == expected_hash, download_summary
                assert download_summary["summary_tree_hash_source"] == "verified_chunks", download_summary
                assert tree_sha256_map(destination) == expected_sha256

                resumed_download_summary = root / f"summary-download-resume-{backend}.json"
                resumed_download = subprocess.run(
                    download_command + ["--resume", "--json-summary", str(resumed_download_summary)],
                    text=True, capture_output=True, timeout=90)
                assert resumed_download.returncode == 0, resumed_download.stdout + resumed_download.stderr
                resumed_download_result = json.loads(resumed_download_summary.read_text(encoding="utf-8"))
                assert resumed_download_result["tree_hash"] == expected_hash, resumed_download_result
                assert resumed_download_result["summary_tree_hash_source"] == "filesystem_read", resumed_download_result
                failed_summary_path = root / f"summary-failed-{backend}.json"
                failed = subprocess.run(command + ["--max-chunks", "1", "--json-summary", str(failed_summary_path)],
                                        text=True, capture_output=True, timeout=90)
                assert failed.returncode != 0, failed.stdout + failed.stderr
                if failed_summary_path.exists():
                    failed_summary = json.loads(failed_summary_path.read_text(encoding="utf-8"))
                    assert failed_summary["summary_tree_hash_source"] == "filesystem_read", failed_summary
            assert len(observed_hashes) == 1, observed_hashes
            print(json.dumps({"result": "pass", "reference_tree_hash": expected_hash,
                              "hardware_available": hardware_available, "runs": results}, sort_keys=True))
        finally:
            stop_server(server, server_log)


if __name__ == "__main__":
    main()