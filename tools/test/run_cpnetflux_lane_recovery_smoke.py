#!/usr/bin/env python3
"""Deterministic two-lane local recovery coverage for persistent tree transfers."""
import argparse
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from cpnetflux_fault_proxy import FaultProxy
from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, tree_hash, wait_for_control, stop_server


def make_dataset(root: Path) -> tuple[str, int, int]:
    root.mkdir(parents=True, exist_ok=False)
    block = bytes(range(256)) * 4096
    large = root / "large-ranges.bin"
    with large.open("wb") as output:
        for _ in range(64):
            output.write(block)
    (root / "small-a.bin").write_bytes(b"lane-a\n" * 8192)
    (root / "nested").mkdir()
    (root / "nested" / "small-b.bin").write_bytes(bytes(range(251)) * 257)
    return tree_hash(root)


def make_tls_files(root: Path) -> tuple[Path, Path]:
    cert, key = root / "cert.pem", root / "key.pem"
    subprocess.run(
        [
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(cert), "-days", "1",
            "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return cert, key


def run_case(build_dir: Path, root: Path, direction: str, tls: bool, pipeline: int) -> dict:
    case = root / f"{direction}-{'tls-pipeline' if tls else 'plain'}"
    case.mkdir()
    source = case / "source"
    expected = make_dataset(source)
    server_root = case / "server-root"
    server_root.mkdir()
    if direction == "download":
        (server_root / "dataset").mkdir()
        for path in source.rglob("*"):
            relative = path.relative_to(source)
            destination = server_root / "dataset" / relative
            if path.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
            else:
                destination.write_bytes(path.read_bytes())
    control_port = free_port()
    data_base = clamp_passive_data_port_base(free_port())
    cert = key = None
    if tls:
        cert, key = make_tls_files(case)

    server_command = [
        str(build_dir / "cpnetflux-gridftp-server"),
        "--host", "127.0.0.1",
        "--port", str(control_port),
        "--data-port-base", str(data_base),
        "--root", str(server_root),
        "--connections", "2",
        "--chunk-size", "65536",
        "--buffer-size", "32768",
        "--checksum", "crc32c",
        "--link-set-max-links", "2",
        "--link-set-capabilities", "0",
    ]
    if tls:
        server_command += [
            "--tls-mode", "required",
            "--tls-cert-file", str(cert),
            "--tls-key-file", str(key),
            "--data-tls-mode", "required",
        ]

    server_log = case / "server.log"
    with server_log.open("w", encoding="utf-8") as output:
        server = subprocess.Popen(server_command, stdout=output, stderr=subprocess.STDOUT)
    proxy = None
    try:
        if tls:
            deadline = time.monotonic() + 10
            while True:
                try:
                    import socket
                    with socket.create_connection(("127.0.0.1", control_port), timeout=0.5):
                        break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.05)
        else:
            wait_for_control(control_port)
        proxy = FaultProxy(
            "127.0.0.2",
            "127.0.0.1",
            control_port,
            data_base,
            count=8,
            drop_bytes=1024 * 1024,
            drops=1,
            delay=0.0001,
            log=case / "proxy.jsonl",
            target_data_connection=1,
        )
        summary_path = case / "summary.json"
        source_arg = str(source) if direction == "upload" else "dataset"
        destination_arg = "uploaded" if direction == "upload" else str(case / "downloaded")
        command = [
            str(build_dir / f"cpnetflux-tree-{direction}-client"),
            "--host", "127.0.0.2",
            "--port", str(control_port),
            "--source-dir", source_arg,
            "--dest-dir", destination_arg,
            "--connections", "2",
            "--file-parallelism", "2",
            "--chunk-size", "65536",
            "--buffer-size", "32768",
            "--checksum", "crc32c",
            "--control-reuse", "worker",
            "--data-session-reuse", "tree",
            "--file-scheduling", "dynamic",
            "--link-set-links", "2",
            "--data-pending-window", "2",
            "--scheduler", "off",
            "--reconnect-attempts", "2",
            "--transfer-timeout-ms", "120000",
            "--phase-timing", "on",
            "--json-summary", str(summary_path),
        ]
        if pipeline:
            command += ["--control-pipeline-depth", str(pipeline)]
        if tls:
            command += [
                "--tls-mode", "required",
                "--tls-ca-file", str(cert),
                "--tls-server-name", "localhost",
                "--data-tls-mode", "required",
            ]

        started = time.monotonic()
        result = subprocess.run(
            command,
            text=True,
            capture_output=True,
            timeout=150,
        )
        (case / "client.log").write_text(
            result.stdout + result.stderr,
            encoding="utf-8",
        )
        if not summary_path.exists():
            raise AssertionError(
                f"{direction} summary missing, exit={result.returncode}: "
                f"{result.stdout}{result.stderr}"
            )
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        actual_root = server_root / "uploaded" if direction == "upload" else case / "downloaded"
        actual = tree_hash(actual_root)
        assert result.returncode == 0, result.stdout + result.stderr
        assert actual == expected, (actual, expected)
        assert proxy.dropped == 1, proxy.dropped
        assert proxy.target_connection is not None
        assert summary["lane_local_recovery_count"] == 1, summary
        assert summary["control_reconnect_count"] == 0, summary
        assert summary["data_connect_count"] == 3, summary
        assert summary["link_set_accepted_links"] == 2, summary
        ranges = summary["persistent_lane_range_completed"]
        assert len(ranges) == 2 and all(value > 0 for value in ranges), summary
        reset_events = [
            json.loads(line)
            for line in (case / "proxy.jsonl").read_text(encoding="utf-8").splitlines()
            if line
        ]
        reset_events = [event for event in reset_events if event.get("event") == "reset"]
        assert len(reset_events) == 1, reset_events
        assert reset_events[0]["target"] is True, reset_events
        return {
            "case": case.name,
            "direction": direction,
            "tls": tls,
            "pipeline": pipeline,
            "seconds": time.monotonic() - started,
            "expected": expected,
            "actual": actual,
            "summary": summary,
            "proxy_reset": reset_events[0],
        }
    finally:
        if proxy is not None:
            proxy.close()
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path)
    args = parser.parse_args()
    if args.evidence_dir:
        args.evidence_dir.mkdir(parents=True, exist_ok=True)
        context = None
        root = args.evidence_dir
    else:
        context = tempfile.TemporaryDirectory(prefix="cpnetflux-lane-recovery-")
        root = Path(context.name)
    try:
        records = [
            run_case(args.build_dir, root, "upload", False, 0),
            run_case(args.build_dir, root, "download", False, 0),
            run_case(args.build_dir, root, "upload", True, 2),
        ]
        output = {"cases": records}
        (root / "matrix.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(output, sort_keys=True))
        return 0
    finally:
        if context is not None:
            context.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
