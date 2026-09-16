#!/usr/bin/env python3
"""Run a real Shenzhen-to-Shanghai CPNetFlux transfer and resume demonstration."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(REPO_ROOT))

import make_demo_dataset  # noqa: E402
import run_alpha_demo  # noqa: E402
from tools.release import remote_auth  # noqa: E402
from tools.test.gridftp_port_window import (  # noqa: E402
    MAX_PASSIVE_DATA_PORT_BASE,
    assert_epsv_port_in_window,
)


def timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def format_bytes(value: int) -> str:
    units = ("B", "KiB", "MiB", "GiB")
    amount = float(value)
    for unit in units:
        if amount < 1024.0 or unit == units[-1]:
            return f"{amount:.1f}{unit}"
        amount /= 1024.0
    return f"{value}B"


def parse_byte_size(text: str) -> int:
    value = text.strip().replace("_", "")
    match = re.fullmatch(r"(?i)(\d+)([kmg]?i?b?)?", value)
    if not match:
        raise argparse.ArgumentTypeError(f"invalid byte size: {text!r}")
    number = int(match.group(1))
    suffix = (match.group(2) or "").lower()
    factors = {
        "": 1,
        "b": 1,
        "k": 1024,
        "kb": 1024,
        "kib": 1024,
        "m": 1024**2,
        "mb": 1024**2,
        "mib": 1024**2,
        "g": 1024**3,
        "gb": 1024**3,
        "gib": 1024**3,
    }
    if suffix not in factors:
        raise argparse.ArgumentTypeError(f"invalid byte size: {text!r}")
    return number * factors[suffix]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_demo_file(path: Path, size: int, seed: int) -> None:
    remaining = size
    block_index = 0
    with path.open("wb") as handle:
        while remaining > 0:
            block = make_demo_dataset.deterministic_block(seed, path.name, block_index)
            block = block * (1024 * 1024 // len(block))
            length = min(remaining, len(block))
            handle.write(block[:length])
            remaining -= length
            block_index += 1


def run_ssh(
    remote: str,
    command: str,
    *,
    check: bool = True,
    timeout: float | None = 30.0,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        remote_auth.ssh_prefix(remote, root=REPO_ROOT) + [command],
        env=remote_auth.command_env(remote, REPO_ROOT),
        text=True,
        capture_output=True,
        check=check,
        timeout=timeout,
    )


def start_remote_server(
    remote: str,
    build_dir: str,
    root: str,
    control_port: int,
    data_port_base: int,
    connections: int,
    chunk_size: int,
    event_log: str,
    log_path: str,
) -> int:
    command = [
        f"{build_dir.rstrip('/')}/cpnetflux-gridftp-server",
        "--host",
        "0.0.0.0",
        "--port",
        str(control_port),
        "--root",
        root,
        "--data-port-base",
        str(data_port_base),
        "--connections",
        str(connections),
        "--chunk-size",
        str(chunk_size),
        "--buffer-size",
        "65536",
        "--checksum",
        "crc32c",
        "--checksum-backend",
        "auto",
        "--manifest-flush-policy",
        "every_n_chunks",
        "--manifest-flush-interval-chunks",
        "1",
        "--final-verify-policy",
        "verified_chunks",
        "--event-log",
        event_log,
    ]
    quoted = " ".join(shlex.quote(item) for item in command)
    remote_command = (
        f"mkdir -p {shlex.quote(root)}; "
        f"nohup {quoted} >{shlex.quote(log_path)} 2>&1 </dev/null & "
        "printf '%s\\n' $!"
    )
    completed = run_ssh(remote, remote_command)
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if not lines or not lines[-1].isdigit():
        raise RuntimeError(f"remote server did not return a PID: {completed.stdout!r}")
    pid = int(lines[-1])
    time.sleep(0.2)
    alive = run_ssh(remote, f"kill -0 {pid}", check=False)
    if alive.returncode != 0:
        raise RuntimeError(f"remote CPNetFlux server exited during startup (pid={pid})")
    return pid


def stop_remote_server(remote: str, pid: int | None) -> None:
    if not pid:
        return
    run_ssh(remote, f"kill {pid}", check=False, timeout=10)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        result = run_ssh(remote, f"kill -0 {pid}", check=False, timeout=10)
        if result.returncode != 0:
            return
        time.sleep(0.1)
    run_ssh(remote, f"kill -9 {pid}", check=False, timeout=10)


def fetch_remote_text(remote: str, path: str) -> str:
    completed = run_ssh(remote, f"cat {shlex.quote(path)}", check=False, timeout=30)
    if completed.returncode != 0:
        return ""
    return completed.stdout


def parse_manifest_text(text: str, path_label: str) -> dict[str, object]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value

    verified: list[dict[str, object]] = []
    text = values.get("verified_chunks", "")
    for item in text.split(",") if text else []:
        parts = item.split(":")
        if len(parts) != 5:
            raise RuntimeError(f"invalid verified_chunks entry in {path_label}: {item!r}")
        verified.append(
            {
                "chunk_id": int(parts[0]),
                "offset": int(parts[1]),
                "length": int(parts[2]),
                "algorithm": parts[3],
                "checksum": parts[4],
            }
        )

    total_size = int(values.get("total_size", "0"))
    chunk_size = int(values.get("chunk_size", "0"))
    total_chunks = (total_size + chunk_size - 1) // chunk_size if chunk_size else 0
    verified_ids = {int(item["chunk_id"]) for item in verified}
    missing_ids = [chunk_id for chunk_id in range(total_chunks) if chunk_id not in verified_ids]
    return {
        "state": values.get("state", ""),
        "transfer_id": values.get("transfer_id", ""),
        "total_size": total_size,
        "chunk_size": chunk_size,
        "total_chunks": total_chunks,
        "verified": verified,
        "verified_count": len(verified),
        "verified_bytes": sum(int(item["length"]) for item in verified),
        "missing_ids": missing_ids,
        "missing_count": len(missing_ids),
        "completed_ranges": values.get("completed_ranges", ""),
    }


def remote_manifest_snapshot(remote: str, path: str) -> dict[str, object] | None:
    try:
        text = fetch_remote_text(remote, path)
        if not text:
            return None
        return parse_manifest_text(text, path)
    except (OSError, ValueError, RuntimeError):
        return None


def remote_path_exists(remote: str, path: str) -> bool:
    result = run_ssh(remote, f"test -e {shlex.quote(path)}", check=False, timeout=10)
    return result.returncode == 0


def progress_text(snapshot: dict[str, object] | None, total_bytes: int, width: int = 28) -> str:
    verified_bytes = int(snapshot.get("verified_bytes", 0)) if snapshot else 0
    verified_count = int(snapshot.get("verified_count", 0)) if snapshot else 0
    total_chunks = int(snapshot.get("total_chunks", 0)) if snapshot else 0
    missing_count = int(snapshot.get("missing_count", 0)) if snapshot else 0
    ratio = min(1.0, verified_bytes / total_bytes) if total_bytes else 1.0
    filled = int(width * ratio)
    bar = "#" * filled + "." * (width - filled)
    return (
        f"[{bar}] {ratio * 100:5.1f}% "
        f"verified_chunks={verified_count}/{total_chunks} "
        f"missing_chunks={missing_count} "
        f"verified={format_bytes(verified_bytes)}"
    )


def format_chunk_ranges(chunk_ids: list[int], chunk_size: int, total_size: int) -> str:
    if not chunk_ids:
        return "[]"
    ranges: list[tuple[int, int]] = []
    start = chunk_ids[0]
    previous = start
    for chunk_id in chunk_ids[1:]:
        if chunk_id == previous + 1:
            previous = chunk_id
            continue
        ranges.append((start, previous))
        start = previous = chunk_id
    ranges.append((start, previous))

    pieces: list[str] = []
    for begin_id, end_id in ranges:
        begin = begin_id * chunk_size
        end = min(total_size, (end_id + 1) * chunk_size)
        pieces.append(f"{format_bytes(begin)}-{format_bytes(end)}")
    return "[" + ", ".join(pieces) + "]"


class Console:
    def __init__(self, *, color: bool) -> None:
        self.color = color
        self.started_at = time.monotonic()

    def elapsed(self) -> float:
        return time.monotonic() - self.started_at

    def line(self, label: str, message: str) -> None:
        colors = {
            "title": "\033[1;36m",
            "phase": "\033[1;34m",
            "ok": "\033[1;32m",
            "warn": "\033[1;33m",
            "fail": "\033[1;31m",
            "info": "\033[0;37m",
        }
        prefix = f"[{label}]"
        stamp = f"[t={self.elapsed():05.1f}s]"
        if self.color:
            prefix = f"{colors.get(label, colors['info'])}{prefix}{stamp}\033[0m"
        else:
            prefix = f"{prefix}{stamp}"
        print(f"{prefix} {message}", flush=True)

    def progress(self, label: str, message: str) -> None:
        print(f"\r  [t={self.elapsed():05.1f}s] {label}: {message}", end="", flush=True)

    def end_progress(self) -> None:
        print("", flush=True)


def pause(seconds: float) -> None:
    if seconds > 0:
        time.sleep(seconds)


def run_transfer(
    command: list[str],
    log_path: Path,
    manifest_reader: Callable[[], dict[str, object] | None],
    total_bytes: int,
    console: Console,
    label: str,
    timeout_seconds: float,
    progress_interval_seconds: float,
    interrupt_after_seconds: float | None = None,
) -> tuple[int, str, bool]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        log.write("$ " + " ".join(command) + "\n\n")
        log.flush()
        process = subprocess.Popen(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        started = time.monotonic()
        spinner = "|/-\\"
        tick = 0
        interrupted = False
        while process.poll() is None:
            elapsed = time.monotonic() - started
            if interrupt_after_seconds is not None and elapsed >= interrupt_after_seconds:
                console.end_progress()
                console.line(
                    "warn",
                    f"{label} 到达计划中断点，主动终止客户端 "
                    f"(transfer_elapsed={elapsed:.1f}s)",
                )
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                interrupted = True
                break
            if elapsed > timeout_seconds:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                raise RuntimeError(f"{label} timed out after {timeout_seconds:.1f}s")
            snapshot = manifest_reader()
            status = progress_text(snapshot, total_bytes)
            console.progress(f"{spinner[tick % len(spinner)]} {label}", status)
            tick += 1
            time.sleep(progress_interval_seconds)
        console.end_progress()
    text = log_path.read_text(encoding="utf-8", errors="replace")
    return int(process.returncode or 0), text, interrupted


def open_stor(host: str, control_port: int, target: str, *, resume_id: str = ""):
    sock, buffer = run_alpha_demo.login_type_i(host, control_port)
    if resume_id:
        reply = run_alpha_demo.send_command(sock, buffer, f"REST GFID:{resume_id}")
        if run_alpha_demo.reply_code(reply) != 350:
            sock.close()
            raise RuntimeError(f"REST failed: {reply}")
    epsv = run_alpha_demo.send_command(sock, buffer, "EPSV")
    if run_alpha_demo.reply_code(epsv) != 229:
        sock.close()
        raise RuntimeError(f"EPSV failed: {epsv}")
    data_port = run_alpha_demo.parse_epsv_port(epsv)
    stor = run_alpha_demo.send_command(sock, buffer, f"STOR {target}")
    if run_alpha_demo.reply_code(stor) != 150:
        sock.close()
        raise RuntimeError(f"STOR failed: {stor}")
    return sock, buffer, data_port, run_alpha_demo.parse_transfer_id(stor)


def client_command(
    build_dir: Path,
    server_host: str,
    data_port: int,
    source: Path,
    connections: int,
    chunk_size: int,
    transfer_id: str,
    event_log: Path,
    *,
    resume: bool = False,
    max_chunks: int | None = None,
) -> list[str]:
    command = [
        str(build_dir / "cpnetflux-file-client"),
        "--host",
        server_host,
        "--port",
        str(data_port),
        "--input",
        str(source),
        "--connections",
        str(connections),
        "--chunk-size",
        str(chunk_size),
        "--buffer-size",
        "65536",
        "--checksum",
        "crc32c",
        "--checksum-backend",
        "auto",
        "--transfer-id",
        transfer_id,
        "--event-log",
        str(event_log),
    ]
    if resume:
        command.append("--resume")
    if max_chunks is not None:
        command.extend(["--max-chunks", str(max_chunks)])
    return command


def extract_metric(text: str, key: str) -> int | None:
    match = re.search(rf"\b{re.escape(key)}=(\d+)", text)
    return int(match.group(1)) if match else None


def sleep_with_message(console: Console, seconds: float, message: str) -> None:
    if seconds <= 0:
        return
    console.line("info", f"{message} ({seconds:.1f}s)")
    pause(seconds)


def run_demo(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir).resolve()
    client_bin = build_dir / "cpnetflux-file-client"
    if not client_bin.is_file() or not client_bin.stat().st_mode & 0o111:
        raise RuntimeError(f"missing executable: {client_bin}")
    if not args.remote_root.strip() or args.remote_root.rstrip("/") == "":
        raise RuntimeError("--remote-root must be a non-root directory")

    run_id = f"{timestamp()}_recovery-demo"
    result_dir = Path(args.results_dir) / run_id
    result_dir.mkdir(parents=True, exist_ok=True)
    source = result_dir / "source.bin"
    partial_log = result_dir / "client-partial.log"
    resume_log = result_dir / "client-resume.log"
    client_events = result_dir / "client-events.jsonl"
    server_log = result_dir / "server.log"
    server_events = result_dir / "server-events.jsonl"
    report_path = result_dir / "demo-report.json"
    remote_root = f"{args.remote_root.rstrip('/')}/{run_id}"
    destination = f"{remote_root}/recovery-demo.bin"
    manifest_path = f"{destination}.cpnetflux.manifest"
    remote_server_log = f"{remote_root}/server.log"
    remote_server_events = f"{remote_root}/server-events.jsonl"
    total_chunks = (args.bytes + args.chunk_size - 1) // args.chunk_size
    if args.partial_chunks >= total_chunks:
        raise RuntimeError(
            f"--partial-chunks must be below total chunks ({total_chunks}), got {args.partial_chunks}"
        )
    if not 1 <= args.data_port_base <= MAX_PASSIVE_DATA_PORT_BASE:
        raise RuntimeError(
            f"--data-port-base must fit the 512-port window (1..{MAX_PASSIVE_DATA_PORT_BASE})"
        )

    console = Console(color=not args.no_color and sys.stdout.isatty())
    report: dict[str, object] = {
        "status": "fail",
        "direction": "shenzhen_to_shanghai",
        "transport": "real_cross_region_tcp",
        "started_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "result_dir": str(result_dir),
        "source": str(source),
        "destination": destination,
        "manifest": manifest_path,
        "remote": args.remote,
        "server_host": args.server_host,
        "remote_root": remote_root,
        "remote_build_dir": args.remote_build_dir,
        "control_port": args.control_port,
        "data_port_base": args.data_port_base,
        "passive_data_port_end": args.data_port_base + 511,
        "connections": args.connections,
        "chunk_size": args.chunk_size,
        "checksum": "crc32c",
        "total_bytes": args.bytes,
        "interrupt_after_seconds": args.interrupt_after_seconds,
        "partial_chunks": args.partial_chunks,
    }
    server_pid: int | None = None
    transfer_id = ""

    console.line("title", "CPNetFlux 深圳 -> 上海并发可靠传输演示")
    console.line(
        "info",
        "深圳本地客户端 -> 上海公网服务端 | 中断保留 manifest -> --resume 补传 -> SHA-256 PASS",
    )
    console.line(
        "info",
        f"结果目录: {result_dir} | remote={args.remote} | server={args.server_host}:{args.control_port} | "
        f"connections={args.connections} | chunk_size={format_bytes(args.chunk_size)} | "
        f"total={format_bytes(args.bytes)}",
    )

    try:
        console.line("phase", "1/5 准备深圳源文件和上海 CPNetFlux 服务端")
        write_demo_file(source, args.bytes, args.seed)
        source_hash = sha256_file(source)
        report["source_sha256"] = source_hash
        check_remote = run_ssh(
            args.remote,
            " && ".join(
                [
                    f"test -x {shlex.quote(args.remote_build_dir.rstrip('/') + '/cpnetflux-gridftp-server')}",
                    f"command -v sha256sum >/dev/null",
                ]
            ),
            check=False,
            timeout=20,
        )
        if check_remote.returncode != 0:
            raise RuntimeError(
                "上海侧缺少可执行服务端或 sha256sum: "
                + (check_remote.stderr or check_remote.stdout).strip()
            )
        server_pid = start_remote_server(
            args.remote,
            args.remote_build_dir,
            remote_root,
            args.control_port,
            args.data_port_base,
            args.connections,
            args.chunk_size,
            remote_server_events,
            remote_server_log,
        )
        report["remote_server_pid"] = server_pid
        console.line(
            "ok",
            f"上海服务端已启动 pid={server_pid} bind=0.0.0.0 "
            f"control_port={args.control_port} data_port_window={args.data_port_base}..{args.data_port_base + 511}",
        )
        sleep_with_message(console, args.pause_seconds, "准备进入真实跨域传输")

        manifest_reader = lambda: remote_manifest_snapshot(args.remote, manifest_path)
        console.line("phase", "2/5 启动深圳 -> 上海并发 STOR")
        control, control_buffer, data_port, transfer_id = open_stor(
            args.server_host, args.control_port, "recovery-demo.bin"
        )
        assert_epsv_port_in_window(data_port, args.data_port_base)
        report["transfer_id"] = transfer_id
        report["partial_data_port"] = data_port
        console.line(
            "info",
            f"transfer_id={transfer_id} | 上海 EPSV data_port={data_port} | connections={args.connections}",
        )
        partial_command = client_command(
            build_dir,
            args.server_host,
            data_port,
            source,
            args.connections,
            args.chunk_size,
            transfer_id,
            client_events,
            max_chunks=args.partial_chunks if args.partial_chunks > 0 else None,
        )
        partial_returncode, partial_text, interrupted = run_transfer(
            partial_command,
            partial_log,
            manifest_reader,
            args.bytes,
            console,
            "跨域并发 STOR",
            args.timeout_seconds,
            args.progress_interval_seconds,
            interrupt_after_seconds=(
                None if args.partial_chunks > 0 else args.interrupt_after_seconds
            ),
        )
        if args.partial_chunks > 0 and partial_returncode == 0:
            raise RuntimeError("partial transfer unexpectedly succeeded")
        if args.partial_chunks == 0 and not interrupted:
            raise RuntimeError(
                "transfer completed before the scheduled interruption; increase --bytes "
                "or reduce --interrupt-after-seconds"
            )
        try:
            failure_reply = run_alpha_demo.read_reply(control, control_buffer)
        finally:
            control.close()
        if run_alpha_demo.reply_code(failure_reply) != 550:
            raise RuntimeError(f"partial transfer did not fail closed: {failure_reply}")
        console.line("warn", "已注入跨域中断：深圳客户端停止，上海服务端返回 550，未提交最终文件")

        snapshot = manifest_reader()
        if snapshot is None:
            raise RuntimeError(f"上海 manifest 不可读: {manifest_path}")
        temp_path = f"{destination}.part.{transfer_id}"
        if not remote_path_exists(args.remote, temp_path):
            raise RuntimeError(f"partial transfer did not leave remote temp file: {temp_path}")
        if remote_path_exists(args.remote, destination):
            raise RuntimeError("partial transfer unexpectedly committed remote destination")
        if snapshot["verified_count"] == 0:
            raise RuntimeError("partial transfer did not record any verified chunks")
        report["partial"] = {
            "returncode": partial_returncode,
            "interrupted": interrupted,
            "client_verified_bytes": extract_metric(partial_text, "verified_bytes"),
            "manifest": snapshot,
            "failure_reply": failure_reply,
        }
        console.line(
            "ok",
            f"上海临时文件已保留: {Path(temp_path).name} | manifest.state={snapshot['state']}",
        )
        console.line(
            "info",
            f"manifest: verified_chunks={snapshot['verified_count']}/{snapshot['total_chunks']} "
            f"verified={format_bytes(int(snapshot['verified_bytes']))} missing={snapshot['missing_count']}",
        )
        preview = snapshot["missing_ids"][:8]
        suffix = "..." if len(snapshot["missing_ids"]) > len(preview) else ""
        console.line("info", f"completed_ranges={snapshot['completed_ranges']}")
        console.line(
            "info",
            f"missing_ranges={format_chunk_ranges(snapshot['missing_ids'], int(snapshot['chunk_size']), int(snapshot['total_size']))}",
        )
        console.line("info", f"missing chunk ids: {preview}{suffix}")
        sleep_with_message(console, args.pause_seconds, "展示跨域中断后的上海 manifest 事实源")

        console.line("phase", "3/5 读取上海 manifest，建立恢复计划")
        console.line(
            "info",
            f"REST GFID:{transfer_id} | 复用 verified_chunks={snapshot['verified_count']} | 只请求 missing_ranges",
        )
        sleep_with_message(console, args.pause_seconds, "准备恢复跨域传输")

        console.line("phase", "4/5 --resume 补传缺失分块到上海")
        control, control_buffer, resume_data_port, resumed_id = open_stor(
            args.server_host, args.control_port, "recovery-demo.bin", resume_id=transfer_id
        )
        assert_epsv_port_in_window(resume_data_port, args.data_port_base)
        if resumed_id != transfer_id:
            raise RuntimeError(f"resume transfer id changed: {resumed_id} != {transfer_id}")
        report["resume_data_port"] = resume_data_port
        resume_command = client_command(
            build_dir,
            args.server_host,
            resume_data_port,
            source,
            args.connections,
            args.chunk_size,
            transfer_id,
            client_events,
            resume=True,
        )
        resume_returncode, resume_text, _ = run_transfer(
            resume_command,
            resume_log,
            manifest_reader,
            args.bytes,
            console,
            "跨域 RESUME",
            args.timeout_seconds,
            args.progress_interval_seconds,
        )
        if resume_returncode != 0:
            raise RuntimeError(resume_text[-2000:])
        complete_reply = run_alpha_demo.read_reply(control, control_buffer)
        try:
            quit_reply = run_alpha_demo.send_command(control, control_buffer, "QUIT")
        finally:
            control.close()
        if run_alpha_demo.reply_code(complete_reply) != 226:
            raise RuntimeError(f"resume did not reach 226 COMPLETE: {complete_reply}")
        report["resume"] = {
            "returncode": resume_returncode,
            "client_sent_bytes": extract_metric(resume_text, "sent_bytes"),
            "client_skipped_bytes": extract_metric(resume_text, "skipped_bytes"),
            "client_resent_bytes": extract_metric(resume_text, "resent_bytes"),
            "client_verified_bytes": extract_metric(resume_text, "verified_bytes"),
            "complete_reply": complete_reply,
            "quit_reply": quit_reply,
        }
        skipped = extract_metric(resume_text, "skipped_bytes")
        resent = extract_metric(resume_text, "resent_bytes")
        console.line(
            "ok",
            f"resume accepted: skipped_bytes={format_bytes(skipped or 0)} "
            f"resent_bytes={format_bytes(resent or 0)} reply={complete_reply[0] if complete_reply else '226 COMPLETE'}",
        )
        sleep_with_message(console, args.pause_seconds, "等待上海最终 manifest 提交")

        console.line("phase", "5/5 上海 CRC32C、COMPLETE 和最终 SHA-256 校验")
        final_snapshot = manifest_reader()
        if final_snapshot is None:
            raise RuntimeError(f"上海最终 manifest 不可读: {manifest_path}")
        destination_sha_result = run_ssh(
            args.remote,
            f"sha256sum {shlex.quote(destination)}",
            check=False,
            timeout=60,
        )
        if destination_sha_result.returncode != 0:
            raise RuntimeError(f"上海目标文件 sha256sum 失败: {destination_sha_result.stderr.strip()}")
        destination_hash = destination_sha_result.stdout.split()[0]
        report["destination_sha256"] = destination_hash
        report["final_manifest"] = final_snapshot
        if final_snapshot["state"] != "committed":
            raise RuntimeError(f"final manifest state is {final_snapshot['state']!r}")
        if final_snapshot["missing_count"] != 0:
            raise RuntimeError(f"final manifest still has missing chunks: {final_snapshot['missing_ids']}")
        if source_hash != destination_hash:
            raise RuntimeError(f"SHA-256 mismatch: {source_hash} != {destination_hash}")
        records = final_snapshot["verified"]
        checksum_preview = ", ".join(
            f"#{item['chunk_id']}={item['checksum']}" for item in records[:3]
        )
        if len(records) > 3:
            checksum_preview += f", ... #{records[-1]['chunk_id']}={records[-1]['checksum']}"
        console.line(
            "ok",
            f"CRC32C verified_chunks={len(records)}/{final_snapshot['total_chunks']} | {checksum_preview}",
        )
        console.line("ok", f"COMPLETE committed={Path(destination).name} on Shanghai | SHA-256={destination_hash}")

        report["status"] = "pass"
        report["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        console.line("ok", f"PASS | report={report_path}")
        return 0
    except Exception as error:
        report["error"] = str(error)
        report["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        console.line("fail", f"FAIL: {error}")
        console.line("info", f"report={report_path}")
        return 1
    finally:
        try:
            stop_remote_server(args.remote, server_pid)
        except Exception as error:  # pragma: no cover - cleanup error is reported in logs.
            console.line("warn", f"上海服务端清理异常: {error}")
        for remote_path, local_path in (
            (remote_server_log, server_log),
            (remote_server_events, server_events),
        ):
            text = fetch_remote_text(args.remote, remote_path)
            if text:
                local_path.write_text(text, encoding="utf-8")
        report["server_log"] = str(server_log)
        report["server_events"] = str(server_events)
        report["remote_server_log"] = remote_server_log
        report["remote_server_events"] = remote_server_events
        if args.cleanup_remote:
            run_ssh(args.remote, f"rm -rf {shlex.quote(remote_root)}", check=False, timeout=30)
        report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the Shenzhen-to-Shanghai CPNetFlux concurrent transfer/resume demo."
    )
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--results-dir", default="tools/perf/results")
    parser.add_argument("--remote", default="cpnetflux-beta-shanghai", help="SSH target for the Shanghai server")
    parser.add_argument("--server-host", default="47.116.174.181", help="Shanghai public address used by data/control clients")
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux-Beta/build")
    parser.add_argument("--remote-root", default="/tmp/cpnetflux-recovery-demo")
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--bytes", type=parse_byte_size, default=200 * 1024 * 1024)
    parser.add_argument("--connections", type=int, default=4)
    parser.add_argument("--chunk-size", type=parse_byte_size, default=4 * 1024 * 1024)
    parser.add_argument(
        "--partial-chunks",
        type=int,
        default=8,
        help="deterministic interruption after this many chunks; 0 uses time interruption",
    )
    parser.add_argument("--seed", type=int, default=20260907)
    parser.add_argument("--pause-seconds", type=float, default=1.5)
    parser.add_argument("--interrupt-after-seconds", type=float, default=6.0)
    parser.add_argument("--progress-interval-seconds", type=float, default=2.0)
    parser.add_argument("--timeout-seconds", type=float, default=900.0)
    parser.add_argument("--cleanup-remote", action="store_true", help="remove this run's remote evidence directory after completion")
    parser.add_argument("--no-color", action="store_true")
    args = parser.parse_args()
    if args.bytes <= 0:
        parser.error("--bytes must be greater than zero")
    if args.connections <= 0:
        parser.error("--connections must be greater than zero")
    if args.chunk_size <= 0:
        parser.error("--chunk-size must be greater than zero")
    if args.partial_chunks < 0:
        parser.error("--partial-chunks must be non-negative")
    if args.pause_seconds < 0:
        parser.error("--pause-seconds must be non-negative")
    if args.interrupt_after_seconds <= 0:
        parser.error("--interrupt-after-seconds must be greater than zero")
    if args.progress_interval_seconds <= 0:
        parser.error("--progress-interval-seconds must be greater than zero")
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be greater than zero")
    if args.control_port <= 0 or args.control_port > 65535:
        parser.error("--control-port must be between 1 and 65535")
    if args.data_port_base < 1 or args.data_port_base > MAX_PASSIVE_DATA_PORT_BASE:
        parser.error(
            f"--data-port-base must be between 1 and {MAX_PASSIVE_DATA_PORT_BASE} "
            "to reserve the complete 512-port window"
        )
    return run_demo(args)


if __name__ == "__main__":
    raise SystemExit(main())
