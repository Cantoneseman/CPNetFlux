#!/usr/bin/env python3
"""Run deterministic lane-local recovery cases across Shenzhen and Shanghai."""
from __future__ import annotations

import argparse
import json
import os
import shlex
import socket
import subprocess
import time
from pathlib import Path

from cpnetflux_fault_proxy import FaultProxy
from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import stop_server, tree_hash

SSH_OPTIONS = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=15"]
SHENZHEN_PUBLIC = "120.25.121.51"
SHANGHAI_PUBLIC = "47.116.174.181"
SHENZHEN_BIND = "192.168.10.22"
SHANGHAI_BIND = "192.168.10.138"
REMOTE_ALIAS = "gridflux-beta-shanghai"
REMOTE_ROOT = Path("/tmp/cpnetflux-runs/V3-LANE-DATA-FAULT-RECOVERY-01/interop")


def run_process(command: list[str], *, timeout: int = 180) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, timeout=timeout, check=False)


def ssh_command(alias: str, argv: list[str]) -> list[str]:
    return ["ssh", *SSH_OPTIONS, alias, shlex.join(argv)]


def ssh_run(alias: str, argv: list[str], *, timeout: int = 180) -> subprocess.CompletedProcess[str]:
    return run_process(ssh_command(alias, argv), timeout=timeout)


def scp_to(alias: str, source: Path, destination: str, *, recursive: bool = False) -> None:
    command = ["scp", *SSH_OPTIONS]
    if recursive:
        command.append("-r")
    command += [str(source), f"{alias}:{destination}"]
    result = run_process(command, timeout=180)
    if result.returncode:
        raise RuntimeError(f"scp to {alias} failed:\n{result.stdout}{result.stderr}")


def scp_from(alias: str, source: str, destination: Path, *, recursive: bool = False) -> None:
    command = ["scp", *SSH_OPTIONS]
    if recursive:
        command.append("-r")
    command += [f"{alias}:{source}", str(destination)]
    result = run_process(command, timeout=180)
    if result.returncode:
        raise RuntimeError(f"scp from {alias} failed:\n{result.stdout}{result.stderr}")


def remote_start(alias: str, argv: list[str], log: str) -> int:
    command = f"nohup {shlex.join(argv)} > {shlex.quote(log)} 2>&1 </dev/null & echo $!"
    result = run_process(["ssh", *SSH_OPTIONS, alias, command], timeout=30)
    if result.returncode:
        raise RuntimeError(f"remote start failed:\n{result.stdout}{result.stderr}")
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not lines or not lines[-1].isdigit():
        raise RuntimeError(f"remote start returned no pid: {result.stdout}{result.stderr}")
    return int(lines[-1])


def remote_stop(alias: str, pid: int) -> None:
    result = ssh_run(alias, ["kill", "-TERM", str(pid)], timeout=20)
    if result.returncode not in (0, 1):
        raise RuntimeError(f"remote stop failed for {pid}: {result.stdout}{result.stderr}")
    for _ in range(20):
        probe = ssh_run(alias, ["kill", "-0", str(pid)], timeout=20)
        if probe.returncode:
            return
        time.sleep(0.1)


def wait_for_tcp(host: str, port: int, timeout: float = 15.0) -> None:
    deadline = time.monotonic() + timeout
    last: OSError | None = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return
        except OSError as error:
            last = error
            time.sleep(0.1)
    raise RuntimeError(f"{host}:{port} did not accept TCP: {last}")


def make_dataset(root: Path) -> tuple[str, int, int]:
    root.mkdir(parents=True, exist_ok=False)
    block = bytes(range(256)) * 4096
    with (root / "large-ranges.bin").open("wb") as output:
        for _ in range(64):
            output.write(block)
    (root / "small-a.bin").write_bytes(b"lane-a\n" * 8192)
    (root / "nested").mkdir()
    (root / "nested" / "small-b.bin").write_bytes(bytes(range(251)) * 257)
    return tree_hash(root)


