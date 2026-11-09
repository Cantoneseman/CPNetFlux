#!/usr/bin/env python3
"""Restore the link-before-committed-save state, then exercise real V3 CLIs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, stop_server, wait_for_control


def crc32c(data):
    value = 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
    return value ^ 0xffffffff


def fields(path):
    raw = path.read_bytes()
    body, suffix = raw.rsplit(b'manifest_body_crc32c=', 1)
    assert crc32c(body) == int(suffix.strip(), 16), path
    return dict(line.split('=', 1) for line in raw.decode().splitlines())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--evidence-dir', type=Path)
    args = parser.parse_args(); build = args.build_dir.resolve()
    if args.evidence_dir:
        args.evidence_dir.mkdir(parents=True, exist_ok=True)
    env = {k:v for k,v in os.environ.items() if not k.startswith('CPNETFLUX_')}
    files = {'data.bin': bytes(range(256)) * 16, 'nested/part.bin': b'abc12345' * 2048, 'empty': b''}
    expected = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
    rows = []
    with tempfile.TemporaryDirectory(prefix='cpnetflux-publication-cli-') as temporary:
        root = Path(temporary)
        for direction in ('upload', 'download'):
            case = root / direction; case.mkdir()
            source = case / 'source'; source.mkdir()
            server_root = case / 'server'; server_root.mkdir()
            for name, data in files.items():
                path = source / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            shutil.copytree(source, server_root / 'source')
            destination = server_root / 'uploaded' if direction == 'upload' else case / 'download'
            source_arg = str(source) if direction == 'upload' else 'source'
            dest_arg = 'uploaded' if direction == 'upload' else str(destination)
            port = free_port(); data_port = clamp_passive_data_port_base(free_port())
            server = None; server_log = None; server_index = 0
            receiver_args = ['--final-verify-policy', 'verified_chunks', '--commit-sync-policy', 'none',
                             '--manifest-flush-policy', 'every_n_chunks', '--manifest-flush-interval-chunks', '16']

            def start():
                nonlocal server, server_log, server_index
                log = (args.evidence_dir or case) / f'{direction}-server-{server_index}.log'
                server_log = log
                server_index += 1
                with log.open('w') as output:
                    server = subprocess.Popen([str(build / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
                        '--port', str(port), '--data-port-base', str(data_port), '--root', str(server_root),
                        '--checksum', 'crc32c', *receiver_args], env=env, stdout=output, stderr=subprocess.STDOUT)
                wait_for_control(port)

            def run(label, resume):
                summary = (args.evidence_dir or case) / f'{direction}-{label}.json'
                command = [str(build / f'cpnetflux-tree-{direction}-client'), '--host', '127.0.0.1',
                    '--local-bind-address', '127.0.0.1', '--port', str(port), '--connections', '1',
                    '--file-parallelism', '1', '--control-reuse', 'worker', '--data-session-reuse', 'tree',
                    '--file-scheduling', 'dynamic', '--source-dir', source_arg, '--dest-dir', dest_arg,
                    '--checksum', 'crc32c', '--phase-timing', 'off', '--json-summary', str(summary)]
                if direction == 'download': command += receiver_args
                if resume: command.append('--resume')
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
                if args.evidence_dir:
                    (args.evidence_dir / f'{direction}-{label}.log').write_text(result.stdout + result.stderr)
                assert result.returncode == 0, result.stdout + result.stderr
                value = json.loads(summary.read_text())
                assert value['result'] == 'pass' and value['actual_transport'] == 'v3', value
                assert not value.get('fallback_reason'), value
                if resume: assert value['bytes_transferred'] == 0, value
                return value

            try:
                start(); run('initial', False); stop_server(server, server_log); server = None
                staged = []
                for name in files:
                    target = destination / name
                    suffix = '.cpnetflux.manifest' if direction == 'upload' else '.cpnetflux.download.manifest'
                    manifest = Path(str(target) + suffix)
                    original = fields(manifest); assert original['state'] == 'committed'
                    temp = Path(bytes.fromhex(original['temp_path_hex']).decode())
                    assert not temp.exists(); os.link(target, temp)
                    raw = manifest.read_bytes().replace(b'state=committed\n', b'state=transferring\n')
                    body = raw.rsplit(b'manifest_body_crc32c=', 1)[0]
                    candidate = manifest.with_name(manifest.name + '.restore31')
                    candidate.write_bytes(body + f'manifest_body_crc32c={crc32c(body):08x}\n'.encode())
                    os.replace(candidate, manifest)
                    assert fields(manifest)['state'] == 'transferring'
                    assert os.path.samefile(target, temp)
                    staged.append((name, target, temp, manifest, target.stat().st_ino, original['transfer_id']))
                start(); restored = run('restore-resume', True); repeated = run('repeat-resume', True)
                for name, target, temp, manifest, inode, transfer in staged:
                    assert not temp.exists() and target.stat().st_ino == inode
                    assert hashlib.sha256(target.read_bytes()).hexdigest() == expected[name]
                    saved = fields(manifest)
                    assert saved['state'] == 'committed' and saved['transfer_id'] == transfer
                rows.append({'direction': direction, 'restored_members': len(staged), 'sha256': expected,
                    'resume_payload_bytes': restored['bytes_transferred'],
                    'repeat_payload_bytes': repeated['bytes_transferred'], 'inodes_unchanged': True,
                    'checkpoint_body_crcs_valid': True, 'temp_aliases_retired': True})
            finally:
                if server is not None: stop_server(server, server_log)
    report = {'scope': 'faithful staged-state CLI resume; not SIGKILL/power-loss or throughput',
              'rows': rows, 'owned_fixtures_removed': not root.exists()}
    if args.evidence_dir: (args.evidence_dir / 'publication-smoke-result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
