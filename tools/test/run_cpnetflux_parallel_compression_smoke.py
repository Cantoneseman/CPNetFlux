#!/usr/bin/env python3
"""Exercise negotiated V3 compression with real clients and SHA-256 verification."""
import argparse
import json
import os
import secrets
import socket
import ssl
import subprocess
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, start_server, stop_server, tree_hash
from gridftp_port_window import clamp_passive_data_port_base
from run_gridftp_data_tls_smoke import generate_cert, wait_tls_server


def run(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def negotiate(port, suffix, ca=None, token=None):
    connection = socket.create_connection(("127.0.0.1", port), timeout=5)
    if ca:
        connection = ssl.create_default_context(cafile=str(ca)).wrap_socket(
            connection, server_hostname="localhost")
    with connection:
        reader = connection.makefile("rwb", buffering=0)
        assert reader.readline().startswith(b"220")
        for command, prefix in [("USER " + ("token" if token else "cpnetflux"), b"331"),
                                ("PASS " + (token or "cpnetflux"), b"230")]:
            reader.write(command.encode() + b"\r\n")
            assert reader.readline().startswith(prefix)
        reader.write(("XCPNETFLUX V3 CHECKSUM=crc32c RESUME=0 DATA_TLS=" +
                      ("required " if ca else "off ") + suffix + "\r\n").encode())
        return reader.readline()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--tls", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-parallel-compression.") as temporary:
        root = Path(temporary)
        source, target = root / "source", root / "server"
        source.mkdir()
        target.mkdir()
        (source / "empty").write_bytes(b"")
        (source / "repeated").write_bytes(b"CPNetFlux V3 ordered compression\n" * 65536)
        (source / "random").write_bytes(os.urandom(256 * 1024))
        for index in range(64):
            folder = source / str(index % 8)
            folder.mkdir(exist_ok=True)
            (folder / str(index)).write_bytes(bytes([index]) * (8192 + index))
        expected = tree_hash(source)
        port, data = free_port(), free_port()
        log = root / "server.log"
        ca = root / "cert.pem" if args.tls else None
        token = None
        auth = []
        if args.tls:
            token = secrets.token_hex(24)
            token_file = root / "token"
            token_file.write_text(token + "\n")
            token_file.chmod(0o600)
            auth = ["--auth-mode", "token", "--auth-token-file", str(token_file)]
        if args.tls:
            key = root / "key.pem"
            if not generate_cert(ca, key):
                raise RuntimeError("openssl is required for the compression/TLS gate")
            command = [str(build / "cpnetflux-gridftp-server"), "--host", "127.0.0.1",
                       "--port", str(port), "--root", str(target), "--data-port-base",
                       str(clamp_passive_data_port_base(data)), "--checksum", "crc32c",
                       "--tls-mode", "required", "--tls-cert-file", str(ca),
                       "--tls-key-file", str(key), "--data-tls-mode", "required",
                       *auth]
            with log.open("w") as handle:
                server = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
        else:
            server = start_server(build, target, port, data, log)
        try:
            if ca:
                wait_tls_server(port, ca)
            assert b"compression=zlib" in negotiate(port, "COMPRESSION=zlib", ca, token)
            assert negotiate(port, "COMPRESSION=unknown", ca, token).startswith(b"504")
            assert negotiate(port, "COMPRESSION=zlib COMPRESSION=zlib", ca, token).startswith(b"504")
            summaries = {}
            for mode in ("off", "auto"):
                summary = root / (mode + ".json")
                common = ["--host", "127.0.0.1", "--port", str(port),
                          "--file-parallelism", "4",
                          "--data-pending-window", "2", "--chunk-size", "65536",
                          "--compression", mode, "--json-summary", str(summary), *auth]
                if ca:
                    common += ["--tls-mode", "required", "--tls-ca-file", str(ca),
                               "--tls-server-name", "localhost", "--data-tls-mode", "required"]
                run([str(build / "cpnetflux-tree-upload-client"),
                     "--source-dir", str(source), "--dest-dir", mode, *common])
                assert tree_hash(target / mode) == expected
                observed = json.loads(summary.read_text())
                summaries[mode] = observed
                assert observed["actual_mode"] == "persistent_tree", observed
                if mode == "off":
                    assert observed["compression_attempts"] == 0, observed
                else:
                    assert observed["compressed_frames"] > 0, observed
                    assert observed["raw_fallback_frames"] > 0, observed
                    assert 1 <= observed["compression_workers_observed"] <= 4, observed
                download = root / (mode + "-download")
                run([str(build / "cpnetflux-tree-download-client"),
                     "--source-dir", mode, "--dest-dir", str(download), *common])
                assert tree_hash(download) == expected
                downloaded = json.loads(summary.read_text())
                if mode == "auto":
                    assert downloaded["compressed_frames"] > 0, downloaded
                # Completed upload replay must preserve checkpoint identity and avoid payload.
                if mode == "auto":
                    run([str(build / "cpnetflux-tree-upload-client"),
                         "--source-dir", str(source), "--dest-dir", mode,
                         "--resume", *common])
                    resumed = json.loads(summary.read_text())
                    assert resumed["bytes_transferred"] == 0, resumed
                    assert tree_hash(target / mode) == expected
            assert summaries["auto"]["wire_bytes"] < summaries["off"]["wire_bytes"]
            print(json.dumps({"result": "pass", "tls": args.tls, "files": expected[1],
                              "logical_bytes": expected[2], "tree_sha256": expected[0],
                              "raw_wire_bytes": summaries["off"]["wire_bytes"],
                              "compressed_wire_bytes": summaries["auto"]["wire_bytes"]}))
        finally:
            stop_server(server, log)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
