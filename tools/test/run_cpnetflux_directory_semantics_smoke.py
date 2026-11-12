#!/usr/bin/env python3
"""V3 whole-tree oracle: file bytes AND ordinary directory paths."""
import argparse
import json
import tempfile
import socket
import select
import threading
from pathlib import Path
from tree_smoke_common import free_port, start_server, stop_server, run_checked, tree_hash


def directory_paths(root):
    return sorted(p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_dir())


def check_tree(source, target):
    assert target.is_dir(), f"missing tree root: {target}"
    expected = directory_paths(source)
    actual = directory_paths(target)
    assert expected == actual, f"directory mismatch: missing={sorted(set(expected)-set(actual))}, extra={sorted(set(actual)-set(expected))}"
    assert tree_hash(source) == tree_hash(target), "file contents/count/bytes mismatch"


def write_legacy_file_manifest(path):
    lines = [line for line in path.read_text().splitlines() if not line.startswith(("directories=", "manifest_body_crc32c="))]
    lines = ["manifest_version=1" if line.startswith("manifest_version=") else line for line in lines]
    body = ("\n".join(lines)+"\n").encode()
    crc = 0xffffffff
    for byte in body:
        crc ^= byte
        for _ in range(8): crc = (crc >> 1) ^ (0x82f63b78 if crc & 1 else 0)
    path.write_bytes(body + f"manifest_body_crc32c={crc ^ 0xffffffff:08x}\n".encode())


