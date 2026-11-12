#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', required=True, type=Path)
args = parser.parse_args()
binary = args.build_dir.resolve() / 'cpnetflux-session-record-bench'
for role in ['download', 'upload']:
    for policy in ['every_n_chunks', 'final_only']:
        with tempfile.TemporaryDirectory(prefix='cpnetflux-record-bench-') as directory:
            result = subprocess.run([str(binary), role, '32', policy, directory],
                                    check=True, text=True, capture_output=True)
            row = json.loads(result.stdout)
            assert row['benchmark'] == 'session_metadata_only'
            assert row['role'] == role and row['manifest_flush_policy'] == policy
            assert row['records'] == row['lookup_calls'] == 32
            assert row['payload_bytes_written'] == 0 and not row['network_transfer']
            assert row['persisted_coverage_verified']
            assert row['save_attempts'] == row['manifest_flush_count'] == (4 if policy == 'every_n_chunks' else 2)
            assert row['wall_seconds'] >= row['lookup_seconds'] >= 0
            assert row['serialize_seconds'] > 0 and row['serialized_bytes'] > 0
            assert row['save_seconds'] >= row['serialize_seconds'], row
            substeps = [row[key] for key in ['record_prepare_seconds', 'range_validation_seconds',
                                             'record_format_seconds', 'body_crc_seconds']]
            assert all(value >= 0 for value in substeps)
            assert sum(substeps) <= row['serialize_seconds'], row
            assert row['record_preparation_calls'] > 0
            print(role, policy, 'PASS')
