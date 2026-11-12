#!/usr/bin/env python3
"""Bounded private tmpfs V3 admission or one fixed formal M2 row; never scans."""
import argparse
import json
import os
from pathlib import Path
import signal
import time
import private_tmpfs as private
import run_small_file_baseline as base
import run_large_data_ab as large
import receive_network as net
from run_receive_network import receive_gate
from run_summary_default_gate import verify_chunks
from run_summary_path_ab import ready

def validate_receive_deltas(values):
    assert values['rx_out_of_buffer_delta']==0 and values['rx_missed_errors_delta']==0

def validate_netdev_deltas(values):
    assert values['rx_missed_errors']==0

def resource_gate(client,sender,source,host_mem_available,host_pswpout_delta):
    if not all(isinstance(value,dict) for value in (client,sender,source)):
        return False
    client_mem=client.get('mem_available_min_bytes',0)
    sender_mem=sender.get('mem_available_min_bytes',0)
    source_mem=source.get('mem_available_bytes',0)
    source_process=source.get('process') or {}
    return (min(host_mem_available,client_mem,sender_mem,source_mem)>=12*base.GIB and
        client.get('owned_swap_peak_bytes')==0 and client.get('owned_swap_current_bytes')==0 and
        sender.get('owned_swap_peak_bytes')==0 and source_process.get('swap_bytes')==0 and
        client.get('pswpout_delta')==0 and sender.get('pswpout_delta')==0 and
        source.get('host_pswpout_delta_since_seed')==0 and host_pswpout_delta==0 and
        sender.get('error') is None)
def validate_kernel_pacing_receipts(rows):
    assert len(rows)==4 and {r['lane'] for r in rows}==set(range(4)), 'kernel pacing lane receipts'
    assert all(r['enabled'] is True and r['requested_bytes_per_second']==2000000000 and
        r['effective_ceiling_bytes_per_second']==2000000000 and
        r['application_pacer_enabled'] is False and
        r['scope']=='TCP_socket_ceiling_not_achieved_rate' for r in rows), 'kernel pacing configuration'
    return dict(lanes=4,per_socket_ceiling_bytes_per_second=2000000000,
                ceiling_scope='TCP_socket_configuration_not_achieved_rate',application_pacer_enabled=False)

def validate_summary(summary):
    assert summary['connections']==4 and summary['actual_transport']=='v3'
    assert summary['data_transport']=='tcp' and summary['data_frame_logical_bytes']==262144
    assert not summary['control_tls_verified'] and not summary['data_tls_verified']
    ends=summary['lane_endpoints']
    assert len(ends)==4 and {(r['channel_index'],r['lane_index']) for r in ends}=={(i,0) for i in range(4)}
    assert all(e['data_connections']==1 and e['data_local_address']=='192.168.100.1'
               and e['data_peer_address']=='192.168.100.2'
               and 1048576<=e['tcp_receive_buffer_bytes']<=2097152
               and not e.get('tcp_exact_read',{}).get('enabled',False) for e in ends)