def copy_tree_for_server(source: Path, alias: str | None, destination: Path) -> None:
    if alias is None:
        destination.mkdir(parents=True, exist_ok=True)
        for path in source.rglob("*"):
            target = destination / path.relative_to(source)
            if path.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.write_bytes(path.read_bytes())
        return
    ssh_run(alias, ["mkdir", "-p", str(destination.parent)], timeout=30)
    scp_to(alias, source, str(destination), recursive=True)


def client_command(binary: str, host: str, port: int, source: str, destination: str, summary: str,
                   reconnect_attempts: int = 2, scheduler: str = "off",
                   transfer_timeout_ms: int = 120000) -> list[str]:
    command = [
        binary,
        "--host", host,
        "--port", str(port),
        "--source-dir", source,
        "--dest-dir", destination,
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
        "--scheduler", scheduler,
        "--reconnect-attempts", str(reconnect_attempts),
        "--transfer-timeout-ms", str(transfer_timeout_ms),
        "--phase-timing", "on",
        "--json-summary", summary,
    ]
    if scheduler == "global":
        command += ["--scheduler-policy", "fixed",
                    "--scheduler-metrics-dir", str(Path(summary).parent / "metrics")]
    return command


def server_command(binary: str, root: str, port: int, data_base: int) -> list[str]:
    return [
        binary,
        "--host", "127.0.0.1",
        "--port", str(port),
        "--data-port-base", str(data_base),
        "--root", root,
        "--connections", "2",
        "--chunk-size", "65536",
        "--buffer-size", "32768",
        "--checksum", "crc32c",
        "--link-set-max-links", "2",
        "--link-set-capabilities", "0",
    ]


def parse_proxy_log(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]


