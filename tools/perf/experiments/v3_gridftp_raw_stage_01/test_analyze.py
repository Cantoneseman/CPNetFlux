import tempfile
import unittest
from pathlib import Path
from analyze import describe, grid_records, percentile, range_observation_summary, union_seconds


class AnalysisContract(unittest.TestCase):
    def test_concurrent_intervals_are_not_added_as_wall(self):
        self.assertEqual(union_seconds([(0,3),(1,2),(2,4),(6,7)]),5)

    def test_invalid_interval_is_rejected(self):
        with self.assertRaises(ValueError): union_seconds([(4,3)])

    def test_unknown_fields_stay_unknown(self):
        self.assertIsNone(describe([None,None])['sum'])
        self.assertEqual(describe([None,.1,.3])['count'],2)

    def test_percentile_interpolates(self):
        self.assertAlmostEqual(percentile([0,10],.95),9.5)

    def test_gridftp_zero_byte_file_is_a_real_file(self):
        with tempfile.TemporaryDirectory()as directory:
            path=Path(directory)/'transfer.log'
            path.write_text('DATE=20261022120001.500000 START=20261022120001.250000 FILE=/tmp/v3-raw/test/empty NBYTES=0 STREAMS=4 CODE=226\n')
            rows=grid_records(path)['test']
            self.assertEqual(len(rows),1);self.assertEqual(rows[0]['bytes'],0)
            self.assertEqual(rows[0]['end']-rows[0]['start'],.25)

    def test_range_observation_summary_preserves_legacy_and_missing_states(self):
        source = {"name": "logs/run-v3-server.log", "status": "present", "malformed_records": 0}
        export = {
            "source_logs": [source],
            "observations": [{
                "source_log": source["name"],
                "observation_status": "legacy_partial",
            }],
        }
        summary = range_observation_summary(export, "run-v3-server.log")
        self.assertEqual(summary, {
            "status": "partial",
            "records": 1,
            "counts": {"legacy_partial": 1},
        })
        self.assertEqual(
            range_observation_summary({"source_logs": [], "observations": []}, "absent.log"),
            {"status": "missing", "records": 0, "counts": {}},
        )

    def test_missing_sampling_is_not_zero_usage(self):
        from analyze import sample_metrics
        self.assertEqual(sample_metrics(Path('/not-a-real-cpnetflux-path')),{'samples':0})


if __name__=='__main__':unittest.main()
