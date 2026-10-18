#!/usr/bin/env python3
"""Transparent TCP relay with deterministic, lane-scoped data resets."""
import argparse
import json
import re
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
        drop_control_after_data_connection=None,
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
        self.drop_control_after_data_connection = drop_control_after_data_connection
        if self.target_data_connection is None and drop_control_after_data_connection is not None:
            self.target_data_connection = drop_control_after_data_connection
        self.target_connection = None
        self.data_connection_count = 0
        self.control_connection_count = 0
        self.control_sockets = {}
        self.data_port_control_connection = {}
        self.data_connection_number = {}
        self.data_connection_control = {}
        self.control_lane_index = {}
        self.control_batch_id = {}
        self.control_reset_done = False
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
                        self.data_connection_number[connection_id] = self.data_connection_count
                        control_connection = self.data_port_control_connection.get(port)
                        self.data_connection_control[connection_id] = control_connection
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
                            control_connection=control_connection,
                            target=target,
                        )
                    else:
                        self.control_connection_count += 1
                        self.control_sockets[connection_id] = (client, backend)
                        self.event(
                            event="control_connected",
                            connection=connection_id,
                            control_connection=self.control_connection_count,
                        )
                    self.connections.update((client, backend))
                for source, target_socket, from_server in (
                    (client, backend, False), (backend, client, True)
                ):
                    thread = threading.Thread(
                        target=self.pump,
                        args=(source, target_socket, data, connection_id, port, from_server),
                        daemon=True,
                    )
                    self.threads.append(thread)
                    thread.start()
            except (OSError, TimeoutError):
                if self.stopped.is_set():
                    return

    def pump(self, source, target, data, connection_id, data_port, from_server):
        try:
            tail = b""
            control_tail = b""
            request_tail = b""
            while not self.stopped.is_set():
                block = source.recv(4096)
                if not block:
                    break
                window = tail + block
                tail = window[-9:]
                if not data and from_server:
                    control_window = control_tail + block
                    control_tail = control_window[-256:]
                    passive = re.search(rb"229[^\r\n]*\(\|\|\|(\d+)\|\)", control_window)
                    if passive:
                        passive_port = int(passive.group(1))
                        with self.lock:
                            self.data_port_control_connection[passive_port] = connection_id
                            self.event(
                                event="epsv_mapped",
                                connection=connection_id,
                                data_port=passive_port,
                            )
                elif not data:
                    request_window = request_tail + block
                    request_tail = request_window[-512:]
                    directory = re.search(
                        rb"XDIRD\s+(?:PUT|GET)\s+([A-Za-z0-9._-]+)\s+(\d+)\s+\d+\s+\d+",
                        request_window,
                    )
                    if directory:
                        batch_id = directory.group(1).decode("ascii")
                        lane_index = int(directory.group(2))
                        with self.lock:
                            self.control_lane_index[connection_id] = lane_index
                            self.control_batch_id[connection_id] = batch_id
                            self.event(
                                event="directory_channel_mapped",
                                connection=connection_id,
                                lane_index=lane_index,
                                batch_id=batch_id,
                            )
                should_fault = data and (
                    self.target_data_connection is None
                    or connection_id == self.target_connection
                )
                reset_control_connection = None
                if data or self.drop_terminal or self.drop_result:
                    reset = False
                    with self.lock:
                        if data:
                            self.forwarded += len(block)
                            if should_fault:
                                self.forwarded_by_connection[connection_id] = (
                                    self.forwarded_by_connection.get(connection_id, 0) + len(block)
                                )
                        forwarded = self.forwarded_by_connection.get(connection_id, 0)
                        data_triggered = (
                            self.drop_control_after_data_connection is None
                            and should_fault
                            and self.drop_bytes
                            and forwarded >= self.drop_bytes
                        )
                        triggered = data_triggered or (
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
                        data_number = self.data_connection_number.get(connection_id)
                        mapped_control = self.data_connection_control.get(connection_id)
                        if (
                            data
                            and not self.control_reset_done
                            and self.drop_control_after_data_connection == data_number
                            and mapped_control is not None
                            and forwarded >= self.drop_bytes
                        ):
                            self.control_reset_done = True
                            reset_control_connection = mapped_control
                            self.event(
                                event="reset",
                                scope="control",
                                connection=mapped_control,
                                control_connection=mapped_control,
                                lane_index=self.control_lane_index.get(mapped_control),
                                batch_id=self.control_batch_id.get(mapped_control),
                                trigger_data_connection=data_number,
                                data_port=data_port,
                                target=True,
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
                if reset_control_connection is not None:
                    with self.lock:
                        control_sockets = self.control_sockets.get(reset_control_connection, ())
                    for sock in control_sockets:
                        try:
                            sock.setsockopt(
                                socket.SOL_SOCKET,
                                socket.SO_LINGER,
                                struct.pack("ii", 1, 0),
                            )
                            sock.shutdown(socket.SHUT_RDWR)
                        except OSError:
                            pass
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
                if not data:
                    self.control_sockets.pop(connection_id, None)

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
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--drop-bytes", type=int, default=262144)
    parser.add_argument("--delay", type=float, default=0.001)
    parser.add_argument("--drops", type=int, default=1)
    parser.add_argument("--target-data-connection", type=int)
    parser.add_argument("--drop-control-after-data-connection", type=int)
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    proxy = FaultProxy(
        args.listen_host,
        args.backend_host,
        args.control_port,
        args.data_base,
        count=args.count,
        drop_bytes=args.drop_bytes,
        drops=args.drops,
        delay=args.delay,
        log=args.log,
        target_data_connection=args.target_data_connection,
        drop_control_after_data_connection=args.drop_control_after_data_connection,
    )
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        proxy.close()
