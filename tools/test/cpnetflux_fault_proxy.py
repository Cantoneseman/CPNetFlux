#!/usr/bin/env python3
"""Transparent TCP relay; resets only its own data connections after a byte budget."""
import argparse
import json
import socket
import struct
import threading
import time

class FaultProxy:
    def __init__(self, listen_host, backend_host, control_port, data_base, count=16,
                 drop_bytes=262144, drops=1, delay=0.001, log=None, drop_terminal=False, on_reset=None, drop_result=False):
        self.listeners, self.connections, self.threads = [], set(), []
        self.lock, self.stopped = threading.Lock(), threading.Event()
        self.drop_terminal, self.on_reset = drop_terminal, on_reset
        self.drop_result = drop_result
        self.drop_bytes, self.remaining, self.delay = drop_bytes, drops, delay
        self.dropped, self.forwarded, self.log = 0, 0, log
        for port in [control_port, *range(data_base, data_base + count)]:
            listener = socket.socket()
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((listen_host, port))
            listener.listen()
            listener.settimeout(0.2)
            self.listeners.append(listener)
            t = threading.Thread(target=self.accept, args=(listener, backend_host, port, port != control_port), daemon=True)
            self.threads.append(t)
            t.start()

    def event(self, **fields):
        if self.log:
            with self.log.open("a") as out:
                out.write(json.dumps({"time": time.time(), **fields}) + "\n")

    def accept(self, listener, host, port, data):
        while not self.stopped.is_set():
            try:
                client, _ = listener.accept()
                backend = socket.create_connection((host, port), timeout=3)
                client.settimeout(None); backend.settimeout(None)
                with self.lock: self.connections.update((client, backend))
                for a, b in ((client, backend), (backend, client)):
                    threading.Thread(target=self.pump, args=(a, b, data), daemon=True).start()
            except (OSError, TimeoutError):
                if self.stopped.is_set(): return

    def pump(self, source, target, data):
        try:
            tail = b""
            while not self.stopped.is_set():
                block = source.recv(4096)
                if not block: break
                window = tail + block
                tail = window[-9:]
                if data or self.drop_terminal or self.drop_result:
                    reset = False
                    with self.lock:
                        if data: self.forwarded += len(block)
                        triggered = (data and self.drop_bytes and self.forwarded >= self.drop_bytes) or (
                            self.drop_terminal and not data and b"226 " in block) or (
                            self.drop_result and data and bytes.fromhex("4746583100010040000a") in window)
                        if triggered and self.remaining:
                            self.forwarded = 0
                            self.remaining -= 1
                            self.dropped += 1
                            reset = True
                            self.event(event="reset", count=self.dropped, port=source.getsockname()[1])
                    if reset:
                        if self.on_reset: self.on_reset()
                        for sock in (source, target):
                            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                            sock.shutdown(socket.SHUT_RDWR)
                        break
                    if data and self.delay: time.sleep(self.delay)
                target.sendall(block)
        except OSError:
            pass
        finally:
            for sock in (source, target):
                try: sock.shutdown(socket.SHUT_RDWR)
                except OSError: pass
                sock.close()
            with self.lock: self.connections.discard(source); self.connections.discard(target)

    def close(self):
        self.stopped.set()
        for sock in self.listeners: sock.close()
        with self.lock:
            for sock in self.connections.copy():
                try: sock.shutdown(socket.SHUT_RDWR)
                except OSError: pass
        for t in self.threads: t.join(timeout=1)

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--listen-host", required=True)
    p.add_argument("--backend-host", default="127.0.0.1")
    p.add_argument("--control-port", type=int, required=True)
    p.add_argument("--data-base", type=int, required=True)
    p.add_argument("--drop-bytes", type=int, default=262144)
    p.add_argument("--drops", type=int, default=1)
    p.add_argument("--log", type=__import__("pathlib").Path)
    a = p.parse_args()
    proxy = FaultProxy(a.listen_host, a.backend_host, a.control_port, a.data_base,
                      drop_bytes=a.drop_bytes, drops=a.drops, log=a.log)
    try:
        while True: time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally: proxy.close()
