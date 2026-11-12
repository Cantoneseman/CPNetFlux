#!/usr/bin/env python3
import csv
import hashlib
import json
import pathlib
import statistics
import sys

BASE = pathlib.Path('/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01')
LAB = BASE/'lab'
OLD = pathlib.Path('/root/projects/CPNetFlux/docs/experiments/2026-10-24-v3-gridftp-optimized-rerun-01')


def write_csv(name, rows):
    with (LAB/name).open('w',newline='') as handle:
        writer = csv.DictWriter(handle,fieldnames=list(rows[0]),lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)


manifest = json.loads((LAB/'evidence-file-manifest.json').read_text())
for item in manifest:
    path = LAB/item['path']
    if not path.resolve().is_relative_to(LAB.resolve()):
        raise RuntimeError('Invalid evidence path')
    data = path.read_bytes()
    assert len(data) == item['bytes'] and hashlib.sha256(data).hexdigest() == item['sha256']
assert json.loads((LAB/'payload-manifest.json').read_text()) == json.loads((OLD/'payload-manifest.json').read_text())
formal = [json.loads(line) for file in sorted(LAB.glob('formal*-results.jsonl'))
          for line in file.read_text().splitlines() if json.loads(line)['repetition'] > 0]
tuning = [json.loads(line) for file in sorted(LAB.glob('tuning*-results.jsonl'))
          for line in file.read_text().splitlines() if json.loads(line)['repetition'] > 0]
assert len(formal) == 36 and len(tuning) == 14
for row in formal+tuning:
    assert row['exit_code'] == 0 and row['hash_match'] and row['observed'] == row['expected']
    for end in ('local','peer'):
        for key in ('rx_errors','tx_errors','rx_dropped','tx_dropped'):
            assert row[end+'_after']['counters'][key] == row[end+'_before']['counters'][key]
for row in formal:
    if row['system'] == 'cpnetflux-v3' and row['profile']['summary_mode'] == 'on':
        summary = row['summary']
        assert summary['actual_file_scheduling'] == 'dynamic' and not summary['fallback_reason']
        assert summary['control_reuse_mode'] == 'worker' and summary['data_session_reuse_mode'] == 'tree'
        assert summary['control_connect_count'] == 4 and summary['data_connect_count'] == 4
        assert summary['summary_tree_hash_backend'] == 'hardware'
        assert summary['summary_tree_hash_source'] == 'verified_chunks'
        assert summary['tree_manifest_generation'] == summary['tree_manifest_persisted_generation']
write_csv('tuning.csv',[
    {'system':row['system'],'dataset':row['dataset'],'channels':row['profile']['channels'],
     'wall_s':row['wall_seconds'],'gbps':row['expected']['bytes']*8/row['wall_seconds']/1e9,
     'hash_match':row['hash_match']} for row in tuning])
medians = list(csv.DictReader((LAB/'medians.csv').open()))
prior = {row['dataset']:row for row in csv.DictReader((OLD/'comparison.csv').open())}
comparison = []
for dataset in ('dense','large-random','large-repeated','mixed'):
    current = {row['group']:row for row in medians if row['dataset'] == dataset}
    on = current['formal-v3']; off = current['formal-v3-no-summary']; grid = current['formal-gridftp']
    comparison.append({'dataset':dataset,'v3_summary_s':float(on['wall_s']),
        'v3_no_summary_s':float(off['wall_s']),'gridftp_s':float(grid['wall_s']),
        'v3_summary_gbps':float(on['gbps']),'v3_no_summary_gbps':float(off['gbps']),
        'gridftp_gbps':float(grid['gbps']),
        'v3_summary_over_gridftp_time':float(on['wall_s'])/float(grid['wall_s']),
        'v3_no_summary_over_gridftp_time':float(off['wall_s'])/float(grid['wall_s']),
        'old_v3_summary_s':float(prior[dataset]['v3_summary_s']),
        'old_v3_no_summary_s':float(prior[dataset]['v3_no_summary_s']),
        'old_over_new_v3_summary':float(prior[dataset]['v3_summary_s'])/float(on['wall_s']),
        'old_over_new_v3_no_summary':float(prior[dataset]['v3_no_summary_s'])/float(off['wall_s'])})
