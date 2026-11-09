#!/usr/bin/env python3
"""Exercise frozen tree serializer selection and cross-mode V3 resume."""
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
    with tempfile.TemporaryDirectory(prefix='cpnetflux-tree-serializer-') as temporary:
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
                '--checksum', 'crc32c'], stdout=log, stderr=subprocess.STDOUT,
                env=dict(env, CPNETFLUX_SENDER_WORK_PROFILE='1'))
        try:
            wait_for_control(port)
            base = ['--host', '127.0.0.1', '--port', str(port), '--connections', '1',
                    '--file-parallelism', '1', '--control-reuse', 'worker', '--data-session-reuse', 'tree',
                    '--checksum', 'crc32c', '--checksum-backend', 'auto', '--phase-timing', 'off']
            transfers = 0
            for index, mode in enumerate((None, 'original', 'buffered')):
                run_env = dict(env, CPNETFLUX_SUMMARY_PATH='cached',
                               CPNETFLUX_TREE_MANIFEST_PROFILE='1' if mode == 'buffered' else '0',
                               CPNETFLUX_MEMBER_WORK_PROFILE='1' if mode == 'buffered' else '0')
                if mode is not None: run_env['CPNETFLUX_SENDER_WORK_PROFILE'] = '1' if mode == 'buffered' else '0'
                if mode is None: run_env.pop('CPNETFLUX_TREE_MANIFEST_PROFILE')
                if mode is None: run_env.pop('CPNETFLUX_MEMBER_WORK_PROFILE')
                if mode is not None: run_env['CPNETFLUX_TREE_MANIFEST_SERIALIZER'] = mode
                for direction in ('upload', 'download'):
                    summary = root/f'{index}-{direction}.json'
                    destination = f'upload-{index}' if direction == 'upload' else str(root/f'download-{index}')
                    src = str(source) if direction == 'upload' else f'upload-{index}'
                    command = [str(build/f'cpnetflux-tree-{direction}-client'), *base,
                               '--source-dir', src, '--dest-dir', destination, '--json-summary', str(summary)]
                    result = subprocess.run(command, env=run_env, capture_output=True, text=True, timeout=20)
                    assert result.returncode == 0, result.stdout+result.stderr
                    value = json.loads(summary.read_text())
                    sender = value.get('sender_work_profile')
                    assert sender is not None and sender['enabled'] == (mode == 'buffered'), value
                    if sender['enabled']:
                        assert sender['effective'] == ('sender' if direction == 'upload' else 'not_used_receiver'), sender
                        if direction == 'upload':
                            assert sender['started_members'] == sender['sent_members'] == sender['completed_members'] == len(files), sender
                            assert not sender['truncated'] and sender['result_failures'] == 0, sender
                            assert sender['stages']['producer_create']['calls'] == len(files), sender
                    member = value.get('member_work_profile')
                    assert member is not None and member['enabled'] == (mode == 'buffered'), value
                    if member['enabled']:
                        assert member['effective'] == ('receiver' if direction == 'download' else 'not_used_sender'), member
                        if direction == 'download':
                            assert member['completed_members'] == len(files) and not member['truncated'], member
                            assert member['stages']['transaction_begin']['calls'] == len(files), member
                            assert member['stages']['commit']['calls'] == len(files), member
                            assert member['manifest_save']['attempts'] >= 2*len(files), member
                    transfers += 1
                    assert value.get('tree_manifest_serializer_requested') == (mode or 'original'), value
                    assert value.get('tree_manifest_serializer_effective') == (mode or 'original'), value
                    assert value.get('tree_manifest_serializer_scope') == 'all_client_tree_manifest_save_attempts', value
                    profile = value.get('tree_manifest_profile')
                    assert profile is not None and profile['enabled'] == (mode == 'buffered'), value
                    if profile['enabled']:
                        assert not profile['truncated'] and profile['snapshot_copy_records'] > 0, profile
                        assert profile['save']['attempts'] == value['tree_manifest_snapshots'], profile
                        assert profile['callback_wait_on_snapshot_union_seconds'] <= profile['callback_wait_union_seconds'], profile
                        assert profile['finish_barrier_seconds'] >= 0, profile
                    assert value.get('summary_path_effective') == 'cached', value
                    assert value['summary_tree_hash_source'] == 'filesystem_read' and value['tree_hash'] == expected, value
                    assert value['actual_transport'] == 'v3' and value['completed_files'] == len(files), value
                    target = server_root/destination if direction == 'upload' else Path(destination)
                    for name, payload in files.items():
                        assert hashlib.sha256((target/name).read_bytes()).digest() == hashlib.sha256(payload).digest()
                    # Resume both directions with the opposite implementation; format and CRC must agree.
                    if mode is not None:
                        resume_env = dict(run_env, CPNETFLUX_TREE_MANIFEST_SERIALIZER=(
                            'original' if mode == 'buffered' else 'buffered'))
                        resumed = subprocess.run(command+['--resume'], env=resume_env, capture_output=True, text=True, timeout=20)
                        assert resumed.returncode == 0, resumed.stdout+resumed.stderr
                        resumed_value = json.loads(summary.read_text())
                        assert resumed_value['tree_hash'] == expected
                        assert resumed_value['tree_manifest_serializer_effective'] == resume_env['CPNETFLUX_TREE_MANIFEST_SERIALIZER']
                        transfers += 1
            for direction in ('upload', 'download'):
                invalid_env = dict(env, CPNETFLUX_TREE_MANIFEST_SERIALIZER='invalid')
                destination = root/f'invalid-{direction}'
                invalid_base = list(base)
                invalid_base[invalid_base.index('--port')+1] = str(free_port())
                invalid_source = str(source) if direction == 'upload' else 'upload-0'
                invalid_destination = 'invalid-upload' if direction == 'upload' else str(destination)
                result = subprocess.run([str(build/f'cpnetflux-tree-{direction}-client'), *invalid_base,
                    '--source-dir', invalid_source, '--dest-dir', invalid_destination],
                    env=invalid_env, capture_output=True, text=True, timeout=5)
                assert result.returncode != 0 and 'CPNETFLUX_TREE_MANIFEST_SERIALIZER' in result.stdout+result.stderr, result.stdout+result.stderr
                # CLI option validation may create an empty download directory.
                if destination.exists(): assert destination.is_dir() and not any(destination.iterdir())
                assert not Path(str(destination)+'.cpnetflux.tree.download.manifest').exists()
                assert not (server_root/'invalid-upload').exists()
            for direction in ('upload', 'download'):
                invalid_env = dict(env, CPNETFLUX_TREE_MANIFEST_PROFILE='invalid')
                invalid_base = list(base)
                invalid_base[invalid_base.index('--port')+1] = str(free_port())
                result = subprocess.run([str(build/f'cpnetflux-tree-{direction}-client'), *invalid_base,
                    '--source-dir', str(source) if direction == 'upload' else 'upload-0',
                    '--dest-dir', 'invalid-profile' if direction == 'upload' else str(root/'invalid-profile')],
                    env=invalid_env, capture_output=True, text=True, timeout=5)
                assert result.returncode != 0 and 'CPNETFLUX_TREE_MANIFEST_PROFILE' in result.stdout+result.stderr
                assert not (server_root/'invalid-profile').exists()
            for direction in ('upload', 'download'):
                invalid_env = dict(env, CPNETFLUX_MEMBER_WORK_PROFILE='invalid')
                invalid_base = list(base)
                invalid_base[invalid_base.index('--port')+1] = str(free_port())
                result = subprocess.run([str(build/f'cpnetflux-tree-{direction}-client'), *invalid_base,
                    '--source-dir', str(source) if direction == 'upload' else 'upload-0',
                    '--dest-dir', 'invalid-member' if direction == 'upload' else str(root/'invalid-member')],
                    env=invalid_env, capture_output=True, text=True, timeout=5)
                assert result.returncode != 0 and 'CPNETFLUX_MEMBER_WORK_PROFILE' in result.stdout+result.stderr
                assert not (server_root/'invalid-member').exists()
            for direction in ('upload', 'download'):
                invalid_base = list(base); invalid_base[invalid_base.index('--port')+1] = str(free_port())
                result = subprocess.run([str(build/f'cpnetflux-tree-{direction}-client'), *invalid_base,
                    '--source-dir', str(source) if direction == 'upload' else 'upload-0',
                    '--dest-dir', 'invalid-sender' if direction == 'upload' else str(root/'invalid-sender')],
                    env=dict(env, CPNETFLUX_SENDER_WORK_PROFILE='invalid'), capture_output=True, text=True, timeout=5)
                assert result.returncode != 0 and 'CPNETFLUX_SENDER_WORK_PROFILE' in result.stdout+result.stderr
            invalid_server = subprocess.run([str(build/'cpnetflux-gridftp-server'), '--root', str(server_root),
                '--host','127.0.0.1','--port',str(free_port())],
                env=dict(env, CPNETFLUX_SENDER_WORK_PROFILE='invalid'), capture_output=True, text=True, timeout=5)
            assert invalid_server.returncode != 0 and 'CPNETFLUX_SENDER_WORK_PROFILE' in invalid_server.stdout+invalid_server.stderr
            records = [json.loads(line.split(' ',1)[1]) for line in (root/'server.log').read_text().splitlines()
                       if line.startswith('CPNETFLUX_SENDER_WORK ')]
            assert records and any(r['profile']['completed_members'] == len(files) and not r['profile']['truncated']
                                   for r in records), records
            print(json.dumps({'result':'pass','transfers':transfers,'invalid_selectors':4,'reference':expected}))
        finally:
            stop_server(server, root/'server.log')


if __name__ == '__main__': main()
