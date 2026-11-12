#!/usr/bin/env python3
"""S2E: three bounded observer pairs, local whole-file receiver work budget."""
import argparse
import functools
import json
from pathlib import Path
import statistics

import run_small_file_baseline as base
from run_summary_path_ab import ready
from run_tree_serializer_ab import run_verified

STAGES = ('thread_create', 'join_payload_wait', 'thread_join_tail', 'transaction_begin', 'commit', 'result_write')


def run_profile(argv, log, profile_mode, **options):
    return run_verified(argv, log, member_work_profile=profile_mode, tree_manifest_profile='0', **options)


def evaluate(rows):
    assert len(rows) == 6, 'requires exactly three complete observer pairs'
    groups = {m: sorted([r for r in rows if r['profile_mode'] == m], key=lambda r: r['rep']) for m in ('0', '1')}
    for mode, samples in groups.items():
        assert [r['rep'] for r in samples] == [1, 2, 3]
        for r in samples:
            s, p = r['summary'], r['summary']['member_work_profile']
            assert r['result'] == 'pass' and r['output_slot_removed'] and r['t_ready_seconds'] > 0
            assert r['phase_timing'] == 'off' and r['window_request'] == '2'
            assert s['tree_manifest_serializer_requested'] == s['tree_manifest_serializer_effective'] == 'original'
            assert s['summary_path_requested'] == s['summary_path_effective'] == 'cached'
            assert s['summary_tree_hash_source'] == 'filesystem_read' and s['actual_transport'] == 'v3'
            assert not s['tree_manifest_profile']['enabled']
            assert s['completed_files'] == 10000 and s['failed_files'] == s['changed_files'] == 0
            assert s['tree_manifest_event_threshold'] == 32 and s['tree_manifest_max_delay_ms'] == 100
            assert s['tree_manifest_generation'] == s['tree_manifest_persisted_generation'] == 20001
            assert r['resource']['pswpout_delta'] == 0 and r['resource']['independent_checkpoint_body_crcs'] == 10001
            assert r['actual_tree'] == {k: r['expected'][k] for k in ('sha256', 'files', 'bytes')}
            assert r['expected']['files'] == 10000 and r['expected']['bytes'] == 40960000
            assert r['manifest_checks']['committed_member_manifests'] == 10000
            assert r['manifest_checks']['completed_tree_manifests'] == 1
            assert p['enabled'] == (mode == '1')
            if mode == '1':
                assert p['effective'] == 'receiver' and not p['truncated']
                assert p['scope'] == 'completed_local_whole_file_attempts_excludes_final_audit'
                assert p['started_members'] == p['completed_members'] == 10000 and p['excluded_ranges'] == 0
                assert p['manifest_save']['attempts'] >= 20000
                assert p['manifest_save']['failures'] == 0
                assert p['manifest_save']['attempts'] == p['manifest_save']['publish_successes']
                assert set(p['stages']) == set(STAGES) and len(p['lanes']) == 4
                assert sum(lane['completed_members'] for lane in p['lanes']) == 10000
                for stage in STAGES:
                    value = p['stages'][stage]
                    assert value['calls'] == 10000
                    assert 0 <= value['union_seconds'] <= min(value['sum_seconds'], r['t_ready_seconds']) + 1e-6
                    assert abs(sum(lane['stage_sums'][stage] for lane in p['lanes']) - value['sum_seconds']) < 1e-5
                create, tail = (p['stages'][k]['union_seconds'] for k in ('thread_create', 'thread_join_tail'))
                assert max(create, tail) - 1e-6 <= p['thread_envelope_union_seconds'] <= min(create+tail, r['t_ready_seconds']) + 1e-6
    assert len({r['actual_tree']['sha256'] for r in rows}) == len({r['summary']['tree_hash'] for r in rows}) == 1
    report = {'scope': 'observer attribution, not optimization acceptance; no sender/payload gain claim', 'arms': {}}
    for mode, samples in groups.items():
        times = [r['t_ready_seconds'] for r in samples]
        report['arms'][mode] = dict(values=times, median=statistics.median(times),
                                  cv_population=statistics.pstdev(times)/statistics.mean(times))
    report['observer_median_overhead'] = report['arms']['1']['median']/report['arms']['0']['median'] - 1
    report['observer_gate_pass'] = (abs(report['observer_median_overhead']) <= .05 and
                                    all(a['cv_population'] <= .05 for a in report['arms'].values()))
    report['budgets'] = [dict(rep=r['rep'], t_ready_seconds=r['t_ready_seconds'],
        thread_envelope_fraction=r['summary']['member_work_profile']['thread_envelope_union_seconds']/r['t_ready_seconds'],
        profile=r['summary']['member_work_profile']) for r in groups['1']]
    report['thread_envelope_fraction_median'] = statistics.median(b['thread_envelope_fraction'] for b in report['budgets'])
    report['thread_envelope_fraction_max'] = max(b['thread_envelope_fraction'] for b in report['budgets'])
    report['budget_status'] = ('inconclusive_observer_gate' if not report['observer_gate_pass'] else
        'below_10_percent_thread_envelope' if report['thread_envelope_fraction_max'] < .10 else
        'requires_removable_mechanism_and_discount_before_design')
    report['interpretation'] = ('Thread envelope unions constructor and post-loop join tail across lanes; '
        'tail still includes required buffer cleanup/runtime teardown. Join payload wait and worker entry latency '
        'are not added. Transaction/commit contain nested checkpoint saves. All are diagnostic service envelopes, '
        'not conservative removable wall time; sender credit starvation and indirect contention are unmeasured.')
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
    rows = []; case, port = '4k-10000-member28', 49428
    args.size, args.count = 4096, 10000
    try:
        base.peer(args, 'init'); peer_initialized = True
        expected = base.peer(args, 'generate', size=args.size, count=args.count); generated = True
        base.peer(args, 'start', name=case, port=port, data_base=60140); started = True
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
                print(json.dumps({'sample':name, 't_ready':row['t_ready_seconds']}), flush=True)
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
                        assert checked == {k:expected[k] for k in ('sha256', 'files', 'bytes')}
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
