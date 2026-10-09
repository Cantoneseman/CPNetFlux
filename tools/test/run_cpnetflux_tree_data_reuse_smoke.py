#!/usr/bin/env python3
import argparse
import json
import os
import re
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

from tree_smoke_common import free_port, make_tree, tree_hash, wait_for_control, stop_server
from gridftp_port_window import clamp_passive_data_port_base


def unique_json_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"duplicate summary field: {key}"
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cpnetflux-data-reuse-') as directory:
        root = Path(directory)
        source, server_root = root / 'source', root / 'server'
        make_tree(source)
        server_root.mkdir()
        for i in range(24):
            (source / f'small-{i:03}.bin').write_bytes(bytes([i]) * (i + 1))
        expected = tree_hash(source)
        port = free_port()
        data_port = clamp_passive_data_port_base(free_port())
        log = root / 'server.log'
        with log.open('w') as output:
            server = subprocess.Popen([
                str(args.build_dir / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
                '--port', str(port), '--data-port-base', str(data_port),
                '--root', str(server_root), '--connections', '1', '--checksum', 'none',
            ], stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for_control(port)
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                control = sock.makefile('rwb', buffering=0)
                assert control.readline().startswith(b'220')
                control.write(b'XCPNETFLUX V2\r\n')
                assert control.readline().startswith(b'530'), 'negotiation requires authentication'
                control.close()

            def socket_fd_count(pid):
                descriptor_dir = Path(f'/proc/{pid}/fd')
                return sum(
                    os.readlink(descriptor).startswith('socket:[')
                    for descriptor in descriptor_dir.iterdir()
                )

            baseline_sockets = socket_fd_count(server.pid)
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                control = sock.makefile('rwb', buffering=0)
                assert control.readline().startswith(b'220')
                for command, prefix in (('USER cpnetflux', b'331'), ('PASS cpnetflux', b'230'),
                                        ('TYPE I', b'200'), ('OPTS PARALLELISM=1', b'200'),
                                        ('XCPNETFLUX V2 WINDOW=2 CHANNELS=4', b'200')):
                    control.write(command.encode() + b'\r\n')
                    assert control.readline().startswith(prefix), command
                control.write(b'EPSV\r\n')
                passive_reply = control.readline()
                assert passive_reply.startswith(b'229')
                passive_port = int(re.search(rb'\(\|\|\|(\d+)\|\)', passive_reply)[1])
                assert 1 <= passive_port <= 65535
                control.write(b'XDIRP PUT 0 4 0 cancelled\r\n')
                assert control.readline().startswith(b'150')
                control.close()
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and socket_fd_count(server.pid) > baseline_sockets:
                time.sleep(0.02)
            assert socket_fd_count(server.pid) <= baseline_sockets, 'cancelled channel leaked server sockets'

            def run(direction, src, dst, name, extra=(), success=True):
                summary = root / f'{name}.json'
                command = [str(args.build_dir / f'cpnetflux-tree-{direction}-client'),
                           '--host', '127.0.0.1', '--port', str(port), '--source-dir', str(src),
                           '--dest-dir', str(dst), '--checksum', 'none', '--control-reuse', 'worker',
                           '--data-session-reuse', 'tree', '--phase-timing', 'on', '--json-summary', str(summary), *extra]
                result = subprocess.run(command, text=True, capture_output=True, timeout=40)
                assert (result.returncode == 0) == success, result.stdout + result.stderr
                return json.loads(summary.read_text(), object_pairs_hook=unique_json_object)

            upload = run('upload', source, 'uploaded', 'upload')
            assert upload['data_session_reuse_mode'] == 'tree'
            assert upload['data_connect_count'] == 1
            assert upload['completed_files'] == expected[1]
            assert tree_hash(server_root / 'uploaded') == expected
            downloaded = root / 'downloaded'
            download = run('download', 'uploaded', downloaded, 'download')
            assert download['data_session_reuse_mode'] == 'tree'
            assert download['data_connect_count'] == 1
            assert tree_hash(downloaded) == expected
            def check_manifests(directory, suffix):
                manifests = list(directory.rglob(f'*{suffix}'))
                assert len(manifests) == expected[1], (suffix, len(manifests))
                for manifest in manifests:
                    assert 'state=committed\n' in manifest.read_text(), manifest
            check_manifests(server_root / 'uploaded', '.cpnetflux.manifest')
            check_manifests(downloaded, '.cpnetflux.download.manifest')
            dense_source = root / 'dense-128'
            dense_source.mkdir()
            for index in range(128):
                (dense_source / f'file-{index:03}.bin').write_bytes(bytes([index]) * (1024 * 1024))
            dense_expected = tree_hash(dense_source)
            dense_parallel_upload = run('upload', dense_source, 'dense-parallel', 'dense-upload-parallel',
                               ['--file-parallelism', '4'])
            assert dense_parallel_upload['data_session_reuse_mode'] == 'tree'
            assert dense_parallel_upload['data_connect_count'] == 4
            assert dense_parallel_upload['control_connect_count'] == 4
            assert dense_parallel_upload['control_prepare_seconds'] >= 0
            assert dense_parallel_upload['transfer_complete_wait_seconds'] >= 0
            assert dense_parallel_upload['data_pending_high_watermark'] <= dense_parallel_upload['data_pending_window']
            assert dense_parallel_upload['completed_files'] == 128
            assert tree_hash(server_root / 'dense-parallel') == dense_expected
            dense_parallel_download_root = root / 'dense-downloaded-parallel'
            dense_parallel_download = run('download', 'dense-parallel', dense_parallel_download_root,
                                 'dense-download-parallel', ['--file-parallelism', '4'])
            assert dense_parallel_download['data_session_reuse_mode'] == 'tree'
            assert dense_parallel_download['data_connect_count'] == 4
            assert dense_parallel_download['control_connect_count'] == 4
            assert dense_parallel_download['control_prepare_seconds'] >= 0
            assert dense_parallel_download['transfer_complete_wait_seconds'] >= 0
            assert dense_parallel_download['data_pending_high_watermark'] <= dense_parallel_download['data_pending_window']
            assert dense_parallel_download['completed_files'] == 128
            assert tree_hash(dense_parallel_download_root) == dense_expected
            parallel_upload = run('upload', source, 'uploaded-parallel', 'upload-parallel',
                                  ['--file-parallelism', '4'])
            assert parallel_upload['data_session_reuse_mode'] == 'tree'
            assert parallel_upload['data_connect_count'] == 4
            assert parallel_upload['control_connect_count'] == 4
            assert parallel_upload['completed_files'] == expected[1]
            assert tree_hash(server_root / 'uploaded-parallel') == expected
            parallel_download_root = root / 'downloaded-parallel'
            parallel_download = run('download', 'uploaded-parallel', parallel_download_root,
                                    'download-parallel', ['--file-parallelism', '4'])
            assert parallel_download['data_session_reuse_mode'] == 'tree'
            assert parallel_download['data_connect_count'] == 4
            assert parallel_download['control_connect_count'] == 4
            assert parallel_download['completed_files'] == expected[1]
            assert tree_hash(parallel_download_root) == expected

            parallel_collision = server_root / 'collision-parallel'
            parallel_collision.mkdir()
            (parallel_collision / 'alpha.txt').write_bytes(b'keep-existing')
            parallel_failed = run('upload', source, 'collision-parallel', 'collision-parallel',
                                  ['--file-parallelism', '4'], success=False)
            assert parallel_failed['failed_files'] >= 1
            assert parallel_failed['data_connect_count'] == 4
            assert (parallel_collision / 'alpha.txt').read_bytes() == b'keep-existing'
            baseline = run('upload', source, 'baseline', 'baseline', ['--data-session-reuse', 'off'])
            assert baseline['data_session_reuse_mode'] == 'off'
            assert tree_hash(server_root / 'baseline') == expected
            resumed = run('download', 'uploaded', downloaded, 'resume', ['--resume'])
            assert resumed['data_session_reuse_mode'] == 'off'
            assert tree_hash(downloaded) == expected

            collision = server_root / 'collision'
            collision.mkdir()
            (collision / 'alpha.txt').write_bytes(b'keep-existing')
            failed = run('upload', source, 'collision', 'collision', success=False)
            assert failed['failed_files'] == 1
            assert (collision / 'alpha.txt').read_bytes() == b'keep-existing'
            assert (collision / 'small-023.bin').read_bytes() == bytes([23]) * 24

            # Interrupted V2 upload is resumed by the original framed-file engine.
            with socket.create_connection(('127.0.0.1', port), timeout=5) as sock:
                control = sock.makefile('rwb', buffering=0)
                control.readline()
                def cmd(text):
                    control.write(text.encode() + b'\r\n')
                    return control.readline()
                assert cmd('USER cpnetflux').startswith(b'331')
                assert cmd('PASS cpnetflux').startswith(b'230')
                assert cmd('TYPE I').startswith(b'200')
                assert cmd('OPTS PARALLELISM=1').startswith(b'200')
                assert cmd('XCPNETFLUX V2').startswith(b'200')
                reply = cmd('EPSV')
                passive = int(re.search(rb'\(\|\|\|(\d+)\|\)', reply)[1])
                assert cmd('XDIR PUT interrupted').startswith(b'150')
                transfer_id = b'interrupted-v2'
                relative = b'partial.bin'
                total = 131072
                payload = (struct.pack('!HHHHQQ', 1, len(transfer_id), 0, 0, total, 65536)
                           + transfer_id + struct.pack('!HH', len(relative), 0)
                           + relative + struct.pack('!Q', 0))
                def frame(kind, offset, data):
                    header = struct.pack('!IHHHHIQQIIQ', 0x47465831, 1, 64, kind, 0,
                                         1, 1, offset, len(data), 0, total)
                    return header + bytes(16) + data
                with socket.create_connection(('127.0.0.1', passive), timeout=5) as data:
                    data.sendall(frame(8, 0, payload))
                    data.sendall(frame(1, 0, bytes([19]) * 65536))
                assert control.readline().startswith(b'550')
                partial = server_root / 'interrupted' / 'partial.bin'
                checkpoint = Path(str(partial) + '.cpnetflux.manifest')
                assert checkpoint.exists(), 'V2 checkpoint must survive disconnect'
                assert 'state=failed\n' in checkpoint.read_text()
                original = root / 'resume-source.bin'
                original.write_bytes(bytes([19]) * 65536 + bytes([27]) * 65536)
                passive = int(re.search(rb'\(\|\|\|(\d+)\|\)', cmd('EPSV'))[1])
                assert cmd('REST GFID:interrupted-v2').startswith(b'350')
                assert cmd('STOR interrupted/partial.bin').startswith(b'150')
                resumed_file = subprocess.run([
                    str(args.build_dir / 'cpnetflux-file-client'), '--host', '127.0.0.1',
                    '--port', str(passive), '--input', str(original), '--transfer-id', 'interrupted-v2',
                    '--checksum', 'none', '--chunk-size', '65536', '--connections', '1', '--resume',
                ], text=True, capture_output=True, timeout=15)
                assert resumed_file.returncode == 0, resumed_file.stdout + resumed_file.stderr
                assert control.readline().startswith(b'226')
                assert partial.read_bytes() == original.read_bytes()
                assert 'state=committed\n' in checkpoint.read_text()

            # Parent symlinks and traversal must not escape the negotiated directory.
            (server_root / 'unsafe').mkdir()
            outside = root / 'outside'
            outside.mkdir()
            (server_root / 'unsafe' / 'nested').symlink_to(outside, target_is_directory=True)
            unsafe = run('upload', source, 'unsafe', 'unsafe', success=False)
            assert not list(outside.iterdir())
            assert unsafe['failed_files'] >= 2

            dense = root / 'dense'
            dense.mkdir()
            block = bytes(range(256)) * 4096
            for i in range(128):
                (dense / f'f{i:03}.bin').write_bytes(block)
            dense_expected = tree_hash(dense)
            dense_upload = run('upload', dense, 'dense', 'dense-upload')
            dense_download = run('download', 'dense', root / 'dense-downloaded', 'dense-download')
            for item in (dense_upload, dense_download):
                assert item['file_count'] == item['completed_files'] == 128
                assert item['data_connect_count'] == 1
                assert item['bytes_transferred'] == 128 * 1048576
            assert tree_hash(server_root / 'dense') == dense_expected
            assert tree_hash(root / 'dense-downloaded') == dense_expected
            print(json.dumps({'upload': upload, 'download': download, 'resume': resumed,
                              'collision': failed, 'dense_upload': dense_upload,
                              'dense_download': dense_download}, sort_keys=True))
            measured = ('file_count', 'completed_files', 'data_connect_count',
                        'control_connect_count', 'elapsed_seconds', 'throughput_gbps', 'tree_hash')
            print(json.dumps({
                'parallel_128x1MiB_upload': {key: dense_parallel_upload[key] for key in measured},
                'parallel_128x1MiB_download': {key: dense_parallel_download[key] for key in measured},
                'parallel_small_upload': {key: parallel_upload[key] for key in measured},
                'parallel_small_download': {key: parallel_download[key] for key in measured},
                'parallel_collision': {key: parallel_failed.get(key) for key in
                                       ('result', 'failed_files', 'data_connect_count')},
            }, sort_keys=True))
        finally:
            stop_server(server, log)

        # Explicit capability rejection must take the original path before payload.
        port = free_port()
        with log.open('w') as output:
            server = subprocess.Popen([
                str(args.build_dir / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
                '--port', str(port), '--data-port-base', str(data_port),
                '--root', str(server_root), '--checksum', 'none', '--commit-sync-policy', 'fsync_file',
            ], stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for_control(port)
            fallback = run('upload', source, 'unsupported-fallback', 'unsupported-fallback')
            assert fallback['data_session_reuse_mode'] == 'off'
            assert tree_hash(server_root / 'unsupported-fallback') == expected
        finally:
            stop_server(server, log)


if __name__ == '__main__':
    main()
