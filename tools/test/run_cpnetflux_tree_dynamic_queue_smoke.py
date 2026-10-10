#!/usr/bin/env python3
"""Correctness/observation gate; does not claim loopback or WAN performance gains."""
import argparse
import json
import math
import re
import socket
import subprocess
from pathlib import Path
from tree_smoke_common import free_port, tree_hash, wait_for_control, stop_server
from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_tree_data_reuse_smoke import unique_json_object


def check_timings(summary, count, sender, scheduling):
    assert summary['actual_mode'] == 'persistent_tree', summary
    assert summary['actual_file_scheduling'] == scheduling, summary
    assert summary['fallback_reason'] == ''
    for key in ('control_prepare_seconds', 'data_connect_seconds', 'queue_wait_seconds'):
        assert math.isfinite(summary[key]) and summary[key] >= 0
    assert summary['queue_high_watermark'] <= summary['queue_capacity']
    assert summary['data_pending_high_watermark'] <= summary['data_pending_window']
    assert summary['phase_timing_schema'] == 'persistent_file_timing_v1'
    timings = summary['file_timings']
    assert len(timings) == count, (len(timings), count)
    assert len({t['file_id'] for t in timings}) == count
    assert len({t['relative_path'] for t in timings}) == count
    for t in timings:
        for key in ('payload_io_seconds', 'file_result_seconds', 'manifest_seconds', 'wall_seconds'):
            assert math.isfinite(t[key]) and t[key] >= 0, t
        assert math.isfinite(t['checksum_seconds']) and t['checksum_seconds'] >= 0
        assert t['measurement_scope'] == ('local_sender' if sender else 'local_receiver')
        if t['size']:
            assert 0 <= t['first_payload_seconds'] <= t['wall_seconds'], t
        else:
            assert t['first_payload_seconds'] is None
        if sender:
            assert t['finalize_seconds'] is None and t['write_seconds'] is None
            assert t['read_seconds'] >= 0
            if scheduling == 'dynamic':
                assert t['queue_wait_seconds'] >= 0
        else:
            assert t['queue_wait_seconds'] is None and t['read_seconds'] is None
            assert t['finalize_seconds'] >= 0 and t['write_seconds'] >= 0


