#!/usr/bin/env python3
"""Summarize this bounded lab run; concurrent stage sums are not wall time."""
import argparse
import csv
import datetime as dt
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from range_observation import export_range_observations


def percentile(values, q):
    if not values: return None
    ordered=sorted(values); position=(len(ordered)-1)*q
    lo=int(position); hi=math.ceil(position)
    return ordered[lo]+(ordered[hi]-ordered[lo])*(position-lo)


def describe(values):
    values=[v for v in values if isinstance(v,(int,float))]
    if not values: return {'count':0,'sum':None,'p50':None,'p95':None,'max':None}
    return {'count':len(values),'sum':sum(values),'p50':statistics.median(values),
            'p95':percentile(values,.95),'max':max(values)}


def union_seconds(intervals):
    end=None; total=0
    for begin,finish in sorted(intervals):
        if finish<begin: raise ValueError('negative interval')
        if end is None or begin>end: total+=finish-begin
        elif finish>end: total+=finish-end
        end=finish if end is None else max(end,finish)
    return total


def grid_records(path):
    groups=defaultdict(list)
    if not path.exists(): return groups
    for line in path.read_text().splitlines():
        record=dict(re.findall(r'(\S+)=([^ ]+)',line))
        if 'FILE' not in record or '/v3-raw/' not in record['FILE']: continue
        name=record['FILE'].split('/v3-raw/',1)[1].split('/',1)[0]
        def clock(key): return dt.datetime.strptime(record[key],'%Y%m%d%H%M%S.%f').replace(tzinfo=dt.timezone.utc).timestamp()
        groups[name].append({'start':clock('START'),'end':clock('DATE'),
            'bytes':int(record['NBYTES']),'code':int(record['CODE']), 'streams':int(record['STREAMS'])})
    return groups


def sample_metrics(path):
    if not path.exists(): return {'samples':0}
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    if not rows: return {'samples':0}
    first,last=rows[0],rows[-1]; duration=last['monotonic']-first['monotonic']
    net={k:last['net'][k]-first['net'][k] for k in first['net']}
    peak_fd=0; peak_rss=0; proc_cpu=defaultdict(list); io=defaultdict(list)
    for row in rows:
        for p in row['processes']:
            if p['comm']=='cpnetflux-deskt': continue
            fields=p['stat'].split(')',1)[1].split()
            peak_fd=max(peak_fd,p['fd_count']);peak_rss=max(peak_rss,int(fields[21])*4096)
            proc_cpu[p['pid']].append((int(fields[11])+int(fields[12]))/100)
            io[p['pid']].append(dict((k,int(v))for k,v in re.findall(r'(\w+): (\d+)',p['io'])))
    cpu_lower_bound=sum(max(v)-min(v) for v in proc_cpu.values())
    read_bytes=sum(max(x['read_bytes'] for x in v)-min(x['read_bytes'] for x in v)for v in io.values())
    write_bytes=sum(max(x['write_bytes'] for x in v)-min(x['write_bytes'] for x in v)for v in io.values())
    a=list(map(int,first['cpu'].split()[1:]));b=list(map(int,last['cpu'].split()[1:]));delta=[y-x for x,y in zip(a,b)]
    # guest and guest_nice are already included in user and nice.
    denominator=sum(delta[:8])
    return {'samples':len(rows),'sample_span_seconds':duration,'net_delta':net,
        'nic_tx_gbps':net['tx_bytes']*8/duration/1e9 if duration else None,
        'nic_rx_gbps':net['rx_bytes']*8/duration/1e9 if duration else None,
        'process_cpu_sampled_lower_bound_seconds':cpu_lower_bound,
        'process_peak_fd':peak_fd,'process_peak_rss_bytes':peak_rss,
        'physical_read_bytes_sampled':read_bytes,'physical_write_bytes_sampled':write_bytes,
        'system_busy_fraction':1-(delta[3]+delta[4])/denominator if denominator else None,
        'system_iowait_fraction':delta[4]/denominator if denominator else None}


def range_observation_summary(export, server_log):
    source_name=f"logs/{Path(server_log).name}"
    source=next((row for row in export.get("source_logs",[]) if row.get("name")==source_name),None)
    rows=[row for row in export.get("observations",[]) if row.get("source_log")==source_name]
    counts={}
    for row in rows:
        status=row.get("observation_status","missing")
        counts[status]=counts.get(status,0)+1
    if not rows or source is None or source.get("status")=="missing":
        status="missing"
    elif (counts.get("enabled")==len(rows) and
          source.get("malformed_records",0)==0):
        status="complete"
    else:
        status="partial"
    return {"status":status,"records":len(rows),"counts":counts}


