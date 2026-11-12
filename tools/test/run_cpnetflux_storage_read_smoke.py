import argparse
from pathlib import Path
import subprocess
import tempfile


def crc32c(data):
    value = 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x82f63b78 if value & 1 else 0)
    return f'{value ^ 0xffffffff:08x}'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--preservation-only', action='store_true')
    args = parser.parse_args()
    binary = args.build_dir.resolve() / 'cpnetflux-storage-bench'
    with tempfile.TemporaryDirectory(prefix='cpnetflux-storage-read-') as temporary:
        root = Path(temporary)
        payload = bytes(range(251)) * 17 + b'CPNetFlux'
        removed = []
        for truncated in [False, True]:
            source = root / ('truncated.bin' if truncated else 'source.bin')
            source.write_bytes(payload)
            before = source.stat()
            command = [str(binary), '--path', str(source), '--mode', 'read', '--bytes',
                       str(len(payload) + int(truncated)), '--buffer-size', '113', '--iterations', '1']
            result = subprocess.run(command, text=True, capture_output=True, timeout=20)
            assert (result.returncode != 0) == truncated, result.stdout + result.stderr
            if not source.exists():
                removed.append(truncated)
            else:
                assert source.stat().st_ino == before.st_ino and source.read_bytes() == payload
        assert not removed, f'read mode removed input for truncated cases {removed}'
        if args.preservation_only:
            return
        for size in [1, 113, 65532]:
            command = [str(binary), '--path', str(source), '--mode', 'read', '--bytes',
                       str(len(payload)), '--buffer-size', str(size), '--iterations', '2',
                       '--read-checksum', 'crc32c']
            result = subprocess.run(command, text=True, capture_output=True, timeout=20)
            assert result.returncode == 0, result.stdout + result.stderr
            rows = [dict(part.split('=', 1) for part in line.split()[1:])
                    for line in result.stdout.splitlines() if line.startswith('storage_bench ')]
            cases = [row for row in rows if row['aggregate'] == 'false']
            assert len(cases) == 2
            for row in cases:
                assert row['read_checksum'] == 'crc32c'
                assert row['read_checksum_backend'] in ['hardware', 'software']
                assert row['read_checksum_value'] == crc32c(payload), row
                assert int(row['read_total_bytes']) == len(payload), row
                assert int(row['read_call_count']) == (len(payload) + size - 1) // size
                assert 0 <= float(row['read_checksum_seconds']) <= float(row['elapsed_seconds'])
            assert source.read_bytes() == payload
        for options in [['--mode', 'write', '--read-checksum', 'crc32c'],
                        ['--mode', 'all', '--read-checksum', 'crc32c'],
                        ['--mode', 'read', '--read-checksum', 'bad']]:
            result = subprocess.run([str(binary), '--path', str(source), *options],
                                    capture_output=True, timeout=20)
            assert result.returncode == 2
            assert source.read_bytes() == payload
    print('storage read preservation, independent CRC32C and option rejection passed')


if __name__ == '__main__':
    main()
