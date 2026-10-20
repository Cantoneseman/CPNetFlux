#!/usr/bin/env python3
"""Bounded V3 lane matrix; peer mode requires an already verified same-source build."""
from __future__ import annotations

import argparse
import hashlib
import inspect
import json
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from cpnetflux_fault_proxy import FaultProxy
from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_lane_recovery_smoke import make_tls_files
from run_cpnetflux_lane_recovery_interop import ssh_run, remote_start, remote_stop, wait_for_tcp
from tree_smoke_common import free_port, tree_hash, file_sha256, stop_server


def make_dataset(root: Path, lanes: int) -> None:
    root.mkdir(parents=True, exist_ok=False)
    (root / "nested").mkdir()
    for number in range(20):
        (root / "nested" / f"small-{number:02}.bin").write_bytes(
            bytes((number,)) * (4096 + number))
    (root / "empty.bin").write_bytes(b"")
    # Force striping above the planner's lanes * 16 MiB threshold.
    remaining = (lanes * 16 + 1) * 1024 * 1024 + 19
    block = bytes(range(256)) * 4096
    with (root / "large-ranges.bin").open("wb") as output:
        while remaining:
            count = min(remaining, len(block))
            output.write(block[:count])
            remaining -= count


def checkpoints(root: Path) -> list[dict]:
    records = []
    for path in sorted(root.rglob("*.manifest")):
        fields = dict(line.split("=", 1) for line in path.read_text().splitlines()
                      if "=" in line)
        records.append({"path": str(path.relative_to(root)),
                        "sha256": file_sha256(path),
                        "state": fields.get("state"),
                        "transfer_id": fields.get("transfer_id"),
                        "verified_chunks": len(fields.get("verified_chunks", "").split(","))
                        if fields.get("verified_chunks") else 0})
    return records


