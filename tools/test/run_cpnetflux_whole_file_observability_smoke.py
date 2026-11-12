#!/usr/bin/env python3
"""Check whole-file session observations against real data and manifest publications."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_tree_manifest_coalesce_smoke import RenameObserver
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control


def observed(row):
    assert row['range_id'] == 0, 'fixture must use the whole-file receiver'
    assert 'whole_file_observation' in row, 'missing actual whole-file session observation'
    details = row['whole_file_observation']
    assert details and details['schema'] == 'cpnetflux_whole_file_observation_v1'
    saves = details['manifest_save']
    assert saves['attempts'] == saves['serialize_calls'] == saves['publish_successes']
    assert saves['attempts'] >= 5 and saves['failures'] == 0
    assert saves['save_seconds'] >= saves['serialize_seconds'] > 0
    assert saves['serialized_bytes'] > 0
    assert details['record_lookup_calls'] == 32
    assert details['record_lookup_seconds'] > 0 and details['session_sort_seconds'] > 0
    return saves


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    build = parser.parse_args().build_dir.absolute()
    with tempfile.TemporaryDirectory(prefix='cpnetflux-whole-observation-') as temp:
        root = Path(temp)
        source = root / 'source'
        source.mkdir()
        block = bytes(index % 251 for index in range(65536))
        with (source / 'payload.bin').open('wb') as output:
            for _ in range(512):
                output.write(block)
        expected = tree_hash(source)
        for phase in ['on', 'off']:
            peer, dest = root / ('peer-' + phase), root / ('dest-' + phase)
            peer.mkdir()
            dest.mkdir()
            port, data = free_port(), clamp_passive_data_port_base(free_port())
            log = root / ('server-' + phase + '.log')
            env = dict(os.environ, CPNETFLUX_RANGE_TIMING='off')
            with log.open('w') as output:
                server = subprocess.Popen([str(build / 'cpnetflux-gridftp-server'), '--host', '127.0.0.1',
                    '--port', str(port), '--root', str(peer), '--data-port-base', str(data),
                    '--connections', '1', '--checksum', 'crc32c', '--chunk-size', '1048576',
                    '--manifest-flush-policy', 'every_n_chunks', '--manifest-flush-interval-chunks', '16'],
                    stdout=output, stderr=subprocess.STDOUT, env=env)
            try:
                wait_for_control(port)
                for direction in ['upload', 'download']:
                    summary = root / (direction + '-' + phase + '.json')
                    observer = RenameObserver(dest, 'payload.bin.cpnetflux.download.manifest') if direction == 'download' else None
                    try:
                        command = [str(build / ('cpnetflux-tree-' + direction + '-client')),
                            '--host', '127.0.0.1', '--port', str(port),
                            '--source-dir', str(source) if direction == 'upload' else 'received',
                            '--dest-dir', 'received' if direction == 'upload' else str(dest),
                            '--connections', '1', '--file-parallelism', '1', '--chunk-size', '1048576',
                            '--checksum', 'crc32c', '--checksum-backend', 'auto', '--phase-timing', phase,
                            '--json-summary', str(summary)]
                        if direction == 'download':
                            command += ['--manifest-flush-policy', 'every_n_chunks',
                                        '--manifest-flush-interval-chunks', '16']
                        done = subprocess.run(command, capture_output=True, text=True, env=env, timeout=60)
                        assert done.returncode == 0, done.stdout + done.stderr
                        assert tree_hash(peer / 'received' if direction == 'upload' else dest) == expected
                        result = json.loads(summary.read_text())
                        if phase == 'off':
                            assert result.get('file_timings', []) == []
                        elif direction == 'download':
                            assert len(result['file_timings']) == 1
                            saves = observed(result['file_timings'][0])
                            observer.drain()
                            assert saves['publish_successes'] == observer.count, (saves, observer.count)
                        else:
                            assert result['file_timings'][0].get('whole_file_observation') is None
                    finally:
                        if observer:
                            observer.close()
                remote = [row for line in log.read_text().splitlines()
                          if line.startswith('CPNETFLUX_PERSISTENT_TIMING ')
                          for row in json.loads(line.split(' ', 1)[1])['files']
                          if row['measurement_scope'] == 'local_receiver']
                assert len(remote) == 1
                observed(remote[0])
            finally:
                stop_server(server, log)
    print(json.dumps({'result': 'pass', 'directions': 2, 'client_phase_modes': 2,
                      'download_publications_independently_observed': True}))


if __name__ == '__main__':
    main()
