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


class SynchronizedResetGate:
    """Wait until every targeted data connection is ready, then allow one reset per connection."""

    def __init__(self, target_connections, timeout=10.0):
        self.target_connections = frozenset(int(value) for value in target_connections)
        if len(self.target_connections) < 2:
            raise ValueError("synchronized reset requires at least two target connections")
        self.timeout = timeout
        self._condition = threading.Condition()
        self._ready_connections = set()
        self.reset_connections = set()
        self._failed = False

    def wait_and_claim(self, data_connection):
        if data_connection not in self.target_connections:
            return False
        deadline = time.monotonic() + self.timeout
        with self._condition:
            if self._failed:
                return False
            self._ready_connections.add(data_connection)
            self._condition.notify_all()
            while self.target_connections - self._ready_connections and not self._failed:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self._failed = True
                    self._condition.notify_all()
                    return False
                self._condition.wait(remaining)
            if self._failed or data_connection in self.reset_connections:
                return False
            self.reset_connections.add(data_connection)
            self._condition.notify_all()
            return True


class SynchronizedMixedResetGate:
    """Release one data fault and one control fault only after both are ready."""

    def __init__(self, participants, timeout=10.0):
        self.participants = frozenset((str(kind), int(number)) for kind, number in participants)
        kinds = {kind for kind, _ in self.participants}
        if len(self.participants) != 2 or kinds != {"data", "control"}:
            raise ValueError("mixed reset requires one data and one control participant")
        self.timeout = timeout
        self._condition = threading.Condition()
        self.ready_participants = set()
        self.reset_participants = set()
        self._failed = False

    def wait_and_claim(self, participant):
        participant = (str(participant[0]), int(participant[1]))
        if participant not in self.participants:
            return False
        deadline = time.monotonic() + self.timeout
        with self._condition:
            if self._failed:
                return False
            self.ready_participants.add(participant)
            self._condition.notify_all()
            while self.participants - self.ready_participants and not self._failed:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self._failed = True
                    self._condition.notify_all()
                    return False
                self._condition.wait(remaining)
            if self._failed or participant in self.reset_participants:
                return False
            self.reset_participants.add(participant)
            self._condition.notify_all()
            return True


class PauseFileGate:
    """Pause exactly once after a byte threshold until the gate file is removed."""

    def __init__(self, path):
        self.path = Path(path)

    def pause(self, stopped, connection_id, forwarded_bytes, emit):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.touch(exist_ok=True)
        emit(event="data_stalled", connection=connection_id,
             gate=str(self.path), forwarded_bytes=forwarded_bytes)
        while self.path.exists() and not stopped.wait(0.01):
            pass
        if not stopped.is_set():
            emit(event="data_forwarding_resumed", connection=connection_id,
                 gate=str(self.path), forwarded_bytes=forwarded_bytes)


class SequentialResetPlan:
    """Advance the next data connection target after each completed reset."""

    def __init__(self, fault_count, first_connection=2):
        if int(fault_count) < 1:
            raise ValueError("sequential reset requires at least one fault")
        self.remaining_faults = int(fault_count)
        self.next_connection = int(first_connection)
        self.reset_connections = []

    def is_target(self, data_connection):
        return (
            self.remaining_faults > 0
            and int(data_connection) == self.next_connection
        )

    def claim(self, data_connection, next_connection):
        data_connection = int(data_connection)
        if not self.is_target(data_connection):
            return False
        self.remaining_faults -= 1
        self.reset_connections.append(data_connection)
        self.next_connection = int(next_connection)
        return True


