#!/usr/bin/env python3
"""Bounded 1GiB candidate matrices and the formal 8GiB M1 A/B gate."""
import argparse
import gzip
import json
import os
import random
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess

import run_small_file_baseline as base
from run_summary_default_gate import residency, verify_chunks
from run_summary_path_ab import ready

SIZE = base.GIB


def matrix_labels(mechanism, rep):
    labels = {
        'frame': ('65532','262144'),
        'receive-buffer': ('off','1048576'),
        'profile-overhead': ('off','on'),
        'm1': ('default','combined'),
    }[mechanism]
    return labels if rep % 2 else labels[::-1]


def arm_options(mechanism, label):
    if mechanism == 'frame':
        return int(label), 'off'
    if mechanism == 'receive-buffer':
        return 262144, label
    if mechanism == 'profile-overhead':
        return 262144, 'off'
    if mechanism == 'm1':
        return (None, 'off') if label == 'default' else (262144, '1048576')
    raise ValueError(f'unknown mechanism: {mechanism}')


def validate_receiver_profile_request(action, mechanism, size_gib, enabled):
    if mechanism == 'profile-overhead':
        if action != 'matrix' or size_gib != 1 or enabled:
            raise ValueError('profile-overhead requires its dedicated 1 GiB matrix without --receiver-profile')
        return
    if enabled and (action != 'matrix' or size_gib != 1 or mechanism not in ('frame','receive-buffer')):
        raise ValueError('receiver profile is limited to 1 GiB frame/receive-buffer matrix diagnostics')


def validate_profile_rows(file_rows, enabled, range_timing=None):
    if range_timing is None:range_timing=enabled
    for row in file_rows:
        assert bool(row.get('range_observation_enabled')) == bool(range_timing), 'range timing mode mismatch'
        payload=row.get('receiver_payload_observation')
        cpu=row.get('receiver_cpu_observation')
        if enabled:
            assert isinstance(payload,dict) and isinstance(cpu,dict), 'profile observation missing'
            assert payload.get('schema')=='cpnetflux_receiver_payload_observation_v2'
            assert cpu.get('schema')=='cpnetflux_receiver_cpu_observation_v1'
            missing_payload=set(base.RECEIVER_PAYLOAD_PROFILE_FIELDS)-payload.keys()
            missing_cpu=set(base.RECEIVER_CPU_PROFILE_FIELDS)-cpu.keys()
            assert not missing_payload and not missing_cpu, 'profile fields missing'
            assert isinstance(cpu.get('write_cpu_owner'),str), 'profile CPU owner missing'
        else:
            assert payload is None and cpu is None, 'profile unexpectedly enabled'


def validate_profile_frame_count(profile, actual_frames):
    assert profile['payload']['totals'].get('data_frame_count') == actual_frames, 'profile frame count mismatch'


def annotate_profile_analysis(report, enabled):
    if enabled:
        report['receiver_profile_enabled']=True
        report['throughput_is_performance_evidence']=False
        report['interpretation']='instrumented diagnostic; do not use throughput as a performance result'
    return report