def manifest_check(root, count, download):
    suffix = '.cpnetflux.download.manifest' if download else '.cpnetflux.manifest'
    files = list(root.rglob('*' + suffix))
    assert len(files) == count, (root, len(files), count)
    assert all('state=committed\n' in f.read_text() for f in files)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--case', choices=['dense', 'mixed', 'large', 'compatibility'], required=True)
    parser.add_argument('--evidence-dir', type=Path, required=True)
    parser.add_argument('--legacy-build-dir', type=Path)
    args = parser.parse_args()
    if args.case == 'compatibility' and not args.legacy_build_dir:
        parser.error('compatibility requires --legacy-build-dir for a fixed pre-dynamic server')
    root = args.evidence_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    source, server_root = root / 'source', root / 'server'
    source.mkdir()
    server_root.mkdir()
    if args.case == 'dense':
        sizes = [1048576] * 128
    elif args.case == 'mixed':
        sizes = [0, 1, 19, 65535, 65536, 65537, 1048576, 5 * 1048576, 17 * 1048576] + [31] * 48
    elif args.case == 'compatibility':
        sizes = [0, 1, 65537]
    else:
        sizes = [64 * 1048576]  # Whole-file regression only; range mode is deferred.
    for index, size in enumerate(sizes):
        path = source / f'f-{index:03d}.bin'
        with path.open('wb') as out:
            block = bytes([index % 256]) * min(1048576, max(size, 1))
            remaining = size
            while remaining:
                piece = block[:min(remaining, len(block))]
                out.write(piece)
                remaining -= len(piece)
    expected = tree_hash(source)
    (root / 'input.json').write_text(json.dumps({'sha256_tree': expected, 'sizes': sizes}))
    port = free_port()
    log = root / 'server.log'
    checksum = 'none' if args.case == 'compatibility' else 'crc32c'
    with log.open('w') as output:
        server = subprocess.Popen([
            str((args.legacy_build_dir if args.case == 'compatibility' else args.build_dir) / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
            '--port', str(port), '--data-port-base', str(clamp_passive_data_port_base(free_port())),
            '--root', str(server_root), '--connections', '1', '--checksum', checksum,
        ], stdout=output, stderr=subprocess.STDOUT)
    try:
        wait_for_control(port)
        def run(direction, src, dst, name, mode='dynamic', extra=(), success=True):
            summary = root / (name + '.json')
            reuse = 'off' if args.case == 'compatibility' else 'tree'
            command = [str(args.build_dir / f'cpnetflux-tree-{direction}-client'),
                '--host', '127.0.0.1', '--port', str(port), '--source-dir', str(src), '--dest-dir', str(dst),
                '--connections', '1', '--file-parallelism', '4', '--checksum', checksum,
                '--data-session-reuse', reuse, '--file-scheduling', mode, '--phase-timing', 'on',
                '--json-summary', str(summary), *extra]
            p = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (root / (name + '.log')).write_text(p.stdout + p.stderr)
            assert (p.returncode == 0) == success, (p.returncode, p.stdout, p.stderr)
            return json.loads(summary.read_text(), object_pairs_hook=unique_json_object)
        modes = [] if args.case == 'compatibility' else (['dynamic', 'static'] if args.case == 'mixed' else ['dynamic'])
        for mode in modes:
            up = run('upload', source, 'up-' + mode, 'upload-' + mode, mode)
            check_timings(up, len(sizes), True, mode)
            assert up['completed_files'] == len(sizes) and up['data_connect_count'] == 4
            assert tree_hash(server_root / ('up-' + mode)) == expected
            manifest_check(server_root / ('up-' + mode), len(sizes), False)
            target = root / ('download-' + mode)
            down = run('download', 'up-' + mode, target, 'download-' + mode, mode)
            check_timings(down, len(sizes), False, mode)
            assert down['completed_files'] == len(sizes) and down['data_connect_count'] == 4
            assert tree_hash(target) == expected
            manifest_check(target, len(sizes), True)
        if args.case == 'compatibility':
            for direction, src, dst in (
                ('upload', source, 'up-compatibility'),
                ('download', 'up-compatibility', root / 'down-compatibility'),
            ):
                fallback = run(direction, src, dst, direction + '-compatibility')
                assert fallback['requested_mode'] == 'v1'
                assert fallback['actual_mode'] == fallback['actual_file_scheduling'] == 'v1'
                assert fallback['fallback_reason'] == ''
                assert fallback['completed_files'] == len(sizes)
            assert tree_hash(server_root / 'up-compatibility') == expected
            assert tree_hash(root / 'down-compatibility') == expected
            manifest_check(server_root / 'up-compatibility', len(sizes), False)
            manifest_check(root / 'down-compatibility', len(sizes), True)
        if args.case == 'mixed':
            # V2 resume reuses the persistent tree session and validates the final tree.
            resumed = run('download', 'up-dynamic', root / 'download-dynamic', 'resume',
                          extra=['--resume'])
            assert resumed['actual_mode'] == 'persistent_tree'
            assert resumed['data_session_reuse_mode'] == 'tree'
            assert resumed['fallback_reason'] == ''
            assert tree_hash(root / 'download-dynamic') == expected
            collision = server_root / 'collision'
            collision.mkdir()
            (collision / 'f-008.bin').write_bytes(b'keep-existing')
            failed = run('upload', source, 'collision', 'collision', success=False)
            assert failed['result'] == 'fail'
            assert (collision / 'f-008.bin').read_bytes() == b'keep-existing'
            # Missing batch participants: closing an attached control must reclaim all its sockets.
            def sockets():
                return sum(p.readlink().as_posix().startswith('socket:[')
                           for p in Path(f'/proc/{server.pid}/fd').iterdir())
            import time
            before = sockets()
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                control = sock.makefile('rwb', buffering=0)
                assert control.readline().startswith(b'220')
                negotiation = ('XCPNETFLUX V2 WINDOW=2 CHANNELS=4 SCHED=dynamic '
                               'CHECKSUM=crc32c RESUME=0 DATA_TLS=off')
                for cmd in ('USER cpnetflux', 'PASS cpnetflux', 'TYPE I', 'OPTS PARALLELISM=1',
                            negotiation, 'EPSV'):
                    control.write(cmd.encode() + b'\r\n')
                    reply = control.readline()
                    assert reply[:1] in (b'2', b'3')
                passive = int(re.search(rb'\(\|\|\|(\d+)\|\)', reply)[1])
                control.write(b'XDIRD PUT ' + b'a' * 32 + b' 0 4 0 cancelled\r\n')
                assert control.readline().startswith(b'150')
                with socket.create_connection(('127.0.0.1', passive), timeout=3):
                    control.close()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and sockets() > before:
                time.sleep(.02)
            assert sockets() <= before, 'dynamic batch leaked sockets after cancellation'
        print(json.dumps({'case': args.case, 'hash': expected, 'result': 'passed',
                          'range_mode': 'deferred'}, sort_keys=True))
    finally:
        stop_server(server, log)


if __name__ == '__main__':
    main()
