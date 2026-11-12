#!/usr/bin/env python3
"""S2F: at most three sender-observer pairs, fixed files and receiver policy."""
import argparse
import json
from pathlib import Path
import statistics
import run_small_file_baseline as base
from run_summary_path_ab import ready
from run_tree_serializer_ab import run_verified

STAGES = ('producer_create','producer_loop','join_producer_work','producer_join_tail','credit_ready',
          'credit_empty_at_resume','credit_unproven','credit_terminal','result_read','result_drain')


def evaluate(rows):
    assert len(rows)==6, 'requires exactly three complete observer pairs'
    groups={m:sorted([r for r in rows if r['profile_mode']==m],key=lambda r:r['rep']) for m in ('0','1')}
    for mode,samples in groups.items():
        assert [r['rep'] for r in samples]==[1,2,3]
        for r in samples:
            s,server=r['summary'],r['server']
            assert r['result']=='pass' and r['output_slot_removed'] and r['t_ready_seconds']>0
            assert r['window_request']=='2' and r['phase_timing']=='off'
            assert s['actual_transport']=='v3' and s['completed_files']==10000 and s['failed_files']==s['changed_files']==0
            assert s['summary_path_requested']==s['summary_path_effective']=='cached'
            assert s['summary_tree_hash_source']=='filesystem_read'
            assert s['tree_manifest_serializer_requested']==s['tree_manifest_serializer_effective']=='original'
            assert s['tree_manifest_event_threshold']==32 and s['tree_manifest_max_delay_ms']==100
            assert s['tree_manifest_generation']==s['tree_manifest_persisted_generation']==20001
            assert not any(s[k]['enabled'] for k in ('tree_manifest_profile','member_work_profile','sender_work_profile'))
            assert r['expected']['files']==10000 and r['expected']['bytes']==40960000
            assert r['actual_tree']=={k:r['expected'][k] for k in ('sha256','files','bytes')}
            assert r['manifest_checks']['committed_member_manifests']==10000 and r['manifest_checks']['completed_tree_manifests']==1
            assert r['resource']['independent_checkpoint_body_crcs']==10001
            for resource in (r['resource'],server['resource']):
                assert 'error' not in resource and resource['pswpout_delta']==resource['owned_swap_peak_bytes']==0
                assert resource['mem_available_min_bytes']>=12*1024**3
            assert server['sender_work_profile_mode']==mode
            records=server['sender_phases']['sender_work_profiles'];assert len(records)==1
            record=records[0];assert record['channels']==record['attempt_records']==4
            p=record['profile'];assert p['enabled']==(mode=='1')
            if mode=='1':
                assert p['effective']=='sender' and p['scope']=='local_whole_file_sender_attempts_excludes_ranges'
                assert not p['truncated'] and p['started_members']==p['sent_members']==p['completed_members']==10000
                assert p['excluded_ranges']==p['result_failures']==0
                assert len(p['lanes'])==4 and sum(l['completed_members'] for l in p['lanes'])==10000
                assert set(p['stages'])==set(STAGES)
                for k,v in p['stages'].items():
                    assert 0<=v['union_seconds']<=min(v['sum_seconds'],r['t_ready_seconds'])+1e-6
                    assert abs(sum(l['stage_sums'][k] for l in p['lanes'])-v['sum_seconds'])<1e-5
                for k in ('producer_create','producer_loop','join_producer_work','producer_join_tail','result_read'):
                    assert p['stages'][k]['calls']==10000
                assert p['stages']['result_drain']['calls']==4
                assert p['stages']['credit_unproven']['calls']==p['stages']['credit_terminal']['calls']==0
                create,tail=(p['stages'][k]['union_seconds'] for k in ('producer_create','producer_join_tail'))
                assert max(create,tail)-1e-6<=p['thread_envelope_union_seconds']<=min(create+tail,r['t_ready_seconds'])+1e-6
    assert len({r['actual_tree']['sha256'] for r in rows})==len({r['summary']['tree_hash'] for r in rows})==1
    report={'scope':'sender attribution; no optimization or cross-host interval addition','arms':{}}
    for mode,samples in groups.items():
        values=[r['t_ready_seconds'] for r in samples]
        report['arms'][mode]=dict(values=values,median=statistics.median(values),cv_population=statistics.pstdev(values)/statistics.mean(values))
    report['observer_median_overhead']=report['arms']['1']['median']/report['arms']['0']['median']-1
    report['observer_gate_pass']=(abs(report['observer_median_overhead'])<=.05 and all(a['cv_population']<=.05 for a in report['arms'].values()))
    report['budgets']=[]
    for r in groups['1']:
        p=r['server']['sender_phases']['sender_work_profiles'][0]['profile'];t=r['t_ready_seconds']
        report['budgets'].append(dict(rep=r['rep'],t_ready_seconds=t,profile=p,
            thread_envelope_fraction=p['thread_envelope_union_seconds']/t,
            credit_ready_fraction=p['stages']['credit_ready']['union_seconds']/t,
            max_lane_thread_fraction=max(l['stage_sums']['producer_create']+l['stage_sums']['producer_join_tail'] for l in p['lanes'])/t))
    report['budget_status']=('inconclusive_observer_gate' if not report['observer_gate_pass'] else
        'below_10_percent_sender_thread_envelope' if max(b['thread_envelope_fraction'] for b in report['budgets'])<.10 else
        'requires_removable_mechanism_and_discount_before_design')
    report['interpretation']=('Producer loop, result reads and final drain overlap useful work; not pooling savings. '
        'Credit-ready proves queued metadata throughout a monotone wait, not that receiver commit can be skipped. '
        'Thread envelopes and ready waits overlap; do not add, or add to receiver clock intervals. '
        'No conservative task gain or candidate acceptance follows from a material envelope alone.')
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',required=True,type=Path);parser.add_argument('--payload-root',required=True,type=Path)
    parser.add_argument('--commit',required=True);args=parser.parse_args();assert base.node()==1
    args.evidence.mkdir(parents=True,exist_ok=True)
    local,remote=base.receipt(args.commit),base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',{'R770-1':local,'R770-2':remote})
    assert not args.payload_root.exists();args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    peer_initialized=generated=False;rows=[];args.size,args.count=4096,10000
    try:
        base.peer(args,'init');peer_initialized=True
        expected=base.peer(args,'generate',size=args.size,count=args.count);generated=True
        for rep in range(1,4):
            for mode in (('0','1') if rep%2 else ('1','0')):
                name=f'4k-10000-sender29-profile{mode}-r{rep}';started=False
                try:
                    base.peer(args,'start',name=name,port=49429,data_base=60160,sender_work_profile=mode);started=True
                    ready(49429)
                    row=base.client_sample(args,name,expected,'2','off',49429,summary_path='cached',
                        tree_manifest_serializer='original',runner=run_verified)
                finally:
                    if started:
                        server=base.peer(args,'stop',name=name);base.save(args.evidence/(name+'.server.json'),server)
                row.update(rep=rep,profile_mode=mode,server=server);rows.append(row)
                base.save(args.evidence/'samples'/(name+'.receipt.json'),row);base.save(args.evidence/'results.json',rows)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
                print(json.dumps({'sample':name,'t_ready':row['t_ready_seconds']}),flush=True)
    finally:
        try:
            if generated:
                try:
                    checked=base.peer(args,'source-check');base.save(args.evidence/'source-final.json',checked)
                    assert checked=={k:expected[k] for k in ('sha256','files','bytes')}
                finally:base.save(args.evidence/'source-cleanup.json',base.peer(args,'source-cleanup'))
        finally:
            try:
                if peer_initialized:base.save(args.evidence/'peer-finish.json',base.peer(args,'finish'))
            finally:
                base.check_root(args.payload_root);(args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
    report=evaluate(rows);report['payload_root_removed']=not args.payload_root.exists()
    base.save(args.evidence/'analysis.json',report);print(json.dumps(report),flush=True)


if __name__=='__main__':main()
