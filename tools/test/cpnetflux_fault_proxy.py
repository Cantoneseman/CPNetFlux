#!/usr/bin/env python3
"""Transparent TCP relay with deterministic, lane-scoped data resets."""
import argparse
import json
import socket
import struct
import threading
import time
from pathlib import Path


class FaultProxy:
    def __init__(
        self,
        listen_host,
        backend_host,
        control_port,
        data_base,
        count=16,
        drop_bytes=262144,
        drops=1,
        delay=0.001,
        log=None,
        drop_terminal=False,
        on_reset=None,
        drop_result=False,
        target_data_connection=None,
    ):
        self.listeners = []
        self.connections = set()
        self.threads = []
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.drop_terminal = drop_terminal
        self.on_reset = on_reset
        self.drop_result = drop_result
        self.drop_bytes = drop_bytes
        self.remaining = drops
        self.delay = delay
        self.dropped = 0
        self.forwarded = 0
        self.forwarded_by_connection = {}
        self.log = Path(log) if log else None
        self.target_data_connection = target_data_connection
        self.target_connection = None
        self.data_connection_count = 0
        self.next_connection = 1

        for port in [control_port, *range(data_base, data_base + count)]:
            listener = socket.socket()
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((listen_host, port))
            listener.listen()
            listener.settimeout(0.2)
            self.listeners.append(listener)
            thread = threading.Thread(
                target=self.accept,
                args=(listener, backend_host, port, port != control_port),
                daemon=True,
            )
            self.threads.append(thread)
            thread.start()

    def event(self, **fields):
        if self.log:
            with self.log.open("a", encoding="utf-8") as output:
                output.write(json.dumps({"time": time.time(), **fields}) + "\n")

    def accept(self, listener, host, port, data):
        while not self.stopped.is_set():
            try:
                client, _ = listener.accept()
                backend = socket.create_connection((host, port), timeout=3)
                client.settimeout(None)
                backend.settimeout(None)
                with self.lock:
                    connection_id = self.next_connection
                    self.next_connection += 1
                    target = False
                    if data:
                        self.data_connection_count += 1
                        target = (
                            self.target_data_connection is not None
                            and self.data_connection_count == self.target_data_connection
                        )
                        if target:
                            self.target_connection = connection_id
                        self.event(
                            event="data_connected",
                            connection=connection_id,
                            data_connection=self.data_connection_count,
                            data_port=port,
                            target=target,
                        )
                    self.connections.update((client, backend))
                for source, target_socket in ((client, backend), (backend, client)):
                    thread = threading.Thread(
                        target=self.pump,
                        args=(source, target_socket, data, connection_id, port),
                        daemon=True,
                    )
                    self.threads.append(thread)
                    thread.start()
            except (OSError, TimeoutError):
                if self.stopped.is_set():
                    return

    def pump(self, source, target, data, connection_id, data_port):
        try:
            tail = b""
            while not self.stopped.is_set():
                block = source.recv(4096)
                if not block:
                    break
                window = tail + block
                tail = window[-9:]
                should_fault = data and (
                    self.target_data_connection is None
                    or connection_id == self.target_connection
                )
                if data or self.drop_terminal or self.drop_result:
                    reset = False
                    with self.lock:
                        if data:
                            self.forwarded += len(block)
                            if should_fault:
                                self.forwarded_by_connection[connection_id] = (
                                    self.forwarded_by_connection.get(connection_id, 0) + len(block)
                                )
                        triggered = (
                            should_fault
                            and self.drop_bytes
                            and self.forwarded_by_connection.get(connection_id, 0) >= self.drop_bytes
                        ) or (
                            self.drop_terminal
                            and not data
                            and b"226 " in block
                        ) or (
                            self.drop_result
                            and data
                            and bytes.fromhex("4746583100010040000a") in window
                        )
                        if triggered and self.remaining:
                            self.remaining -= 1
                            self.dropped += 1
                            reset = True
                            self.event(
                                event="reset",
                                count=self.dropped,
                                connection=connection_id,
                                data_port=data_port,
                                target=should_fault,
                            )
                    if reset:
                        if self.on_reset:
                            self.on_reset()
                        for sock in (source, target):
                            try:
                                sock.setsockopt(
                                    socket.SOL_SOCKET,
                                    socket.SO_LINGER,
                                    struct.pack("ii", 1, 0),
                                )
                                sock.shutdown(socket.SHUT_RDWR)
                            except OSError:
                                pass
                        break
                    if data and self.delay:
                        time.sleep(self.delay)
                target.sendall(block)
        except OSError:
            pass
        finally:
            for sock in (source, target):
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass
            with self.lock:
                self.connections.discard(source)
                self.connections.discard(target)

    def close(self):
        self.stopped.set()
        for listener in self.listeners:
            try:
                listener.close()
            except OSError:
                pass
        with self.lock:
            for sock in self.connections.copy():
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        for thread in self.threads:
            thread.join(timeout=1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-host", required=True)
    parser.add_argument("--backend-host", default="127.0.0.1")
    parser.add_argument("--control-port", type=int, required=True)
    parser.add_argument("--data-base", type=int, required=True)
    parser.add_argument("--drop-bytes", type=int, default=262144)
    parser.add_argument("--drops", type=int, default=1)
    parser.add_argument("--target-data-connection", type=int)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    proxy = FaultProxy(
        args.listen_host,
        args.backend_host,
        args.control_port,
        args.data_base,
        drop_bytes=args.drop_bytes,
        drops=args.drops,
        log=args.log,
        target_data_connection=args.target_data_connection,
    )
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        proxy.close()
