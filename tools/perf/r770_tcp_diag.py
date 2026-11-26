#!/usr/bin/env python3
"""Auditable IPv4 TCP socket snapshots and destroy-event decoding for R770 runs."""
from datetime import datetime, timezone
import errno
import select
import socket
import struct
import time

NETLINK_SOCK_DIAG = 4
SOL_NETLINK = 270
NETLINK_ADD_MEMBERSHIP = 1
SKNLGRP_INET_TCP_DESTROY = 1
SOCK_DIAG_BY_FAMILY = 20
NLMSG_ERROR = 2
NLMSG_DONE = 3
NLMSG_OVERRUN = 4
NLM_F_REQUEST = 1
NLM_F_DUMP = 0x300
NLM_F_DUMP_INTR = 0x10
INET_DIAG_INFO = 2
TCP_INFO_MIN_BYTES = 216
SOCKET_DIAG_HEADER_BYTES = 72
NETLINK_READ_BYTES = 1024 * 1024
DIRECT_IPS = frozenset(("192.168.100.1", "192.168.100.2"))
EXCLUDED_PORTS = frozenset((22, 2121))
CONTROL_PORTS = frozenset((45582, 45583, 45592, 45593, 45596))
GRID_DATA_PORTS = frozenset(
    (*range(45730, 45755), *range(45760, 45785),
     *range(45830, 45855), *range(45860, 45885))
)
CP_DATA_PORTS = frozenset(range(61350, 61354))
DATA_PORTS = GRID_DATA_PORTS | CP_DATA_PORTS
EXPERIMENT_PORTS = CONTROL_PORTS | DATA_PORTS
TCP_TIME_WAIT = 6
TCP_LISTEN = 10
TCP_NEW_SYN_RECV = 12
NON_FLOW_TCP_STATES = frozenset((TCP_TIME_WAIT, TCP_LISTEN, TCP_NEW_SYN_RECV))
TCP_STATE_NAMES = {
    1: "ESTABLISHED", 2: "SYN_SENT", 3: "SYN_RECV", 4: "FIN_WAIT1",
    5: "FIN_WAIT2", TCP_TIME_WAIT: "TIME_WAIT", 7: "CLOSE", 8: "CLOSE_WAIT",
    9: "LAST_ACK", TCP_LISTEN: "LISTEN", 11: "CLOSING", TCP_NEW_SYN_RECV: "NEW_SYN_RECV",
}


class SocketDiagError(RuntimeError):
    """A diagnostic query or multicast stream could not be trusted."""


def _align4(length):
    return (length + 3) & ~3


def _iter_netlink_messages(data):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 16:
            if any(data[offset:]):
                raise SocketDiagError("truncated netlink message header")
            return
        length, kind, flags, sequence, port_id = struct.unpack_from("=IHHII", data, offset)
        if length < 16 or offset + length > len(data):
            raise SocketDiagError("invalid netlink message length")
        payload = data[offset + 16:offset + length]
        yield kind, flags, sequence, port_id, payload
        next_offset = offset + _align4(length)
        if next_offset > len(data):
            if offset + length == len(data):
                return
            raise SocketDiagError("truncated netlink message alignment")
        offset = next_offset


def _parse_attributes(data):
    attrs = {}
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            if any(data[offset:]):
                raise SocketDiagError("truncated inet_diag attribute header")
            break
        length, attr_type = struct.unpack_from("=HH", data, offset)
        if length < 4 or offset + length > len(data):
            raise SocketDiagError("invalid inet_diag attribute length")
        attrs[attr_type & 0x3FFF] = data[offset + 4:offset + length]
        next_offset = offset + _align4(length)
        if next_offset > len(data):
            if offset + length == len(data):
                break
            raise SocketDiagError("truncated inet_diag attribute alignment")
        offset = next_offset
    return attrs


