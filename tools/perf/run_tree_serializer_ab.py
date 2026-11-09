#!/usr/bin/env python3
"""S2C: five bounded, interleaved original/buffered complete V3 task pairs."""
import argparse
import json
from pathlib import Path
import statistics

import run_small_file_baseline as base
from run_summary_path_ab import ready


def crc_table():
    table = []
    for byte in range(256):
        value = byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
        table.append(value)
    return table


CRC_TABLE = crc_table()


def crc32c(data):
    value = 0xffffffff
    for byte in data:
        value = CRC_TABLE[(value ^ byte) & 255] ^ (value >> 8)
    return value ^ 0xffffffff


def checkpoint_crcs(root):
    count = 0
    for path in root.rglob('*.manifest'):
        assert not path.is_symlink()
        data = path.read_bytes()
        body, separator, trailer = data.rpartition(b'manifest_body_crc32c=')
        assert separator and len(trailer) == 9 and trailer.endswith(b'\n'), path
        assert trailer == f'{crc32c(body):08x}\n'.encode(), ('checkpoint CRC mismatch', str(path))
        count += 1
    return count


def run_verified(argv, log, **options):
    resource = base.run_guarded(argv, log, **options)
    # This independent audit runs after spawn-to-exit timing, before payload cleanup.
    slot = Path(argv[argv.index('--dest-dir') + 1]).parent
    resource['independent_checkpoint_body_crcs'] = checkpoint_crcs(slot)
    assert resource['independent_checkpoint_body_crcs'] == 10001
    return resource


