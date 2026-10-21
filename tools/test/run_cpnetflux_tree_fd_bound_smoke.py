#!/usr/bin/env python3
"""Transfer more files than the receiver's fd limit without weakening integrity."""
import argparse
import json
import resource
import secrets
import subprocess
import tempfile
from pathlib import Path

from gridftp_port_window import clamp_passive_data_port_base
from run_gridftp_data_tls_smoke import generate_cert, wait_tls_server
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control


def bound_descriptors():
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    resource.setrlimit(resource.RLIMIT_NOFILE, (min(96, soft), hard))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--tls", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-fd-bound.") as temporary:
        root = Path(temporary)
        source, target = root / "source", root / "server"
        source.mkdir()
        target.mkdir()
        for index in range(192):
            (source / f"{index:04}.bin").write_bytes(bytes([index % 251]) * 2048)
        expected = tree_hash(source)
        port, data_port = free_port(), clamp_passive_data_port_base(free_port())
        auth, client_tls, server_tls = [], [], []
        cert, key = root / "cert.pem", root / "key.pem"
        if args.tls:
            token = root / "token"
            token.write_text(secrets.token_hex(24) + "\n")
            token.chmod(0o600)
            auth = ["--auth-mode", "token", "--auth-token-file", str(token)]
            if not generate_cert(cert, key):
                raise RuntimeError("openssl is required for the TLS descriptor gate")
            server_tls = ["--tls-mode", "required", "--tls-cert-file", str(cert),
                          "--tls-key-file", str(key), "--data-tls-mode", "required"]
            client_tls = ["--tls-mode", "required", "--tls-ca-file", str(cert),
                          "--tls-server-name", "localhost", "--data-tls-mode", "required"]
        log = root / "server.log"
        command = [str(build / "cpnetflux-gridftp-server"), "--host", "127.0.0.1",
                   "--port", str(port), "--root", str(target), "--data-port-base",
                   str(data_port), "--checksum", "crc32c", *auth, *server_tls]
        with log.open("w") as handle:
            server = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT,
                                      preexec_fn=bound_descriptors)
        try:
            if args.tls:
                wait_tls_server(port, cert)
            else:
                wait_for_control(port)
            for mode in ("off", "auto"):
                for direction in ("upload", "download"):
                    summary = root / f"{mode}-{direction}.json"
                    downloaded = root / f"{mode}-download"
                    source_path = str(source) if direction == "upload" else mode
                    dest_path = mode if direction == "upload" else str(downloaded)
                    command = [str(build / f"cpnetflux-tree-{direction}-client"),
                               "--host", "127.0.0.1", "--port", str(port),
                               "--source-dir", source_path, "--dest-dir", dest_path,
                               "--control-reuse", "worker", "--data-session-reuse", "tree",
                               "--file-scheduling", "dynamic",
                               "--file-parallelism", "4", "--chunk-size", "65536",
                               "--checksum", "crc32c", "--compression", mode,
                               "--json-summary", str(summary), *auth, *client_tls]
                    result = subprocess.run(command, text=True, capture_output=True,
                                            timeout=60, preexec_fn=bound_descriptors)
                    if result.returncode:
                        raise RuntimeError(f"{direction}/{mode}: {result.stdout}{result.stderr}")
                    observed = json.loads(summary.read_text())
                    assert observed["result"] == "pass", observed
                    assert observed["completed_files"] == expected[1], observed
                    actual = tree_hash(target / mode if direction == "upload" else downloaded)
                    assert actual == expected, (direction, mode, actual, expected)
            print(json.dumps({"result": "pass", "fd_soft_limit": 96,
                              "files": expected[1], "tls": args.tls,
                              "directions": ["upload", "download"], "modes": ["off", "auto"],
                              "tree_sha256": expected[0]}))
        finally:
            stop_server(server, log)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
