"""Activity gate math and train-only selection; no microphone or reference model."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from validate_vad import activity, choose_gate, longest_run


class VadTest(unittest.TestCase):
    def test_runs_and_digital_silence(self):
        self.assertEqual(longest_run([True, True, False, True]), 2)
        self.assertEqual(longest_run([]), 0)
        for mode in range(4):
            self.assertEqual(activity(bytes(64000), mode)["speech_frames"], 0)
        with self.assertRaises(ValueError):
            activity(bytes(100), 1)

    def test_calibration_does_not_select_gate(self):
        def case(split, label, count):
            return {"split": split, "expected": label,
                    "vad": {str(mode): {"longest_run": count} for mode in range(4)}}
        train = [case("train", "palavra", 12), case("train", "_unknown", 15), case("train", "_silence", 4)]
        a, _ = choose_gate(train + [case("calibration", "palavra", 3)])
        b, _ = choose_gate(train + [case("calibration", "_silence", 99)])
        self.assertEqual(a, b)
        self.assertEqual(a["minimum_frames"], 5)
        self.assertEqual(a["mode"], 0)


if __name__ == "__main__":
    unittest.main()