def remote_python(alias: str, body: str, *args: str):
    # Arguments stay structured until the existing SSH helper quotes them.
    result = ssh_run(alias, ["python3", "-c", body, *map(str, args)], timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def remote_data(alias: str, operation: str, root: Path, lanes: int = 0):
    prelude = "from pathlib import Path\nimport sys,json,hashlib\n"
    functions = "\n".join(inspect.getsource(fn)
                          for fn in (file_sha256, tree_hash, make_dataset, checkpoints))
    statement = {
        "make": "make_dataset(Path(sys.argv[1]),int(sys.argv[2])); print('{}')",
        "inspect": "print(json.dumps({'tree':tree_hash(Path(sys.argv[1])),"
                   "'checkpoints':checkpoints(Path(sys.argv[1]))}))",
    }[operation]
    return json.loads(remote_python(alias, prelude + functions + "\n" + statement, root, lanes))


def peer_remove(alias: str, root: Path) -> None:
    # Only remove this runner's explicitly named case payload.
    assert root.is_absolute() and "/V3-LOGICAL-10LINK-01/" in str(root)
    remote_python(alias,
                  "from pathlib import Path\nimport shutil,sys\n"
                  "p=Path(sys.argv[1]).resolve()\n"
                  "assert str(p).startswith('/tmp/cpnetflux-runs/V3-LOGICAL-10LINK-01/')\n"
                  "assert p.name in ('dataset','uploaded')\n"
                  "shutil.rmtree(p,ignore_errors=True)\n", root)


def probe_admission(host: str, port: int, max_links: int) -> list[dict]:
    records = []
    # No data command is sent; every rejection is before payload.
    for channels, links, caps, expected in [
        (10, 10, 0, 200 if max_links == 10 else 504),
        (0, 1, 0, 504), (11, 10, 0, 504),
        (8, 10, 0, 504), (10, 0, 0, 504),
        (10, 11, 0, 504), (10, 10, 1, 504),
    ]:
        with socket.create_connection((host, port), timeout=5) as sock:
            stream = sock.makefile("rwb", buffering=0)
            def reply():
                line = stream.readline().decode().strip()
                assert line, "missing control reply"
                return line
            assert reply().startswith("220")
            stream.write(b"USER cpnetflux\r\n")
            login = reply()
            if login.startswith("331"):
                stream.write(b"PASS cpnetflux\r\n")
                assert reply().startswith("230")
            else:
                assert login.startswith("230"), login
            command = (f"XCPNETFLUX V3 WINDOW=2 CHANNELS={channels} "
                       f"CHECKSUM=crc32c RESUME=0 DATA_TLS=off "
                       f"LINKS={links} LINK_CAPS={caps} SCHED=dynamic")
            stream.write((command + "\r\n").encode())
            result = reply()
            assert result.startswith(str(expected)), (command, result, expected)
            assert "V3" in result, result
            records.append({"command": command, "reply": result, "payload_commands": 0})
    return records


def validate_ranges(summary: dict, lanes: int) -> list[dict]:
    size = (lanes * 16 + 1) * 1024 * 1024 + 19
    timings = [entry for entry in summary["file_timings"]
               if entry["relative_path"] == "large-ranges.bin"]
    assert timings, "large file has no timing/range evidence"
    # range_id=0 is the V3 whole-file sentinel, with offset/length both zero.
    if lanes == 1:
        assert len(timings) == 1 and timings[0]["range_id"] == 0, timings
        assert timings[0]["offset"] == timings[0]["length"] == 0, timings
    ranges = sorted({(entry["offset"], entry["length"] if entry["range_id"] else
                      entry["size"]) for entry in timings})
    cursor = 0
    for offset, length in ranges:
        assert offset == cursor and length > 0, (cursor, offset, length)
        cursor += length
    assert cursor == size, (cursor, size)
    assert len(ranges) >= lanes, (lanes, ranges)
    channels = {entry["channel_index"] for entry in timings}
    assert len(channels) == lanes, (lanes, channels)
    return [{"offset": offset, "length": length} for offset, length in ranges]


def run_case(args, root: Path, lanes: int, direction: str,
             *, tls: bool = False, reset: bool = False) -> dict:
    label = f"{lanes}-{direction}-{'tls' if tls else 'reset' if reset else 'plain'}"
    case = root / label
    case.mkdir()
    source, server_root = case / "source", case / "server-root"
    server_root.mkdir()
    make_dataset(source, lanes)
    expected = tree_hash(source)
    alias = args.peer_alias
    remote_case = Path(args.peer_run_dir) / label if alias else None
    if alias:
        result = ssh_run(alias, ["mkdir", str(remote_case)])
        assert result.returncode == 0, result.stderr
        peer_root = remote_case / "server-root"
        result = ssh_run(alias, ["mkdir", str(peer_root)])
        assert result.returncode == 0, result.stderr
    else:
        peer_root = server_root
    if direction == "download":
        if alias:
            remote_data(alias, "make", peer_root / "dataset", lanes)
        else:
            shutil.copytree(source, peer_root / "dataset")
    control = args.peer_port if alias else free_port()
    data_base = args.peer_data_base if alias else clamp_passive_data_port_base(free_port())
    cert = key = None
    if tls:
        cert, key = make_tls_files(case)
    server_command = [
        str((Path(args.peer_build_dir) if alias else args.build_dir) /
            "cpnetflux-gridftp-server"), "--host", "0.0.0.0" if alias else "127.0.0.1",
        "--port", str(control), "--data-port-base", str(data_base),
        "--root", str(peer_root), "--connections", "2",
        "--chunk-size", "65536", "--buffer-size", "32768",
        "--checksum", "crc32c", "--link-set-max-links", "10",
        "--link-set-capabilities", "0",
    ]
    if tls:
        server_command += ["--tls-mode", "required", "--tls-cert-file", str(cert),
                           "--tls-key-file", str(key), "--data-tls-mode", "required"]
    server_log = case / "server.log"
    server = proxy = None
    try:
        if alias:
            server = remote_start(alias, server_command, str(remote_case / "server.log"))
        else:
            with server_log.open("w") as output:
                server = subprocess.Popen(server_command, stdout=output,
                                          stderr=subprocess.STDOUT)
        host = args.peer_host if alias else "127.0.0.1"
        wait_for_tcp(host, control)
        if reset:
            proxy = FaultProxy("127.0.0.2", "127.0.0.1", control, data_base,
                               count=16, drop_bytes=512 * 1024, drops=1,
                               delay=0.0001, log=case / "proxy.jsonl",
                               target_data_connection=10)
            host = "127.0.0.2"
        admissions = probe_admission(host, control, 10) if lanes == 10 and not tls else []
        summary_path = case / "summary.json"
        command = [
            str(args.build_dir / f"cpnetflux-tree-{direction}-client"),
            "--host", host, "--port", str(control),
            "--source-dir", str(source) if direction == "upload" else "dataset",
            "--dest-dir", "uploaded" if direction == "upload" else str(case / "downloaded"),
            "--connections", "2", "--file-parallelism", str(lanes),
            "--link-set-links", str(lanes), "--chunk-size", "65536",
            "--buffer-size", "32768", "--checksum", "crc32c",
            "--control-reuse", "worker", "--data-session-reuse", "tree",
            "--file-scheduling", "dynamic", "--data-pending-window", "2",
            "--scheduler", "off", "--reconnect-attempts", "1" if reset else "0",
            "--transfer-timeout-ms", "120000", "--phase-timing", "on",
            "--json-summary", str(summary_path),
        ]
        if tls:
            command += ["--control-pipeline-depth", "2", "--tls-mode", "required",
                        "--tls-ca-file", str(cert), "--tls-server-name", "localhost",
                        "--data-tls-mode", "required"]
        (case / "commands.json").write_text(json.dumps(
            {"server": server_command, "client": command}, indent=2))
        started = time.monotonic()
        result = subprocess.run(command, text=True, capture_output=True, timeout=150)
        (case / "client.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            failure = {"label": label, "client_exit": result.returncode,
                       "server_exit_before_cleanup": None if alias else server.poll(),
                       "server_root_files": [] if alias else
                       sorted(str(p.relative_to(server_root)) for p in server_root.rglob("*"))}
            (case / "failure.json").write_text(json.dumps(failure, indent=2))
            raise AssertionError((label, result.stdout, result.stderr, failure))

        summary = json.loads(summary_path.read_text())
        for key_name, expected_value in (
            ("result", "pass"), ("actual_transport", "v3"),
            ("actual_mode", "persistent_tree"), ("fallback_reason", ""),
            ("link_set_requested_links", lanes), ("link_set_accepted_links", lanes),
            ("data_connect_count", lanes + int(reset)),
            ("lane_local_recovery_count", int(reset)), ("control_reconnect_count", 0),
        ):
            assert summary[key_name] == expected_value, (label, key_name, summary)
        assert len(set(summary["persistent_lane_ids"])) == lanes, summary
        counts = summary["persistent_lane_range_completed"]
        assert len(counts) == lanes, (label, counts)
        if lanes > 1:
            assert all(count > 0 for count in counts), (label, counts)
        else:
            assert {entry["channel_index"] for entry in summary["file_timings"]} == {0}
        ranges = validate_ranges(summary, lanes)
        destination = peer_root / "uploaded" if direction == "upload" else case / "downloaded"
        observed = (remote_data(alias, "inspect", destination)
                    if alias and direction == "upload" else
                    {"tree": tree_hash(destination), "checkpoints": checkpoints(destination)})
        assert tuple(observed["tree"]) == expected, (expected, observed)
        manifests = observed["checkpoints"]
        assert len(manifests) == expected[1], (label, len(manifests), expected)
        assert all(entry["state"] == "committed" for entry in manifests), manifests
        resets = []
        if reset:
            resets = [json.loads(line) for line in (case / "proxy.jsonl").read_text().splitlines()
                      if json.loads(line).get("event") == "reset"]
            assert proxy.dropped == 1 and len(resets) == 1, resets
            assert resets[0]["target"] is True, resets
        record = {"case": label, "lanes": lanes, "direction": direction,
                  "tls": tls, "reset": reset, "seconds": time.monotonic() - started,
                  "expected_tree": expected, "observed_tree": observed["tree"],
                  "checkpoints": manifests, "ranges": ranges,
                  "lane_work_counts": counts, "admission": admissions,
                  "proxy_resets": resets, "result": "pass"}
        (case / "result.json").write_text(json.dumps(record, indent=2))
        return record
    finally:
        if proxy:
            proxy.close()
        if server is not None:
            if alias:
                remote_stop(alias, server)
                server_log.write_text(remote_python(alias, "import sys; print(open(sys.argv[1]).read())",
                                                    remote_case / "server.log"))
            else:
                stop_server(server, server_log)
        # Evidence is retained; fixtures and ephemeral private keys are disposable.
        for payload in [source, server_root / "dataset", server_root / "uploaded",
                        case / "downloaded"]:
            if payload.exists():
                shutil.rmtree(payload)
        if alias:
            for payload in [peer_root / "dataset", peer_root / "uploaded"]:
                peer_remove(alias, payload)
        if key:
            key.unlink(missing_ok=True)


def probe_peer_eight(args, root: Path) -> list[dict]:
    case = root / "peer-eight-admission"
    case.mkdir()
    port = free_port()
    command = [str(args.build_dir / "cpnetflux-gridftp-server"),
               "--host", "127.0.0.1", "--port", str(port), "--root", str(case),
               "--checksum", "crc32c", "--link-set-max-links", "8",
               "--link-set-capabilities", "0"]
    log = case / "server.log"
    with log.open("w") as output:
        server = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
    try:
        wait_for_tcp("127.0.0.1", port)
        return probe_admission("127.0.0.1", port, 8)
    finally:
        stop_server(server, log)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--lanes", nargs="+", type=int, default=[1, 2, 4, 8, 10],
                        choices=[1, 2, 4, 8, 10])
    parser.add_argument("--peer-alias")
    parser.add_argument("--peer-host")
    parser.add_argument("--peer-build-dir")
    parser.add_argument("--peer-run-dir")
    parser.add_argument("--peer-port", type=int, default=24600)
    parser.add_argument("--peer-data-base", type=int, default=24610)
    parser.add_argument("--skip-extras", action="store_true")
    args = parser.parse_args()
    if args.peer_alias:
        assert all([args.peer_host, args.peer_build_dir, args.peer_run_dir])
        assert str(Path(args.peer_run_dir)).startswith(
            "/tmp/cpnetflux-runs/V3-LOGICAL-10LINK-01/")
        result = ssh_run(args.peer_alias, ["mkdir", args.peer_run_dir])
        assert result.returncode == 0, result.stderr
    context = None if args.evidence_dir else tempfile.TemporaryDirectory(
        prefix="cpnetflux-logical-links-")
    root = args.evidence_dir or Path(context.name)
    root.mkdir(parents=True, exist_ok=True)
    try:
        admission = probe_peer_eight(args, root)
        records = []
        for lanes in args.lanes:
            for direction in ("upload", "download"):
                record = run_case(args, root, lanes, direction)
                records.append(record)
                print(json.dumps({"case": record["case"], "result": "pass",
                                  "seconds": record["seconds"]}), flush=True)
        if not args.peer_alias and not args.skip_extras:
            for tls, reset in [(True, False), (False, True)]:
                for direction in ("upload", "download"):
                    record = run_case(args, root, 10, direction, tls=tls, reset=reset)
                    records.append(record)
                    print(json.dumps({"case": record["case"], "result": "pass",
                                      "seconds": record["seconds"]}), flush=True)
        (root / "matrix.json").write_text(json.dumps(
            {"cases": records, "peer_eight_admission": admission,
             "boundary": "logical TCP lanes; no physical path or throughput claim"}, indent=2))
        print(f"PASS: {len(records)} transfers; admission/coverage/hash/checkpoint checks passed")
        return 0
    finally:
        if context:
            context.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