write_csv('comparison.csv',comparison)
diagnostics = json.loads((LAB/'diagnostics.json').read_text())
resource_rows = []
for group in ('formal-v3','formal-gridftp','formal-v3-no-summary'):
    for dataset in ('dense','large-random','large-repeated','mixed'):
        detail = [row for row in diagnostics if row['name'].startswith(group+'-'+dataset+'-r')]
        item = {'group':group,'dataset':dataset}
        for end in ('local','peer'):
            for key in ('process_cpu_sampled_lower_bound_seconds','process_peak_fd',
                        'process_peak_rss_bytes','physical_read_bytes_sampled',
                        'physical_write_bytes_sampled','system_iowait_fraction',
                        'system_busy_fraction','nic_tx_gbps','nic_rx_gbps'):
                values = [row[end].get(key) for row in detail]
                item[end+'_'+key] = statistics.median(values) if all(isinstance(value,(int,float)) for value in values) else None
        item['range_count'] = statistics.median([row['range_count'] for row in detail]) if all('range_count' in row for row in detail) else None
        resource_rows.append(item)
write_csv('resource-medians.csv',resource_rows)
pipeline_rows = []
for dataset in ('dense','large-random','large-repeated','mixed'):
    rows = [row['summary'] for row in formal if row['system'] == 'cpnetflux-v3'
            and row['dataset'] == dataset and row['profile']['summary_mode'] == 'on']
    item = {'dataset':dataset}
    for key in ('payload_pipeline_budget_bytes','payload_pipeline_high_watermark_bytes',
                'payload_pipeline_producer_waits','payload_pipeline_consumer_waits',
                'data_pending_high_watermark','tree_manifest_snapshots',
                'tree_manifest_write_seconds','tree_manifest_state_lock_wait_seconds'):
        item[key] = statistics.median([row[key] for row in rows])
    pipeline_rows.append(item)
write_csv('pipeline-medians.csv',pipeline_rows)
stage_rows = []
for dataset in ('dense','large-random','large-repeated','mixed'):
    rows = [row for row in formal if row['system'] == 'cpnetflux-v3' and row['dataset'] == dataset
            and row['profile']['summary_mode'] == 'on']
    detail = [row for row in diagnostics if row['name'].startswith('formal-v3-'+dataset+'-r')]
    stage = {'dataset':dataset}
    for key in ('scan_seconds','control_prepare_seconds','data_connect_seconds',
                'queue_wait_seconds','transfer_complete_wait_seconds','elapsed_seconds'):
        values = [row['summary'].get(key) for row in rows]
        stage[key] = statistics.median(values) if all(isinstance(value,(int,float)) for value in values) else None
    for key in ('read_seconds','payload_io_seconds','file_result_seconds','manifest_seconds','checksum_seconds'):
        values = [item['sender_stages'][key]['sum'] for item in detail]
        stage['sender_'+key] = statistics.median(values) if all(value is not None for value in values) else None
    for key in ('write_seconds','checksum_seconds','manifest_seconds','finalize_seconds','payload_io_seconds'):
        values = [item['receiver_stages'][key]['sum'] for item in detail]
        stage['receiver_'+key] = statistics.median(values) if all(value is not None for value in values) else None
    for key in rows[0]['summary']:
        if key.startswith('summary_tree_hash'):
            values = [row['summary'][key] for row in rows]
            stage[key] = statistics.median(values) if all(isinstance(value,(int,float)) for value in values) else str(values[0])
    stage_rows.append(stage)
write_csv('stage-medians.csv',stage_rows)
selected = [{key:value for key,value in row['summary'].items()
             if not isinstance(value,(list,dict))} | {'dataset':row['dataset'],'rep':row['repetition']}
            for row in formal if row['system'] == 'cpnetflux-v3' and row['profile']['summary_mode'] == 'on']
(LAB/'v3-observed-summary-scalars.json').write_text(json.dumps(selected,indent=2)+'\n')
audit = {'formal_pass':36,'tuning_pass':14,'input_matches_20261024':True,
         'evidence_files_verified':len(manifest),'formal_and_tuning_nic_error_drop_delta':0,
         'source_sha256_excluded_from_wall':True,'destination_sha256_excluded_from_wall':True,
         'transport':'TCP/TLS over Ethernet','native_verbs':False,'numa_pinning':False}
(LAB/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps({'audit':audit,'comparison':comparison,'stages':stage_rows},indent=2))
