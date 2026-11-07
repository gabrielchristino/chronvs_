"""Timing/gain invariance checks for the calibration-only DTW control."""
from pathlib import Path
import sys
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import SAMPLES
from diagnose_alignment import dtw_distance, features


class AlignmentTest(unittest.TestCase):
    def test_variable_duration_alignment(self):
        a = np.array([[0.], [1.], [2.]])
        b = np.array([[0.], [0.], [1.], [2.], [2.]])
        self.assertEqual(dtw_distance(a, b), 0)
        self.assertEqual(dtw_distance(b, a), 0)
        self.assertGreater(dtw_distance(a, a[::-1]), 0)

    def test_gain_invariance_without_clipping(self):
        samples = np.random.default_rng(41).integers(-1000, 1001, SAMPLES, dtype=np.int16)
        a = features(samples.astype("<i2").tobytes())
        b = features((samples * 2).astype("<i2").tobytes())
        self.assertEqual(a.shape, (48, 26))
        np.testing.assert_allclose(a, b, rtol=1e-4, atol=1e-4)

    def test_silence_and_invalid_input(self):
        self.assertTrue(np.isfinite(features(bytes(SAMPLES * 2))).all())
        with self.assertRaises(ValueError):
            features(bytes(32))
        with self.assertRaises(ValueError):
            dtw_distance(np.empty((0, 3)), np.empty((1, 3)))


if __name__ == "__main__":
    unittest.main()
