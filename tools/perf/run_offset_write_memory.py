#!/usr/bin/env python3
"""Bounded local tmpfs service-rate experiment; no network or V3 claims."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time

PROJECT = Path(__file__).resolve().parents[2]
GIB = 1024 ** 3


def meminfo():
    return {line.split(':')[0]: int(line.split()[1]) * 1024
            for line in Path('/proc/meminfo').read_text().splitlines() if line.endswith('kB')}


def swap():
    return {parts[0]: int(parts[1]) for line in Path('/proc/vmstat').read_text().splitlines()
            if (parts := line.split())[0] in ('pswpin', 'pswpout')}


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def cleanup(path, workers):
    if path.is_symlink():
        raise ValueError('refusing a symlink payload directory')
    if not path.exists():
        return
    if not path.is_dir():
        raise ValueError('payload root is not a directory')
    allowed = {'shared.bin', *(f'lane-{i}.bin' for i in range(workers))}
    entries = list(path.iterdir())
    if any(entry.name not in allowed or not stat.S_ISREG(entry.lstat().st_mode) for entry in entries):
        raise ValueError('refusing unexpected payload entries')
    for entry in entries:
        entry.unlink()
    path.rmdir()


def measure(binary, path, layout, workers, size, mode, output, backend='pwrite'):
    if path.exists() or path.is_symlink():
        raise FileExistsError(path)
    if meminfo()['MemAvailable'] < 12 * GIB + size + 256 * 1024 ** 2:
        raise RuntimeError('insufficient host memory before sample')
    if shutil.disk_usage('/dev/shm').free < size + 512 * 1024 ** 2:
        raise RuntimeError('insufficient tmpfs space before sample')
    before = swap()
    command = [str(binary), str(path), layout, str(workers), str(size), mode, '65532', '1048576',
               '--backend=' + backend]
    receipt = {'command': command, 'layout': layout, 'workers': workers, 'write_mode': mode, 'backend': backend,
               'result': 'fail', 'minimum_available_bytes': meminfo()['MemAvailable'],
               'maximum_owned_rss_bytes': 0, 'maximum_owned_swap_bytes': 0,
               'samples': 0, 'owned_samples': 0, 'memory_sampling_seconds': 0.05, 'swap_before': before}
    process = None
    try:
        started = time.monotonic()
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        receipt['pid'] = process.pid
        while process.poll() is None:
            available = meminfo()['MemAvailable']
            receipt['minimum_available_bytes'] = min(receipt['minimum_available_bytes'], available)
            try:
                values = {line.split(':')[0]: int(line.split()[1]) * 1024
                          for line in Path('/proc', str(process.pid), 'status').read_text().splitlines()
                          if line.endswith('kB')}
            except FileNotFoundError:
                values = {}
            if 'VmRSS' in values:
                receipt['owned_samples'] += 1
            receipt['maximum_owned_rss_bytes'] = max(receipt['maximum_owned_rss_bytes'], values.get('VmRSS', 0))
            receipt['maximum_owned_swap_bytes'] = max(receipt['maximum_owned_swap_bytes'], values.get('VmSwap', 0))
            receipt['samples'] += 1
            if available < 12 * GIB or values.get('VmSwap', 0) or values.get('VmRSS', 0) > GIB:
                raise RuntimeError('memory, RSS or owned-swap guard failed')
            if time.monotonic() - started > 120:
                raise TimeoutError('diagnostic exceeded 120 seconds')
            time.sleep(0.05)
        stdout, stderr = process.communicate(timeout=5)
        receipt.update(exit_code=process.returncode, stdout=stdout, stderr=stderr,
                       whole_process_seconds=time.monotonic() - started)
        if process.returncode != 0:
            raise RuntimeError(f'diagnostic exited {process.returncode}: {stderr}')
        row = json.loads(stdout)
        assert row['result'] == 'pass' and row['diagnostic_only'] and not row['v3'] and not row['network']
        assert not row['durability'] and row['owned_files_removed'] and not row['kept_files']
        assert row['logical_bytes'] == row['written_bytes'] == row['verified_bytes'] == size
        assert row['layout'] == layout and row['workers'] == workers and row['write_mode'] == mode
        expected_calls = 17408 * (size // GIB)
        assert row['payload_write_calls'] == expected_calls
        assert row['write_at_all_calls'] == (expected_calls if backend == 'pwrite' else 0)
        assert row['mapped_copy_calls'] == (expected_calls if backend == 'mmap' else 0)
        assert row['backend'] == backend
        assert row['file_count'] == row['write_inode_count'] == (workers if layout == 'independent' else 1)
        assert row['write_fd_count'] == (1 if layout == 'shared' else workers)
        if backend == 'mmap':
            assert row['mapping_calls'] == row['unmapping_calls'] == size // (64 * 1024 ** 2)
            assert row['maximum_worker_mapping_bytes'] <= row['mapping_window_bytes'] == 64 * 1024 ** 2
            assert row['core_dump_disabled'] and row['mapping_lifecycle_in_write_interval']
        else:
            assert row['mapping_calls'] == row['unmapping_calls'] == row['maximum_worker_mapping_bytes'] == 0
        assert row['frame_bytes'] == 65532 and row['chunk_bytes'] == 1048576
        assert row['schema_version'] == 3
        assert row['write_thread_minor_faults'] >= 0 and row['write_thread_major_faults'] >= 0
        assert row['initial_file_bytes'] == (0 if mode == 'fresh' else size)
        assert row['prefill_bytes'] == (size if mode == 'rewrite' else 0)
        if mode == 'rewrite':
            assert row['initial_allocated_bytes'] >= size
            assert row['prefill_seconds'] > 0 and row['prefill_cpu_seconds'] >= 0
            assert 0 <= row['prefill_system_cpu_seconds'] <= row['prefill_cpu_seconds'] + 1e-6
            assert row['prefill_minor_faults'] >= 0 and row['prefill_major_faults'] >= 0
        else:
            assert row['initial_allocated_bytes'] == 0
            assert all(row[key] == 0 for key in ('prefill_seconds', 'prefill_cpu_seconds',
                'prefill_system_cpu_seconds', 'prefill_minor_faults', 'prefill_major_faults'))
        assert receipt['samples'] > 0 and receipt['owned_samples'] > 0 and not path.exists()
        after = swap()
        receipt['swap_after'] = after
        receipt['pswpout_delta_pages'] = after['pswpout'] - before['pswpout']
        assert receipt['pswpout_delta_pages'] == 0, 'host swap-out occurred'
        receipt.update(result='pass', measurement=row)
    except BaseException as error:
        receipt['error'] = str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=5)
        try:
            cleanup(path, workers)
            receipt['payload_removed'] = not path.exists()
        except BaseException as error:
            receipt.update(result='fail', cleanup_error=str(error))
            raise
        finally:
            save(output, receipt)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--gib', type=int, default=8, choices=range(1, 9))
    parser.add_argument('--repetitions', type=int, default=5, choices=range(1, 6))
    parser.add_argument('--modes', nargs='+', choices=('fresh', 'sparse', 'rewrite'),
                        default=['fresh', 'sparse', 'rewrite'])
    parser.add_argument('--layouts', nargs='+', choices=('shared', 'shared-open', 'independent'),
                        default=['shared', 'independent'])
    parser.add_argument('--backends', nargs='+', choices=('pwrite', 'mmap'), default=['pwrite'])
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', args.run_id):
        parser.error('invalid run id')
    if any(len(set(values)) != len(values) for values in (args.modes, args.layouts, args.backends)):
        parser.error('duplicate modes, layouts or backends')
    if 'mmap' in args.backends and ('fresh' in args.modes or 'shared' not in args.layouts):
        parser.error('mmap requires sized files and a shared layout')
    if subprocess.check_output(['git', '-C', str(PROJECT), 'rev-parse', 'HEAD'], text=True).strip() != args.commit:
        parser.error('HEAD does not match --commit')
    subprocess.run(['git', '-C', str(PROJECT), 'diff', '--quiet', 'HEAD'], check=True)
    if subprocess.check_output(['findmnt', '-n', '-o', 'FSTYPE', '--target', '/dev/shm'], text=True).strip() != 'tmpfs':
        parser.error('/dev/shm is not tmpfs')
    output = args.output_root.resolve()
    if output.is_relative_to(PROJECT):
        parser.error('evidence must stay outside the source tree')
    output.mkdir(parents=True, exist_ok=False)
    temporary = Path('/dev/shm') / ('cpnetflux-offset-' + args.run_id)
    if temporary.exists() or temporary.is_symlink():
        raise FileExistsError(temporary)
    binary = PROJECT / 'build/cpnetflux-offset-write-bench'
    configuration = (PROJECT / 'build/CMakeCache.txt').read_text().splitlines()
    state = {'commit': args.commit, 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
             'configuration': [s for s in configuration if s.startswith('CPNETFLUX_') and ':BOOL=' in s],
             'build_type': next(s for s in configuration if s.startswith('CMAKE_BUILD_TYPE:')),
             'task': 'offset_write_only_no_network_no_V3_no_durability',
             'gib': args.gib, 'repetitions': args.repetitions, 'modes': args.modes,
             'layouts': args.layouts, 'backends': args.backends,
             'samples': [], 'result': 'fail'}
    temporary.mkdir()
    try:
        orders = ((1, 2, 4), (4, 2, 1), (2, 4, 1), (1, 4, 2), (4, 1, 2))
        for repetition in range(1, args.repetitions + 1):
            for index, workers in enumerate(orders[repetition - 1]):
                for mode in (args.modes if repetition % 2 else reversed(args.modes)):
                    cases = [(layout, 'pwrite') for layout in args.layouts] if 'pwrite' in args.backends else []
                    if 'mmap' in args.backends:
                        cases.append(('shared', 'mmap'))
                    rotation = (repetition + index) % len(cases)
                    cases = cases[rotation:] + cases[:rotation]
                    if repetition % 2 == 0:
                        cases.reverse()
                    for layout, backend in cases:
                        name = f'{mode}-{layout}-{backend}-w{workers}-r{repetition:02d}'
                        target = output / (name + '.json')
                        assert not target.exists()
                        receipt = measure(binary, temporary / name, layout, workers, args.gib * GIB,
                                          mode, target, backend=backend)
                        state['samples'].append(name)
                        print(json.dumps({'sample': name, 'result': receipt['result'],
                                          'write_gbps': receipt['measurement']['write_gbps']}), flush=True)
        state['result'] = 'pass'
    finally:
        try:
            temporary.rmdir()
            state['payload_root_removed'] = not temporary.exists()
        except BaseException as error:
            state.update(result='fail', cleanup_error=str(error))
            raise
        finally:
            save(output / 'run.json', state)


if __name__ == '__main__':
    main()
