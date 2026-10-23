#!/usr/bin/env python3
"""Observe real tree-manifest renames and verify V3 dense upload/download outputs."""
import argparse
import ctypes
import json
import math
import os
import select
import shutil
import struct
import subprocess
import tempfile
import time
from pathlib import Path

from cpnetflux_fault_proxy import FaultProxy
from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control


class RenameObserver:
    def __init__(self, parent, name):
        libc = ctypes.CDLL(None, use_errno=True)
        self.fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        if self.fd < 0:
            raise OSError(ctypes.get_errno(), "inotify_init1")
        # CREATE events separate identical renames so inotify cannot merge them.
        self.name = name
        self.count = 0
        if libc.inotify_add_watch(self.fd, os.fsencode(parent), 0x180) < 0:  # IN_CREATE | IN_MOVED_TO
            os.close(self.fd)
            raise OSError(ctypes.get_errno(), "inotify_add_watch")

    def drain(self):
        while select.select([self.fd], [], [], 0)[0]:
            data = os.read(self.fd, 65536)
            offset = 0
            while offset < len(data):
                _, mask, _, size = struct.unpack_from("iIII", data, offset)
                name = os.fsdecode(data[offset + 16:offset + 16 + size].split(b"\0", 1)[0])
                if mask & 0x4000:
                    raise AssertionError("inotify queue overflow")
                if mask & 0x80 and name == self.name:
                    self.count += 1
                offset += 16 + size

    def close(self):
        os.close(self.fd)


def run(build, root, source, server_root, port, direction, label, coalesced, expected):
    destination = root / ("download-" + label)
    summary_path = root / (label + "-" + direction + ".json")
    manifest = (Path(str(source) + ".cpnetflux.tree.upload.manifest") if direction == "upload"
                else Path(str(destination) + ".cpnetflux.tree.download.manifest"))
    observer = RenameObserver(manifest.parent, manifest.name)
    command = [
        str(build / ("cpnetflux-tree-" + direction + "-client")),
        "--host", "127.0.0.1", "--port", str(port),
        "--source-dir", str(source) if direction == "upload" else "upload-" + label,
        "--dest-dir", "upload-" + label if direction == "upload" else str(destination),
        "--checksum", "crc32c", "--checksum-backend", "auto",
        "--file-parallelism", "4", "--connections", "4", "--link-set-links", "4",
        "--chunk-size", "4194304", "--buffer-size", "65536",
        "--control-reuse", "worker", "--data-session-reuse", "tree",
        "--control-pipeline-depth", "2", "--json-summary", str(summary_path),
    ]
    started = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        while process.poll() is None:
            observer.drain()
            if time.monotonic() - started > 90:
                process.kill()
                raise AssertionError("dense transfer timed out")
            time.sleep(0.002)
        stdout, stderr = process.communicate(timeout=5)
        wall = time.monotonic() - started
        observer.drain()
        assert process.returncode == 0, stdout + stderr
        actual = tree_hash(server_root / ("upload-" + label) if direction == "upload" else destination)
        assert actual == expected, (actual, expected)
        summary = json.loads(summary_path.read_text())
        assert summary["completed_files"] == expected[1], summary
        text = manifest.read_text()
        assert text.count(":completed:") == expected[1], text[:500]
        if coalesced:
            limit = 6 + math.ceil(2 * expected[1] / 32) + math.ceil(wall / 0.1)
            assert observer.count <= limit, (observer.count, limit, wall)
            assert summary["tree_manifest_generation"] == summary["tree_manifest_persisted_generation"], summary
            assert summary["tree_manifest_snapshots"] >= 1, summary
            assert summary["tree_manifest_serialized_bytes"] >= manifest.stat().st_size, summary
        return dict(label=label, direction=direction, wall_seconds=wall,
                    manifest_renames=observer.count, summary=summary, sha256_tree=actual)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        observer.close()