def run(args):
    private.require_enabled(args.enable_private_tmpfs)
    assert base.node()==1 and os.geteuid()!=0
    args.mode='v3'  # This entry point only executes V3 rows.
    spec=private.validate_profile(args)
    args.size_gib=spec['size_gib'];args.size,args.count=args.size_gib*base.GIB,1
    large.SIZE=args.size
    args.reproducible_source=spec['name']!='admission'
    sample_name='private55-combined-1g' if spec['name']=='admission' else 'private58-'+spec['name'][3:]+'-8g'
    mount=json.loads((args.evidence/'mount-ready.json').read_text())
    assert os.readlink('/proc/self/ns/mnt')==mount['private_namespace']
    assert os.path.ismount(args.payload_root)
    options=mount['mount_options'];huge=spec['huge_policy']
    assert 'huge='+huge in options or (huge=='never' and 'huge=' not in options),'mount huge policy mismatch'
    capacity=os.statvfs(args.payload_root)
    assert capacity.f_blocks*capacity.f_frsize==spec['mount_limit_bytes'],'private capacity mismatch'
    if spec['name']!='admission':
        assert mount['experiment_profile']==spec['name']
        assert mount['mount_limit_bytes']==spec['mount_limit_bytes'] and mount['huge_policy']==huge
    if args.native_setup:
        assert mount.get('setup_strategy')=='native_syscalls' and mount.get('supervisor_euid')==0
    assert base.mem()>=spec['preflight_mem_bytes'],'bounded receiver preflight'
    broad_before=net.snapshot(full=True)
    before=base.snapshot();base.save(args.evidence/'preflight-r1.json',before)
    local=base.receipt(args.commit);remote=base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',dict(local=local,remote=remote))
    base.save(args.evidence/'inventory-r1.json',net.inventory())
    assert not list(args.payload_root.iterdir())
    (args.payload_root/base.MARKER).write_text(base.TOKEN)
    init_attempt=False;start_attempt=False;row=None;expected=None;source=None;server=None;failures=[];cleanup={}
    gates=dict(network=False,resources=False,content=False,pages=False)
    if args.kernel_tcp_pacing or spec['name']!='admission':gates['kernel_pacing']=False
    if args.native_setup:gates['native_setup']=True
    page=dict(samples=0,target_shmem_pmd_mapped_peak_bytes=0,actual_uid=os.getuid(),
              scope='sampled actual receiver target VMAs; peak is not total file coverage')
    def runner(command,log,**kw):
        def observe(proc,state):
            try:
                text=(Path('/proc')/str(proc.pid)/'smaps').read_text()
                page['target_shmem_pmd_mapped_peak_bytes']=max(
                    page['target_shmem_pmd_mapped_peak_bytes'],private.target_pmd_bytes(text,args.payload_root))
                page['samples']+=1
            except FileNotFoundError:pass
        return base.run_guarded(command,log,process_observer=observe,**kw)
    def cancel(signum,frame):raise InterruptedError('experiment cancelled '+str(signum))
    previous={s:signal.signal(s,cancel) for s in (signal.SIGTERM,signal.SIGINT)}
    try:
        init_attempt=True
        peer=base.peer(args,'init');base.save(args.evidence/'preflight-r2.json',peer)
        assert peer['mem_available_bytes']>=spec['preflight_mem_bytes'],'bounded sender preflight'
        expected=large.own_peer(args,'seed')
        start_attempt=True
        base.peer(args,'start',name='private55',port=49337,data_base=61000,sender_pacing='off',
                  kernel_pacing='2000000000' if args.kernel_tcp_pacing else 'off')
        ready(49337)
        idle=[net.snapshot()];time.sleep(.5);idle.append(net.snapshot())
        base.save(args.evidence/'idle.json',idle);receive_gate(net.analyze(*idle))
        source=large.own_peer(args,'residency');base.save(args.evidence/'source-residency.json',source)
        assert source['all_resident'] and source['host_pswpout_delta_since_seed']==0
        assert source['mem_available_bytes']>=12*base.GIB and source['process']['swap_bytes']==0
        try:
            row=base.client_sample(args,sample_name,expected,'2','on',49337,
                mapped_download=True,data_frame_bytes=262144,tcp_receive_buffer_bytes='1048576',
                receiver_profile=False,range_timing=False,network_profile=True,network_settle_seconds=.5,
                tcp_exact_read='off',runner=runner)
        finally:base.save(args.evidence/'receiver-pages.json',page)
        base.save(args.evidence/'results.json',[row])
        validate_summary(row['summary'])
        row['verified_chunks']=verify_chunks(row['manifest_checks']['member_example'],args.size)
        assert row['verified_chunks']==args.size//base.MiB
        row['mapped_mechanism']=large.mapped_checks(args,sample_name,262144,
                                                  receiver_profile=False,range_timing=False)
        gates['content']=True
        gates['pages']=page['samples']>0 and ((page['target_shmem_pmd_mapped_peak_bytes']>0)
            if huge=='within_size' else page['target_shmem_pmd_mapped_peak_bytes']==0)
        assert gates['pages'],'actual receiver page policy proof missing'
        window=row['receiver_network_window']
        receive_gate(window['analysis']);receive_gate(window['settled_analysis'])
        broad_delta=large.network_counter_deltas(row['before']['net'],row['after']['net'])
        validate_netdev_deltas(broad_delta)
        gates['network']=True
    except BaseException as error:
        failures.append(dict(stage='run',error=repr(error)))
    finally:
        # No validator is allowed to skip subsequent ownership cleanup or source validation.
        for s in previous:signal.signal(s,signal.SIG_IGN)
        if start_attempt:
            try:
                server=base.peer(args,'stop',name='private55');base.save(args.evidence/'server.json',server)
                assert not server['sender_phases'].get('sender_pacing',[]),'pacing unexpectedly active'
                if args.kernel_tcp_pacing:
                    proof=validate_kernel_pacing_receipts(server['sender_phases'].get('tcp_kernel_pacing',[]))
                    base.save(args.evidence/'kernel-pacing-receipts.json',dict(
                        receipts=server['sender_phases']['tcp_kernel_pacing'],proof=proof))
                    gates['kernel_pacing']=True
                elif spec['name']!='admission':
                    assert not server['sender_phases'].get('tcp_kernel_pacing',[]),'control kernel pacing active'
                    gates['kernel_pacing']=True
                server_delta=large.network_counter_deltas(server['before']['net'],server['after']['net'])
                validate_netdev_deltas(server_delta)
            except BaseException as error:
                gates['network']=False;failures.append(dict(stage='server-stop',error=repr(error)))
        if init_attempt:
            try:
                checked=base.peer(args,'source-check');base.save(args.evidence/'source-check.json',checked)
                assert expected is not None and checked=={k:expected[k] for k in ('sha256','files','bytes')}
            except BaseException as error:
                gates['content']=False;failures.append(dict(stage='source-check',error=repr(error)))
            try:cleanup['peer_source']=large.own_peer(args,'cleanup')
            except BaseException as error:failures.append(dict(stage='source-cleanup',error=repr(error)))
            try:cleanup['peer_root']=base.peer(args,'finish')
            except BaseException as error:failures.append(dict(stage='peer-finish',error=repr(error)))
        try:
            base.check_root(args.payload_root)
            (args.payload_root/base.MARKER).unlink()
            cleanup['local_mount_empty']=not list(args.payload_root.iterdir())
            assert cleanup['local_mount_empty']
        except BaseException as error:failures.append(dict(stage='local-cleanup',error=repr(error)))
        for s,handler in previous.items():signal.signal(s,handler)
        base.save(args.evidence/'cleanup.json',cleanup)
    try:
        broad_after=net.snapshot(full=True)
        broad=net.analyze(broad_before,broad_after)
        base.save(args.evidence/'broad-network.json',dict(before=broad_before,after=broad_after,analysis=broad))
        receive_gate(broad)
        task_delta=large.network_counter_deltas(before['net'],base.snapshot()['net'])
        validate_netdev_deltas(task_delta)
    except BaseException as error:
        gates['network']=False;failures.append(dict(stage='broad-final-gate',error=repr(error)))
    try:
        gates['resources']=resource_gate(
            row.get('resource') if row is not None else None,
            server.get('resource') if server is not None else None,
            source,base.mem(),base.pswpout()-before['pswpout'])
        if not gates['resources']:raise AssertionError('memory or swap resource gate')
    except BaseException as error:
        gates['resources']=False
        failures.append(dict(stage='resources',error=repr(error)))
    scope=('one instrumented feasibility; no A/B, CV, M2 or stability claim' if spec['name']=='admission'
           else 'one 8GiB row of formal M2; matrix decision external')
    report=dict(scope=scope,profile=spec['name'],payload_bytes=args.size,
        gates=gates,failures=failures,cleanup=cleanup,m2_pass=False,
        setup_strategy=mount.get('setup_strategy','python_namespace'),
        pacing='off',exact_read='off',kernel_pacing='2000000000' if args.kernel_tcp_pacing else 'off',
        final_admission_pending_external_unmount=True)
    if row is not None:
        base.save(args.evidence/'results.json',[row])
        total=mount['target_setup_seconds']+row['target_preparation_seconds']+row['t_ready_seconds']
        report.update(client_wall_seconds=row['t_ready_seconds'],client_raw_gbps=row['whole_task_gbps'],
            private_target_setup_seconds=mount['target_setup_seconds'],
            target_directory_prepare_seconds=row['target_preparation_seconds'],
            complete_target_task_seconds=total,complete_target_task_gbps=args.size*8/total/1e9,
            denominator='private namespace/setup + target directory creation + client launch to exit; SHA/observation/cleanup separate')
    base.save(args.evidence/'analysis.json',report)
    if failures:base.save(args.evidence/'failure.json',failures)
    print(json.dumps(report),flush=True)
    return int(bool(failures))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action',choices=['run'],required=True)
    p.add_argument('--enable-private-tmpfs',action='store_true')
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--payload-root',type=Path,required=True)
    p.add_argument('--kernel-tcp-pacing',action='store_true')
    p.add_argument('--native-setup',action='store_true')
    p.add_argument('--profile',choices=['admission','m2-control','m2-candidate'],default='admission')
    p.add_argument('--commit',required=True)
    return run(p.parse_args())

if __name__=='__main__':raise SystemExit(main())
