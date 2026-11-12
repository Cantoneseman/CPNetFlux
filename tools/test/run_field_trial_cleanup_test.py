#!/usr/bin/env python3
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))
from field_trial_cleanup import preserve_tree_manifest


class FieldTrialCleanupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "payload"; self.root.mkdir()
        self.evidence = self.base / "retained"
        self.path = self.root / "dst-tls.cpnetflux.tree.download.manifest"
        self.path.write_bytes(b"manifest evidence\n")

    def preserve(self, path=None, evidence=None):
        return preserve_tree_manifest(path or self.path, payload_root=self.root, evidence_root=evidence or self.evidence)

    def test_archive_hash_verified_before_source_removed(self):
        data = self.path.read_bytes(); result = self.preserve()
        self.assertFalse(self.path.exists())
        self.assertEqual((self.evidence / self.path.name).read_bytes(), data)
        self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest())

    def test_unexpected_file_is_preserved(self):
        other = self.root / "unrelated.txt"; other.write_bytes(b"keep")
        with self.assertRaises(ValueError): self.preserve(other)
        self.assertEqual(other.read_bytes(), b"keep")
        self.assertFalse(self.evidence.exists())

    def test_symlink_is_preserved_and_target_untouched(self):
        other = self.base / "outside"; other.write_bytes(b"keep")
        self.path.unlink(); self.path.symlink_to(other)
        with self.assertRaises(ValueError): self.preserve()
        self.assertTrue(self.path.is_symlink())
        self.assertEqual(other.read_bytes(), b"keep")

    def test_archive_collision_keeps_both_files(self):
        self.evidence.mkdir(); retained = self.evidence / self.path.name; retained.write_bytes(b"older")
        with self.assertRaises(FileExistsError): self.preserve()
        self.assertTrue(self.path.is_file())
        self.assertEqual(retained.read_bytes(), b"older")

    def test_nested_input_and_inside_payload_archive_rejected(self):
        nested = self.root / "child"; nested.mkdir(); path = nested / self.path.name; path.write_bytes(b"keep")
        with self.assertRaises(ValueError): self.preserve(path)
        with self.assertRaises(ValueError): self.preserve(evidence=self.root / "evidence")
        self.assertTrue(self.path.exists()); self.assertTrue(path.exists())


if __name__ == "__main__": unittest.main()
