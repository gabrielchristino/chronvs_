"""Sequence/CTC math regressions; synthetic signals do not measure ASR quality."""
import itertools
import json
import math
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import SAMPLES, sha256, write_wav
from phonetic_units import UNIT_SPEC, choose_prompts, tokenize, units_and_pairs
from phonetic_data import load_phonetic_dataset
from phonetic_ctc import (SmallPhoneticCTC, ctc_log_score, edit_distance, greedy_ctc,
                          rank_words, sequence_features)


class PhoneticTest(unittest.TestCase):
    def test_ipa_units(self):
        self.assertEqual(tokenize("/pˈɐ̃ʊ̃/"), ["p", "ɐ̃", "ʊ̃"])
        self.assertEqual(tokenize("t͡ʃˈi dʒˈia"), ["tʃ", "i", "|", "dʒ", "i", "a"])
        self.assertNotEqual(tokenize("pˈa"), tokenize("pˈã"))
        self.assertEqual(tokenize("ˈpá ˌpáː"), ["p", "á", "|", "p", "á"])
        with self.assertRaises(ValueError):
            tokenize("̃a")

    def test_coverage(self):
        entries = [{"text": "pa", "units": ["p", "a"]},
                   {"text": "ba", "units": ["b", "a"]}]
        chosen, units, pairs = choose_prompts(entries, {"p", "b", "a"}, {("p", "a"), ("b", "a")}, 2)
        self.assertEqual(len(chosen), 2)
        self.assertEqual(units, {"p", "b", "a"})
        self.assertEqual(pairs, {("p", "a"), ("b", "a")})
        self.assertEqual(units_and_pairs(["p", "|", "a"])[1], set())

    def test_ctc_score_matches_all_paths_and_torch(self):
        probabilities = np.random.default_rng(7).dirichlet(np.ones(3), size=4)
        logp = np.log(probabilities)
        for target in ([], [1], [1, 1], [1, 2], [1, 2, 1]):
            total = 0
            for path in itertools.product(range(3), repeat=4):
                collapsed, previous = [], 0
                for token in path:
                    if token and token != previous:
                        collapsed.append(token)
                    previous = token
                if collapsed == target:
                    total += math.prod(probabilities[t, token] for t, token in enumerate(path))
            actual = ctc_log_score(logp, target)
            self.assertAlmostEqual(actual, math.log(total), places=10)
            expected = torch.nn.functional.ctc_loss(torch.from_numpy(logp).unsqueeze(1),
                torch.tensor(target, dtype=torch.long), torch.tensor([4]),
                torch.tensor([len(target)]), reduction="sum").item()
            self.assertAlmostEqual(actual, -expected, places=10)
        self.assertEqual(ctc_log_score(np.empty((0, 3)), []), 0)
        self.assertEqual(ctc_log_score(np.empty((0, 3)), [1]), -math.inf)
        self.assertEqual(ctc_log_score(np.log(probabilities[:1]), [1, 1]), -math.inf)

    def test_homophones_and_repeated_units(self):
        probabilities = np.full((5, 3), .005)
        probabilities[np.arange(5), [0, 1, 0, 1, 0]] = .99
        self.assertEqual(greedy_ctc(probabilities), [1, 1])
        entries = [{"text": "word-a", "units": ["p", "p"]},
                   {"text": "word-b", "units": ["p", "p"]}]
        ranked = rank_words(np.log(probabilities), entries, ["<blank>", "p", "a"])
        self.assertEqual(ranked[0][1], ranked[1][1])
        self.assertEqual(edit_distance([1, 2], [1, 3, 2]), 1)

    def test_causality_and_finite_gradient(self):
        torch.set_num_threads(2)
        torch.manual_seed(19)
        model = SmallPhoneticCTC(8, hidden=16)
        x = torch.randn(2, 198, 32)
        logits = model(x)
        self.assertEqual(tuple(logits.shape), (2, 198, 8))
        torch.testing.assert_close(logits[:, :30], model(x[:, :30]), rtol=1e-5, atol=1e-6)
        loss = torch.nn.functional.ctc_loss(logits.log_softmax(-1).transpose(0, 1),
            torch.tensor([1, 2, 2, 3]), torch.tensor([198, 198]), torch.tensor([2, 2]))
        loss.backward()
        self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters()))
        features = sequence_features(bytes(SAMPLES * 2))
        self.assertEqual(features.shape, (198, 32))
        self.assertTrue(np.isfinite(features).all())

    def test_gain_normalized_sequence_features(self):
        samples = np.random.default_rng(97).integers(-1000, 1001, SAMPLES, dtype=np.int16)
        a = sequence_features(samples.astype("<i2").tobytes(), gain_normalize=True)
        b = sequence_features((samples * 2).astype("<i2").tobytes(), gain_normalize=True)
        np.testing.assert_allclose(a, b, atol=1e-5, rtol=1e-5)
        np.testing.assert_allclose(a.mean(axis=1), 0, atol=1e-6)

    def test_frozen_phonetic_transcription_and_audio(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "person/occasion"
            directory.mkdir(parents=True)
            plan = {"unit_spec": UNIT_SPEC, "prompts": [{"text": "pato", "units": ["p", "a", "t", "u"], "reviewed": False}]}
            plan_bytes = json.dumps(plan).encode()
            (directory / "plan.json").write_bytes(plan_bytes)
            meta = {"format": "chronvs-phonetic-v1", "speaker": "person", "session": "occasion",
                    "split": "train", "plan_sha256": sha256(plan_bytes)}
            (directory / "session.json").write_text(json.dumps(meta), encoding="utf-8")
            pcm = bytes(SAMPLES * 2)
            write_wav(directory / "00000.wav", pcm)
            row = {"file": "00000.wav", "prompt_index": 0, "text": "pato", "pcm_sha256": sha256(pcm)}
            manifest = directory / "manifest.jsonl"
            manifest.write_text(json.dumps(row) + "\n", encoding="utf-8")
            rows = load_phonetic_dataset(root)
            self.assertEqual(rows[0]["units"], ["p", "a", "t", "u"])
            self.assertFalse(rows[0]["reviewed"])
            self.assertEqual(load_phonetic_dataset(root, {"evaluation"}), [])
            lexicon = {"unit_spec": UNIT_SPEC, "tokens": ["<blank>", "p", "a", "t", "u"],
                       "entries": plan["prompts"]}
            lexicon_path = root / "lexicon.json"
            lexicon_path.write_text(json.dumps(lexicon), encoding="utf-8")
            command = [sys.executable, str(ROOT / "tools/voice/train_phonetic.py"),
                       "--dataset", str(root), "--phonetic-only", "--phonetic-dataset", str(root),
                       "--lexicon", str(lexicon_path), "--epochs", "1", "--hidden", "8"]
            blocked = subprocess.run(command + ["--output", str(root / "blocked")], capture_output=True)
            self.assertNotEqual(blocked.returncode, 0)
            self.assertFalse((root / "blocked").exists())
            trained = subprocess.run(command + ["--allow-unreviewed", "--output", str(root / "candidate")], capture_output=True)
            self.assertEqual(trained.returncode, 0, trained.stderr.decode(errors="replace"))
            report = json.loads((root / "candidate/report.json").read_text(encoding="utf-8"))
            self.assertFalse(report["deployable"])
            self.assertTrue(report["phonetic_only"])
            self.assertEqual(report["speech_samples"], 1)
            self.assertEqual(report["observed_nonblank_units"], 4)
            row["text"] = "bato"
            manifest.write_text(json.dumps(row) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "transcription"):
                load_phonetic_dataset(root)


if __name__ == "__main__":
    unittest.main()