def analyze(root):
    root=Path(root); peer=root/'peer'; grid=grid_records(peer/'gridftp/transfer.log')
    output=[]; stages=[]; diagnostics=[]
    server_logs=sorted((root/'logs').glob('*-v3-server.log')) if (root/'logs').exists() else []
    range_export=export_range_observations(server_logs, root)
    for filename in sorted(root.glob('formal*-results.jsonl')):
        group=filename.stem.removesuffix('-results')
        receiver_batches=defaultdict(list)
        server_log=root/'logs'/f'{group}-v3-server.log'
        if server_log.exists():
            for line in server_log.read_text().splitlines():
                marker='CPNETFLUX_PERSISTENT_TIMING '
                if marker not in line: continue
                try: batch=json.loads(line.split(marker,1)[1])
                except json.JSONDecodeError: continue
                receiver_batches[batch['batch_id']].extend(batch['files'])
        receivers=iter(receiver_batches.values())
        for line in filename.read_text().splitlines():
            row=json.loads(line)
            if row['repetition']==0: continue
            summary=row.get('summary',{})
            if row['exit_code'] or not row['hash_match']: raise ValueError('invalid formal row')
            name=group+'-'+row['dataset']+'-r'+str(row['repetition'])
            prefix='v3' if row['system']=='cpnetflux-v3' else 'gridftp'
            core=summary.get('elapsed_seconds')
            item={'group':group,'dataset':row['dataset'],'rep':row['repetition'],
                'files':row['expected']['file_count'],'bytes':row['expected']['bytes'],
                'wall_s':row['wall_seconds'],'core_s':core,
                'gbps':row['expected']['bytes']*8/row['wall_seconds']/1e9,
                'client_user_s':row['client_cpu']['user_seconds'],
                'client_system_s':row['client_cpu']['system_seconds'],
                'client_max_rss_kib':row['client_cpu']['max_rss_kib'],
                'external_sha256_s':row.get('verification_wall_seconds'),
                'post_core_s':row['wall_seconds']-core if core else None,'hash_match':True}
            output.append(item)
            detail={'name':name,'summary_scope':summary.get('phase_timing_scope'),
                    'local':sample_metrics(root/'metrics'/f'{prefix}-{name}.local.jsonl'),
                    'peer':sample_metrics(peer/'metrics'/f'{prefix}-{name}.peer.jsonl')}
            if prefix=='v3':
                received=next(receivers,[])
                detail['receiver_timing_records']=len(received)
                range_summary=range_observation_summary(range_export,server_log.name)
                detail['range_observation_status']=range_summary['status']
                detail['receiver_range_observation_records']=range_summary['records']
                detail['receiver_range_observation_counts']=range_summary['counts']
                detail['receiver_stages']={k:describe([f.get(k)for f in received])for k in (
                    'write_seconds','checksum_seconds','manifest_seconds','finalize_seconds','payload_io_seconds','file_result_seconds','wall_seconds')}
            if prefix=='v3' and row['profile']['summary_mode']=='on':
                assert summary['actual_transport']=='v3' and summary['actual_mode']=='persistent_tree'
                assert summary['compression_attempts']==0 and summary['compressed_frames']==0
                assert summary['control_tls_verified'] and summary['data_tls_verified']
                assert summary['data_connect_count']==row['profile']['channels']
                detail['sender_stages']={}
                for key in ('queue_wait_seconds','first_payload_seconds','read_seconds','payload_io_seconds',
                            'file_result_seconds','manifest_seconds','checksum_seconds','wall_seconds'):
                    values=describe([f.get(key)for f in summary['file_timings']])
                    detail['sender_stages'][key]=values
                    stages.append({'name':name,'stage':key,**values})
                detail['task_stages']={k:summary.get(k)for k in ('scan_seconds','control_prepare_seconds',
                    'data_connect_seconds','queue_wait_seconds','transfer_complete_wait_seconds','elapsed_seconds')}
                detail['range_count']=sum(summary.get('persistent_lane_range_completed',[]))
                detail['pipeline']={k:v for k,v in summary.items()if 'pipeline_' in k or 'pending_' in k}
            if prefix=='gridftp':
                records=grid.get(name,[])
                if len(records)!=item['files'] or sum(r['bytes']for r in records)!=item['bytes']:
                    raise ValueError(f'GridFTP log coverage mismatch: {name}, {len(records)}')
                if any(r['code']!=226 for r in records): raise ValueError('failed GridFTP file')
                intervals=[(r['start'],r['end'])for r in records]
                detail['gridftp_file_interval_union_s']=union_seconds(intervals)
                detail['gridftp_file_span_s']=max(b for a,b in intervals)-min(a for a,b in intervals)
                detail['gridftp_file_durations']=describe([b-a for a,b in intervals])
                detail['gridftp_non_file_interval_s']=row['wall_seconds']-detail['gridftp_file_interval_union_s']
            diagnostics.append(detail)
    if len(output)!=36: raise ValueError(f'expected 36 formal rows, got {len(output)}')
    aggregated=[]
    for key in sorted({(r['group'],r['dataset'])for r in output}):
        rows=[r for r in output if (r['group'],r['dataset'])==key]
        if len(rows)!=3: raise ValueError('expected three repetitions')
        aggregated.append({'group':key[0],'dataset':key[1],
            **{k:statistics.median([r[k]for r in rows]) if rows[0][k]is not None else None
               for k in ('files','bytes','wall_s','core_s','gbps','post_core_s','client_user_s','client_system_s','external_sha256_s')},
            'wall_min_s':min(r['wall_s']for r in rows),'wall_max_s':max(r['wall_s']for r in rows)})
    return output,stages,diagnostics,aggregated


def main():
    parser=argparse.ArgumentParser();parser.add_argument('root');args=parser.parse_args()
    root=Path(args.root);output,stages,diagnostics,aggregated=analyze(root)
    for name,rows in [('cases.csv',output),('sender-stages.csv',stages),('medians.csv',aggregated)]:
        with (root/name).open('w',newline='')as f:
            writer=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');writer.writeheader();writer.writerows(rows)
    (root/'diagnostics.json').write_text(json.dumps(diagnostics,indent=2))
    print(json.dumps(aggregated,indent=2))


if __name__=='__main__':main()
