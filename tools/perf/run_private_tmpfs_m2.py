#!/usr/bin/env python3
"""One fixed five-pair 8GiB M2 experiment. Hard failure stops; no retries or scans."""
import argparse,hashlib,json,math,os,re,shlex,statistics,subprocess,sys
from pathlib import Path
from types import SimpleNamespace
import private_tmpfs as private
import run_private_tmpfs_v3 as single
import run_small_file_baseline as base
import run_large_data_ab as large
import receive_network as net
from run_receive_network import receive_gate

SIZE=8*base.GIB
ORDER=[(rep,arm) for rep in range(1,6)
       for arm in (('control','candidate') if rep%2 else ('candidate','control'))]
HARD_GATES={'network','resources','content','pages','kernel_pacing','native_setup',
            'cleanup','no_failures','child_ok'}

def require(value,message):
    if not value:raise ValueError(message)

def read(path):return json.loads(path.read_text())
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def validate_row(row):
    require(row['arm'] in ('control','candidate') and row['rep'] in range(1,6),'invalid row identity')
    require(row['bytes']==SIZE and row['files']==1,'requires complete 8GiB single-file task')
    require(re.fullmatch('[0-9a-f]{40}',row['commit']) is not None,'fixed commit missing')
    require(re.fullmatch('[0-9a-f]{64}',row['sha256']) is not None,'content digest missing')
    require(row['verified_chunks']==8192 and row['data_frames']==32768,'integrity/mechanism work mismatch')
    require(set(row['gates'])==HARD_GATES and all(v is True for v in row['gates'].values()),'hard gate failed or missing')
    parts=row['components'];require(set(parts)=={'setup','directory','client'},'complete denominator missing')
    require(all(isinstance(x,(float,int)) and math.isfinite(x) and x>0 for x in parts.values()),'invalid component time')
    total=row['complete_target_task_seconds']
    require(isinstance(total,(float,int)) and math.isfinite(total) and total>0,'invalid total')
    require(math.isclose(total,sum(parts.values()),rel_tol=0,abs_tol=1e-9),'necessary cost excluded')

def collect_rows(run_one,checkpoint):
    rows=[]
    for rep,arm in ORDER:
        row=run_one(rep,arm)
        validate_row(row)
        require((row['rep'],row['arm'])==(rep,arm),'row identity differs from dispatched task')
        if rows:
            require(row['sha256']==rows[0]['sha256'] and row['commit']==rows[0]['commit'],'data/commit changed')
        rows.append(row);checkpoint(rows)
    return rows

def evaluate(rows,*,recovery_gate,scene_gate):
    require([(r['rep'],r['arm']) for r in rows]==ORDER,'requires ten AB/BA interleaved tasks')
    for row in rows:validate_row(row)
    require(len({r['sha256'] for r in rows})==len({r['commit'] for r in rows})==1,'data/commit changed')
    report=dict(scope='8GiB complete private-target V3; five interleaved pairs; composite candidate',
                arms={},pair_time_reductions=[],m2_pass=False,recovery_gate=recovery_gate,scene_gate=scene_gate)
    groups={arm:sorted((r for r in rows if r['arm']==arm),key=lambda r:r['rep']) for arm in ('control','candidate')}
    for arm,values in groups.items():
        seconds=[r['complete_target_task_seconds'] for r in values]
        rates=[SIZE*8/t/1e9 for t in seconds]
        report['arms'][arm]=dict(seconds=seconds,gbps=rates,median_seconds=statistics.median(seconds),
            median_gbps=statistics.median(rates),time_cv=statistics.pstdev(seconds)/statistics.mean(seconds),
            rate_cv=statistics.pstdev(rates)/statistics.mean(rates))
    report['pair_time_reductions']=[1-b['complete_target_task_seconds']/a['complete_target_task_seconds']
        for a,b in zip(groups['control'],groups['candidate'])]
    report['improved_pairs']=sum(x>0 for x in report['pair_time_reductions'])
    report['median_task_rate_change']=report['arms']['candidate']['median_gbps']/report['arms']['control']['median_gbps']-1
    report['performance_gate']=(report['arms']['candidate']['median_gbps']>=40 and
        max(a['time_cv'] for a in report['arms'].values())<=.05 and report['improved_pairs']>=4)
    report['m2_pass']=report['performance_gate'] and recovery_gate is True and scene_gate is True
    report['gate']='candidate median >=40Gbps; both time CV<=5%; >=4/5 paired improvements; every hard/recovery/scene gate'
    return report

