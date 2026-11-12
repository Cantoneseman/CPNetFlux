#!/usr/bin/env python3
"""Check descriptor ownership, mapped bytes/window transitions and child faults."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

from run_cpnetflux_offset_write_bench_smoke import expected

MIB = 1024 ** 2


def success(binary, root, layout, workers, size, backend, frame=65532, chunk=1048576, mode='sparse', populate='lazy'):
    path = root / f'{layout}-{backend}-{workers}-{size}-{chunk}'
    command = [str(binary), str(path), layout, str(workers), str(size), mode,
               str(frame), str(chunk)]
    if backend == 'mmap':
        command.append('--backend=mmap')
    if populate != 'lazy':
        command.append('--populate=' + populate)
    command.append('--keep-files')
    completed = subprocess.run(command, capture_output=True, text=True, timeout=30)
    assert completed.returncode == 0, completed.stderr
    row = json.loads(completed.stdout)
    assert row['schema_version'] == 3 and row['backend'] == backend
    assert row['file_count'] == row['write_inode_count'] == 1
    assert row['write_fd_count'] == (workers if layout == 'shared-open' else 1)
    assert row['initial_file_bytes'] == size
    assert row['prefill_bytes'] == (size if mode == 'rewrite' else 0)
    if mode == 'rewrite':
        assert row['initial_allocated_bytes'] >= size
    else:
        assert row['initial_allocated_bytes'] == 0
    assert row['diagnostic_only'] and not row['network'] and not row['v3']
    assert not row['durability'] and row['written_bytes'] == row['verified_bytes'] == size
    assert row['elapsed_seconds'] > 0 and 0 <= row['task_system_cpu_seconds'] <= row['task_cpu_seconds'] + 1e-6
    required = ('write_thread_minor_faults', 'write_thread_major_faults', 'prefill_seconds',
                'prefill_cpu_seconds', 'prefill_system_cpu_seconds', 'prefill_minor_faults',
                'prefill_major_faults')
    assert all(key in row for key in required), 'missing write/prefill attribution'
    assert all(row[key] >= 0 for key in required)
    if mode == 'rewrite':
        assert row['prefill_seconds'] > 0
        assert row['prefill_system_cpu_seconds'] <= row['prefill_cpu_seconds'] + 1e-6
    else:
        assert all(row[key] == 0 for key in required if key.startswith('prefill_'))
    total_calls = 0
    with (path / 'shared.bin').open('rb') as stream:
        for lane in range(workers):
            begin, end = size * lane // workers, size * (lane + 1) // workers
            content, calls = expected(begin, end, lane, frame, chunk)
            assert stream.read(len(content)) == content
            total_calls += calls
        assert stream.read(1) == b''
    assert row['payload_write_calls'] == total_calls
    assert row['write_at_all_calls'] == (total_calls if backend == 'pwrite' else 0)
    assert row['mapped_copy_calls'] == (total_calls if backend == 'mmap' else 0)
    if backend == 'mmap':
        if populate == 'write':
            assert row['mapping_phase_cpu_clock'] == 'CLOCK_THREAD_CPUTIME_ID'
            assert row['population_policy'] == 'write'
            assert row['population_calls'] == row['mapping_calls']
            assert row['population_bytes'] >= size
            assert row['population_failures'] == 0 and row['population_seconds'] > 0
            assert row['population_cpu_seconds'] >= 0
            for phase in ('mapping', 'population', 'unmapping'):
                assert 0 <= row[phase + '_seconds'] <= row['elapsed_seconds'] * workers + 0.1
                assert 0 <= row[phase + '_cpu_seconds'] <= row['write_thread_cpu_seconds'] + 0.1
        if size >= MIB:
            assert row['write_thread_minor_faults'] > 0
        assert row['mapping_calls'] == row['unmapping_calls'] > 0
        assert row['maximum_worker_mapping_bytes'] <= 64 * MIB
        assert row['mapping_window_bytes'] == 64 * MIB
        assert row['core_dump_disabled'] and row['mapping_lifecycle_in_write_interval']
        if workers == 1 and size > 64 * MIB:
            assert row['mapping_calls'] == 2
    else:
        assert row['mapping_calls'] == row['unmapping_calls'] == row['maximum_worker_mapping_bytes'] == 0
    (path / 'shared.bin').unlink()
    path.rmdir()
    cleaned = subprocess.run(command[:-1], capture_output=True, text=True, timeout=30)
    assert cleaned.returncode == 0, cleaned.stderr
    assert json.loads(cleaned.stdout)['owned_files_removed'] and not path.exists()


def faults(binary, library, root):
    spec = importlib.util.spec_from_file_location('runner',
        Path(__file__).resolve().parents[1] / 'perf/run_offset_write_memory.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    original = os.environ.copy()
    results = []
    try:
        for mode in ('fail', 'truncate', 'populate-partial', 'populate-truncate'):
            path = root / ('fault-' + mode)
            output = root / ('fault-' + mode + '.json')
            os.environ.update(LD_PRELOAD=str(library), CPNETFLUX_OFFSET_WRITE_FAULT=mode,
                              CPNETFLUX_OFFSET_WRITE_FAULT_FILE=str(path / 'shared.bin'))
            try:
                runner.measure(binary, path, 'shared', 4, 1024 ** 3, 'sparse', output, backend='mmap',
                               populate='write' if mode.startswith('populate-') else 'lazy')
            except RuntimeError:
                pass
            else:
                raise AssertionError('faulted child was accepted')
            receipt = json.loads(output.read_text())
            assert receipt['result'] == 'fail' and receipt['payload_removed'] and not path.exists()
            assert receipt['exit_code'] == (-signal.SIGBUS if mode == 'truncate' else 2), receipt
            assert not receipt['stdout'] and 'measurement' not in receipt
            if mode == 'fail':
                assert 'mmap' in receipt['stderr']
            elif mode.startswith('populate-'):
                assert 'madvise' in receipt['stderr']
                if mode == 'populate-partial':
                    assert 'CPNETFLUX_POPULATE_PARTIAL_INJECTED' in receipt['stderr']
            results.append({'fault': mode, 'exit_code': receipt['exit_code'], 'payload_removed': True})
    finally:
        for name in ('LD_PRELOAD', 'CPNETFLUX_OFFSET_WRITE_FAULT', 'CPNETFLUX_OFFSET_WRITE_FAULT_FILE'):
            if name in original:
                os.environ[name] = original[name]
            else:
                os.environ.pop(name, None)
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--fault-library', type=Path)
    parser.add_argument('--case', choices=('shared-open', 'mmap', 'populate', 'faults', 'all'), default='all')
    args = parser.parse_args()
    binary = args.build_dir.resolve() / 'cpnetflux-offset-write-bench'
    cases = 0
    fault_results = []
    with tempfile.TemporaryDirectory(prefix='cpnetflux-offset-backend-') as directory:
        root = Path(directory)
        if args.case in ('shared-open', 'all'):
            for workers in (1, 2, 4):
                for size in (workers, 2 * MIB + 37):
                    success(binary, root, 'shared-open', workers, size, 'pwrite')
                    cases += 2
        if args.case in ('populate', 'all'):
            for workers in (1, 2, 4):
                for size in (workers, 2 * MIB + 37):
                    success(binary, root, 'shared', workers, size, 'mmap', populate='write')
                    cases += 2
            success(binary, root, 'shared', 1, 64 * MIB + 37, 'mmap', chunk=100003, populate='write')
            cases += 2
            for options in (['--populate=invalid'], ['--populate=write', '--populate=write'],
                            ['--populate=write', '--backend=pwrite']):
                target = root / 'invalid-populate'
                rejected = subprocess.run([str(binary), str(target), 'shared', '1', '1', 'sparse',
                    '65532', '1048576', *options], capture_output=True, text=True, timeout=5)
                assert rejected.returncode != 0 and not rejected.stdout and not target.exists()
        if args.case in ('mmap', 'all'):
            for workers in (1, 2, 4):
                for size in (workers, 2 * MIB + 37):
                    success(binary, root, 'shared', workers, size, 'mmap')
                    cases += 2
            for workers in (1, 4):
                success(binary, root, 'shared', workers, 64 * MIB + 37, 'mmap')
                cases += 2
            success(binary, root, 'shared', 1, 64 * MIB + 37, 'mmap', chunk=100003)
            cases += 2
            success(binary, root, 'shared', 4, 2 * MIB + 37, 'mmap', mode='rewrite')
            cases += 2
            for layout, mode in (('independent', 'sparse'), ('shared-open', 'sparse'), ('shared', 'fresh')):
                target = root / 'invalid-mmap'
                rejected = subprocess.run([str(binary), str(target), layout, '1', '1', mode,
                                           '65532', '1048576', '--backend=mmap'],
                                          capture_output=True, text=True, timeout=5)
                assert rejected.returncode != 0 and not rejected.stdout and not target.exists()
        if args.case in ('faults', 'all'):
            assert args.fault_library and args.fault_library.is_file()
            fault_results = faults(binary, args.fault_library.resolve(), root)
    print(json.dumps({'success_cases': cases, 'independent_content_verified': True,
                      'faults': fault_results, 'case': args.case}))


if __name__ == '__main__':
    main()