def run_case(build_dir: Path, root: Path, *, server_side: str, transfer: str,
             fault_kind: str, remote_bin: Path, reconnect_attempts: int = 2,
             control_reset_data_connections: list[int] | None = None,
             data_reset_connections: list[int] | None = None,
             remote_root: Path = REMOTE_ROOT,
             expect_budget_exhausted: bool = False,
             expect_no_recovery: bool = False, scheduler: str = "off",
             transfer_timeout_ms: int = 120000) -> dict:
    multi_data_fault = fault_kind == "multi-data"
    mixed_fault = fault_kind == "mixed"
    fault_budget_matrix = multi_data_fault or mixed_fault
    if data_reset_connections is None:
        data_reset_connections = [1, 2] if multi_data_fault else ([1] if mixed_fault else [2])
    control_targets = (
        control_reset_data_connections or [2] if mixed_fault else
        control_reset_data_connections or []
    )
    fault_count = (
        len(data_reset_connections) + len(control_targets)
        if mixed_fault else len(data_reset_connections) if multi_data_fault else 1
    )
    case_suffix = (f"{server_side}-{transfer}-{fault_kind}-budget-{reconnect_attempts}"
                   if fault_budget_matrix else f"{server_side}-{transfer}-{fault_kind}")
    case = root / case_suffix
    case.mkdir()
    source = case / "source"
    expected = make_dataset(source)
    server_root = case / "server-root"
    server_root.mkdir()
    server_alias = REMOTE_ALIAS if server_side == "shanghai" else None
    client_alias = REMOTE_ALIAS if server_side == "shenzhen" else None
    server_host = SHANGHAI_PUBLIC if server_side == "shanghai" else SHENZHEN_PUBLIC
    control_port = 24600 if server_side == "shanghai" else 24700
    data_base = clamp_passive_data_port_base(24610 if server_side == "shanghai" else 24710)
    remote_case = remote_root / root.name / case.name
    remote_server_root = remote_case / "server-root"
    remote_source = remote_case / "source"
    remote_downloaded = remote_case / "downloaded"
    remote_log = remote_case / "server.log"
    remote_proxy_log = remote_case / "proxy.jsonl"
    remote_server_pid: int | None = None
    remote_proxy_pid: int | None = None
    local_server: subprocess.Popen[str] | None = None
    local_proxy: FaultProxy | None = None
    try:
        if server_alias or client_alias:
            parent = ssh_run(REMOTE_ALIAS, ["mkdir", "-p", str(remote_case.parent)], timeout=30)
            if parent.returncode:
                raise RuntimeError(f"remote evidence directory creation failed: {parent.stdout}{parent.stderr}")
            created = ssh_run(REMOTE_ALIAS, ["mkdir", str(remote_case)], timeout=30)
            if created.returncode:
                raise RuntimeError(f"remote case path already exists or cannot be created: {remote_case}: "
                                   f"{created.stdout}{created.stderr}")
        if server_alias:
            created = ssh_run(server_alias, ["mkdir", "-p", str(remote_server_root)], timeout=30)
            if created.returncode:
                raise RuntimeError(f"remote server root creation failed: {created.stdout}{created.stderr}")
        if transfer == "download":
            server_dataset = remote_server_root / "dataset" if server_alias else server_root / "dataset"
            copy_tree_for_server(source, server_alias, server_dataset)
        elif client_alias:
            ssh_run(client_alias, ["mkdir", "-p", str(remote_case)], timeout=30)
            scp_to(client_alias, source, str(remote_case), recursive=True)

        if server_alias or client_alias:
            deployment_alias = server_alias or client_alias
            scp_to(deployment_alias, build_dir / "cpnetflux-gridftp-server",
                   str(remote_bin / "cpnetflux-gridftp-server"))
            scp_to(deployment_alias, build_dir / "cpnetflux-tree-upload-client",
                   str(remote_bin / "cpnetflux-tree-upload-client"))
            scp_to(deployment_alias, build_dir / "cpnetflux-tree-download-client",
                   str(remote_bin / "cpnetflux-tree-download-client"))
        if server_alias:
            scp_to(server_alias, Path(__file__).with_name("cpnetflux_fault_proxy.py"),
                   str(remote_bin / "cpnetflux_fault_proxy.py"))
            remote_server_pid = remote_start(
                server_alias,
                server_command(str(remote_bin / "cpnetflux-gridftp-server"), str(remote_server_root), control_port, data_base),
                str(remote_log),
            )
            time.sleep(0.4)
            if fault_kind == "control":
                control_targets = control_reset_data_connections or [2]
                fault_args = [item for target in control_targets
                              for item in ("--drop-control-after-data-connection", str(target))]
            elif multi_data_fault:
                fault_args = [item for target in data_reset_connections
                              for item in ("--target-data-connection", str(target))]
                fault_args.append("--synchronize-data-resets")
            elif mixed_fault:
                fault_args = [item for target in data_reset_connections
                              for item in ("--target-data-connection", str(target))]
                fault_args.extend(
                    item for target in control_targets
                    for item in ("--drop-control-after-data-connection", str(target))
                )
                fault_args.append("--synchronize-mixed-resets")
            else:
                fault_args = ["--target-data-connection", "2"]
            remote_proxy_pid = remote_start(
                server_alias,
                [
                    "python3", str(remote_bin / "cpnetflux_fault_proxy.py"),
                    "--listen-host", SHANGHAI_BIND if server_side == "shanghai" else SHENZHEN_BIND,
                    "--backend-host", "127.0.0.1",
                    "--control-port", str(control_port),
                    "--data-base", str(data_base),
                    "--count", "8",
                    "--delay", "0.0001",
                    "--drop-bytes", str(32768),
                    "--drops", str(fault_count),
                    *fault_args,
                    "--log", str(remote_proxy_log),
                ],
                str(remote_case / "proxy.log"),
            )
        else:
            local_server_log = case / "server.log"
            command = server_command(str(build_dir / "cpnetflux-gridftp-server"), str(server_root), control_port, data_base)
            handle = local_server_log.open("w", encoding="utf-8")
            local_server = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
            handle.close()
            if fault_kind == "control":
                fault_options = {"drop_control_after_data_connection": (
                    control_reset_data_connections[0]
                    if control_reset_data_connections and len(control_reset_data_connections) == 1
                    else (control_reset_data_connections or [2]))}
            elif multi_data_fault:
                fault_options = {
                    "target_data_connections": data_reset_connections,
                    "synchronize_data_resets": True,
                }
            elif mixed_fault:
                fault_options = {
                    "target_data_connections": data_reset_connections,
                    "synchronize_mixed_resets": True,
                    "drop_control_after_data_connection": control_targets,
                }
            else:
                fault_options = {"target_data_connection": 2}
            local_proxy = FaultProxy(
                SHENZHEN_BIND, "127.0.0.1", control_port, data_base,
                count=8, drop_bytes=32768,
                drops=fault_count,
                delay=0.0001, log=case / "proxy.jsonl", **fault_options,
            )

        wait_for_tcp(server_host if server_alias else SHENZHEN_BIND, control_port)
        client_binary_name = f"cpnetflux-tree-{transfer}-client"
        if transfer == "upload":
            source_arg = str(remote_source) if client_alias else str(source)
            destination_arg = "uploaded"
        else:
            source_arg = "dataset"
            destination_arg = str(remote_downloaded) if client_alias else str(case / "downloaded")
        summary_name = str(remote_case / "summary.json") if client_alias else str(case / "summary.json")
        command = client_command(
            str(remote_bin / client_binary_name) if client_alias else str(build_dir / client_binary_name),
            server_host, control_port, source_arg, destination_arg, summary_name,
            reconnect_attempts=reconnect_attempts,
            scheduler=scheduler,
            transfer_timeout_ms=transfer_timeout_ms,
        )
        started = time.monotonic()
        if client_alias:
            result = ssh_run(client_alias, command, timeout=180)
            (case / "client.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            if result.returncode and not (expect_budget_exhausted or expect_no_recovery):
                raise AssertionError(result.stdout + result.stderr)
            scp_from(client_alias, str(remote_case / "summary.json"), case / "summary.json")
            if transfer == "download":
                scp_from(client_alias, str(remote_downloaded), case / "result", recursive=True)
            if scheduler == "global":
                scp_from(client_alias, str(remote_case / "metrics"), case / "metrics", recursive=True)
        else:
            result = run_process(command, timeout=180)
            (case / "client.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            if result.returncode and not (expect_budget_exhausted or expect_no_recovery):
                raise AssertionError(result.stdout + result.stderr)
        if server_alias:
            remote_stop(server_alias, remote_proxy_pid)
            remote_proxy_pid = None
            remote_stop(server_alias, remote_server_pid)
            remote_server_pid = None
            scp_from(server_alias, str(remote_proxy_log), case / "proxy.jsonl")
            scp_from(server_alias, str(remote_log), case / "server.log")
            if transfer == "download" and not client_alias:
                actual_root = case / "downloaded"
            elif expect_budget_exhausted or expect_no_recovery:
                actual_root = None
            else:
                remote_result_root = remote_server_root / "uploaded" if transfer == "upload" else remote_downloaded
                scp_from(server_alias, str(remote_result_root), case / "result", recursive=True)
                actual_root = case / "result"
        else:
            assert local_server is not None
            stop_server(local_server, case / "server.log")
            local_server = None
            assert local_proxy is not None
            local_proxy.close()
            local_proxy = None
            actual_root = server_root / "uploaded" if transfer == "upload" else (
                case / "result" if client_alias else case / "downloaded")
        summary = json.loads((case / "summary.json").read_text(encoding="utf-8"))
        proxy_events = parse_proxy_log(case / "proxy.jsonl")
        resets = [event for event in proxy_events if event.get("event") == "reset"]
        if multi_data_fault:
            assert len(resets) == len(data_reset_connections), resets
            assert all(event.get("target") is True for event in resets), resets
            assert {event.get("data_connection") for event in resets} == set(data_reset_connections), resets
            reset_times = sorted(event["time"] for event in resets)
            assert reset_times[-1] - reset_times[0] <= 0.5, resets
        elif mixed_fault:
            data_resets = [event for event in resets if event.get("scope") == "data"]
            control_resets = [event for event in resets if event.get("scope") == "control"]
            assert len(data_resets) == len(control_resets) == 1, resets
            assert all(event.get("target") is True for event in resets), resets
            assert data_resets[0].get("data_connection") == data_reset_connections[0], resets
            assert control_resets[0].get("trigger_data_connection") == control_targets[0], resets
            assert data_resets[0].get("lane_index") in (0, 1), resets
            assert control_resets[0].get("lane_index") in (0, 1), resets
            assert data_resets[0].get("lane_index") != control_resets[0].get("lane_index"), resets
            reset_times = sorted(event["time"] for event in resets)
            assert reset_times[-1] - reset_times[0] <= 0.5, resets
        if expect_budget_exhausted or expect_no_recovery:
            assert result.returncode != 0, result.stdout + result.stderr
            recovery_used = summary.get("recovery_attempts_used", 0)
            if expect_budget_exhausted:
                expected_recoveries = min(reconnect_attempts, fault_count) if fault_budget_matrix else reconnect_attempts
                assert recovery_used == expected_recoveries, summary
                assert summary.get("lane_local_recovery_count", 0) == expected_recoveries, summary
            else:
                assert reconnect_attempts == 0, reconnect_attempts
                assert recovery_used == 0, summary
                assert summary.get("lane_local_recovery_count", 0) == 0, summary
                assert summary.get("lane_control_reconnect_count", 0) == 0, summary
            client_output = (case / "client.log").read_text(encoding="utf-8")
            if expect_budget_exhausted:
                if fault_budget_matrix:
                    assert "reconnect attempt budget exhausted" in summary.get("error", {}).get("message", ""), summary
                assert "reconnect attempt budget exhausted" in client_output, client_output
            return {
                "case": case.name, "server_side": server_side, "transfer": transfer,
                "fault_kind": fault_kind, "expected_budget": reconnect_attempts,
                "actual_recoveries": recovery_used, "actual_faults": len(resets),
                "summary": summary,
            }
        actual = tree_hash(actual_root)
        assert actual == expected, (actual, expected)
        expected_faults = fault_count
        assert len(resets) == expected_faults and all(event.get("target") is True for event in resets), resets
        if not fault_budget_matrix:
            assert resets[0].get("scope", "data") == fault_kind, resets
        assert summary["lane_local_recovery_count"] == expected_faults, summary
        assert summary["recovery_attempts_used"] == expected_faults, summary
        assert summary["reconnect_attempts"] == 0, summary
        if scheduler == "global":
            assert summary["scheduler_mode"] == "global", summary
        assert summary["control_reconnect_count"] == 0, summary
        if fault_kind == "control":
            assert summary["lane_control_reconnect_count"] == 1, summary
            assert summary["reconnect_attempts"] == 0, summary
            assert summary["control_connect_count"] == 3, summary
            failed_lane = resets[0].get("lane_index")
            assert failed_lane in (0, 1), resets
            healthy_timings = [
                item for item in summary["file_timings"]
                if item["channel_index"] != failed_lane
            ]
            assert healthy_timings and all(item["attempt"] == 0 for item in healthy_timings), summary
            lane_events = [event for event in proxy_events
                           if event.get("event") == "directory_channel_mapped"]
            failed_lane_events = [event for event in lane_events
                                  if event.get("lane_index") == failed_lane]
            healthy_lane_events = [event for event in lane_events
                                   if event.get("lane_index") != failed_lane]
            assert len(failed_lane_events) == 2, lane_events
            assert len({event.get("batch_id") for event in failed_lane_events}) == 1, lane_events
            assert len(healthy_lane_events) == 1, lane_events
        elif mixed_fault:
            assert summary["lane_control_reconnect_count"] == 1, summary
            assert summary["control_connect_count"] == 3, summary
        else:
            assert summary["lane_control_reconnect_count"] == 0, summary
        assert summary["data_connect_count"] == 2 + expected_faults, summary
        ranges = summary["persistent_lane_range_completed"]
        assert len(ranges) == 2 and all(value > 0 for value in ranges), summary
        return {
            "case": case.name,
            "server_side": server_side,
            "transfer": transfer,
            "fault_kind": fault_kind,
            "seconds": time.monotonic() - started,
            "expected": expected,
            "actual": actual,
            "summary": summary,
            "proxy_resets": resets,
        }
    finally:
        if local_proxy is not None:
            local_proxy.close()
        if local_server is not None:
            stop_server(local_server, case / "server.log")
        if remote_proxy_pid is not None:
            remote_stop(REMOTE_ALIAS, remote_proxy_pid)
        if remote_server_pid is not None:
            remote_stop(REMOTE_ALIAS, remote_server_pid)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--server-side", choices=("shanghai", "shenzhen", "both"), default="both",
                        help="run only one server side while diagnosing, or both for acceptance")
    parser.add_argument("--fault-kind", choices=("data", "control", "multi-data", "mixed"), default="data")
    parser.add_argument("--reconnect-attempts", type=int, default=2)
    parser.add_argument("--control-reset-data-connection", type=int, action="append")
    parser.add_argument("--data-reset-connection", type=int, action="append")
    parser.add_argument("--multi-fault-budget-matrix", action="store_true")
    parser.add_argument("--mixed-fault-budget-matrix", action="store_true")
    parser.add_argument("--remote-root", type=Path, default=REMOTE_ROOT)
    parser.add_argument("--expect-budget-exhausted", action="store_true")
    parser.add_argument("--expect-no-recovery", action="store_true")
    parser.add_argument("--transfer", choices=("upload", "download", "both"), default="upload")
    parser.add_argument("--scheduler", choices=("off", "global"), default="off")
    parser.add_argument("--transfer-timeout-ms", type=int, default=120000)
    parser.add_argument("--remote-bin", type=Path, default=Path("/tmp/cpnetflux-runs/V3-LANE-DATA-FAULT-RECOVERY-01/bin"))
    args = parser.parse_args()
    if args.transfer_timeout_ms <= 0:
        parser.error("--transfer-timeout-ms must be positive")
    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    run_id = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()) + f"-{time.time_ns()}"
    root = args.evidence_dir / run_id
    root.mkdir(parents=True, exist_ok=False)
    args.remote_bin = Path(args.remote_bin)
    ssh_run(REMOTE_ALIAS, ["mkdir", "-p", str(args.remote_bin)], timeout=30)
    scp_to(REMOTE_ALIAS, Path(__file__).with_name("cpnetflux_fault_proxy.py"), str(args.remote_bin / "cpnetflux_fault_proxy.py"))
    if args.multi_fault_budget_matrix and args.fault_kind != "multi-data":
        parser.error("--multi-fault-budget-matrix requires --fault-kind multi-data")
    if args.mixed_fault_budget_matrix and args.fault_kind != "mixed":
        parser.error("--mixed-fault-budget-matrix requires --fault-kind mixed")
    records = []
    server_sides = ("shanghai", "shenzhen") if args.server_side == "both" else (args.server_side,)
    transfers = ("upload", "download") if args.transfer == "both" else (args.transfer,)
    fault_budget_matrix = args.multi_fault_budget_matrix or args.mixed_fault_budget_matrix
    budgets = (0, 1, 2) if fault_budget_matrix else (args.reconnect_attempts,)
    for server_side in server_sides:
        for transfer in transfers:
            for budget in budgets:
                records.append(run_case(
                    args.build_dir, root, server_side=server_side,
                    transfer=transfer, fault_kind=args.fault_kind,
                    remote_bin=args.remote_bin, reconnect_attempts=budget,
                    control_reset_data_connections=args.control_reset_data_connection,
                    data_reset_connections=args.data_reset_connection,
                    remote_root=args.remote_root,
                    expect_budget_exhausted=(args.expect_budget_exhausted or
                                             (fault_budget_matrix and budget < 2)),
                    expect_no_recovery=args.expect_no_recovery,
                    scheduler=args.scheduler,
                    transfer_timeout_ms=args.transfer_timeout_ms))
    input_commit = os.environ.get("CPNETFLUX_INPUT_COMMIT")
    if not input_commit:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=Path(__file__).resolve().parents[2],
            text=True,
            capture_output=True,
            check=False,
        )
        input_commit = revision.stdout.strip() if revision.returncode == 0 else "unknown"
    output = {"cases": records, "server_side_selection": args.server_side,
              "input_commit": input_commit, "evidence_run_id": run_id}
    (root / "matrix.json").write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(output, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
