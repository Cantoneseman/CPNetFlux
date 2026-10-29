#!/usr/bin/env python3
import json
import math
import os
import pathlib
import shutil
import statistics
import subprocess
import time

BASE = pathlib.Path('/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01')
PEER = 'Su@192.168.100.2'


def proxy_check(label):
    command = ['grid-proxy-info', '-file', str(BASE/'credentials/x509up_u1002'), '-timeleft']
    left = int(subprocess.check_output(command, text=True).strip())
    if left < 3600:
        raise RuntimeError('GSI proxy needs refresh before measurement')
    (BASE/'evidence'/f'gsi-seconds-{label}.txt').write_text(str(left)+'\n')


def run_group(tag, system, channels, datasets, reps, summary='on'):
    proxy_check(tag)
    env = dict(os.environ, CPNETFLUX_RAW_BASE=str(BASE), RUN_TAG=tag,
               SYSTEMS=system, CHANNELS=str(channels), PENDING='8',
               DATASETS=datasets, REPS=str(reps), SUMMARY_MODE=summary,
               FAST_POLICY='1')
    log = BASE/'logs'/f'{tag}-driver.log'
    started = time.time()
    print(json.dumps({'start_group':tag, 'channels':channels, 'epoch':started}), flush=True)
    with log.open('w') as handle:
        result = subprocess.run(['python3', str(BASE/'runner.py')], env=env,
                                stdout=handle, stderr=subprocess.STDOUT, timeout=7200)
    record = {'tag':tag, 'system':system, 'channels':channels, 'summary':summary,
              'started_epoch':started, 'finished_epoch':time.time(), 'exit':result.returncode}
    with (BASE/'group-index.jsonl').open('a') as handle:
        handle.write(json.dumps(record)+'\n')
    print(json.dumps(record), flush=True)
    if result.returncode:
        raise RuntimeError(f'Group failed: {tag}; see {log}')


def network_baseline():
    for streams in (1, 8):
        peer = subprocess.Popen(['ssh', '-o', 'BatchMode=yes', PEER,
                                 'iperf3 -s -1 -p 36666'],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            time.sleep(1)
            result = subprocess.run(['iperf3', '-c', '192.168.100.2', '-B', '192.168.100.1',
                                     '-p', '36666', '-P', str(streams), '-t', '5', '-J'],
                                    capture_output=True, text=True, timeout=30)
            (BASE/'evidence'/f'iperf3-p{streams}.json').write_text(result.stdout)
            output, _ = peer.communicate(timeout=15)
            (BASE/'evidence'/f'iperf3-server-p{streams}.txt').write_text(output)
            if result.returncode or peer.returncode:
                raise RuntimeError('iperf baseline failed')
            received = json.loads(result.stdout)['end']['sum_received']['bits_per_second']/1e9
            print(json.dumps({'iperf_streams':streams, 'receiver_gbps':received}), flush=True)
        finally:
            if peer.poll() is None:
                peer.terminate()
                peer.wait(timeout=10)


def select_lanes(system):
    candidates = []
    for path in sorted(BASE.glob(f'tuning-{system}-*-results.jsonl')):
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        rows = [row for row in rows if row['repetition'] > 0]
        if len(rows) != 2 or any(row['exit_code'] or not row['hash_match'] for row in rows):
            raise RuntimeError('invalid tuning group')
        lanes = rows[0]['profile']['channels']
        candidates.append({'channels':lanes, 'score_geomean_wall_s':math.exp(statistics.mean(
            math.log(row['wall_seconds']) for row in rows)),
            'times':{row['dataset']:row['wall_seconds'] for row in rows}})
    best = min(candidates, key=lambda row:row['score_geomean_wall_s'])
    return {'selected':best['channels'], 'selection':'lowest geometric mean of dense/random process wall',
            'candidates':candidates}


def main():
    marker = BASE/'group-index.jsonl'
    if marker.exists():
        raise RuntimeError('Refusing to overwrite an existing run')
    if shutil.disk_usage(BASE).free < 16*1024**3:
        raise RuntimeError('insufficient source space')
    proxy_check('before-network')
    network_baseline()
    for system, lanes in [('v3',(4,8,10)), ('gridftp',(4,8,10,16))]:
        for count in lanes:
            run_group(f'tuning-{system}-{count}', system, count, 'dense,large-random', 1)
    selection = {system:select_lanes(system) for system in ('v3','gridftp')}
    (BASE/'selection.json').write_text(json.dumps(selection, indent=2)+'\n')
    print(json.dumps({'selection':selection}), flush=True)
    datasets = 'dense,large-random,large-repeated,mixed'
    run_group('formal-v3', 'v3', selection['v3']['selected'], datasets, 3)
    run_group('formal-gridftp', 'gridftp', selection['gridftp']['selected'], datasets, 3)
    run_group('formal-v3-no-summary', 'v3', selection['v3']['selected'], datasets, 3, 'off')
    proxy_check('after-formal')
    print(json.dumps({'experiment':'pass', 'formal_samples':36, 'tuning_samples':14}), flush=True)


if __name__ == '__main__':
    main()
