#!/usr/bin/env python3
"""Validate actual output patterns, partitions, cleanup and partial IO failure."""
import argparse
import importlib.util
import json
from pathlib import Path
import resource
import signal
import subprocess
import tempfile


def expected(begin, end, lane, frame, chunk):
    block = bytearray(1 + (i * 31 + lane * 17) % 251 for i in range(frame))
    result = bytearray()
    calls = 0
    while begin < end:
        length = min(frame, end - begin, chunk - begin % chunk)
        for index in range(min(8, length)):
            block[index] = 1 + ((begin >> (8 * index)) & 255) % 251
        result.extend(block[:length])
        begin += length
        calls += 1
    return result, calls


def limited_file_size():
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (32768, 32768))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    binary = args.build_dir.resolve() / 'cpnetflux-offset-write-bench'
    rows = []
    with tempfile.TemporaryDirectory(prefix='cpnetflux-offset-smoke-') as temporary:
        root = Path(temporary)
        for layout in ('shared', 'independent'):
            for mode in ('fresh', 'sparse', 'rewrite'):
                for workers in (1, 2, 4):
                    for size in (workers, 2 * 1024 ** 2 + 37):
                        path = root / f'{layout}-{mode}-{workers}-{size}'
                        command = [str(binary), str(path), layout, str(workers), str(size),
                                   mode, '65532', '1048576', '--keep-files']
                        completed = subprocess.run(command, capture_output=True, text=True, timeout=30)
                        assert completed.returncode == 0, completed.stderr
                        row = json.loads(completed.stdout)
                        assert row['written_bytes'] == row['verified_bytes'] == row['logical_bytes'] == size
                        assert row['diagnostic_only'] and not row['v3'] and not row['network'] and not row['durability']
                        assert row['kept_files'] and not row['owned_files_removed']
                        assert row['elapsed_seconds'] > 0 and row['write_gbps'] > 0
                        assert row['prefill_bytes'] == (size if mode == 'rewrite' else 0)
                        assert row['schema_version'] == 2
                        assert row['initial_file_bytes'] == (0 if mode == 'fresh' else size)
                        if mode == 'rewrite':
                            assert row['initial_allocated_bytes'] >= size
                        else:
                            assert row['initial_allocated_bytes'] == 0
                        assert row['file_count'] == (1 if layout == 'shared' else workers)
                        assert row['payload_buffer_bound_bytes'] == max(workers, 2) * 65532
                        call_count = 0
                        joined = bytearray()
                        for lane in range(workers):
                            begin, end = size * lane // workers, size * (lane + 1) // workers
                            content, calls = expected(begin, end, lane, 65532, 1048576)
                            call_count += calls
                            joined.extend(content)
                            if layout == 'independent':
                                output = path / f'lane-{lane}.bin'
                                assert output.read_bytes() == content
                                output.unlink()
                        if layout == 'shared':
                            output = path / 'shared.bin'
                            assert output.read_bytes() == joined
                            output.unlink()
                        assert row['write_at_all_calls'] == call_count
                        path.rmdir()
                        # Same workload with default cleanup exercises the owned-file destructor contract.
                        clean = subprocess.run(command[:-1], capture_output=True, text=True, timeout=30)
                        assert clean.returncode == 0, clean.stderr
                        assert json.loads(clean.stdout)['owned_files_removed'] and not path.exists()
                        rows.append({'layout': layout, 'mode': mode, 'workers': workers, 'bytes': size})

        # A real short pwrite followed by EFBIG must fail, release every worker and clean its files.
        for workers in (1, 4):
            partial = root / f'partial-io-{workers}'
            failed = subprocess.run([str(binary), str(partial), 'shared', str(workers), '1048576',
                                     'fresh', '65532', '1048576'], preexec_fn=limited_file_size,
                                    capture_output=True, text=True, timeout=10)
            assert failed.returncode != 0 and not failed.stdout and not partial.exists(), failed

        existing = root / 'existing'
        existing.mkdir()
        marker = existing / 'shared.bin'
        marker.write_bytes(b'preserve unrelated bytes')
        link = root / 'existing-link'
        link.symlink_to(existing, target_is_directory=True)
        for target in (existing, link):
            rejected = subprocess.run([str(binary), str(target), 'shared', '1', '1',
                                       'fresh', '65532', '1048576'], capture_output=True, text=True, timeout=5)
            assert rejected.returncode != 0 and not rejected.stdout
            assert marker.read_bytes() == b'preserve unrelated bytes'
        spec = importlib.util.spec_from_file_location('runner',
            Path(__file__).resolve().parents[1] / 'perf/run_offset_write_memory.py')
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        foreign = existing / 'unrelated.txt'
        foreign.write_text('preserve')
        for target in (existing, link):
            try:
                runner.cleanup(target, 1)
            except ValueError:
                pass
            else:
                raise AssertionError('unsafe cleanup was accepted')
            assert marker.read_bytes() == b'preserve unrelated bytes' and foreign.read_text() == 'preserve'
        for position, value in ((2, 'invalid'), (3, '0'), (3, '65'), (4, '0'),
                                (4, str(8 * 1024 ** 3 + 1)), (5, 'invalid'),
                                (6, '65537'), (7, '0'), (8, '--invalid')):
            target = root / 'invalid'
            command = [str(binary), str(target), 'shared', '1', '1', 'fresh', '65532', '1048576', '--keep-files']
            command[position] = value
            rejected = subprocess.run(command, capture_output=True, text=True, timeout=5)
            assert rejected.returncode != 0 and not rejected.stdout and not target.exists()
    print(json.dumps({'success_cases': len(rows) * 2, 'independent_content_verified': True,
                      'partial_io_cleanup': True, 'existing_paths_preserved': True}))


if __name__ == '__main__':
    main()
