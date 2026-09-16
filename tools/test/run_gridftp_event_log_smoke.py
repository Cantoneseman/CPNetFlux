#!/usr/bin/env python3
"""Exercise GridFTP control/file event logging with token auth and no secret leakage."""

from __future__ import annotations

import argparse
import json
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from run_gridftp_control_token_smoke import (  # noqa: E402
    TOKEN,
    assert_epsv_port_in_window,
    connect_control,
    clamp_passive_data_port_base,
    free_port,
    make_file,
    parse_epsv_port,
    parse_transfer_id,
    read_reply,
    reply_code,
    send_command,
    sha256_file,
)


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "cpnetflux-gridftp-server"
    client_bin = build_dir / "cpnetflux-file-client"
    download_bin = build_dir / "cpnetflux-file-download-client"
    if not server_bin.exists() or not client_bin.exists() or not download_bin.exists():
        raise FileNotFoundError(f"missing CPNetFlux binaries in {build_dir}")

    with tempfile.TemporaryDirectory(prefix="cpnetflux-event-log.") as temp_text:
        temp = Path(temp_text)
        root = temp / "root"
        root.mkdir()
        source = temp / "source.bin"
        make_file(source, 256 * 1024)
        expected_sha = sha256_file(source)
        token_file = temp / "token.txt"
        token_file.write_text(TOKEN + "\n", encoding="utf-8")
        token_file.chmod(0o600)
        event_log = temp / "events.jsonl"
        server_log = temp / "server.log"
        control_port = free_port()
        data_port_base = clamp_passive_data_port_base(free_port())
        server_cmd = [
            str(server_bin),
            "--host",
            "0.0.0.0",
            "--port",
            str(control_port),
            "--root",
            str(root),
            "--data-port-base",
            str(data_port_base),
            "--auth-mode",
            "token",
            "--auth-token-file",
            str(token_file),
            "--connections",
            "1",
            "--event-log",
            str(event_log),
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)
        try:
            sock, buffer, greeting = connect_control(control_port)
            with sock:
                assert reply_code(greeting) == 220
                assert reply_code(send_command(sock, buffer, "SIZE uploaded.bin")) == 530
                assert reply_code(send_command(sock, buffer, "USER token")) == 331
                assert reply_code(send_command(sock, buffer, "PASS wrong-token")) == 530
                assert reply_code(send_command(sock, buffer, "USER token")) == 331
                assert reply_code(send_command(sock, buffer, "PASS " + TOKEN)) == 230
                assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                assert_epsv_port_in_window(parse_epsv_port(epsv), data_port_base)
                stor = send_command(sock, buffer, "STOR uploaded.bin")
                assert reply_code(stor) == 150, stor
                transfer_id = parse_transfer_id(stor)
                subprocess.run(
                    [
                        str(client_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(parse_epsv_port(epsv)),
                        "--input",
                        str(source),
                        "--transfer-id",
                        transfer_id,
                        "--event-log",
                        str(event_log),
                    ],
                    check=True,
                )
                assert reply_code(read_reply(sock, buffer)) == 226
                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229
                assert_epsv_port_in_window(parse_epsv_port(epsv), data_port_base)
                retr = send_command(sock, buffer, "RETR uploaded.bin")
                assert reply_code(retr) == 150
                retr_id = parse_transfer_id(retr)
                output = temp / "download.bin"
                subprocess.run(
                    [
                        str(download_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(parse_epsv_port(epsv)),
                        "--output",
                        str(output),
                        "--transfer-id",
                        retr_id,
                        "--event-log",
                        str(event_log),
                    ],
                    check=True,
                )
                assert reply_code(read_reply(sock, buffer)) == 226
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            if sha256_file(root / "uploaded.bin") != expected_sha or sha256_file(output) != expected_sha:
                raise RuntimeError("event log smoke transfer hash mismatch")

            text = event_log.read_text(encoding="utf-8")
            if TOKEN in text or "wrong-token" in text or re.search(r"PASS\\s+\\S+", text):
                raise RuntimeError("event log leaked token or PASS command")
            events = [json.loads(line) for line in text.splitlines() if line.strip()]
            required = {
                "timestamp",
                "component",
                "event",
                "transfer_id",
                "direction",
                "path",
                "result",
                "error_code",
                "message",
                "elapsed_seconds",
                "bytes",
            }
            if not events or any(not required.issubset(event) for event in events):
                raise RuntimeError("event log JSONL missing required fields")
            codes = {event["error_code"] for event in events}
            if "auth_required" not in codes or "auth_failed" not in codes or "ok" not in codes:
                raise RuntimeError(f"event log missing expected error codes: {codes}")
            print(f"event_log={event_log}")
            print("gridftp event log smoke passed")
            return 0
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            if server.returncode not in (0, -15, -9):
                print(server_log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux event log smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    raise SystemExit(main())
