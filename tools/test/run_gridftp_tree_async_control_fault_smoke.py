#!/usr/bin/env python3
"""Real-client async tree control overlap and fault smoke.

The overlap checks run the production tree upload/download clients against the
real CPNetFlux control server. The fault checks run the production upload client
through a transparent control proxy in front of that same server; the proxy only
injects the second pending STOR response. Data sockets still connect directly to
and are handled by the real server.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import BinaryIO

from tree_smoke_common import free_port, run_checked, stop_server, wait_for_control


def make_two_file_tree(root: Path, *, first_size: int = 1024 * 1024) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "a.dat").write_bytes(b"a" * first_size)
    (root / "b.dat").write_bytes(b"b" * 1024)


def start_logged_server(
    build_dir: Path,
    root: Path,
    control_port: int,
    data_port: int,
    log: Path,
    event_log: Path,
    *,
    delay_complete_ms: int = 0,
) -> subprocess.Popen[str]:
    cmd = [
        str(build_dir / "cpnetflux-gridftp-server"),
        "--host",
        "0.0.0.0",
        "--port",
        str(control_port),
        "--root",
        str(root),
        "--data-port-base",
        str(data_port),
        "--connections",
        "2",
        "--checksum",
        "crc32c",
        "--event-log",
        str(event_log),
    ]
    env = os.environ.copy()
    if delay_complete_ms > 0:
        env["CPNETFLUX_TEST_DELAY_BEFORE_DATA_COMPLETE_MS"] = str(delay_complete_ms)
    handle = log.open("w", encoding="utf-8")
    process = subprocess.Popen(cmd, stdout=handle, stderr=subprocess.STDOUT, env=env, text=True)
    handle.close()
    wait_for_control(control_port)
    return process


def event_records(path: Path) -> list[dict[str, object]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def assert_second_start_before_first_complete(records: list[dict[str, object]], prefix: str) -> None:
    start_event = f"{prefix}_start"
    complete_event = f"{prefix}_complete"
    starts = [(index, item) for index, item in enumerate(records) if item.get("event") == start_event]
    completes = [(index, item) for index, item in enumerate(records) if item.get("event") == complete_event]
    if len(starts) < 2 or not completes:
        raise RuntimeError(f"missing {prefix} overlap events: {records}")
    if not starts[1][0] < completes[0][0]:
        raise RuntimeError(
            f"{prefix} did not start the second file before the first complete: "
            f"second_start={starts[1][0]} first_complete={completes[0][0]}"
        )


def run_real_overlap(build_dir: Path, temp: Path, direction: str) -> dict[str, object]:
    server_root = temp / f"{direction}-root"
    event_log = temp / f"{direction}-events.jsonl"
    server_log = temp / f"{direction}-server.log"
    control_port = free_port()
    data_port = free_port()
    if direction == "upload":
        source = temp / "upload-source"
        make_two_file_tree(source)
        dest_arg = "dataset"
        delay_ms = 700
    else:
        source = server_root / "dataset"
        make_two_file_tree(source, first_size=8 * 1024 * 1024)
        dest_arg = str(temp / "download-dest")
        delay_ms = 0
    server_root.mkdir(parents=True, exist_ok=True)
    server = start_logged_server(
        build_dir,
        server_root,
        control_port,
        data_port,
        server_log,
        event_log,
        delay_complete_ms=delay_ms,
    )
    try:
        summary = temp / f"{direction}-summary.json"
        common = [
            "--host",
            "127.0.0.1",
            "--port",
            str(control_port),
            "--connections",
            "1",
            "--file-parallelism",
            "1",
            "--control-reuse",
            "worker",
            "--control-pipeline-depth",
            "1",
            "--scheduler",
            "off",
            "--compression",
            "off",
            "--checksum",
            "crc32c",
            "--phase-timing",
            "on",
            "--json-summary",
            str(summary),
        ]
        if direction == "upload":
            run_checked([
                str(build_dir / "cpnetflux-tree-upload-client"),
                *common,
                "--source-dir",
                str(source),
                "--dest-dir",
                dest_arg,
            ])
            prefix = "stor"
        else:
            run_checked([
                str(build_dir / "cpnetflux-tree-download-client"),
                *common,
                "--source-dir",
                "dataset",
                "--dest-dir",
                dest_arg,
            ])
            prefix = "retr"
    finally:
        stop_server(server, server_log)
    records = event_records(event_log)
    assert_second_start_before_first_complete(records, prefix)
    summary_doc = json.loads(summary.read_text(encoding="utf-8"))
    return {
        "kind": "real-overlap",
        "direction": direction,
        "event_log": str(event_log),
        "summary": str(summary),
        "second_start_before_first_complete": True,
        "control_connect_count": summary_doc.get("control_connect_count"),
        "slot_count": summary_doc.get("control_pipeline_slot_count"),
        "pending_high_watermark": summary_doc.get("control_pipeline_pending_high_watermark"),
    }


class ControlProxy:
    def __init__(self, real_port: int, mode: str) -> None:
        self.real_port = real_port
        self.mode = mode
        self.listen_port = free_port()
        self.errors: list[str] = []
        self.injected = threading.Event()
        self._stop = threading.Event()
        self._listener: socket.socket | None = None
        self._threads: list[threading.Thread] = []
        self._sockets: list[socket.socket] = []
        self._lock = threading.Lock()
        self._stor_count = 0
        self._client_eofs = 0
        self._server_eofs = 0

    @property
    def client_eofs(self) -> int:
        with self._lock:
            return self._client_eofs

    @property
    def server_eofs(self) -> int:
        with self._lock:
            return self._server_eofs

    def start(self) -> None:
        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind(("127.0.0.1", self.listen_port))
        self._listener.listen(16)
        thread = threading.Thread(target=self._accept_loop, name=f"proxy-{self.mode}-accept")
        thread.start()
        self._threads.append(thread)

    def wait_closed(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            live = [
                thread for thread in self._threads
                if thread.is_alive() and not thread.name.endswith("-accept")
            ]
            if not live:
                break
            for thread in live:
                thread.join(timeout=0.05)
        live_names = [
            thread.name for thread in self._threads
            if thread.is_alive() and not thread.name.endswith("-accept")
        ]
        if live_names:
            raise RuntimeError(f"proxy control threads still live after client exit: {live_names}")

    def stop(self) -> None:
        self._stop.set()
        sockets = list(self._sockets)
        if self._listener is not None:
            sockets.append(self._listener)
        for sock in sockets:
            self._close(sock)
        for thread in list(self._threads):
            thread.join(timeout=2.0)
        live_names = [thread.name for thread in self._threads if thread.is_alive()]
        if live_names:
            raise RuntimeError(f"proxy residual threads after stop: {live_names}")
        if self.errors:
            raise RuntimeError("; ".join(self.errors))

    def _remember_socket(self, sock: socket.socket) -> None:
        with self._lock:
            self._sockets.append(sock)

    def _close(self, sock: socket.socket) -> None:
        try:
            sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            sock.close()
        except OSError:
            pass

    def _accept_loop(self) -> None:
        assert self._listener is not None
        while not self._stop.is_set():
            try:
                client, _ = self._listener.accept()
            except OSError:
                break
            try:
                server = socket.create_connection(("127.0.0.1", self.real_port), timeout=5.0)
            except OSError as error:
                self.errors.append(f"connect real server failed: {error}")
                self._close(client)
                continue
            client.settimeout(None)
            server.settimeout(None)
            self._remember_socket(client)
            self._remember_socket(server)
            state = {"next_stor_ordinal": 0, "injected": False}
            t1 = threading.Thread(
                target=self._client_to_server,
                args=(client, server, state),
                name=f"proxy-{self.mode}-c2s",
            )
            t2 = threading.Thread(
                target=self._server_to_client,
                args=(client, server, state),
                name=f"proxy-{self.mode}-s2c",
            )
            t1.start()
            t2.start()
            self._threads.extend([t1, t2])

    def _client_to_server(self, client: socket.socket, server: socket.socket, state: dict[str, object]) -> None:
        try:
            source = client.makefile("rb")
            while not self._stop.is_set():
                line = source.readline()
                if not line:
                    with self._lock:
                        self._client_eofs += 1
                    break
                if line.upper().startswith(b"STOR "):
                    with self._lock:
                        self._stor_count += 1
                        state["next_stor_ordinal"] = self._stor_count
                server.sendall(line)
        except OSError:
            pass
        finally:
            self._close(server)
            self._close(client)

    def _server_to_client(self, client: socket.socket, server: socket.socket, state: dict[str, object]) -> None:
        try:
            source = server.makefile("rb")
            while not self._stop.is_set():
                line = source.readline()
                if not line:
                    with self._lock:
                        self._server_eofs += 1
                    break
                ordinal = int(state.get("next_stor_ordinal", 0) or 0)
                if ordinal >= 2 and not state.get("injected") and line.startswith(b"150 "):
                    state["injected"] = True
                    self.injected.set()
                    if self.mode == "reject":
                        client.sendall(b"450 Async transfer window is full\r\n")
                        self._close(server)
                        self._close(client)
                    elif self.mode == "disconnect":
                        self._close(server)
                        self._close(client)
                    elif self.mode == "hold":
                        # Keep both controls open and wait for the production client to hit
                        # its pipeline control deadline and close the candidate control.
                        continue
                    else:
                        raise RuntimeError(f"unknown proxy mode {self.mode}")
                    break
                client.sendall(line)
        except OSError:
            pass
        finally:
            if self.mode != "hold" or self._stop.is_set():
                self._close(server)
                self._close(client)


def run_proxy_fault_client(build_dir: Path, temp: Path, mode: str) -> dict[str, object]:
    source = temp / f"proxy-{mode}-source"
    make_two_file_tree(source)
    server_root = temp / f"proxy-{mode}-root"
    event_log = temp / f"proxy-{mode}-events.jsonl"
    server_log = temp / f"proxy-{mode}-server.log"
    real_port = free_port()
    data_port = free_port()
    server_root.mkdir(parents=True, exist_ok=True)
    server = start_logged_server(build_dir, server_root, real_port, data_port, server_log, event_log)
    proxy = ControlProxy(real_port, mode)
    proxy.start()
    started = time.monotonic()
    try:
        cmd = [
            str(build_dir / "cpnetflux-tree-upload-client"),
            "--host",
            "127.0.0.1",
            "--port",
            str(proxy.listen_port),
            "--source-dir",
            str(source),
            "--dest-dir",
            "dataset",
            "--connections",
            "1",
            "--file-parallelism",
            "1",
            "--control-reuse",
            "worker",
            "--control-pipeline-depth",
            "1",
            "--scheduler",
            "off",
            "--compression",
            "off",
            "--checksum",
            "crc32c",
        ]
        completed = subprocess.run(cmd, text=True, capture_output=True, check=False, timeout=45.0)
        elapsed = time.monotonic() - started
        proxy.wait_closed(3.0)
    except subprocess.TimeoutExpired as error:
        proxy.stop()
        stop_server(server, server_log)
        raise RuntimeError(f"{mode} client exceeded 45s watchdog") from error
    finally:
        proxy.stop()
        stop_server(server, server_log)
    if not proxy.injected.is_set():
        raise RuntimeError(f"{mode} proxy never intercepted the second STOR 150 reply")
    if completed.returncode == 0:
        raise RuntimeError(f"{mode} fault unexpectedly succeeded")
    diagnostic = (completed.stderr + "\n" + completed.stdout).lower()
    expected_fragments = {
        "reject": ("window is full", "450"),
        "disconnect": ("closed", "eof", "reset", "broken pipe"),
        "hold": ("timed out", "timeout"),
    }[mode]
    if not any(fragment in diagnostic for fragment in expected_fragments):
        raise RuntimeError(f"{mode} diagnostic did not match target fault: {diagnostic.strip()}")
    if mode == "hold" and not (29.0 <= elapsed < 45.0):
        raise RuntimeError(f"hold timeout elapsed {elapsed:.3f}s outside expected [29, 45)")
    return {
        "kind": "proxy-fault",
        "mode": mode,
        "returncode": completed.returncode,
        "elapsed_seconds": round(elapsed, 3),
        "client_eofs": proxy.client_eofs,
        "server_eofs": proxy.server_eofs,
        "diagnostic_excerpt": (completed.stderr or completed.stdout).strip().splitlines()[:3],
        "server_event_log": str(event_log),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-async-control-fault.") as temp_text:
        temp = Path(temp_text)
        results: list[dict[str, object]] = []
        results.append(run_real_overlap(build_dir, temp, "upload"))
        results.append(run_real_overlap(build_dir, temp, "download"))
        results.append(run_proxy_fault_client(build_dir, temp, "reject"))
        results.append(run_proxy_fault_client(build_dir, temp, "disconnect"))
        results.append(run_proxy_fault_client(build_dir, temp, "hold"))
        print(json.dumps(results, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