def receipt(commit):
    result=base.receipt(commit)
    result['binaries']['cpnetflux-private-tmpfs-launcher']=base.sha(base.PROJECT/'build/cpnetflux-private-tmpfs-launcher')
    result['source_objects']={name:base.output('git','-C',str(base.PROJECT),'rev-parse','HEAD:'+name)
                              for name in ('src','include','tests','tools','CMakeLists.txt')}
    result.update(memory_headroom=dict(mem_available_bytes=private.available_memory(),mem_free_bytes=private.free_memory()),
                  snapshot=base.snapshot(),inventory=net.inventory(),
                  qdisc=base.output('tc','qdisc','show','dev','eno17095np0'),host=private.host_state())
    stat=os.statvfs('/dev/shm');result['shm_used_bytes']=(stat.f_blocks-stat.f_bfree)*stat.f_frsize
    result['owned_paths']=[str(p) for p in Path('/dev/shm').glob('cpnetflux-s0-*')]
    result['temporary_listener']=base.output('ss','-H','-ltnp','sport = :49337')
    return result

def receipts(commit):
    local=receipt(commit)
    command=['python3',str(Path(__file__).resolve()),'--action','receipt','--commit',commit]
    result=subprocess.run(['ssh','-n','-o','BatchMode=yes','r770-2',shlex.join(command)],
                          text=True,capture_output=True,timeout=60)
    require(result.returncode==0,result.stderr)
    remote=json.loads(result.stdout)
    for key in ('commit','configuration','binaries','source_objects','project'):
        require(local[key]==remote[key],'build mismatch: '+key)
    for r in (local,remote):
        require(r['shm_used_bytes']==0 and r['owned_paths']==[] and r['temporary_listener']=='','owned resource not clear')
    return dict(local=local,remote=remote)

def recovery_receipt(path,builds):
    data=read(path)
    require(data['commit']==builds['local']['commit'],'recovery revision mismatch')
    require(data['configuration']==builds['local']['configuration'] and
            data['binaries']==builds['local']['binaries'],'recovery build mismatch')
    require(set(data['hosts'])=={'r770-1','r770-2'},'both recovery hosts required')
    for row in data['hosts'].values():
        require(row['exit_code']==0 and row['kernel_pacing']=='2000000000','candidate recovery missing')
        log=(path.parent/row['log']).resolve()
        require(path.parent.resolve() in log.parents,'recovery log outside evidence')
        require(digest(log)==row['sha256'],'recovery evidence changed')
        text=log.read_text()
        require('100% tests passed, 0 tests failed out of 2' in text,'recovery test failure')
        for name in ('cpnetflux_large_data_interrupted_resume_smoke','cpnetflux_large_data_receive_buffer_resume_smoke'):
            require(re.search(r'Test\s+#\d+: '+name+r'\s+\.+\s+Passed',text) is not None,'recovery case missing')
    return True

