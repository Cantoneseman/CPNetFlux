#!/usr/bin/env python3
"""Exercise summary path selection and canonical hashes through real V3 transfers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, stop_server, wait_for_control


def reference(files):
    value = 0xffffffff
    for name, data in sorted(files.items()):
        stream = name.encode('utf-8') + b'\0' + str(len(data)).encode() + b'\0' + data + b'\0'
        for byte in stream:
            value ^= byte
            for _ in range(8):
                value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
    return f'crc32c:{value ^ 0xffffffff:08x}'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', required=True, type=Path)
    args = p.parse_args(); build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix='cpnetflux-summary-path-') as temporary:
        root = Path(temporary); source = root/'source'; source.mkdir()
        server_root = root/'server'; server_root.mkdir()
        files = {'a/x': b'nested', 'a-.bin': b'prefix', 'a0': b'', 'z.bin': bytes(range(256)),
                 'nested/\u6df1\u5c42/\u6570\u636e.bin': b'utf8', 'space name': b'space'}
        for name, data in files.items():
            path = source/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
        expected = reference(files)
        port = free_port(); data = clamp_passive_data_port_base(free_port())
        env = {k:v for k,v in os.environ.items() if not k.startswith('CPNETFLUX_')}
        with (root/'server.log').open('w') as log:
            server = subprocess.Popen([str(build/'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
                '--port', str(port), '--data-port-base', str(data), '--root', str(server_root),
                '--checksum', 'crc32c'], stdout=log, stderr=subprocess.STDOUT, env=env)
        try:
            wait_for_control(port)
            base = ['--host', '127.0.0.1', '--port', str(port), '--connections', '1',
                    '--file-parallelism', '1', '--control-reuse', 'worker', '--data-session-reuse', 'tree',
                    '--checksum', 'crc32c', '--checksum-backend', 'auto', '--phase-timing', 'off']
            for index, mode in enumerate((None, 'original', 'cached')):
                run_env = dict(env)
                if mode is not None: run_env['CPNETFLUX_SUMMARY_PATH'] = mode
                for direction in ('upload', 'download'):
                    summary = root/f'{index}-{direction}.json'
                    destination = f'upload-{index}' if direction == 'upload' else str(root/f'download-{index}')
                    src = str(source) if direction == 'upload' else f'upload-{index}'
                    command = [str(build/f'cpnetflux-tree-{direction}-client'), *base,
                               '--source-dir', src, '--dest-dir', destination, '--json-summary', str(summary)]
                    result = subprocess.run(command, env=run_env, capture_output=True, text=True, timeout=20)
                    assert result.returncode == 0, result.stdout+result.stderr
                    value = json.loads(summary.read_text())
                    assert value.get('summary_path_requested') == (mode or 'original'), value
                    assert value.get('summary_path_effective') == (mode or 'original'), value
                    assert value['summary_tree_hash_source'] == 'filesystem_read' and value['tree_hash'] == expected, value
                    assert value['actual_transport'] == 'v3' and value['completed_files'] == len(files), value
                    target = server_root/destination if direction == 'upload' else Path(destination)
                    for name, payload in files.items():
                        assert hashlib.sha256((target/name).read_bytes()).digest() == hashlib.sha256(payload).digest()
                    if direction == 'download' and mode == 'cached':
                        resumed = subprocess.run(command+['--resume'], env=run_env, capture_output=True, text=True, timeout=20)
                        assert resumed.returncode == 0, resumed.stdout+resumed.stderr
                        assert json.loads(summary.read_text())['tree_hash'] == expected
            for direction in ('upload', 'download'):
                invalid_env = dict(env, CPNETFLUX_SUMMARY_PATH='invalid')
                destination = root/f'invalid-{direction}'
                invalid_base = list(base)
                invalid_base[invalid_base.index('--port')+1] = str(free_port())
                invalid_source = str(source) if direction == 'upload' else 'upload-0'
                invalid_destination = 'invalid-upload' if direction == 'upload' else str(destination)
                result = subprocess.run([str(build/f'cpnetflux-tree-{direction}-client'), *invalid_base,
                    '--source-dir', invalid_source, '--dest-dir', invalid_destination],
                    env=invalid_env, capture_output=True, text=True, timeout=5)
                assert result.returncode != 0 and 'CPNETFLUX_SUMMARY_PATH' in result.stdout+result.stderr, result.stdout+result.stderr
                # CLI option validation may create an empty download directory.
                if destination.exists(): assert destination.is_dir() and not any(destination.iterdir())
                assert not Path(str(destination)+'.cpnetflux.tree.download.manifest').exists()
                assert not (server_root/'invalid-upload').exists()
            print(json.dumps({'result':'pass','transfers':7,'invalid_selectors':2,'reference':expected}))
        finally:
            stop_server(server, root/'server.log')


if __name__ == '__main__': main()
