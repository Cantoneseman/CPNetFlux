#!/usr/bin/env python3
"""Reject frozen incompatible profiles before launching a process or listener."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "perf"))
from field_trial_profiles import APPLICATION, KERNEL, sender_pacing_environment, validate_sender_profile


class FieldTrialProfilesTest(unittest.TestCase):
    def test_plain_profile_keeps_accepted_comparison_pacer(self):
        env = sender_pacing_environment(tls=False)
        self.assertEqual(env[APPLICATION], "1500000000")
        self.assertEqual(env[KERNEL], "off")
        validate_sender_profile(env, tls=False)

    def test_tls_overrides_inherited_plain_profile(self):
        env = sender_pacing_environment(tls=False)
        env.update(sender_pacing_environment(tls=True))
        self.assertEqual(env[APPLICATION], "off")
        self.assertEqual(env[KERNEL], "off")
        validate_sender_profile(env, tls=True)

    def test_stage80_tls_profile_is_rejected_offline(self):
        with self.assertRaisesRegex(ValueError, "TLS requires"):
            validate_sender_profile({APPLICATION: "1500000000", KERNEL: "off"}, tls=True)

    def test_tls_cannot_use_kernel_pacer_either(self):
        with self.assertRaisesRegex(ValueError, "TLS requires"):
            validate_sender_profile({APPLICATION: "off", KERNEL: "2000000000"}, tls=True)

    def test_two_pacers_cannot_be_combined(self):
        with self.assertRaisesRegex(ValueError, "mutually exclusive"):
            validate_sender_profile({APPLICATION: "1500000000", KERNEL: "2000000000"}, tls=False)

    def test_unreviewed_rate_is_rejected(self):
        for key in (APPLICATION, KERNEL):
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_sender_profile({key: "123"}, tls=False)


if __name__ == "__main__":
    unittest.main()
