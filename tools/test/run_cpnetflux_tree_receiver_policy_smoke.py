#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f'duplicate JSON key: {key}'
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix='cpnetflux-receiver-policy-') as directory:
        root = Path(directory)
        source = root / 'source'
        source.mkdir()
        (source / 'empty').write_bytes(b'')
        block = bytes(range(256)) * 4096
        with (source / 'payload.bin').open('wb') as output:
            for _ in range(4):
                output.write(block)
            output.write(b'partial-chunk-123')
        remote = root / 'server'
        shutil.copytree(source, remote / 'payload')
        expected = tree_hash(source)

        def start(algorithm):
            service_port = free_port()
            service_log = root / ('server-' + algorithm + '.log')
            with service_log.open('w') as output:
                service = subprocess.Popen([
                    str(build / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
                    '--port', str(service_port), '--data-port-base', str(clamp_passive_data_port_base(free_port())),
                    '--root', str(remote), '--connections', '4', '--auth-mode', 'anonymous',
                    '--tls-mode', 'off', '--data-tls-mode', 'off', '--checksum', algorithm,
                ], stdout=output, stderr=subprocess.STDOUT)
            try:
                wait_for_control(service_port)
            except Exception:
                stop_server(service, service_log)
                raise
            return service, service_port, service_log

        server, port, log = start('crc32c')
        try:

            def run(name, extra=(), *, lanes=1, checksum='crc32c', upload=False, batch_env='1', server_port=None):
                summary = root / (name + '.json')
                destination = root / name
                environment = os.environ.copy()
                environment['CPNETFLUX_RANGE_CHECKPOINT_BATCH'] = batch_env
                command = [str(build / ('cpnetflux-tree-upload-client' if upload else
                                         'cpnetflux-tree-download-client')),
                           '--host', '127.0.0.1', '--port', str(server_port or port),
                           '--source-dir', str(source) if upload else 'payload',
                           '--dest-dir', name if upload else str(destination),
                           '--connections', str(lanes), '--file-parallelism', str(lanes),
                           '--checksum', checksum, '--checksum-backend', 'software',
                           '--auth-mode', 'anonymous', '--tls-mode', 'off', '--data-tls-mode', 'off',
                           '--control-reuse', 'worker', '--data-session-reuse', 'tree',
                           '--file-scheduling', 'dynamic', '--transfer-timeout-ms', '60000',
                           '--json-summary', str(summary), *extra]
                completed = subprocess.run(command, text=True, capture_output=True,
                                           env=environment, timeout=75)
                assert completed.returncode == 0, (name, completed.stdout, completed.stderr)
                result = json.loads(summary.read_text(), object_pairs_hook=unique_object)
                assert result['result'] == 'pass' and result['actual_mode'] == 'persistent_tree', result
                assert result['recovery_attempts_used'] == 0, result
                assert tree_hash(remote / name if upload else destination) == expected, name
                policy = result['receiver_policy']
                if not upload:
                    assert policy['location'] == 'local_client', policy
                    assert policy['source'] == 'local_configuration', policy
                return policy

            policy = run('verified', ['--final-verify-policy', 'verified_chunks',
                                     '--manifest-flush-policy', 'every_n_chunks',
                                     '--manifest-flush-interval-chunks', '4',
                                     '--commit-sync-policy', 'fsync_file_and_dir'])
            assert policy['final_verify_policy_requested'] == 'verified_chunks', policy
            assert policy['final_verify_policy_effective'] == 'verified_chunks', policy
            assert policy['post_write_readback'] is False, policy
            assert policy['manifest_flush_policy'] == 'every_n_chunks', policy
            assert policy['manifest_flush_interval_chunks'] == 4, policy
            assert policy['commit_sync_policy'] == 'fsync_file_and_dir', policy
            assert policy['range_checkpoint_batch_chunks_effective'] is None, policy

            policy = run('defaults')
            assert policy['final_verify_policy_requested'] == 'full', policy
            assert policy['final_verify_policy_effective'] == 'full', policy
            assert policy['post_write_readback'] is True, policy
            assert policy['manifest_flush_policy'] == 'every_n_chunks', policy
            assert policy['manifest_flush_interval_chunks'] == 16, policy
            assert policy['commit_sync_policy'] == 'none', policy
            assert policy['range_checkpoint_batch_chunks_override'] is None, policy

            unverified_server, unverified_port, unverified_log = start('none')
            try:
                policy = run('no-checksum', ['--final-verify-policy', 'verified_chunks'],
                             checksum='none', server_port=unverified_port)
            finally:
                stop_server(unverified_server, unverified_log)
            assert policy['final_verify_policy_requested'] == 'verified_chunks', policy
            assert policy['final_verify_policy_effective'] == 'coverage_only', policy
            assert policy['post_write_readback'] is False, policy

            policy = run('final-only', ['--manifest-flush-policy', 'final_only',
                                        '--commit-sync-policy', 'fsync_file'])
            assert policy['manifest_flush_policy'] == 'final_only', policy
            assert policy['commit_sync_policy'] == 'fsync_file', policy

            policy = run('upload', upload=True)
            assert policy['location'] == 'remote_server' and policy['source'] == 'unobserved', policy
            assert all(value is None for key, value in policy.items()
                       if key not in ['location', 'source']), policy

            with (remote / 'payload' / 'striped.bin').open('wb') as output:
                for _ in range(40):
                    output.write(block)
            expected = tree_hash(remote / 'payload')
            policy = run('range-override', ['--final-verify-policy', 'verified_chunks',
                                           '--range-checkpoint-batch-chunks', '16'], lanes=2)
            assert policy['range_checkpoint_batch_chunks_override'] == 16, policy
            assert policy['range_checkpoint_batch_chunks_effective'] == 16, policy
            policy = run('range-auto', ['--range-checkpoint-batch-chunks', 'auto'],
                         lanes=2, batch_env='4')
            assert policy['range_checkpoint_batch_chunks_override'] is None, policy
            assert policy['range_checkpoint_batch_chunks_effective'] == 4, policy
            print(json.dumps({'result': 'pass', 'cases': 7, 'range_policy_without_phase_timing': True}))
        finally:
            stop_server(server, log)


if __name__ == '__main__':
    main()
