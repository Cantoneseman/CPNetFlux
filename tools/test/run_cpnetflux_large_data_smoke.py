#!/usr/bin/env python3
"""Exercise negotiated DATA sizes in real V3 file transfers and reject unsupported peers."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading

from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control
from gridftp_port_window import clamp_passive_data_port_base


def command(build, direction, port, source, dest, summary, size):
    return [str(build / f"cpnetflux-tree-{direction}-client"),
            "--host", "127.0.0.1", "--port", str(port),
            "--source-dir", str(source), "--dest-dir", str(dest),
            "--connections", "4", "--file-parallelism", "4", "--link-set-links", "4",
            "--data-session-reuse", "tree", "--control-reuse", "worker",
            "--chunk-size", "1048576", "--checksum", "crc32c", "--compression", "off",
            "--data-frame-bytes", str(size), "--phase-timing", "on",
            "--transfer-timeout-ms", "15000", "--json-summary", str(summary)]


def run(build, receive_buffer):
    with tempfile.TemporaryDirectory(prefix="cpnetflux-large-data-") as tmp:
        root = Path(tmp)
        source, peer = root / "source", root / "peer"
        source.mkdir(); peer.mkdir()
        block = bytes(i % 251 for i in range(65536))
        # Striped real file plus short tail, nested file, and empty file.
        (source / "large.bin").write_bytes(block * 256 + block[:17])
        (source / "nested").mkdir()
        (source / "nested" / "tail.bin").write_bytes(block * 4 + block[:31])
        (source / "empty").touch()
        expected = tree_hash(source)
        port = free_port()
        log = root / "server.log"
        with log.open("w") as output:
            server = subprocess.Popen([str(build / "cpnetflux-gridftp-server"),
                "--host", "127.0.0.1", "--port", str(port), "--root", str(peer),
                "--data-port-base", str(clamp_passive_data_port_base(free_port())),
                "--connections", "4", "--link-set-max-links", "4", "--checksum", "crc32c",
                "--chunk-size", "1048576"], stdout=output, stderr=subprocess.STDOUT,
                env=dict(os.environ, CPNETFLUX_RANGE_TIMING="on",
                         CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES=receive_buffer))
        try:
            wait_for_control(port)
            with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
                stream = sock.makefile("rwb", buffering=0)
                def reply(text=None):
                    if text is not None: stream.write((text + "\r\n").encode())
                    return stream.readline().decode()
                assert reply().startswith("220")
                login = reply("USER cpnetflux")
                assert login.startswith(("230", "331")), login
                if login.startswith("331"): assert reply("PASS cpnetflux").startswith("230")
                base = "XCPNETFLUX V3 WINDOW=1 CHANNELS=4 CHECKSUM=crc32c DATA_TLS=off DATA_TRANSPORT=tcp"
                good = reply(base + " DATA_BYTES=262144")
                assert good.startswith("200") and "data_bytes=262144 max_data_bytes=262144" in good, good
                for suffix in (" DATA_BYTES=262148", " DATA_BYTES=262144 DATA_BYTES=262144",
                               " DATA_BYTES=0", " DATA_BYTES=262144 DATA_TLS=required"):
                    assert reply(base + suffix).startswith("504")
            rows = []
            for size in (65532, 262144):
                for execution in ("queued", "inline"):
                    remote = f"received-{size}-{execution}"
                    for direction in ("upload", "download"):
                        target = root / f"download-{size}-{execution}"
                        if direction == "download": target.mkdir()
                        summary = root / f"{direction}-{size}-{execution}.json"
                        cmd = command(build, direction, port,
                                      source if direction == "upload" else remote,
                                      remote if direction == "upload" else target, summary, size)
                        env = dict(os.environ, CPNETFLUX_RANGE_TIMING="on",
                                   CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES=receive_buffer,
                                   CPNETFLUX_PAYLOAD_PIPELINE_DEPTH="2",
                                   CPNETFLUX_SENDER_PAYLOAD_EXECUTION=execution,
                                   CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION=execution)
                        done = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=45)
                        assert done.returncode == 0, done.stdout[-4000:] + done.stderr[-4000:] + log.read_text()[-4000:]
                        assert tree_hash(peer / remote if direction == "upload" else target) == expected
                        data = json.loads(summary.read_text())
                        assert data["result"] == "pass" and data["actual_transport"] == "v3"
                        assert data["data_frame_logical_bytes"] == size
                        if direction == "download" and receive_buffer != "off":
                            assert all(1048576 <= lane['tcp_receive_buffer_bytes'] <= 2097152
                                       for lane in data['lane_endpoints']), data['lane_endpoints']
                        frames = (sum(f["stage_execution"]["read_calls"] for f in data["file_timings"])
                                  if direction == "upload" else
                                  sum(f["stage_execution"]["write_calls"] for f in data["file_timings"]))
                        expected_frames = (16 * (17 if size == 65532 else 4) + 1 +
                                           (5 if size == 65532 else 2))
                        assert frames == expected_frames, (size, direction, frames, expected_frames)
                        rows.append(dict(size=size, direction=direction, execution=execution, frames=frames))
            # Unsupported combinations must fail locally before any connection/payload.
            base_cmd = command(build, "upload", 1, source, "unused", root / "reject.json", 262144)
            for extra in (["--checksum", "none"], ["--chunk-size", "2097152"],
                          ["--compression", "auto"], ["--data-transport", "rdma"],
                          ["--tls-mode", "required"]):
                done = subprocess.run(base_cmd + extra, capture_output=True, text=True, timeout=5)
                assert done.returncode != 0 and "large DATA requires" in done.stderr + done.stdout, done
            # A peer returning old V3 success without confirming DATA capacity cannot enable large DATA.
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0)); listener.listen(1)
                old_port = listener.getsockname()[1]
                transcript = []
                errors = []
                def old_peer():
                    try:
                        listener.settimeout(10)
                        conn, _ = listener.accept()
                        with conn:
                            conn.settimeout(5)
                            f = conn.makefile("rwb", buffering=0)
                            f.write(b"220 test\r\n")
                            while True:
                                text = f.readline().decode().strip()
                                if not text: break
                                transcript.append(text.split()[0])
                                if text.startswith("USER"): answer = "331 password"
                                elif text.startswith("PASS"): answer = "230 logged in"
                                elif text.startswith("XCPNETFLUX"):
                                    answer = ("200 CPNetFlux V3 file-transactions/1 window=1 channels=4 "
                                              "checksum=crc32c resume=0 data_tls=off data_transport=tcp links=4 link_caps=0")
                                else: answer = "200 okay"
                                f.write((answer + "\r\n").encode())
                    except Exception as error: errors.append(str(error))
                worker = threading.Thread(target=old_peer, daemon=True); worker.start()
                # Use one lane so only one fake control connection is required.
                cmd = command(build, "upload", old_port, source, "unsupported", root / "old.json", 262144)
                cmd += ["--connections", "1", "--file-parallelism", "1", "--link-set-links", "1"]
                done = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
                worker.join(timeout=6)
                assert done.returncode != 0 and "XCPNETFLUX" in transcript and "EPSV" not in transcript, transcript
                assert not errors, errors
            print(json.dumps(dict(result="pass", transfers=rows, unsupported_peer_rejected_before_payload=True)))
        finally:
            stop_server(server, log)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--receive-buffer", choices=("off", "1048576"), default="off")
    args = parser.parse_args()
    run(args.build_dir.resolve(), args.receive_buffer)
