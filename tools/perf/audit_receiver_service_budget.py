#!/usr/bin/env python3
"""Audit fixed historical receiver evidence; never collect new performance samples."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

BASE_REF = '025c38a99994dfe39bf3444c60faf77e44b1f933'
SOURCE_REF = '5664854dc268b151ade7adcd0aabb757e7b5c3c1'
HEADER = re.compile(r'\s(\d+)/(\d+)\s+\[(\d+)\]\s+([\d.]+):\s+(\d+)\s+cpu-clock:')
FRAME = re.compile(r'^\s*([0-9a-f]+)\s+(.+)\s+\(([^()]*)\)\s*$')
CRC = 'cpnetflux::checksum::crc32cCombine('


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024**2), b''):
            value.update(block)
    return value.hexdigest()


def parse_perf(text):
    periods = Counter()
    callers = Counter()
    count = total = ancestor_count = 0
    for block in text.strip().split('\n\n'):
        if not block.strip():
            continue
        lines = block.splitlines()
        header = HEADER.search(lines[0])
        require(header is not None, 'unrecognized perf sample header')
        period = int(header[5])
        require(period > 0, 'nonpositive perf period')
        frames = []
        for line in lines[1:]:
            if not line.strip():
                continue
            match = FRAME.fullmatch(line)
            require(match is not None, 'unrecognized perf frame')
            symbol = re.sub(r'\+0x[0-9a-f]+(?:/0x[0-9a-f]+)?$', '', match[2])
            frames.append((symbol, match[3]))
        physical = [frame for frame in frames if frame[1] != 'inlined']
        require(physical, 'sample has no physical frame')
        leaf = physical[0]
        periods[leaf] += period
        count += 1
        total += period
        if any(CRC in symbol for symbol, _ in frames):
            ancestor_count += 1
        if CRC in leaf[0]:
            # A missing caller is unknown, never evidence of an unlocked call.
            visible = {name for name in ('RangeFileCoordinator::', 'PersistentDataSession::')
                       if any(name in symbol for symbol, _ in physical[1:])}
            callers[next(iter(visible)) if len(visible) == 1 else
                    ('ambiguous' if visible else 'other_or_unwound')] += period
    require(count > 0, 'empty perf input')
    return {'samples': count, 'period_total': total, 'leaf_periods': periods,
            'crc_self_callers_period': dict(callers), 'crc_ancestor_samples': ancestor_count}


def check_profile(parsed, receipt):
    require(parsed['samples'] == receipt['samples'], 'sample total mismatch')
    require(parsed['period_total'] == receipt['period_total'], 'period total mismatch')
    require(receipt['lost_samples'] == 0, 'lost perf samples')
    expected = [row for row in receipt['flat_self_top'] if CRC in row['symbol']]
    require(len(expected) == 1, 'missing/ambiguous reference CRC Self')
    percent = 100 * sum(period for (symbol, _), period in parsed['leaf_periods'].items()
                        if CRC in symbol) / parsed['period_total']
    require(abs(percent - expected[0]['period_percent']) < 1e-9, 'CRC Self mismatch')
    return percent


def self_test():
    def sample(frames, period=7):
        return ('worker 10/11 [002] 1.000: ' + str(period) + ' cpu-clock:\n' +
                '\n'.join('  ab ' + symbol + ' (' + dso + ')' for symbol, dso in frames))
    crc = CRC + 'unsigned int, unsigned int, unsigned long)'
    inline = ('matrixTimes', 'inlined')
    a = sample([inline, (crc + '+0x12/0x90', '/binary')])
    b = sample([('handle_irq', '[kernel.kallsyms]'), (crc, '/binary')], 3)
    parsed = parse_perf(a + '\n\n' + b)
    require(parsed['samples'] == 2 and parsed['period_total'] == 10, 'period control')
    require(parsed['crc_ancestor_samples'] == 2, 'ancestor control')
    require(parsed['crc_self_callers_period'] == {'other_or_unwound': 7}, 'inline/IRQ control')
    receipt = {'samples': 2, 'period_total': 10, 'lost_samples': 0,
               'flat_self_top': [{'symbol': crc, 'period_percent': 70}]}
    require(check_profile(parsed, receipt) == 70, 'known Self control')
    caller = parse_perf(sample([(crc, '/binary'), ('RangeFileCoordinator::write', '/binary')]))
    require(caller['crc_self_callers_period'] == {'RangeFileCoordinator::': 7}, 'caller control')
    rejections = [lambda: parse_perf(''), lambda: parse_perf('malformed\n  ab f (/binary)'),
                  lambda: parse_perf(sample([inline])),
                  lambda: parse_perf(sample([('f', '/binary')], 0)),
                  lambda: parse_perf(a.replace('  ab ', '  invalid ', 1)),
                  lambda: check_profile(parsed, dict(receipt, samples=3)),
                  lambda: check_profile(parsed, dict(receipt, period_total=11)),
                  lambda: check_profile(parsed, dict(receipt, lost_samples=1)),
                  lambda: check_profile(parsed, dict(receipt, flat_self_top=[
                      {'symbol': crc, 'period_percent': 100}]))]
    for reject in rejections:
        try:
            reject()
        except ValueError:
            continue
        raise ValueError('negative control was accepted')
    return {'pass': True, 'positive_controls': 5, 'negative_controls': len(rejections)}


def audit(repo, root):
    inputs = []

    def load(relative):
        path = root / relative
        data = path.read_bytes()
        inputs.append({'path': str(path), 'bytes': len(data), 'sha256': digest(data)})
        return json.loads(data)

    prefix = '2026-11-09-v3-kernel-service-17/'
    full = load(prefix + 'analysis.json')
    require(full['low_overhead_gate_pass'] and full['integrity_resource_pass'], 'invalid full-task contract')
    receipt = load(prefix + 'profiles-R770-1.json')
    require(receipt['decoded_on_capture_host'] and receipt['capture_interface_address'] == '192.168.100.1',
            'profile host/decoder contract')
    profiles = []
    require(len(receipt['profiles']) == 3, 'expected three receiver profiles')
    for row in receipt['profiles']:
        path = Path(row['path'])
        require(path.is_relative_to(root / prefix), 'profile outside evidence scope')
        raw_hash = file_digest(path)
        require(raw_hash == row['data_sha256'], 'raw profile digest mismatch')
        inputs.append({'path': str(path), 'bytes': path.stat().st_size, 'sha256': raw_hash})
        script = Path(str(path) + '.script.txt')
        data = script.read_bytes()
        inputs.append({'path': str(script), 'bytes': len(data), 'sha256': digest(data)})
        parsed = parse_perf(data.decode())
        percent = check_profile(parsed, row)
        profiles.append({'path': str(path), 'samples': parsed['samples'],
                         'period_total': parsed['period_total'], 'crc_self_percent': percent,
                         'crc_self_callers_period': parsed['crc_self_callers_period'],
                         'crc_ancestor_samples': parsed['crc_ancestor_samples'],
                         'exclusive_categories': row['exclusive_categories']})

    metrics = sorted((root / prefix / 'metrics').glob('*.verified.json'))
    require(len(metrics) == 6, 'expected six full tasks')
    for path in metrics:
        row = load(path.relative_to(root).as_posix())
        require(row['commit'] == BASE_REF and row['result'] == 'pass' and row['terminals_pass']
                and row['sampling_contract_pass'] and row['fresh_output_removed']
                and row['bytes'] == 8 * 1024**3 and row['checkpoint_verified_chunks'] == 8192
                and row['output_residency_before_sha']['all_resident']
                and row['sender_io_delta']['read_bytes'] == 0
                and all(guard['pass'] for guard in row['memory_guards'].values()),
                'full-task metric acceptance mismatch: ' + path.name)

    sources = []
    spans = []
    paths = ['src/checksum/crc32c.cpp', 'src/checksum/checksum.cpp',
             'src/core/io/work_executor.cpp', 'src/core/io/persistent_data_session.cpp',
             'src/core/io/dynamic_file_queue.cpp', 'src/core/io/persistent_tree_transfer.cpp',
             'src/storage/mapped_file_window.cpp', 'include/cpnetflux/core/io/persistent_data_session.h']
    for path in paths:
        before, after = [subprocess.check_output(['git', '-C', str(repo), 'show', ref + ':' + path])
                         for ref in (BASE_REF, SOURCE_REF)]
        sources.append({'path': path, 'base_sha256': digest(before), 'source_sha256': digest(after),
                        'equal': before == after})
        boundaries = None
        if path.endswith('persistent_tree_transfer.cpp'):
            boundaries = ('    static Status write(const std::shared_ptr<Entry>& entry',
                          '    static std::pair<std::uint64_t, std::uint64_t> takeStats(')
        elif path.endswith('dynamic_file_queue.cpp'):
            boundaries = ('common::Status DynamicFileQueue::startIo(',
                          'common::Status DynamicFileQueue::awaitResult(')
        if boundaries:
            blocks = []
            for text in (before.decode(), after.decode()):
                require(all(text.count(boundary) == 1 for boundary in boundaries), 'source span ambiguous')
                start = text.index(boundaries[0])
                end = text.index(boundaries[1], start)
                blocks.append(text[start:end].encode())
            spans.append({'path': path, 'start_literal': boundaries[0], 'end_literal': boundaries[1],
                          'base_sha256': digest(blocks[0]), 'source_sha256': digest(blocks[1]),
                          'equal': blocks[0] == blocks[1]})
        if path.endswith('persistent_data_session.h'):
            require(b'kMaxPayload = 65536;' in after and b'chunkSize = 1048576;' in after,
                    'logical work assumptions changed')
    require(all(row['equal'] for row in sources[:4]) and all(row['equal'] for row in spans),
            'reviewed receiver hot paths changed; audit requires a new source review')

    byte_work = load('2026-11-09-v3-byte-work-18/analysis.json')
    crc = load('2026-11-09-v3-byte-work-18/representative-crc/analysis.json')
    mapped = load('2026-11-09-v3-mapped-cost-14/mapped-analysis.json')
    collapse = load('2026-11-09-v3-mapped-collapse-19/summary.json')
    ownership = load('2026-11-10-v3-tcp-buffer-ownership-32/analysis.json')
    varied = [row['diagnostic']['elapsed_seconds'] / row['diagnostic']['iterations']
              for row in crc['rows'] if row['name'].startswith('varied-65532-')]
    require(len(varied) == 3 and collapse['local_gate_pass'] is False, 'diagnostic contract mismatch')
    chunks = 1024
    full_frames, tail = divmod(1024**2, 65532)
    frame_count = (full_frames + bool(tail)) * chunks
    return {'schema': 'cpnetflux_receiver_budget_audit_v1', 'source_ref': SOURCE_REF,
            'base_ref': BASE_REF, 'self_test': self_test(), 'inputs': inputs,
            'receiver_profiles': profiles, 'verified_full_tasks': len(metrics),
            'full_task_groups': full['groups'], 'network_events_still_open': full['network_events'],
            'source_files': sources, 'source_spans': spans,
            'logical_work_per_GiB': {'chunk_bytes': 1024**2, 'max_data_bytes': 65532,
                'chunks': chunks, 'frames': frame_count, 'tail_data_bytes_per_chunk': tail,
                'receiver_payload_crc_scan_bytes': 1024**3,
                'receiver_session_plus_chunk_combine_calls': 2 * frame_count,
                'receiver_matrix_applications_two_accumulators':
                    2 * chunks * (full_frames * (65532).bit_count() + tail.bit_count()),
                'steady_chunk_tail_to_full_resize_value_init_bytes': (65532 - tail) * chunks,
                'scope': 'logical source work; excludes control/boundary frames and first allocation; not measured DRAM traffic'},
            'distinct_scope_diagnostics': {
                'mapped14_profile_on': {k: mapped['groups']['1'][k] for k in
                                       ('copy_cpu_seconds', 'map_cpu_seconds', 'unmap_cpu_seconds')},
                'mapped18_summary': byte_work['summary'],
                'crc18_varied_single_chunk_accumulator_cpu_seconds_per_GiB': varied,
                'collapse19': {k: collapse[k] for k in ('ordinary', 'collapse', 'local_gate_pass')},
                'ownership32_receiver': {k: ownership[k] for k in ('copied', 'zerocopy')},
                'scope': 'different diagnostic/instrumentation contracts; CPU sums are not full-task wall savings'},
            'decision': {'budget_established': False, 'conservative_task_gain_percent': None,
                'accepted_mechanisms': [], 'runtime_change_authorized_by_this_audit': False,
                'reason': 'CRC lock caller/overlap budget remains unknown; no reviewed mechanism establishes >=10% removable complete-task time.',
                'not_a_global_performance_ceiling': True}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--evidence-root', type=Path, default=Path('/home/Su/experiments/CPNetFlux'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.self_test:
        print(json.dumps(self_test(), indent=2))
        return
    require(args.output is not None, '--output is required')
    root = args.evidence_root.resolve()
    output = args.output.resolve()
    require(output.parent == root / '2026-11-10-v3-receiver-service-budget-33',
            'output must be in the new budget33 evidence directory')
    require(not output.exists(), 'refusing to overwrite prior audit output')
    result = audit(args.repo, root)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps({'output': str(output), 'profiles': len(result['receiver_profiles']),
                      'full_tasks': result['verified_full_tasks'], 'decision': result['decision']}, indent=2))


if __name__ == '__main__':
    main()