def parse_inet_diag_message(payload, observed_at_ns=None, source="snapshot"):
    if len(payload) < SOCKET_DIAG_HEADER_BYTES:
        raise SocketDiagError("short inet_diag_msg")
    family, state, _timer, retrans = struct.unpack_from("=BBBB", payload, 0)
    if family != socket.AF_INET:
        raise SocketDiagError("unexpected address family in IPv4 diagnostic")
    local_port = struct.unpack_from("!H", payload, 4)[0]
    peer_port = struct.unpack_from("!H", payload, 6)[0]
    local_ip = socket.inet_ntoa(payload[8:12])
    peer_ip = socket.inet_ntoa(payload[24:28])
    cookie_words = struct.unpack_from("=II", payload, 44)
    cookie = "".join("{:08x}".format(word) for word in cookie_words)
    attrs = _parse_attributes(payload[SOCKET_DIAG_HEADER_BYTES:])
    info = attrs.get(INET_DIAG_INFO, b"")
    has_tcp_info = len(info) >= TCP_INFO_MIN_BYTES
    retrans_total = struct.unpack_from("=I", info, 100)[0] if has_tcp_info else None
    bytes_retrans = struct.unpack_from("=Q", info, 208)[0] if has_tcp_info else None
    observed_at_ns = time.time_ns() if observed_at_ns is None else observed_at_ns
    return {
        "source": source,
        "observed_at_utc": datetime.fromtimestamp(
            observed_at_ns / 1_000_000_000, timezone.utc).isoformat(),
        "observed_at_ns": observed_at_ns,
        "local_ip": local_ip,
        "local_port": local_port,
        "peer_ip": peer_ip,
        "peer_port": peer_port,
        "local_tuple": "{}:{}".format(local_ip, local_port),
        "peer_tuple": "{}:{}".format(peer_ip, peer_port),
        "socket_cookie": cookie,
        "tcp_state": state,
        "tcp_state_name": TCP_STATE_NAMES.get(state, "STATE_{}".format(state)),
        "idiag_retrans": retrans,
        "inet_diag_info_bytes": len(info),
        "tcp_info_present": has_tcp_info,
        "retrans_total": retrans_total,
        "bytes_retrans": bytes_retrans,
    }


def parse_diag_datagram(data, observed_at_ns=None, source="snapshot"):
    rows = []
    for kind, _flags, _sequence, _port_id, payload in _iter_netlink_messages(data):
        if kind == NLMSG_OVERRUN:
            raise SocketDiagError("diagnostic datagram reported NLMSG_OVERRUN")
        if kind == NLMSG_ERROR:
            if len(payload) < 4:
                raise SocketDiagError("short NLMSG_ERROR")
            error_code = struct.unpack_from("=i", payload, 0)[0]
            if error_code:
                raise SocketDiagError("netlink error {}".format(error_code))
            continue
        if kind == SOCK_DIAG_BY_FAMILY:
            rows.append(parse_inet_diag_message(payload, observed_at_ns, source))
    return rows


def is_experiment_socket(row):
    return (
        row.get("local_ip") in DIRECT_IPS
        and row.get("peer_ip") in DIRECT_IPS
        and row.get("local_ip") != row.get("peer_ip")
        and row.get("local_port") not in EXCLUDED_PORTS
        and row.get("peer_port") not in EXCLUDED_PORTS
        and (row.get("local_port") in EXPERIMENT_PORTS
             or row.get("peer_port") in EXPERIMENT_PORTS)
    )


def is_data_socket(row):
    return row.get("local_port") in DATA_PORTS or row.get("peer_port") in DATA_PORTS


def is_tcp_flow_socket(row):
    """Exclude TIME_WAIT minisockets, listeners, and request_sock entries."""
    state = row.get("tcp_state")
    return isinstance(state, int) and state not in NON_FLOW_TCP_STATES


def socket_key(row):
    return (
        row.get("socket_cookie"),
        row.get("local_ip"), row.get("local_port"),
        row.get("peer_ip"), row.get("peer_port"),
    )


def reverse_tuple(row):
    return row["peer_ip"], row["peer_port"], row["local_ip"], row["local_port"]