def reject_legacy_capability(build, server_port, source, destination):
    listener = socket.socket(); listener.bind(("127.0.0.1", 0)); listener.listen(1)
    port = listener.getsockname()[1]
    commands = []; failures = []
    def relay():
        try:
            listener.settimeout(15)
            with listener.accept()[0] as client, socket.create_connection(("127.0.0.1", server_port), timeout=15) as peer:
                buffers = {client: b"", peer: b""}
                while True:
                    readable, _, _ = select.select([client, peer], [], [], 15)
                    if not readable: raise TimeoutError("capability relay stalled")
                    for incoming in readable:
                        data = incoming.recv(65536)
                        if not data: return
                        buffers[incoming] += data
                        while b"\n" in buffers[incoming]:
                            line, buffers[incoming] = buffers[incoming].split(b"\n", 1)
                            if incoming is client:
                                commands.append(line)
                                peer.sendall(line + b"\n")
                            else:
                                client.sendall(line.replace(b" directories=1", b"") + b"\n")
        except Exception as exc:
            failures.append(repr(exc))
        finally:
            listener.close()
    thread = threading.Thread(target=relay, daemon=True); thread.start()
    try:
        run_checked([str(build/"cpnetflux-tree-upload-client"), "--host", "127.0.0.1", "--port", str(port),
            "--source-dir", str(source), "--dest-dir", destination, "--file-parallelism", "1"], expect_success=False)
    finally:
        thread.join(16)
    assert not thread.is_alive() and not failures, failures
    assert any(line.startswith(b"XCPNETFLUX V3") for line in commands)
    assert not any(line.startswith((b"XDIR ", b"XDIRP ", b"XDIRD ")) for line in commands), "file-only peer reached runtime"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-directory-semantics-") as td:
        temp = Path(td)
        root = temp / "server"; root.mkdir()
        port = free_port(); log = temp / "server.log"
        server = start_server(args.build_dir.resolve(), root, port, free_port(), log)
        try:
            for fixture in ("mixed", "directories-only", "empty-root"):
                source = temp / ("source-" + fixture); source.mkdir()
                if fixture != "empty-root":
                    (source / "empty").mkdir()
                    (source / "中文目录" / "空叶").mkdir(parents=True)
                    (source / "space dir" / "nested" / "empty leaf").mkdir(parents=True)
                if fixture == "mixed":
                    (source / "empty.bin").write_bytes(b"")
                    (source / "space dir" / "content.bin").write_bytes(bytes(range(251))*43)
                for lanes in (1, 2):
                    name = fixture + "-" + str(lanes)
                    summary = temp / (name + "-upload.json")
                    run_checked([str(args.build_dir.resolve()/"cpnetflux-tree-upload-client"),
                        "--host", "127.0.0.1", "--port", str(port), "--source-dir", str(source),
                        "--dest-dir", name, "--file-parallelism", str(lanes),
                        "--json-summary", str(summary)])
                    assert json.loads(summary.read_text())["result"] == "pass"
                    check_tree(source, root / name)
                    target = temp / (name + "-download")
                    run_checked([str(args.build_dir.resolve()/"cpnetflux-tree-download-client"),
                        "--host", "127.0.0.1", "--port", str(port), "--source-dir", name,
                        "--dest-dir", str(target), "--file-parallelism", str(lanes)])
                    check_tree(source, target)
                    up_summary = json.loads(summary.read_text())
                    assert up_summary["directory_count"] == up_summary["completed_directories"] == len(directory_paths(source))
                    if fixture == "mixed" and lanes == 2:
                        # Remove only empty task-owned paths; old Completed facts must not skip recreation.
                        for base in (root / name, target):
                            (base / "empty").rmdir()
                            (base / "space dir" / "nested" / "empty leaf").rmdir()
                        stop_server(server, log)
                        server = start_server(args.build_dir.resolve(), root, port, free_port(), log)
                        for direction, src, dst in (("upload", str(source), name), ("download", name, str(target))):
                            run_checked([str(args.build_dir.resolve()/("cpnetflux-tree-"+direction+"-client")),
                                "--host", "127.0.0.1", "--port", str(port), "--source-dir", src,
                                "--dest-dir", dst, "--file-parallelism", "2", "--resume"])
                        check_tree(source, root / name); check_tree(source, target)
                        print("PASS restart-and-directory-revalidation", flush=True)
                        write_legacy_file_manifest(Path(str(source)+".cpnetflux.tree.upload.manifest"))
                        run_checked([str(args.build_dir.resolve()/"cpnetflux-tree-upload-client"),
                            "--host", "127.0.0.1", "--port", str(port), "--source-dir", str(source),
                            "--dest-dir", name, "--file-parallelism", "2", "--resume"])
                        check_tree(source, root/name)
                        assert "manifest_version=2" in Path(str(source)+".cpnetflux.tree.upload.manifest").read_text()
                        write_legacy_file_manifest(Path(str(target)+".cpnetflux.tree.download.manifest"))
                        run_checked([str(args.build_dir.resolve()/"cpnetflux-tree-download-client"),
                            "--host", "127.0.0.1", "--port", str(port), "--source-dir", name,
                            "--dest-dir", str(target), "--file-parallelism", "2", "--resume"], expect_success=False)
                        check_tree(source,target)
                        print("PASS legacy-upload-rescan-and-legacy-download-rejection", flush=True)
                    print("PASS", name, flush=True)
            outside = temp / "outside"; outside.mkdir()
            (temp / "unsafe-parent").symlink_to(outside, target_is_directory=True)
            run_checked([str(args.build_dir.resolve()/"cpnetflux-tree-download-client"),
                "--host", "127.0.0.1", "--port", str(port), "--source-dir", "mixed-2",
                "--dest-dir", str(temp/"unsafe-parent"/"must-not-create")], expect_success=False)
            assert not list(outside.iterdir()), "destination ancestor symlink caused an out-of-root write"
            conflict = root / "conflict"; conflict.mkdir()
            (conflict / "empty").write_bytes(b"preserve type-conflict bytes")
            run_checked([str(args.build_dir.resolve()/"cpnetflux-tree-upload-client"),
                "--host", "127.0.0.1", "--port", str(port), "--source-dir", str(temp/"source-mixed"),
                "--dest-dir", "conflict"], expect_success=False)
            assert (conflict/"empty").read_bytes() == b"preserve type-conflict bytes"
            print("PASS unsafe-ancestor-and-file-conflict", flush=True)
            reject_legacy_capability(args.build_dir.resolve(), port, temp/"source-mixed", "unsupported-peer")
            assert not (root/"unsupported-peer").exists()
            print("PASS legacy-directory-capability-rejected", flush=True)
        finally:
            stop_server(server, log)
    print("directory semantics smoke passed")


if __name__ == "__main__":
    main()