def evaluate(rows):
    assert len(rows) == 10, 'requires exactly five complete pairs'
    groups = {mode: sorted([r for r in rows if r['tree_manifest_serializer_request'] == mode],
                          key=lambda r: r['rep']) for mode in ('original', 'buffered')}
    for mode, samples in groups.items():
        assert len(samples) == 5 and [r['rep'] for r in samples] == list(range(1, 6)), 'incomplete pairs'
        for r in samples:
            s = r['summary']
            assert r['result'] == 'pass' and r['output_slot_removed'] and r['t_ready_seconds'] > 0
            assert r['phase_timing'] == 'off' and r['window_request'] == '2'
            assert r['expected']['files'] == s['completed_files'] == 10000
            assert r['expected']['bytes'] == 4096 * 10000 and s['failed_files'] == s['changed_files'] == 0
            assert s['actual_transport'] == 'v3'
            assert s['tree_manifest_serializer_requested'] == s['tree_manifest_serializer_effective'] == mode
            assert s['tree_manifest_serializer_scope'] == 'all_client_tree_manifest_save_attempts'
            assert s['summary_path_requested'] == s['summary_path_effective'] == 'cached'
            assert s['summary_tree_hash_source'] == 'filesystem_read'
            assert s['tree_manifest_event_threshold'] == 32 and s['tree_manifest_max_delay_ms'] == 100
            assert s['tree_manifest_generation'] == s['tree_manifest_persisted_generation']
            assert s['tree_manifest_snapshots'] > 0 and s['tree_manifest_serialized_bytes'] > 0
            assert r['actual_tree'] == {k: r['expected'][k] for k in ('sha256', 'files', 'bytes')}
            assert r['manifest_checks']['committed_member_manifests'] == 10000
            assert r['manifest_checks']['completed_tree_manifests'] == 1
            assert r['resource']['pswpout_delta'] == 0
            assert r['resource']['independent_checkpoint_body_crcs'] == 10001
    assert len({r['summary']['tree_hash'] for r in rows}) == 1, 'tree CRC changed'
    assert len({r['actual_tree']['sha256'] for r in rows}) == 1, 'tree SHA changed'
    result = {'scope': '4KiB x10K cached summary, phase off, independent spawn-to-exit', 'arms': {}}
    for mode, samples in groups.items():
        times = [r['t_ready_seconds'] for r in samples]
        arm = {'values': times, 'median': statistics.median(times),
               'p95_descriptive': base.percentile(times, .95),
               'cv_population': statistics.pstdev(times) / statistics.mean(times)}
        for key in ('tree_manifest_snapshots', 'tree_manifest_serialized_bytes', 'tree_manifest_write_seconds',
                    'tree_manifest_state_lock_wait_seconds', 'summary_tree_hash_seconds'):
            arm[key] = [r['summary'][key] for r in samples]
            arm['median_' + key] = statistics.median(arm[key])
        result['arms'][mode] = arm
    original, buffered = result['arms']['original'], result['arms']['buffered']
    result['median_wall_reduction'] = 1 - buffered['median'] / original['median']
    result['paired_wall_reductions'] = [1 - b['t_ready_seconds'] / a['t_ready_seconds']
                                      for a, b in zip(groups['original'], groups['buffered'])]
    result['gate_pass'] = (result['median_wall_reduction'] >= .10 and
                           all(a['cv_population'] <= .05 for a in (original, buffered)) and
                           buffered['p95_descriptive'] <= original['p95_descriptive'])
    result['gate'] = 'median wall reduction>=10%, each CV<=5%, descriptive p95 no worse; integrity/resource gates mandatory'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--payload-root', required=True, type=Path)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    assert base.node() == 1
    args.evidence.mkdir(parents=True, exist_ok=True)
    local, remote = base.receipt(args.commit), base.peer(args, 'receipt')
    assert local == remote
    base.save(args.evidence/'matching-builds.json', {'R770-1': local, 'R770-2': remote})
    assert not args.payload_root.exists()
    args.payload_root.mkdir()
    (args.payload_root/base.MARKER).write_text(base.TOKEN)
    remote_initialized = started = generated = False
    rows = []
    case, port = '4k-10000-serializer26', 49426
    args.size, args.count = 4096, 10000
    try:
        base.peer(args, 'init'); remote_initialized = True
        expected = base.peer(args, 'generate', size=args.size, count=args.count); generated = True
        base.peer(args, 'start', name=case, port=port, data_base=60100); started = True
        ready(port)
        for rep in range(1, 6):
            modes = ('original', 'buffered') if rep % 2 else ('buffered', 'original')
            for mode in modes:
                name = f'{case}-{mode}-r{rep}'
                row = base.client_sample(args, name, expected, '2', 'off', port,
                                         summary_path='cached', tree_manifest_serializer=mode, runner=run_verified)
                row['rep'] = rep; rows.append(row)
                base.save(args.evidence/'samples'/(name+'.receipt.json'), row)
                base.save(args.evidence/'results.json', rows)
                print(json.dumps({'sample': name, 't_ready': row['t_ready_seconds'],
                      'snapshots': row['summary']['tree_manifest_snapshots'],
                      'write_seconds': row['summary']['tree_manifest_write_seconds']}), flush=True)
    finally:
        # Independent cleanup steps still run if a resource/server check fails.
        try:
            if started:
                server = base.peer(args, 'stop', name=case)
                base.save(args.evidence/'server.json', server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta'] == 0
        finally:
            try:
                if generated:
                    try:
                        source_final = base.peer(args, 'source-check')
                        base.save(args.evidence/'source-final.json', source_final)
                        assert source_final == {k: expected[k] for k in ('sha256', 'files', 'bytes')}
                    finally:
                        base.save(args.evidence/'source-cleanup.json', base.peer(args, 'source-cleanup'))
            finally:
                try:
                    if remote_initialized:
                        base.save(args.evidence/'peer-finish.json', base.peer(args, 'finish'))
                finally:
                    base.check_root(args.payload_root)
                    (args.payload_root/base.MARKER).unlink(); args.payload_root.rmdir()
    report = evaluate(rows)
    report['payload_root_removed'] = not args.payload_root.exists()
    base.save(args.evidence/'analysis.json', report)
    print(json.dumps(report), flush=True)


if __name__ == '__main__': main()
