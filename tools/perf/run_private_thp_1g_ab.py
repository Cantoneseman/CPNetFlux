#!/usr/bin/env python3
"""Run five interleaved 1 GiB buffered-mmap A/B pairs under private tmpfs policies."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

import private_tmpfs as private
import run_small_file_baseline as base

PROFILES = {'control': 'thp-1g-control', 'candidate': 'thp-1g-candidate'}
EXPECTED_ORDER = [
    (rep, arm)
    for rep in range(1, 6)
    for arm in (('control', 'candidate') if rep % 2 else ('candidate', 'control'))
]


def evaluate(rows):
    assert [(row['rep'], row['arm']) for row in rows] == EXPECTED_ORDER, 'five-pair AB/BA order mismatch'
    groups = {arm: [row for row in rows if row['arm'] == arm] for arm in PROFILES}
    result = {'scope': 'five-pair 1GiB complete task with per-arm private mount setup', 'arms': {}}
    for arm, values in groups.items():
        times = [row['complete_seconds'] for row in values]
        result['arms'][arm] = {
            'complete_seconds': times,
            'median_seconds': statistics.median(times),
            'median_gbps': 1024**3 * 8 / statistics.median(times) / 1e9,
            'cv': statistics.pstdev(times) / statistics.mean(times),
            'mount_setup_seconds': [row['mount_setup_seconds'] for row in values],
            'target_directory_prepare_seconds': [row['target_directory_prepare_seconds'] for row in values],
        }
    result['paired_improvements'] = [
        1 - next(row for row in rows if row['rep'] == rep and row['arm'] == 'candidate')['complete_seconds'] /
        next(row for row in rows if row['rep'] == rep and row['arm'] == 'control')['complete_seconds']
        for rep in range(1, 6)
    ]
    result['improved_pairs'] = sum(value > 0 for value in result['paired_improvements'])
    result['median_rate_change'] = (
        result['arms']['candidate']['median_gbps'] / result['arms']['control']['median_gbps'] - 1
    )
    result['all_samples_valid'] = all(row['valid'] for row in rows)
    result['performance_gate_pass'] = (
        result['all_samples_valid']
        and max(result['arms']['control']['cv'], result['arms']['candidate']['cv']) <= 0.05
        and result['improved_pairs'] >= 4
        and result['median_rate_change'] >= 0.10
    )
    result['gate'] = 'all correctness/network/cleanup gates; both CV <=5%; >=4/5 improved pairs; median gain >=10%'
    return result


def read_sample(evidence, rep, arm):
    profile = PROFILES[arm]
    folder = evidence / 'samples' / f'r{rep}-{arm}'
    analysis = json.loads((folder / 'analysis.json').read_text())
    acceptance = json.loads((folder / 'acceptance.json').read_text())
    namespace = json.loads((folder / 'namespace.json').read_text())
    mount = json.loads((folder / 'mount-ready.json').read_text())
    host_after = json.loads((folder / 'host-after.json').read_text())
    run = json.loads((folder / 'results.json').read_text())[0]
    source = run['expected']
    network = json.loads((folder / 'sample-network.json').read_text())
    profile_ok = (
        mount['experiment_profile'] == profile
        and mount['huge_policy'] == ('never' if arm == 'control' else 'within_size')
        and mount['mount_limit_bytes'] == 2 * 1024**3
        and ('huge=' + mount['huge_policy'] in mount['mount_options'] or
             (arm == 'control' and 'huge=' not in mount['mount_options']))
    )
    network_ok = all(
        value['rx_missed_errors_delta'] == 0 and value['rx_out_of_buffer_delta'] == 0
        for value in network['analysis'].values()
    )
    retrans_ok = all(0 <= value <= 2 for value in network['tcp_retransmission_deltas'].values())
    content_ok = (
        run['result'] == 'pass'
        and run['actual_tree'] == {key: source[key] for key in ('sha256', 'files', 'bytes')}
        and run['verified_chunks'] == 1024
        and source['bytes'] == 1024**3
        and source['files'] == 1
    )
    cleanup_ok = (
        all(acceptance['gates'].values())
        and acceptance.get('valid_sample') is True
        and not analysis['failures']
        and namespace['unmounted'] is True
        and namespace['remaining_names'] == []
        and host_after['host_unchanged'] is True
        and host_after['payload_root_removed'] is True
    )
    page_bytes = json.loads((folder / 'receiver-pages.json').read_text())[
        'target_shmem_pmd_mapped_peak_bytes'
    ]
    pages_ok = page_bytes == 0 if arm == 'control' else page_bytes > 0
    valid = all((profile_ok, network_ok, retrans_ok, content_ok, cleanup_ok, pages_ok))
    return {
        'rep': rep,
        'arm': arm,
        'valid': valid,
        'complete_seconds': analysis['complete_target_task_seconds'],
        'complete_gbps': analysis['complete_target_task_gbps'],
        'mount_setup_seconds': mount['target_setup_seconds'],
        'target_directory_prepare_seconds': analysis['target_directory_prepare_seconds'],
        'network_ok': network_ok,
        'tcp_retransmission_deltas': network['tcp_retransmission_deltas'],
        'content_ok': content_ok,
        'cleanup_ok': cleanup_ok,
        'page_policy_ok': pages_ok,
        'source_sha256': source['sha256'],
    }


def run_one(args, rep, arm):
    folder = args.evidence / 'samples' / f'r{rep}-{arm}'
    payload = Path('/dev/shm') / f'cpnetflux-s0-private55-thp1g-{args.run_id}-r{rep}-{arm}'
    profile = PROFILES[arm]
    command = [sys.executable, str(Path(__file__).with_name('private_tmpfs.py')),
               '--enable-private-tmpfs', '--native-setup', '--profile', profile,
               '--sender-pacing', '1500000000', '--payload-root', str(payload),
               '--evidence', str(folder), '--mode', 'v3', '--commit', args.commit]
    log = args.evidence / 'samples' / f'r{rep}-{arm}.launcher.log'
    with log.open('x', encoding='utf-8') as output:
        result = subprocess.run(command, stdout=output,
                                stderr=subprocess.STDOUT, check=False, timeout=720)
    if result.returncode:
        raise RuntimeError(f'{rep}-{arm} launcher exited {result.returncode}; see {log}')
    row = read_sample(args.evidence, rep, arm)
    if not row['valid']:
        raise RuntimeError(f'{rep}-{arm} sample failed a correctness, network, page, or cleanup gate')
    print(json.dumps({'rep': rep, 'arm': arm, 'gbps': row['complete_gbps'],
                      'seconds': row['complete_seconds'],
                      'tcp_retransmission_deltas': row['tcp_retransmission_deltas']}), flush=True)
    return row


def run(args):
    if os.geteuid() == 0 or base.node() != 1:
        raise ValueError('run the matrix as Su on R770-1')
    if len(args.run_id) > 24 or not args.run_id.isascii() or not args.run_id.replace('-', '').isalnum():
        raise ValueError('run id must contain at most 24 ASCII letters, digits, or hyphens')
    private.validate_evidence(args.evidence)
    args.evidence.mkdir(parents=True, exist_ok=False)
    (args.evidence / 'samples').mkdir()
    args.payload_root = Path('/dev/shm/cpnetflux-s0-private55-thp1g-receipt-check')
    local = base.receipt(args.commit)
    peer = base.peer(args, 'receipt')
    assert local == peer, 'fixed R770 build receipts differ'
    base.save(args.evidence / 'matching-builds.json', {'local': local, 'peer': peer})
    rows = []
    for rep, arm in EXPECTED_ORDER:
        rows.append(run_one(args, rep, arm))
        base.save(args.evidence / 'samples.json', rows)
    assert len({row['source_sha256'] for row in rows}) == 1, 'source SHA changed across samples'
    result = evaluate(rows)
    base.save(args.evidence / 'analysis.json', result)
    base.save(args.evidence / 'acceptance.json', {
        'all_samples_valid': result['all_samples_valid'],
        'performance_gate_pass': result['performance_gate_pass'],
        'cleanup_gate_pass': all(row['cleanup_ok'] for row in rows),
        'scope': result['scope'],
    })
    print(json.dumps(result), flush=True)
    return 0 if result['performance_gate_pass'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--run-id', required=True)
    args = parser.parse_args()
    raise SystemExit(run(args))


if __name__ == '__main__':
    main()