class SocketDiagObserver:
    def __init__(self):
        self.event_socket = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, NETLINK_SOCK_DIAG)
        self.dump_socket = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, NETLINK_SOCK_DIAG)
        self.event_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, NETLINK_READ_BYTES)
        self.dump_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, NETLINK_READ_BYTES)
        self.event_socket.bind((0, 0))
        self.dump_socket.bind((0, 0))
        self.dump_socket.settimeout(2)
        self.event_socket.setsockopt(
            SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, SKNLGRP_INET_TCP_DESTROY)
        self.event_socket_rcvbuf_bytes = self.event_socket.getsockopt(
            socket.SOL_SOCKET, socket.SO_RCVBUF)
        self.dump_socket_rcvbuf_bytes = self.dump_socket.getsockopt(
            socket.SOL_SOCKET, socket.SO_RCVBUF)
        self.subscribed_at_ns = time.time_ns()
        self.sequence = 0
        self.closed = False

    def snapshot(self):
        self.sequence += 1
        seq = self.sequence
        no_cookie = struct.pack("=II", 0xFFFFFFFF, 0xFFFFFFFF)
        sock_id = (
            struct.pack("!HH", 0, 0) + bytes(32) + struct.pack("=I", 0) + no_cookie
        )
        request_body = struct.pack(
            "=BBBBI", socket.AF_INET, socket.IPPROTO_TCP,
            1 << (INET_DIAG_INFO - 1), 0, 0xFFFFFFFF
        ) + sock_id
        header = struct.pack(
            "=IHHII", 16 + len(request_body), SOCK_DIAG_BY_FAMILY,
            NLM_F_REQUEST | NLM_F_DUMP, seq, self.dump_socket.getsockname()[0]
        )
        self.dump_socket.send(header + request_body)
        rows = []
        complete = False
        while not complete:
            try:
                data, _ancillary, flags, _address = self.dump_socket.recvmsg(NETLINK_READ_BYTES)
            except OSError as error:
                raise SocketDiagError("socket dump failed: {}".format(error)) from error
            if flags & socket.MSG_TRUNC:
                raise SocketDiagError("socket dump datagram truncated")
            for kind, message_flags, message_seq, _port_id, payload in _iter_netlink_messages(data):
                if message_seq != seq:
                    continue
                if message_flags & NLM_F_DUMP_INTR:
                    raise SocketDiagError("socket dump interrupted by concurrent table changes")
                if kind == NLMSG_DONE:
                    if len(payload) >= 4:
                        code = struct.unpack_from("=i", payload, 0)[0]
                        if code:
                            raise SocketDiagError("socket dump completed with error {}".format(code))
                    complete = True
                elif kind == NLMSG_ERROR:
                    if len(payload) < 4:
                        raise SocketDiagError("short socket dump NLMSG_ERROR")
                    code = struct.unpack_from("=i", payload, 0)[0]
                    if code:
                        raise SocketDiagError("socket dump netlink error {}".format(code))
                elif kind == SOCK_DIAG_BY_FAMILY:
                    rows.append(parse_inet_diag_message(payload, source="snapshot"))
                elif kind == NLMSG_OVERRUN:
                    raise SocketDiagError("socket dump reported NLMSG_OVERRUN")
        return rows

    def drain_destroy_events(self):
        events = []
        while select.select((self.event_socket,), (), (), 0)[0]:
            try:
                data, _ancillary, flags, _address = self.event_socket.recvmsg(NETLINK_READ_BYTES)
            except OSError as error:
                if error.errno == errno.ENOBUFS:
                    raise SocketDiagError("NETLINK_SOCK_DIAG multicast overrun (ENOBUFS)") from error
                raise SocketDiagError("destroy-event receive failed: {}".format(error)) from error
            if flags & socket.MSG_TRUNC:
                raise SocketDiagError("destroy-event datagram truncated")
            observed_at_ns = time.time_ns()
            for kind, _message_flags, sequence, port_id, payload in _iter_netlink_messages(data):
                if kind == NLMSG_OVERRUN:
                    raise SocketDiagError("destroy-event stream reported NLMSG_OVERRUN")
                if kind == NLMSG_ERROR:
                    if len(payload) < 4:
                        raise SocketDiagError("short destroy-event NLMSG_ERROR")
                    code = struct.unpack_from("=i", payload, 0)[0]
                    if code:
                        raise SocketDiagError("destroy-event netlink error {}".format(code))
                    continue
                if kind == SOCK_DIAG_BY_FAMILY:
                    row = parse_inet_diag_message(
                        payload, observed_at_ns=observed_at_ns, source="destroy_event")
                    row["netlink_sequence"] = sequence
                    row["netlink_port_id"] = port_id
                    events.append(row)
        return events

    def close(self):
        if not self.closed:
            self.event_socket.close()
            self.dump_socket.close()
            self.closed = True


def loopback_destroy_selftest(timeout_seconds=3):
    observer = SocketDiagObserver()
    listener = client = accepted = None
    try:
        observer.snapshot()
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.settimeout(timeout_seconds)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener_port = listener.getsockname()[1]
        client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client.settimeout(timeout_seconds)
        client.connect(("127.0.0.1", listener_port))
        accepted, _ = listener.accept()
        client.sendall(b"x")
        received = accepted.recv(1)
        client_port = client.getsockname()[1]
        client.close()
        client = None
        accepted.close()
        accepted = None
        listener.close()
        listener = None
        if received != b"x":
            raise SocketDiagError("loopback one-byte payload mismatch")
        deadline = time.monotonic() + timeout_seconds
        matches = []
        while time.monotonic() < deadline and len(matches) < 2:
            for row in observer.drain_destroy_events():
                if (row["local_ip"] == "127.0.0.1" and row["peer_ip"] == "127.0.0.1"
                        and {row["local_port"], row["peer_port"]} == {listener_port, client_port}):
                    matches.append(row)
            if len(matches) < 2:
                time.sleep(0.025)
        if len({row["socket_cookie"] for row in matches}) != 2:
            raise SocketDiagError("loopback destroy events did not cover both TCP endpoints")
        if any(not row["tcp_info_present"] for row in matches):
            raise SocketDiagError("loopback destroy event lacks full TCP_INFO")
        return {
            "payload_bytes": len(received),
            "listener_port": listener_port,
            "client_port": client_port,
            "destroy_events": matches,
            "event_socket_rcvbuf_bytes": observer.event_socket_rcvbuf_bytes,
            "kernel_event_loss_detected": False,
        }
    finally:
        for item in (accepted, client, listener):
            if item is not None:
                item.close()
        observer.close()
