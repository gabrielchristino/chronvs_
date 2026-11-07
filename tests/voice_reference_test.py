"""Reference text metrics without downloading a model or opening any audio."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from reference_metrics import distance, normalize, review_queue, summarize


class ReferenceTest(unittest.TestCase):
    def test_normalization_keeps_meaningful_accents(self):
        self.assertEqual(normalize("  PÃO, quente! "), "pão quente")
        self.assertEqual(normalize("PA\u0303O"), "pão")
        self.assertEqual(normalize("PÃO", True), "pao")
        self.assertNotEqual(normalize("pó"), normalize("pô"))

    def test_distance_and_background_denominators(self):
        cases = [{"expected": "pão", "hypothesis": "pão"},
                 {"expected": "casa", "hypothesis": "asa"},
                 {"expected": "_silence", "hypothesis": "a"},
                 {"expected": "_unknown", "hypothesis": "abacaxi"}]
        result = summarize(cases)
        self.assertEqual(result["known_text_cases"], 2)
        self.assertEqual(result["exact_known"], 1)
        self.assertEqual(result["wer"], .5)
        self.assertEqual(result["silence_nonempty_hypotheses"], 1)
        self.assertEqual(result["untranscribed_unknown_cases"], 1)
        self.assertEqual(distance([], ["a"]), 1)
        self.assertEqual(distance(["a", "b"], ["a"]), 1)

    def test_review_never_auto_corrects_or_flags_unknown_as_wrong(self):
        cases = [{"expected": "pão", "hypothesis": "pao"},
                 {"expected": "_unknown", "hypothesis": "abacaxi"}]
        queue = review_queue(cases)
        self.assertEqual(len(queue), 1)
        self.assertEqual(queue[0]["expected"], "pão")
        self.assertEqual(cases[0]["expected"], "pão")
        self.assertIn("needs-listening", queue[0]["status"])
        self.assertIsNone(summarize([])["wer"])


if __name__ == "__main__":
    unittest.main()