def check_completed_upload(build, root, source, server_root, port, label, policy="off"):
    summary = root / "completed-resume.json"
    command = [
        str(build / "cpnetflux-tree-upload-client"), "--host", "127.0.0.1",
        "--port", str(port), "--source-dir", str(source), "--dest-dir", "upload-" + label,
        "--checksum", "crc32c", "--file-parallelism", "4", "--link-set-links", "4",
        "--chunk-size", "4194304", "--resume", "--json-summary", str(summary),
    ]
    if policy != "off":
        command += ["--scheduler", "global", "--scheduler-policy", policy,
                    "--scheduler-metrics-dir", str(root / "resume-metrics")]
    # Valid committed outputs must be revalidated without retransmitting payload.
    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    resumed = json.loads(summary.read_text())
    target = server_root / ("upload-" + label) / "f0000.bin"
    stamp = target.stat()
    with target.open("r+b") as output:
        output.write(b"X")
    os.utime(target, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    assert result.returncode != 0, "stale Completed record hid corrupted receiver output"
    rejected = json.loads(summary.read_text())
    assert rejected["result"] == "fail", rejected
    assert "checksum" in (result.stdout + result.stderr + json.dumps(rejected)).lower(), rejected
    assert resumed["resume_skipped_bytes"] == sum(p.stat().st_size for p in source.iterdir()), resumed
    assert resumed["bytes_transferred"] == 0, resumed
    shutil.copyfile(source / "f0000.bin", target)
    os.utime(target, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
    return dict(valid_resume=resumed, corrupt_resume=rejected)


def check_crash_resume(build, root, source, server_root, port, data_base, direction, cut):
    name = f"crash-{direction}-{cut}"
    case = root / name
    case.mkdir()
    incoming = case / "received"
    remote = name
    if direction == "upload":
        inputs = case / "source"
        shutil.copytree(source, inputs)
        incoming = server_root / remote
        source_arg, dest_arg = str(inputs), remote
        manifest = Path(str(inputs) + ".cpnetflux.tree.upload.manifest")
    else:
        shutil.copytree(source, server_root / remote)
        source_arg, dest_arg = remote, str(incoming)
        manifest = Path(str(incoming) + ".cpnetflux.tree.download.manifest")
    summary = case / "summary.json"
    command = [
        str(build / ("cpnetflux-tree-" + direction + "-client")),
        "--host", "127.0.0.2", "--port", str(port),
        "--source-dir", source_arg, "--dest-dir", dest_arg,
        "--file-parallelism", "1", "--checksum", "crc32c", "--chunk-size", "4194304",
        "--buffer-size", "65536", "--json-summary", str(summary),
    ]
    proxy = FaultProxy("127.0.0.2", "127.0.0.1", port, data_base,
                       drops=0, delay=0.002, log=case / "proxy.jsonl")
    with (case / "interrupted.log").open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 30
            count = 0
            while process.poll() is None:
                count = len(list(incoming.glob("*.bin"))) if incoming.exists() else 0
                if count >= cut:
                    break
                assert time.monotonic() < deadline, "no committed files before crash deadline"
                time.sleep(0.001)
            assert process.poll() is None, "transfer finished before requested crash window"
            process.kill()  # SIGKILL: neither destructor nor final barrier can run.
            assert process.wait(timeout=5) == -9
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            proxy.close()
    assert manifest.exists(), "initial resumable tree manifest missing"
    stale_completed = manifest.read_text().count(":completed:")
    assert count < len(list(source.glob("*.bin"))), "crash did not interrupt partial transfer"
    command[command.index("127.0.0.2")] = "127.0.0.1"
    result = subprocess.run(command + ["--resume"], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    resumed = json.loads(summary.read_text())
    assert tree_hash(incoming) == tree_hash(source), (direction, cut)
    assert resumed["tree_manifest_generation"] == resumed["tree_manifest_persisted_generation"], resumed
    return dict(direction=direction, kill_after_files=cut, observed_committed_files=count,
                persisted_completed_files=stale_completed, resume_summary=resumed)


def check_write_failure(build, root, source, server_root, port, data_base):
    case = root / "save-failure"
    case.mkdir()
    inputs = case / "source"
    shutil.copytree(source, inputs)
    manifest = Path(str(inputs) + ".cpnetflux.tree.upload.manifest")
    summary = case / "summary.json"
    gate = case / "pause"
    command = [
        str(build / "cpnetflux-tree-upload-client"), "--host", "127.0.0.2",
        "--port", str(port), "--source-dir", str(inputs), "--dest-dir", "save-failure",
        "--checksum", "crc32c", "--file-parallelism", "1",
        "--json-summary", str(summary),
    ]
    proxy = FaultProxy("127.0.0.2", "127.0.0.1", port, data_base, drops=0,
                       delay=0.001, pause_file=gate, pause_after_bytes=1, log=case / "proxy.jsonl")
    with (case / "client.log").open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while not gate.exists():
                assert process.poll() is None, "transfer exited before save failure injection"
                assert time.monotonic() < deadline, "save failure injection did not reach data gate"
                time.sleep(0.002)
            assert manifest.is_file()
            manifest.unlink()
            manifest.mkdir()
            (manifest / "blocker").write_text("force atomic rename to fail")
            gate.unlink()
            assert process.wait(timeout=30) != 0, "failed tree snapshot reported success"
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            proxy.close()
    result = json.loads(summary.read_text())
    assert result["result"] == "fail", result
    assert result["tree_manifest_generation"] > result["tree_manifest_persisted_generation"], result
    assert "tree manifest" in result["error"]["message"], result
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--baseline-build-dir", type=Path)
    parser.add_argument("--files", type=int, default=256)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--resume-check-only", action="store_true")
    args = parser.parse_args()
    if args.files < 64 or args.rounds < 1:
        parser.error("--files must be >=64 and --rounds >=1")
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-coalesce-") as temporary:
        root = args.evidence_dir or Path(temporary)
        root.mkdir(parents=True, exist_ok=True)
        source, server_root = root / "source", root / "server-root"
        source.mkdir()
        server_root.mkdir()
        block = bytes(i % 251 for i in range(65536))
        for index in range(args.files):
            (source / f"f{index:04d}.bin").write_bytes(block)
        expected = tree_hash(source)
        port, data_base = free_port(), clamp_passive_data_port_base(free_port())
        log = root / "server.log"
        with log.open("w") as output:
            server = subprocess.Popen([
                str(args.build_dir.absolute() / "cpnetflux-gridftp-server"),
                "--host", "127.0.0.1", "--port", str(port),
                "--root", str(server_root), "--data-port-base", str(data_base),
                "--connections", "4", "--link-set-max-links", "4", "--checksum", "crc32c",
            ], stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for_control(port)
            rows = []
            for round_index in range(args.rounds):
                variants = [("new", args.build_dir, not args.resume_check_only)]
                if args.baseline_build_dir:
                    old = ("old", args.baseline_build_dir, False)
                    variants = [old, *variants] if round_index % 2 == 0 else [*variants, old]
                for name, build, coalesced in variants:
                    label = f"{name}-{round_index}"
                    for direction in ("upload", "download"):
                        rows.append(run(build.absolute(), root, source, server_root, port,
                                        direction, label, coalesced, expected))
            resume = None
            if not args.baseline_build_dir:
                resume = check_completed_upload(args.build_dir.absolute(), root, source, server_root,
                                                port, f"new-{args.rounds - 1}")
            global_resume = None
            if not args.baseline_build_dir and not args.resume_check_only:
                global_resume = check_completed_upload(args.build_dir.absolute(), root, source,
                    server_root, port, f"new-{args.rounds - 1}", "adaptive")
            crashes = []
            if not args.baseline_build_dir and not args.resume_check_only:
                for direction in ("upload", "download"):
                    for cut in (2, 40):
                        crashes.append(check_crash_resume(args.build_dir.absolute(), root, source,
                                                         server_root, port, data_base, direction, cut))
            failure = None
            if not args.baseline_build_dir and not args.resume_check_only:
                failure = check_write_failure(args.build_dir.absolute(), root, source,
                                              server_root, port, data_base)
            report = dict(result="pass", files=args.files, runs=rows, completed_upload_resume=resume,
                          crash_resume=crashes, save_failure=failure,
                          global_completed_upload_resume=global_resume)
            (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, sort_keys=True))
        finally:
            stop_server(server, log)


if __name__ == "__main__":
    main()