def load_row(folder,rep,arm,commit):
    rows=read(folder/'results.json');require(len(rows)==1,'exactly one transfer per namespace')
    row=rows[0];a=read(folder/'analysis.json');accepted=read(folder/'acceptance.json')
    profile='m2-'+arm;spec=private.profile_spec(profile)
    require(a['profile']==accepted['profile']==profile and accepted['valid_sample'] is True,'row not accepted')
    require(not a['failures'] and a['payload_bytes']==SIZE and a['m2_pass'] is False,'row failure or invalid standalone claim')
    ns=read(folder/'namespace.json');mount=read(folder/'mount-ready.json');host=read(folder/'host-after.json')
    require(ns['unmounted'] and ns['child']['reaped'] and ns['child']['exit_code']==0 and ns['remaining_names']==[],'namespace not drained')
    require(host['host_unchanged'] and host['payload_root_removed'],'host cleanup failed')
    require(mount['experiment_profile']==profile and mount['mount_limit_bytes']==spec['mount_limit_bytes'] and
            mount['huge_policy']==spec['huge_policy'] and mount['uid']==mount['gid']==1002,'mount profile mismatch')
    require(mount['setup_strategy']=='native_syscalls' and mount['supervisor_euid']==0,'native setup missing')
    require(ns['private_namespace']!=ns['host_namespace'],'private namespace missing')
    expected={k:row['expected'][k] for k in ('sha256','files','bytes')}
    require(row['actual_tree']==read(folder/'source-check.json')==expected,'content digest mismatch')
    require(row['result']=='pass' and row['output_slot_removed'],'complete successful task missing')
    single.validate_summary(row['summary'])
    source=read(folder/'source-residency.json');server=read(folder/'server.json')
    require(source['all_resident'] and source['bytes']==SIZE and source['pages']==source['resident_pages']==SIZE//4096,'source residency failed')
    require(single.resource_gate(row['resource'],server['resource'],source,
        min(read(folder/'preflight-r1.json')['mem_available_bytes'],read(folder/'preflight-r2.json')['mem_available_bytes']),
        row['after']['pswpout']-row['before']['pswpout']),'resource failed')
    for window in (row['receiver_network_window']['analysis'],row['receiver_network_window']['settled_analysis'],
                   read(folder/'broad-network.json')['analysis']):
        receive_gate(window);single.validate_receive_deltas(window)
    single.validate_netdev_deltas(large.network_counter_deltas(server['before']['net'],server['after']['net']))
    require(not server['sender_phases'].get('sender_pacing',[]),'application pacing enabled')
    kernel=server['sender_phases'].get('tcp_kernel_pacing',[])
    if arm=='candidate':single.validate_kernel_pacing_receipts(kernel)
    else:require(not kernel,'control ceiling enabled')
    pages=read(folder/'receiver-pages.json')
    require(pages['samples']>0 and pages['actual_uid']==1002,'page sample missing')
    require((pages['target_shmem_pmd_mapped_peak_bytes']>0) if arm=='candidate'
            else pages['target_shmem_pmd_mapped_peak_bytes']==0,'actual page type mismatch')
    require(single.verify_chunks(row['manifest_checks']['member_example'],SIZE)==8192,'verified manifest chunks mismatch')
    large.SIZE=SIZE
    mechanism=large.mapped_checks(SimpleNamespace(evidence=folder),'private58-'+arm+'-8g',262144,
                                   receiver_profile=False,range_timing=False)
    require(mechanism==row['mapped_mechanism'],'mapped mechanism receipt mismatch')
    matching=read(folder/'matching-builds.json')
    require(matching['local']==matching['remote'] and matching['local']['commit']==commit,'row build mismatch')
    parts=dict(setup=mount['target_setup_seconds'],directory=row['target_preparation_seconds'],client=row['t_ready_seconds'])
    total=sum(parts.values())
    require(math.isclose(total,a['complete_target_task_seconds'],abs_tol=1e-12,rel_tol=0),'reported denominator mismatch')
    require(math.isclose(expected['bytes']*8/total/1e9,a['complete_target_task_gbps'],abs_tol=1e-9,rel_tol=0),'reported rate mismatch')
    return dict(rep=rep,arm=arm,commit=commit,**expected,components=parts,
                complete_target_task_seconds=total,gates=accepted['gates'],verified_chunks=row['verified_chunks'],
                data_frames=row['mapped_mechanism']['actual_data_frames'],evidence=str(folder),
                client_resource=row['resource'],server_resource=server['resource'],pages=pages)

def matrix(args):
    require(base.node()==1 and os.geteuid()!=0,'matrix runs as Su on R770-1')
    require(args.enable_formal_m2,'explicit --enable-formal-m2 required')
    require(re.fullmatch('[0-9a-f]{40}',args.commit) is not None,'fixed SHA required')
    evidence=private.validate_evidence(args.evidence);evidence.mkdir(exist_ok=False)
    before=receipts(args.commit);base.save(evidence/'before.json',before)
    floor=private.profile_spec('m2-candidate')['preflight_mem_bytes']
    for r in before.values():
        require(r['snapshot']['mem_available_bytes']>=floor,'8GiB preflight memory floor')
        require(r['memory_headroom']['mem_free_bytes']>=private.profile_spec('m2-candidate')['preflight_free_bytes'],
                '8GiB physical free preflight')
    recovered=recovery_receipt(args.recovery_receipt,before)
    rows=[];attempted=[];failure=None;scene=False
    def one(rep,arm):
        builds=receipts(args.commit)
        for side in ('local','remote'):
            for key in ('snapshot','inventory','qdisc','host'):
                if key=='snapshot':
                    require(builds[side][key]['pswpout']==before[side][key]['pswpout'],'between-task swap-out')
                else:require(builds[side][key]==before[side][key],'between-task environment changed')
            require(builds[side]['snapshot']['mem_available_bytes']>=floor,'between-task headroom')
            require(builds[side]['memory_headroom']['mem_free_bytes']>=private.profile_spec('m2-candidate')['preflight_free_bytes'],
                    'between-task physical free headroom')
        folder=evidence/(str(rep)+'-'+arm)
        root=Path('/dev/shm')/('cpnetflux-s0-private55-m258-'+args.commit[:7]+'-'+str(rep)+'-'+arm)
        attempted.append(dict(rep=rep,arm=arm,evidence=str(folder)))
        base.save(evidence/'attempted.json',attempted)
        command=[sys.executable,str(Path(private.__file__).resolve()),'--enable-private-tmpfs','--native-setup',
                 '--profile','m2-'+arm,'--mode','v3','--commit',args.commit,
                 '--payload-root',str(root),'--evidence',str(folder)]
        if arm=='candidate':command.append('--kernel-tcp-pacing')
        result=private.run_child(command,evidence/(str(rep)+'-'+arm+'.launcher.log'),720,
                                 on_cancel=lambda:(folder/'cancel.request').touch(exist_ok=True))
        require(result['exit_code']==0 and result['reason']=='exit','sample launcher failed: '+str(folder))
        row=load_row(folder,rep,arm,args.commit)
        print(json.dumps(dict(rep=rep,arm=arm,complete_target_task_gbps=SIZE*8/row['complete_target_task_seconds']/1e9)),flush=True)
        return row
    def checkpoint(values):
        rows[:]=values;base.save(evidence/'results.json',rows)
    try:collect_rows(one,checkpoint)
    except BaseException as error:
        failure=dict(error=repr(error),completed_valid_tasks=len(rows),attempted_tasks=len(attempted))
        base.save(evidence/'failure.json',failure)
    finally:
        try:
            after=receipts(args.commit);base.save(evidence/'after.json',after)
            for side in ('local','remote'):
                for key in ('commit','configuration','binaries','source_objects','inventory','qdisc','host'):
                    require(before[side][key]==after[side][key],'final environment changed: '+key)
                require(after[side]['snapshot']['pswpout']==before[side]['snapshot']['pswpout'],'final host swap-out')
            scene=True
        except BaseException as error:
            base.save(evidence/'scene-failure.json',dict(error=repr(error)))
    if failure is None and len(rows)==10:
        report=evaluate(rows,recovery_gate=recovered,scene_gate=scene)
    else:report=dict(scope='formal M2 stopped at hard failure',m2_pass=False,attempted_tasks=len(attempted),
                     completed_valid_tasks=len(rows),failure=failure,scene_gate=scene)
    base.save(evidence/'analysis.json',report);print(json.dumps(report),flush=True)
    return 0 if report['m2_pass'] else 1

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action',choices=['receipt','matrix'],required=True)
    p.add_argument('--commit',required=True);p.add_argument('--evidence',type=Path)
    p.add_argument('--recovery-receipt',type=Path);p.add_argument('--enable-formal-m2',action='store_true')
    args=p.parse_args()
    if args.action=='receipt':print(json.dumps(receipt(args.commit)));return 0
    if args.evidence is None or args.recovery_receipt is None:p.error('matrix needs evidence and recovery receipt')
    return matrix(args)

if __name__=='__main__':raise SystemExit(main())
