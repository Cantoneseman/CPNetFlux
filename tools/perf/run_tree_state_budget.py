#!/usr/bin/env python3
"""S2D bounded observer validation and direct tree-state synchronisation budget."""
import argparse
import functools
import json
from pathlib import Path
import statistics

import run_small_file_baseline as base
from run_summary_path_ab import ready
from run_tree_serializer_ab import run_verified


def run_profile(argv, log, profile_mode, **options):
    return run_verified(argv, log, tree_manifest_profile=profile_mode, **options)


def evaluate(rows):
    assert len(rows) == 6, 'requires three complete observer pairs'
    groups = {mode: sorted([r for r in rows if r['profile_mode'] == mode], key=lambda r: r['rep'])
              for mode in ('0', '1')}
    for mode, samples in groups.items():
        assert len(samples) == 3 and [r['rep'] for r in samples] == [1, 2, 3]
        for r in samples:
            s, p = r['summary'], r['summary']['tree_manifest_profile']
            assert r['result'] == 'pass' and r['output_slot_removed'] and r['t_ready_seconds'] > 0
            assert r['phase_timing'] == 'off' and r['window_request'] == '2'
            assert s['tree_manifest_serializer_requested'] == s['tree_manifest_serializer_effective'] == 'original'
            assert s['summary_path_requested'] == s['summary_path_effective'] == 'cached'
            assert s['summary_tree_hash_source'] == 'filesystem_read' and s['actual_transport'] == 'v3'
            assert s['completed_files'] == 10000 and s['failed_files'] == s['changed_files'] == 0
            assert s['tree_manifest_event_threshold'] == 32 and s['tree_manifest_max_delay_ms'] == 100
            assert s['tree_manifest_generation'] == s['tree_manifest_persisted_generation'] == 20001
            assert r['resource']['pswpout_delta'] == 0 and r['resource']['independent_checkpoint_body_crcs'] == 10001
            assert r['actual_tree'] == {k: r['expected'][k] for k in ('sha256', 'files', 'bytes')}
            assert r['expected']['files'] == 10000 and r['expected']['bytes'] == 40960000
            assert p['enabled'] == (mode == '1')
            if mode == '1':
                assert not p['truncated'] and p['callback_wait_intervals'] == 20000
                assert p['scope'] == 'persistent_writer_attempts_excludes_initial_and_preflight'
                assert p['save']['attempts'] == p['save']['publish_successes'] == s['tree_manifest_snapshots']
                assert p['save']['failures'] == 0 and p['save']['serialized_bytes'] == s['tree_manifest_serialized_bytes']
                assert p['save']['record_sort_calls'] == s['tree_manifest_snapshots']
                assert p['save']['record_copy_items'] == p['snapshot_copy_records'] > 0
                wait, hold, overlap = (p[k] for k in ('callback_wait_union_seconds', 'snapshot_hold_union_seconds',
                                                     'callback_wait_on_snapshot_union_seconds'))
                assert 0 <= overlap <= min(wait, hold) + 1e-6
                assert abs(hold - p['snapshot_copy_seconds'] - p['snapshot_destroy_seconds']) < 1e-6
                for key in ('final_reconcile_seconds', 'finish_barrier_seconds'):
                    assert 0 <= p[key] < r['t_ready_seconds']
    assert len({r['actual_tree']['sha256'] for r in rows}) == len({r['summary']['tree_hash'] for r in rows}) == 1
    report = {'scope': 'observer attribution, not performance acceptance; no buffered rerun', 'arms': {}}
    for mode, samples in groups.items():
        values = [r['t_ready_seconds'] for r in samples]
        report['arms'][mode] = dict(values=values, median=statistics.median(values),
                                   cv_population=statistics.pstdev(values)/statistics.mean(values))
    report['observer_median_overhead'] = report['arms']['1']['median']/report['arms']['0']['median'] - 1
    report['observer_gate_pass'] = (abs(report['observer_median_overhead']) <= .05 and
                                    all(a['cv_population'] <= .05 for a in report['arms'].values()))
    budgets = []
    for r in groups['1']:
        p = r['summary']['tree_manifest_profile']
        tail = p['final_reconcile_seconds'] + p['finish_barrier_seconds']
        direct = p['callback_wait_on_snapshot_union_seconds'] + tail
        generous = p['callback_wait_union_seconds'] + tail
        budgets.append(dict(rep=r['rep'], t_ready_seconds=r['t_ready_seconds'],
            direct_sync_envelope_seconds=direct, direct_sync_envelope_fraction=direct/r['t_ready_seconds'],
            all_callback_wait_envelope_seconds=generous, all_callback_wait_envelope_fraction=generous/r['t_ready_seconds'],
            profile=p))
    report['budgets'] = budgets
    report['direct_sync_envelope_fraction_median'] = statistics.median(b['direct_sync_envelope_fraction'] for b in budgets)
    report['direct_sync_envelope_fraction_max'] = max(b['direct_sync_envelope_fraction'] for b in budgets)
    report['budget_status'] = ('inconclusive_observer_gate' if not report['observer_gate_pass'] else
        'below_10_percent_direct_sync_envelope' if report['direct_sync_envelope_fraction_max'] < .10 else
        'requires_removable_mechanism_and_discount_before_design')
    report['interpretation'] = ('Intervals overlap background service; direct envelope includes all final reconciliation/finish, '
        'not all is removable. It excludes indirect CPU/cache contention and is not a universal speedup bound. '
        'No incremental format or performance gain is accepted by this attribution.')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--payload-root', required=True, type=Path)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args(); assert base.node() == 1
    args.evidence.mkdir(parents=True, exist_ok=True)
    local, remote = base.receipt(args.commit), base.peer(args, 'receipt')
    assert local == remote
    base.save(args.evidence/'matching-builds.json', {'R770-1': local, 'R770-2': remote})
    assert not args.payload_root.exists()
    args.payload_root.mkdir(); (args.payload_root/base.MARKER).write_text(base.TOKEN)
    peer_initialized = generated = started = False
    rows = []; case, port = '4k-10000-state27', 49427
    args.size, args.count = 4096, 10000
    try:
        base.peer(args, 'init'); peer_initialized = True
        expected = base.peer(args, 'generate', size=args.size, count=args.count); generated = True
        base.peer(args, 'start', name=case, port=port, data_base=60120); started = True
        ready(port)
        for rep in range(1, 4):
            for mode in (('0', '1') if rep % 2 else ('1', '0')):
                name = f'{case}-profile{mode}-r{rep}'
                row = base.client_sample(args, name, expected, '2', 'off', port,
                    summary_path='cached', tree_manifest_serializer='original',
                    runner=functools.partial(run_profile, profile_mode=mode))
                row.update(rep=rep, profile_mode=mode); rows.append(row)
                base.save(args.evidence/'samples'/(name+'.receipt.json'), row)
                base.save(args.evidence/'results.json', rows)
                print(json.dumps({'sample': name, 't_ready': row['t_ready_seconds'],
                                  'profile': row['summary']['tree_manifest_profile']}), flush=True)
    finally:
        try:
            if started:
                server = base.peer(args, 'stop', name=case); base.save(args.evidence/'server.json', server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta'] == 0
        finally:
            try:
                if generated:
                    try:
                        checked = base.peer(args, 'source-check'); base.save(args.evidence/'source-final.json', checked)
                        assert checked == {k: expected[k] for k in ('sha256', 'files', 'bytes')}
                    finally: base.save(args.evidence/'source-cleanup.json', base.peer(args, 'source-cleanup'))
            finally:
                try:
                    if peer_initialized: base.save(args.evidence/'peer-finish.json', base.peer(args, 'finish'))
                finally:
                    base.check_root(args.payload_root)
                    (args.payload_root/base.MARKER).unlink(); args.payload_root.rmdir()
    report = evaluate(rows); report['payload_root_removed'] = not args.payload_root.exists()
    base.save(args.evidence/'analysis.json', report)
    print(json.dumps(report), flush=True)


if __name__ == '__main__': main()