class SequentialMixedResetPlan:
    """Advance data/control faults in a fixed order after each reset."""

    def __init__(self, participants):
        self.participants = tuple((str(kind), int(number)) for kind, number in participants)
        if not self.participants or any(kind not in {"data", "control"} for kind, _ in self.participants):
            raise ValueError("sequential mixed reset requires data/control participants")
        self.index = 0
        self.reset_participants = []

    def participant_for(self, data_connection):
        if self.index >= len(self.participants):
            return None
        kind, number = self.participants[self.index]
        if int(number) == int(data_connection):
            return kind
        return None

    def claim(self, kind, data_connection):
        participant = (str(kind), int(data_connection))
        if self.index >= len(self.participants) or self.participants[self.index] != participant:
            return False
        self.reset_participants.append(participant)
        self.index += 1
        return True


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
        target_data_connections=None,
        synchronize_data_resets=False,
        synchronize_mixed_resets=False,
        sequential_data_resets=False,
        sequential_mixed_resets=False,
        sequential_mixed_long_resets=False,
        drop_control_after_data_connection=None,
        pause_file=None,
        pause_after_bytes=0,
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
        self.pause_gate = PauseFileGate(pause_file) if pause_file else None
        self.pause_after_bytes = max(0, int(pause_after_bytes))
        self.pause_forwarded_bytes = 0
        self.pause_triggered = False
        self.dropped = 0
        self.forwarded = 0
        self.forwarded_by_connection = {}
        self.log = Path(log) if log else None
        if drop_control_after_data_connection is None:
            self.control_reset_targets = set()
        elif isinstance(drop_control_after_data_connection, int):
            self.control_reset_targets = {drop_control_after_data_connection}
        else:
            self.control_reset_targets = {int(value) for value in drop_control_after_data_connection}
        self.drop_control_after_data_connection = self.control_reset_targets
        self.target_data_connection = target_data_connection
        self.data_fault_targets = {
            int(value) for value in (target_data_connections or ())
        }
        if target_data_connection is not None:
            self.data_fault_targets.add(int(target_data_connection))
        self.target_data_connections = set(self.data_fault_targets)
        self.target_data_connections.update(self.control_reset_targets)
        if self.target_data_connection is None and self.control_reset_targets:
            self.target_data_connection = min(self.control_reset_targets)
        self.explicit_data_targets = bool(self.target_data_connections)
        self.sequential_data_resets = bool(sequential_data_resets)
        self.sequential_reset_plan = (
            SequentialResetPlan(drops) if self.sequential_data_resets else None
        )
        self.sequential_mixed_resets = bool(sequential_mixed_resets)
        self.sequential_mixed_long_resets = bool(sequential_mixed_long_resets)
        self.sequential_mixed_reset_plan = (
            SequentialMixedResetPlan((
                ("data", 2), ("control", 3), ("data", 4), ("control", 5)
            )) if self.sequential_mixed_long_resets else (
                SequentialMixedResetPlan((
                    ("data", 2), ("control", 3), ("data", 4)
                )) if self.sequential_mixed_resets else None
            )
        )
        self.explicit_data_targets = self.explicit_data_targets or self.sequential_data_resets
        self.explicit_data_targets = self.explicit_data_targets or self.sequential_mixed_resets
        self.explicit_data_targets = self.explicit_data_targets or self.sequential_mixed_long_resets
        self.target_connection = None
        self.target_connections = set()
        self.synchronize_mixed_resets = bool(synchronize_mixed_resets)
        if self.synchronize_mixed_resets and (
            len(self.data_fault_targets) != 1
            or len(self.control_reset_targets) != 1
            or self.data_fault_targets & self.control_reset_targets
        ):
            raise ValueError("mixed reset requires different data and control lane targets")
        self.mixed_reset_gate = (
            SynchronizedMixedResetGate({
                ("data", next(iter(self.data_fault_targets))),
                ("control", next(iter(self.control_reset_targets))),
            })
            if self.synchronize_mixed_resets else None
        )
        self.synchronize_data_resets = bool(
            synchronize_data_resets and len(self.target_data_connections) > 1
            and not self.control_reset_targets
        )
        if (self.sequential_data_resets or self.sequential_mixed_resets
                or self.sequential_mixed_long_resets) and (
            self.control_reset_targets or self.synchronize_data_resets
            or self.synchronize_mixed_resets
        ):
            raise ValueError("sequential reset cannot be combined with another reset mode")
        self.data_reset_gate = (
            SynchronizedResetGate(self.target_data_connections)
            if self.synchronize_data_resets else None
        )
        self.data_reset_ready_logged = set()
        self.mixed_reset_ready_logged = set()
        self.data_reset_done = set()
        self.data_connection_count = 0
        self.control_connection_count = 0
        self.control_sockets = {}
        self.data_port_control_connection = {}
        self.data_connection_number = {}
        self.data_connection_control = {}
        self.control_lane_index = {}
        self.control_batch_id = {}
        self.control_reset_done = set()
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
                            self.sequential_reset_plan is not None
                            and self.sequential_reset_plan.is_target(self.data_connection_count)
                        ) or (
                            self.sequential_mixed_reset_plan is not None
                            and self.sequential_mixed_reset_plan.participant_for(self.data_connection_count) is not None
                        ) or (
                            self.sequential_reset_plan is None
                            and self.explicit_data_targets
                            and self.data_connection_count in self.target_data_connections
                        )
                        if target:
                            self.target_connections.add(connection_id)
                            if self.target_connection is None:
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
                    not self.explicit_data_targets
                    or connection_id in self.target_connections
                )
                reset_control_connection = None
                synchronize_data_connection = None
                mixed_fault_participant = None
                pause_now = False
                pause_bytes = 0
                if data or self.drop_terminal or self.drop_result:
                    reset = False
                    with self.lock:
                        if data:
                            self.forwarded += len(block)
                            if (
                                self.pause_gate is not None
                                and self.pause_after_bytes > 0
                                and not self.pause_triggered
                            ):
                                self.pause_forwarded_bytes += len(block)
                                if self.pause_forwarded_bytes >= self.pause_after_bytes:
                                    self.pause_triggered = True
                                    pause_now = True
                                    pause_bytes = self.pause_forwarded_bytes
                            if should_fault:
                                self.forwarded_by_connection[connection_id] = (
                                    self.forwarded_by_connection.get(connection_id, 0) + len(block)
                                )
                        forwarded = self.forwarded_by_connection.get(connection_id, 0)
                        data_number = self.data_connection_number.get(connection_id)
                        mapped_control = self.data_connection_control.get(connection_id)
                        sequential_target = (
                            self.sequential_reset_plan is not None
                            and self.sequential_reset_plan.is_target(data_number)
                        )
                        sequential_mixed_target = (
                            self.sequential_mixed_reset_plan is not None
                            and self.sequential_mixed_reset_plan.participant_for(data_number)
                        )
                        if (
                            self.synchronize_data_resets
                            and data
                            and should_fault
                            and data_number in self.target_data_connections
                            and self.drop_bytes
                            and forwarded >= self.drop_bytes
                        ):
                            synchronize_data_connection = data_number
                            if data_number not in self.data_reset_ready_logged:
                                self.data_reset_ready_logged.add(data_number)
                                self.event(
                                    event="data_reset_ready",
                                    connection=connection_id,
                                    data_connection=data_number,
                                    lane_index=self.control_lane_index.get(mapped_control),
                                    data_port=data_port,
                                    target=True,
                                )
                        if (
                            self.synchronize_mixed_resets
                            and data
                            and should_fault
                            and self.drop_bytes
                            and forwarded >= self.drop_bytes
                        ):
                            if data_number in self.data_fault_targets:
                                mixed_fault_participant = ("data", data_number)
                            elif data_number in self.control_reset_targets and mapped_control is not None:
                                mixed_fault_participant = ("control", data_number)
                            if (
                                mixed_fault_participant is not None
                                and mixed_fault_participant not in self.mixed_reset_ready_logged
                            ):
                                self.mixed_reset_ready_logged.add(mixed_fault_participant)
                                self.event(
                                    event="mixed_reset_ready",
                                    scope=mixed_fault_participant[0],
                                    connection=connection_id,
                                    data_connection=data_number,
                                    control_connection=mapped_control,
                                    lane_index=self.control_lane_index.get(mapped_control),
                                    batch_id=self.control_batch_id.get(mapped_control),
                                    data_port=data_port,
                                    target=True,
                                )
                        data_triggered = (
                            not self.control_reset_targets
                            and not self.synchronize_data_resets
                            and not self.synchronize_mixed_resets
                            and not self.sequential_data_resets
                            and not self.sequential_mixed_resets
                            and not self.sequential_mixed_long_resets
                            and should_fault
                            and self.drop_bytes
                            and forwarded >= self.drop_bytes
                        )
                        sequential_triggered = (
                            self.sequential_reset_plan is not None
                            and data
                            and should_fault
                            and sequential_target
                            and self.drop_bytes
                            and forwarded >= self.drop_bytes
                        )
                        sequential_mixed_triggered = (
                            sequential_mixed_target == "data"
                            and data
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
                                data_connection=data_number,
                                data_port=data_port,
                                target=should_fault,
                            )
                        if sequential_triggered and self.remaining:
                            if self.sequential_reset_plan.claim(
                                data_number, data_number + 1
                            ):
                                self.remaining -= 1
                                self.dropped += 1
                                reset = True
                                self.event(
                                    event="reset",
                                    count=self.dropped,
                                    scope="data",
                                    connection=connection_id,
                                    data_connection=data_number,
                                    lane_index=self.control_lane_index.get(mapped_control),
                                    batch_id=self.control_batch_id.get(mapped_control),
                                    data_port=data_port,
                                    target=True,
                                    sequence=len(self.sequential_reset_plan.reset_connections),
                                )
                        if sequential_mixed_triggered and self.remaining:
                            if self.sequential_mixed_reset_plan.claim("data", data_number):
                                self.remaining -= 1
                                self.dropped += 1
                                reset = True
                                self.event(
                                    event="reset",
                                    count=self.dropped,
                                    scope="data",
                                    connection=connection_id,
                                    data_connection=data_number,
                                    lane_index=self.control_lane_index.get(mapped_control),
                                    batch_id=self.control_batch_id.get(mapped_control),
                                    data_port=data_port,
                                    target=True,
                                    sequence=len(self.sequential_mixed_reset_plan.reset_participants),
                                )
                        if (
                            sequential_mixed_target == "control"
                            and mapped_control is not None
                            and self.remaining
                            and self.sequential_mixed_reset_plan.claim("control", data_number)
                        ):
                            self.remaining -= 1
                            self.dropped += 1
                            reset_control_connection = mapped_control
                            self.event(
                                event="reset",
                                count=self.dropped,
                                scope="control",
                                connection=mapped_control,
                                control_connection=mapped_control,
                                lane_index=self.control_lane_index.get(mapped_control),
                                batch_id=self.control_batch_id.get(mapped_control),
                                trigger_data_connection=data_number,
                                data_port=data_port,
                                target=True,
                                sequence=len(self.sequential_mixed_reset_plan.reset_participants),
                            )
                        if (
                            data
                            and not self.synchronize_mixed_resets
                            and data_number in self.control_reset_targets
                            and data_number not in self.control_reset_done
                            and mapped_control is not None
                            and forwarded >= self.drop_bytes
                        ):
                            self.control_reset_done.add(data_number)
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
                    if (
                        mixed_fault_participant is not None
                        and self.mixed_reset_gate is not None
                        and self.mixed_reset_gate.wait_and_claim(mixed_fault_participant)
                    ):
                        with self.lock:
                            fault_kind, fault_data_number = mixed_fault_participant
                            if fault_kind == "data" and connection_id not in self.data_reset_done and self.remaining:
                                self.data_reset_done.add(connection_id)
                                self.remaining -= 1
                                self.dropped += 1
                                reset = True
                                self.event(
                                    event="reset",
                                    count=self.dropped,
                                    scope="data",
                                    connection=connection_id,
                                    data_connection=fault_data_number,
                                    lane_index=self.control_lane_index.get(mapped_control),
                                    batch_id=self.control_batch_id.get(mapped_control),
                                    data_port=data_port,
                                    target=True,
                                )
                            elif (
                                fault_kind == "control"
                                and fault_data_number not in self.control_reset_done
                                and mapped_control is not None
                                and self.remaining
                            ):
                                self.control_reset_done.add(fault_data_number)
                                self.remaining -= 1
                                self.dropped += 1
                                reset_control_connection = mapped_control
                                self.event(
                                    event="reset",
                                    count=self.dropped,
                                    scope="control",
                                    connection=mapped_control,
                                    control_connection=mapped_control,
                                    lane_index=self.control_lane_index.get(mapped_control),
                                    batch_id=self.control_batch_id.get(mapped_control),
                                    trigger_data_connection=fault_data_number,
                                    data_port=data_port,
                                    target=True,
                                )
                    if (
                        synchronize_data_connection is not None
                        and self.data_reset_gate is not None
                        and self.data_reset_gate.wait_and_claim(synchronize_data_connection)
                    ):
                        with self.lock:
                            if (
                                connection_id not in self.data_reset_done
                                and self.remaining
                            ):
                                self.data_reset_done.add(connection_id)
                                self.remaining -= 1
                                self.dropped += 1
                                reset = True
                                self.event(
                                    event="reset",
                                    count=self.dropped,
                                    connection=connection_id,
                                    data_connection=synchronize_data_connection,
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
                if pause_now:
                    self.pause_gate.pause(
                        self.stopped, connection_id, pause_bytes, self.event
                    )
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
    parser.add_argument("--target-data-connection", type=int, action="append")
    parser.add_argument("--synchronize-data-resets", action="store_true")
    parser.add_argument("--synchronize-mixed-resets", action="store_true")
    parser.add_argument("--sequential-data-resets", action="store_true")
    parser.add_argument("--sequential-mixed-resets", action="store_true")
    parser.add_argument("--sequential-mixed-long-resets", action="store_true")
    parser.add_argument("--drop-control-after-data-connection", type=int, action="append")
    parser.add_argument("--log", type=Path)
    parser.add_argument("--pause-file", type=Path)
    parser.add_argument("--pause-after-bytes", type=int, default=0)
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
        target_data_connections=args.target_data_connection,
        synchronize_data_resets=args.synchronize_data_resets,
        synchronize_mixed_resets=args.synchronize_mixed_resets,
        sequential_data_resets=args.sequential_data_resets,
        sequential_mixed_resets=args.sequential_mixed_resets,
        sequential_mixed_long_resets=args.sequential_mixed_long_resets,
        drop_control_after_data_connection=args.drop_control_after_data_connection,
        pause_file=args.pause_file,
        pause_after_bytes=args.pause_after_bytes,
    )
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        proxy.close()
