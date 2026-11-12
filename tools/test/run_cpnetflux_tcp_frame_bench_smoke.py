#!/usr/bin/env python3
"""Exercise real frame/session memory paths, boundaries and failure acknowledgements."""
import argparse
import json
from pathlib import Path
import select
import socket
import struct
import subprocess
import threading


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def arguments(binary, role, port, connections, size, mode, crc, frame=65532):
    return [str(binary), role, "127.0.0.1", "127.0.0.1", str(port),
            str(connections), str(size), mode, crc, str(frame)]


def start_server(command):
    server = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if not select.select([server.stdout], [], [], 10)[0]:
        server.kill()
        server.communicate()
        raise AssertionError("server did not announce readiness")
    assert server.stdout.readline().strip() == "CPNETFLUX_TCP_FRAME_READY"
    return server


def result(text):
    return json.loads(next(line for line in text.splitlines() if line.startswith("{")))


def stop(process):
    if process.poll() is None:
        process.kill()
    process.communicate(timeout=5)


def exact(sock, length):
    output = bytearray()
    while len(output) < length:
        block = sock.recv(length - len(output))
        if not block:
            raise RuntimeError("proxy input truncated")
        output.extend(block)
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    binary = args.build_dir / "cpnetflux-tcp-frame-bench"
    rows = []
    for mode, crc in (("raw", "none"), ("v3", "none"), ("v3", "crc32c")):
        for connections in (1, 4):
            # Both the last frame and per-connection partitions are deliberately uneven.
            for size in (connections, 4 * 1024 * 1024 + 37):
                port = free_port()
                server = start_server(arguments(binary, "server", port, connections, size, mode, crc))
                try:
                    client = subprocess.run(arguments(binary, "client", port, connections, size, mode, crc),
                                            capture_output=True, text=True, timeout=20)
                    out, error = server.communicate(timeout=20)
                    assert client.returncode == 0, client.stderr
                    assert server.returncode == 0, error
                    sent, received = result(client.stdout), result(out)
                    expected_frames = sum((size // connections + (i < size % connections) + 65531) // 65532
                                          for i in range(connections))
                    for row in (sent, received):
                        assert row["logical_bytes"] == size and row["receiver_acknowledged"]
                        assert row["payload_chunks"] == expected_frames
                        assert row["data_frames"] == (expected_frames if mode == "v3" else 0)
                        assert row["diagnostic_only"] and not row["file_io"]
                        assert row["elapsed_seconds"] > 0
                    rows.append({"mode": mode, "crc": crc, "connections": connections, "bytes": size})
                finally:
                    stop(server)

    # A proxy changes one actual V3 DATA byte; neither peer may report success.
    port = free_port()
    size = 256 * 1024 + 13
    server = start_server(arguments(binary, "server", port, 1, size, "v3", "crc32c"))
    proxy_errors = []
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(10)
        proxy_port = listener.getsockname()[1]

        def corrupt():
            try:
                with listener.accept()[0] as client, socket.create_connection(("127.0.0.1", port), 10) as peer:
                    client.settimeout(10)
                    client.sendall(exact(peer, 1))
                    for _ in range(2):
                        header = exact(client, 64)
                        body = exact(client, struct.unpack_from("!I", header, 32)[0])
                        if struct.unpack_from("!H", header, 8)[0] == 1:
                            body[4] ^= 1
                        peer.sendall(header + body)
            except Exception as error:
                proxy_errors.append(str(error))

        proxy = threading.Thread(target=corrupt)
        proxy.start()
        try:
            client = subprocess.run(arguments(binary, "client", proxy_port, 1, size, "v3", "crc32c"),
                                    capture_output=True, text=True, timeout=20)
            _, error = server.communicate(timeout=20)
            proxy.join(timeout=10)
            assert not proxy.is_alive() and not proxy_errors, proxy_errors
            assert server.returncode != 0 and "DATA checksum mismatch" in error, error
            assert client.returncode != 0 and '"receiver_acknowledged":true' not in client.stdout
        finally:
            stop(server)

    for position, bad in ((1, "invalid"), (4, "0"), (5, "65"), (6, "0"), (7, "legacy"),
                          (8, "md5"), (9, "65536")):
        command = arguments(binary, "client", 1, 1, 1, "v3", "none")
        command[position] = bad
        invalid = subprocess.run(command, capture_output=True, text=True, timeout=5)
        assert invalid.returncode != 0 and not invalid.stdout
    print(json.dumps({"success_cases": rows, "crc_corruption_rejected": True,
                      "invalid_arguments_rejected": True}, sort_keys=True))


if __name__ == "__main__":
    main()