def expected_write_calls(file_bytes, logical_frame_bytes):
    assert file_bytes % (8 * base.MiB) == 0
    chunks_per_range = (file_bytes // 8) // base.MiB
    frames_per_chunk = 17 if logical_frame_bytes == 65532 else 4
    return chunks_per_range * frames_per_chunk


def network_counter_deltas(before, after):
    if before.keys() != after.keys():
        raise ValueError('network counter sets differ')
    values = {key: after[key] - value for key, value in before.items()}
    if any(value < 0 for value in values.values()):
        raise ValueError('network counter regressed')
    return values


def cgroup_owner():
    """Called in the actual writer/SHA process, not a separate SSH probe."""
    membership = Path('/proc/self/cgroup').read_text()
    entry = next(line[3:] for line in membership.splitlines() if line.startswith('0::'))
    current = Path('/sys/fs/cgroup') / entry.lstrip('/')
    groups = []
    while True:
        fields = {}
        for name in ('memory.current','memory.max','memory.high','memory.swap.current',
                     'memory.swap.max','memory.events','memory.stat','memory.pressure'):
            path = current / name
            if path.exists(): fields[name] = path.read_text().strip()
        groups.append(dict(path=str(current), fields=fields))
        if current == Path('/sys/fs/cgroup'): break
        assert Path('/sys/fs/cgroup') in current.parents
        current = current.parent
    return dict(process=base.proc_info(os.getpid()), membership=membership, ancestors=groups)


def source_probe(args):
    state = json.loads((args.evidence/'peer/seed-resource.json').read_text())
    result = residency(base.owned(args.payload_root,'source')/'f000000.bin')
    result.update(host_pswpout_delta_since_seed=base.pswpout()-state['pswpout_before'],
                  mem_available_bytes=base.mem(), process=base.proc_info(os.getpid()))
    return result


def source_block(reproducible=False):
    return random.Random(0x43504e4656).randbytes(8*base.MiB) if reproducible else os.urandom(8*base.MiB)

def seed(args):
    base.receipt(args.commit)
    assert base.node()==2 and shutil.disk_usage('/dev/shm').free>SIZE+base.GIB
    assert base.mem()>=12*base.GIB, 'seed memory threshold before allocation'
    if getattr(args,'reproducible_source',False):
        import private_tmpfs
        private_tmpfs.memory_headroom(private_tmpfs.profile_spec('m2-control'))
    source=base.owned(args.payload_root,'source');source.mkdir()
    path=source/'f000000.bin';state=base.resource_state();state['owner_before']=cgroup_owner()
    def guard(phase):
        state['phase']=phase;state['samples']+=1
        state['mem_available_min_bytes']=min(state['mem_available_min_bytes'],base.mem())
        state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],base.proc_info(os.getpid())['swap_bytes'])
        assert state['mem_available_min_bytes']>=12*base.GIB,'seed memory threshold'
        assert state['owned_swap_peak_bytes']==0 and base.pswpout()==state['pswpout_before'],'seed swap gate'
    try:
        block=source_block(getattr(args,'reproducible_source',False))
        with path.open('xb') as handle:
            for _ in range(SIZE//len(block)):handle.write(block);guard('write')
        state['owner_after_write']=cgroup_owner()
        guard('sha');expected=base.tree(source);guard('sha_complete')
        state['owner_after_sha']=cgroup_owner()
        expected['residency']=residency(path);guard('residency')
        assert expected['residency']['all_resident'] and expected['bytes']==SIZE
        prefix='fixed pseudorandom' if getattr(args,'reproducible_source',False) else 'random'
        expected['data_shape']=f'{prefix} 8MiB block repeated {SIZE//(8*base.MiB)} times; fully materialized'
        base.save(args.evidence/'peer/source.json',expected)
        return expected
    finally:
        state['owner_final']=cgroup_owner();state['pswpout_delta']=base.pswpout()-state['pswpout_before']
        state['materialized_bytes']=path.stat().st_size if path.exists() else 0
        base.save(args.evidence/'peer/seed-resource.json',state)


def own_peer(args,action):
    argv=['python3',str(Path(__file__).resolve()),'--action',action,'--evidence',str(args.evidence),
          '--payload-root',str(args.payload_root),'--commit',args.commit,
          '--size-gib',str(args.size_gib)]
    if getattr(args,'reproducible_source',False):argv.append('--reproducible-source')
    result=subprocess.run(['ssh','-n','-o','BatchMode=yes','r770-2',shlex.join(argv)],
                          text=True,capture_output=True,timeout=180)
    assert result.returncode==0,result.stderr+result.stdout
    return json.loads(result.stdout)


def mapped_checks(args,name,size,receiver_profile=None,range_timing=None):
    size = 65532 if size is None else size
    if receiver_profile is None:receiver_profile=args.receiver_profile
    if range_timing is None:range_timing=receiver_profile
    with gzip.open(args.evidence/'samples'/(name+'.summary.json.gz'),'rt') as f:data=json.load(f)
    rows=data['file_timings'];assert len(rows)==8
    frames=0
    for index,row in enumerate(sorted(rows,key=lambda r:r['offset'])):
        assert row['offset']==index*SIZE//8 and row['length']==SIZE//8
        assert row['receiver_payload_execution']=='inline'
        assert row['receiver_write_backend']=='mmap_experimental'
        mapped=row['mapped_write']
        expected=expected_write_calls(SIZE, size)
        assert mapped['write_calls']==expected and mapped['written_bytes']==SIZE//8
        assert mapped['map_calls']==mapped['unmap_calls']==SIZE//(512*base.MiB) and mapped['active_mapped_bytes']==0
        assert mapped['window_bound_bytes']==64*base.MiB and mapped['concurrent_window_bound_bytes']==256*base.MiB
        frames+=mapped['write_calls']
    validate_profile_rows(rows,receiver_profile,range_timing)
    assert data['data_frame_logical_bytes']==size
    return dict(ranges=8,actual_data_frames=frames,mapped_window_bytes=64*base.MiB)


def evaluate(rows, size_bytes=SIZE):
    if {row['arm'] for row in rows}=={'default','combined'}:
        return evaluate_m1(rows, size_bytes)
    result={'scope':f'{size_bytes//base.GIB}GiB complete real-file task; not formal M1', 'arms':{}, 'm1_pass':False}
    arms=list(dict.fromkeys(r['arm'] for r in rows))
    assert len(arms)==2
    for label in arms:
        arm=[r for r in rows if r['arm']==label]
        times=[r['t_ready_seconds'] for r in arm];median=statistics.median(times)
        result['arms'][label]=dict(seconds=times,median_seconds=median,
            median_gbps=size_bytes*8/median/1e9,cv=statistics.pstdev(times)/statistics.mean(times),
            sampled_receiver_cpu_seconds=[r['resource']['cpu_seconds_sampled_lower_bound'] for r in arm],
            rss_peak_bytes=max(r['resource']['rss_peak_bytes'] for r in arm))
    result['paired_improvements']=[1-next(r for r in rows if r['rep']==rep and r['arm']==arms[1])['t_ready_seconds']/
        next(r for r in rows if r['rep']==rep and r['arm']==arms[0])['t_ready_seconds'] for rep in range(1,6)]
    result['median_task_rate_change']=result['arms'][arms[1]]['median_gbps']/result['arms'][arms[0]]['median_gbps']-1
    result['improved_pairs']=sum(x>0 for x in result['paired_improvements'])
    return result


def evaluate_profile_overhead(rows,size_bytes):
    assert size_bytes==base.GIB and len(rows)==10, 'requires ten 1 GiB profile-overhead tasks'
    order=[(rep,arm) for rep in range(1,6) for arm in matrix_labels('profile-overhead',rep)]
    assert [(row['rep'],row['arm']) for row in rows]==order, 'requires profile off/on AB/BA interleaving'
    groups={'off':[],'on':[]}
    expected_frames=8*expected_write_calls(size_bytes,262144)
    for row in rows:
        arm=row['arm']
        assert row['frame_bytes']==262144 and row['receive_buffer']=='off', 'profile-overhead config changed'
        assert row['profile_enabled']==(arm=='on') and row['range_timing_requested'] is True
        assert row['expected']['bytes']==size_bytes and row['expected']['files']==1
        assert row['result']=='pass' and row['output_slot_removed']
        assert row['verified_chunks']==size_bytes//base.MiB
        assert row['actual_tree']=={k:row['expected'][k] for k in ('sha256','files','bytes')}
        assert row['resource']['exit_code']==0
        assert row['resource']['owned_swap_peak_bytes']==row['resource']['pswpout_delta']==0
        assert row['resource']['mem_available_min_bytes']>=12*base.GIB
        assert row['source_residency']['all_resident']
        assert row['source_residency']['bytes']==size_bytes
        assert row['source_residency']['process']['swap_bytes']==0
        assert row['source_residency']['host_pswpout_delta_since_seed']==0
        assert row['mechanism']['ranges']==8 and row['mechanism']['actual_data_frames']==expected_frames
        network=row['transfer_network_counter_deltas']
        assert network['rx_missed_errors']==0 and network['rx_out_of_buffer']==0, 'profile overhead network gate'
        window=row['receiver_network_window']
        assert 'before' in window and 'after_transfer' in window and 'analysis' in window
        assert window['analysis']['rx_missed_errors_delta']==network['rx_missed_errors']
        assert window['analysis']['rx_out_of_buffer_delta']==network['rx_out_of_buffer']
        if arm=='on':
            profile=row['receiver_phases']['receiver_profile']
            assert profile['payload']['totals']['data_frame_count']==expected_frames
            assert profile['cpu'] is not None
        else:
            assert row['receiver_phases'].get('receiver_profile') is None
        groups[arm].append(row)
    for arm in groups.values():
        assert len(arm)==5 and [row['rep'] for row in arm]==list(range(1,6))

    cv={arm:statistics.pstdev([r['t_ready_seconds'] for r in rows_arm]) /
                statistics.mean([r['t_ready_seconds'] for r in rows_arm])
         for arm,rows_arm in groups.items()}
    paired=[next(r for r in groups['on'] if r['rep']==rep)['t_ready_seconds'] /
            next(r for r in groups['off'] if r['rep']==rep)['t_ready_seconds']-1
            for rep in range(1,6)]
    median=statistics.median(paired)
    stable=max(cv.values())<=.05
    return {
        'scope':'1 GiB matched V3 TCP profile perturbation diagnostic; not a performance result',
        'arms':{arm:{'seconds':[r['t_ready_seconds'] for r in values],
                     'median_seconds':statistics.median([r['t_ready_seconds'] for r in values]),
                     'cv':cv[arm]} for arm,values in groups.items()},
        'paired_slowdown_fractions':paired,
        'median_paired_slowdown_fraction':median,
        'profile_on_slower_pairs':sum(value>0 for value in paired),
        'profile_effect_resolved':stable,
        'repeatable_profile_perturbation':stable and median>=.05 and sum(value>0 for value in paired)>=4,
        'transfer_network_gate_pass':True,
        'post_process_rx_missed_errors_deltas':[r['post_process_network_counter_deltas']['rx_missed_errors']
                                                for r in rows],
        'throughput_is_performance_evidence':False,
    }


def evaluate_m1(rows, size_bytes):
    assert size_bytes == 8 * base.GIB and len(rows) == 10, 'requires ten 8 GiB M1 tasks'
    groups = {arm: sorted((row for row in rows if row['arm'] == arm), key=lambda row: row['rep'])
              for arm in ('default','combined')}
    order = [(rep,arm) for rep in range(1,6)
             for arm in matrix_labels('m1',rep)]
    assert [(row['rep'],row['arm']) for row in rows] == order, 'requires AB/BA interleaving'
    for values in groups.values():
        assert len(values) == 5 and [row['rep'] for row in values] == list(range(1,6))
        for row in values:
            expected_frame = 65532 if row['arm'] == 'default' else 262144
            expected_buffer = 'off' if row['arm'] == 'default' else '1048576'
            assert row['frame_bytes'] == expected_frame and row['receive_buffer'] == expected_buffer, 'M1 frame/receive_buffer mismatch'
            assert row['expected']['bytes'] == size_bytes and row['expected']['files'] == 1
            assert row['source_residency']['bytes'] == size_bytes
            assert row['result'] == 'pass' and row['output_slot_removed']
            assert row['verified_chunks'] == size_bytes // base.MiB
            assert row['actual_tree'] == {key: row['expected'][key] for key in ('sha256','files','bytes')}
            assert row['resource']['exit_code'] == 0
            assert row['resource']['owned_swap_peak_bytes'] == row['resource']['pswpout_delta'] == 0
            assert row['resource']['mem_available_min_bytes'] >= 12 * base.GIB
            assert row['source_residency']['all_resident']
            assert row['source_residency']['process']['swap_bytes'] == 0
            assert row['source_residency']['host_pswpout_delta_since_seed'] == 0
            assert row['mechanism']['ranges'] == 8
            expected_frames = 8192 * (17 if row['frame_bytes'] == 65532 else 4)
            assert row['mechanism']['actual_data_frames'] == expected_frames
            assert row['network_counter_deltas']['rx_missed_errors'] >= 0
    assert {row['expected']['sha256'] for row in rows} == {row['actual_tree']['sha256'] for row in rows}
    result = {'scope':'8 GiB complete V3 M1; five interleaved pairs',
              'arms':{}, 'pair_improvements':[], 'network_rx_missed_errors_deltas':[],
              'network_gate_pass':False, 'performance_gate_pass':False, 'm1_pass':False}
    for arm, values in groups.items():
        times = [row['t_ready_seconds'] for row in values]
        median = statistics.median(times)
        result['arms'][arm] = {
            'seconds':times, 'median_seconds':median, 'median_gbps':size_bytes*8/median/1e9,
            'cv':statistics.pstdev(times)/statistics.mean(times),
            'sampled_receiver_cpu_seconds':[row['resource']['cpu_seconds_sampled_lower_bound'] for row in values],
            'rss_peak_bytes':max(row['resource']['rss_peak_bytes'] for row in values),
        }
    result['pair_improvements'] = [
        1 - groups['combined'][index]['t_ready_seconds'] / groups['default'][index]['t_ready_seconds']
        for index in range(5)
    ]
    result['improved_pairs'] = sum(value > 0 for value in result['pair_improvements'])
    result['median_task_rate_change'] = (
        result['arms']['combined']['median_gbps'] / result['arms']['default']['median_gbps'] - 1
    )
    result['network_rx_missed_errors_deltas'] = [
        {'arm':row['arm'],'rep':row['rep'],'delta':row['network_counter_deltas']['rx_missed_errors']}
        for row in rows
    ]
    result['network_gate_pass'] = all(item['delta'] == 0 for item in result['network_rx_missed_errors_deltas'])
    result['performance_gate_pass'] = (
        result['arms']['combined']['median_gbps'] >= 30
        and max(result['arms']['default']['cv'], result['arms']['combined']['cv']) <= .05
        and result['improved_pairs'] >= 4
    )
    result['m1_pass'] = result['performance_gate_pass'] and result['network_gate_pass']
    result['gate'] = 'candidate median >=30 Gbps; both arm CV <=5%; >=4/5 paired improvements; zero rx_missed_errors deltas'
    return result


def matrix(args):
    assert base.node()==1
    assert (args.size_gib, args.mechanism) in ((1,'frame'),(1,'receive-buffer'),
                                                (1,'profile-overhead'),(8,'m1'))
    size_bytes = args.size_gib * base.GIB
    args.size,args.count=size_bytes,1
    local=base.receipt(args.commit);peer=base.peer(args,'receipt');assert local==peer
    base.save(args.evidence/'matching-builds.json',dict(local=local,peer=peer))
    assert not args.payload_root.exists()
    assert base.mem()>=12*base.GIB and shutil.disk_usage('/dev/shm').free>size_bytes+base.GIB
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    peer_initialized=False;started=False;rows=[];expected=None;cleanup={}
    server_name='large-data-m1' if args.mechanism=='m1' else 'large-data'
    try:
        base.peer(args,'init');peer_initialized=True
        expected=own_peer(args,'seed')
        base.peer(args,'start',name=server_name,port=49335,data_base=61000);started=True;ready(49335)
        for rep in range(1,6):
            for label in matrix_labels(args.mechanism,rep):
                data_frame_bytes,receive_buffer=arm_options(args.mechanism,label)
                frame_bytes=65532 if data_frame_bytes is None else data_frame_bytes
                name=f'{args.size_gib}g-{args.mechanism}-{label}-r{rep}'
                resident=own_peer(args,'residency');base.save(args.evidence/'samples'/(name+'.source.json'),resident)
                assert resident['all_resident'] and resident['host_pswpout_delta_since_seed']==0,'source residency/swap gate'
                assert resident['mem_available_bytes']>=12*base.GIB and resident['process']['swap_bytes']==0
                assert base.mem()>=12*base.GIB and shutil.disk_usage('/dev/shm').free>size_bytes+base.GIB
                profile_overhead=args.mechanism=='profile-overhead'
                sample_profile=(label=='on') if profile_overhead else args.receiver_profile
                row=base.client_sample(args,name,expected,'2','on',49335,mapped_download=True,
                    data_frame_bytes=data_frame_bytes,tcp_receive_buffer_bytes=receive_buffer,
                    receiver_profile=sample_profile,
                    range_timing=True if profile_overhead else None,
                    network_profile=profile_overhead)
                row.update(rep=rep,frame_bytes=frame_bytes,arm=label,receive_buffer=receive_buffer,
                           source_residency=resident,profile_enabled=sample_profile)
                row['network_counter_deltas']=network_counter_deltas(row['before']['net'],row['after']['net'])
                row['verified_chunks']=verify_chunks(row['manifest_checks']['member_example'],size_bytes)
                row['mechanism']=mapped_checks(args,name,data_frame_bytes,
                    receiver_profile=sample_profile,
                    range_timing=True if profile_overhead else None)
                if profile_overhead:
                    window=row['receiver_network_window']
                    analysis=window['analysis']
                    row['transfer_network_counter_deltas']={
                        'rx_missed_errors':analysis['rx_missed_errors_delta'],
                        'rx_out_of_buffer':analysis['rx_out_of_buffer_delta'],
                    }
                    row['post_process_network_counter_deltas']=network_counter_deltas(
                        window['after_transfer']['netdev'],row['after']['net'])
                if sample_profile:
                    validate_profile_frame_count(row['receiver_phases']['receiver_profile'],
                                                 row['mechanism']['actual_data_frames'])
                if receive_buffer!='off':
                    assert all(1048576<=lane['tcp_receive_buffer_bytes']<=2097152
                               for lane in row['summary']['lane_endpoints'])
                rows.append(row);base.save(args.evidence/'results.json',rows)
                misses=row['network_counter_deltas']['rx_missed_errors']
                print(json.dumps(dict(sample=name,gbps=row['whole_task_gbps'],
                    frames=row['mechanism']['actual_data_frames'],rx_missed_errors=misses,
                    transfer_rx_missed_errors=row.get('transfer_network_counter_deltas',{}).get('rx_missed_errors'))),flush=True)
                if profile_overhead:
                    transfer=row['transfer_network_counter_deltas']
                    assert transfer['rx_missed_errors']==0 and transfer['rx_out_of_buffer']==0, \
                        'profile overhead transfer network counter gate'
                elif args.receiver_profile:
                    assert misses==0, 'receiver profile diagnostic rx_missed_errors gate'
        assert base.peer(args,'source-check')=={k:expected[k] for k in ('sha256','files','bytes')}
    except BaseException as error:
        base.save(args.evidence/'failure.json',dict(error=repr(error),completed_samples=len(rows)))
        raise
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name=server_name);base.save(args.evidence/'server.json',server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
        finally:
            if peer_initialized:
                cleanup['peer_source']=own_peer(args,'cleanup')
                assert cleanup['peer_source'].get('source_removed') is True
                cleanup['peer_root']=base.peer(args,'finish')
                assert cleanup['peer_root'].get('removed') is True
            base.check_root(args.payload_root)
            (args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
            cleanup['local_root_removed']=not args.payload_root.exists()
            base.save(args.evidence/'cleanup.json',cleanup)
            assert cleanup['local_root_removed']
    if args.mechanism=='profile-overhead':
        report=evaluate_profile_overhead(rows,size_bytes)
    else:
        report=annotate_profile_analysis(evaluate(rows,size_bytes),args.receiver_profile)
    report['cleanup_gate_pass']=(cleanup.get('peer_source',{}).get('source_removed') is True
                                 and cleanup.get('peer_root',{}).get('removed') is True
                                 and cleanup.get('local_root_removed') is True)
    if args.mechanism=='m1':
        report['m1_pass']=report['m1_pass'] and report['cleanup_gate_pass']
    base.save(args.evidence/'analysis.json',report)
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action',required=True,choices=('matrix','seed','residency','cleanup'))
    parser.add_argument('--evidence',required=True,type=Path)
    parser.add_argument('--payload-root',required=True,type=Path)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--mechanism',choices=('frame','receive-buffer','profile-overhead','m1'),default='frame')
    parser.add_argument('--size-gib',type=int,choices=(1,8),default=1)
    parser.add_argument('--reproducible-source',action='store_true')
    parser.add_argument('--receiver-profile',action='store_true',
                        help='enable receiver CPU/payload profiling for bounded 1 GiB diagnostics')
    args=parser.parse_args()
    try:
        validate_receiver_profile_request(args.action,args.mechanism,args.size_gib,args.receiver_profile)
    except ValueError as error:
        parser.error(str(error))
    args.evidence.mkdir(parents=True,exist_ok=True)
    SIZE=args.size_gib*base.GIB
    if args.action=='matrix':matrix(args)
    elif args.action=='cleanup':
        source=base.owned(args.payload_root,'source')
        if source.exists():base.remove_owned(args.payload_root,source)
        print(json.dumps(dict(source_removed=not source.exists())))
    else:print(json.dumps(seed(args) if args.action=='seed' else source_probe(args)))
