#!/usr/bin/env python3
"""Reject incomplete, invalid and unstable S2C results before reporting a gain."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'perf'))
from run_tree_serializer_ab import evaluate, checkpoint_crcs, crc32c


def fixture():
    rows = []
    for rep in range(1, 6):
        for mode, time in (('original', 2.), ('buffered', 1.7)):
            expected = {'files': 10000, 'bytes': 40960000, 'sha256': 'abc'}
            rows.append(dict(rep=rep, tree_manifest_serializer_request=mode, result='pass',
                output_slot_removed=True, t_ready_seconds=time, phase_timing='off', window_request='2',
                expected=expected, actual_tree=dict(expected), resource={
                    'pswpout_delta': 0, 'independent_checkpoint_body_crcs': 10001},
                manifest_checks={'committed_member_manifests': 10000, 'completed_tree_manifests': 1},
                summary=dict(completed_files=10000, failed_files=0, changed_files=0, actual_transport='v3',
                    tree_manifest_serializer_requested=mode, tree_manifest_serializer_effective=mode,
                    tree_manifest_serializer_scope='all_client_tree_manifest_save_attempts',
                    summary_path_requested='cached', summary_path_effective='cached',
                    summary_tree_hash_source='filesystem_read', tree_hash='crc32c:12345678',
                    tree_manifest_event_threshold=32, tree_manifest_max_delay_ms=100,
                    tree_manifest_generation=20001, tree_manifest_persisted_generation=20001,
                    tree_manifest_snapshots=100, tree_manifest_serialized_bytes=1000000,
                    tree_manifest_write_seconds=.2, tree_manifest_state_lock_wait_seconds=.01,
                    summary_tree_hash_seconds=.02)))
    return rows


class Evaluation(unittest.TestCase):
    def test_independent_checkpoint_crc_rejects_corruption(self):
        self.assertEqual(crc32c(b'123456789'), 0xe3069283)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'tree.manifest'
            body = b'manifest_version=1\nfiles=\n'
            path.write_bytes(body + f'manifest_body_crc32c={crc32c(body):08x}\n'.encode())
            self.assertEqual(checkpoint_crcs(Path(directory)), 1)
            path.write_bytes(path.read_bytes().replace(b'files=', b'filEs='))
            with self.assertRaises(AssertionError): checkpoint_crcs(Path(directory))

    def test_pass(self):
        result = evaluate(fixture())
        self.assertTrue(result['gate_pass'])
        self.assertAlmostEqual(result['median_wall_reduction'], .15)

    def test_incomplete_or_duplicate_pair(self):
        for rows in (fixture()[:-1], fixture() + [fixture()[0]]):
            with self.assertRaises(AssertionError): evaluate(rows)
        rows = fixture(); rows[0]['rep'] = 2
        with self.assertRaises(AssertionError): evaluate(rows)

    def test_contracts(self):
        for key, value in (('tree_hash', 'different'), ('tree_manifest_serializer_effective', 'original'),
                           ('tree_manifest_event_threshold', 1), ('tree_manifest_persisted_generation', 0),
                           ('summary_tree_hash_source', 'verified_chunks'), ('actual_transport', 'legacy')):
            rows = fixture(); rows[1]['summary'][key] = value
            with self.subTest(key=key), self.assertRaises(AssertionError): evaluate(rows)
        rows = fixture(); rows[1]['actual_tree']['sha256'] = 'bad'
        with self.assertRaises(AssertionError): evaluate(rows)
        rows = fixture(); rows[1]['resource']['pswpout_delta'] = 1
        with self.assertRaises(AssertionError): evaluate(rows)

    def test_insufficient_gain(self):
        rows = fixture()
        for r in rows:
            if r['tree_manifest_serializer_request'] == 'buffered': r['t_ready_seconds'] = 1.9
        self.assertFalse(evaluate(rows)['gate_pass'])

    def test_unstable_or_tail_regression(self):
        for outlier in (1.1, 2.1):
            rows = copy.deepcopy(fixture()); rows[1]['t_ready_seconds'] = outlier
            self.assertFalse(evaluate(rows)['gate_pass'])


if __name__ == '__main__': unittest.main()
