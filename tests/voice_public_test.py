import io
import hashlib
import json
from pathlib import Path
import sys
import unittest
import tempfile
import wave

import numpy as np
import torch
from torch.nn import functional as F

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "voice"))
from prepare_public import canonical_wav, overlap_keys, parse_table, pcm_from_wav
from public_ctc import CompactCharacterCTC, pack_batch, required_ctc_frames, wav_features
from workflow_status import public_training_evidence


class PublicCorpusTests(unittest.TestCase):
    def test_training_evidence_requires_complete_run_and_exact_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = {"dataset": "google/fleurs", "config": "pt_br",
                        "records": [{"split": "train"}, {"split": "validation"}]}
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            report = {"status": "public-character-CTC-development-baseline-not-device-ready",
                      "fingerprint": {"manifest_sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "epochs": 2},
                      "personal_audio_used": False, "test_audio_used": False, "device_exported": False,
                      "train": {"cases": 1}, "validation": {"cases": 1},
                      "history": [{}, {}], "selected_epoch": 1, "parameters": 100}
            report_path = root / "report.json"
            report_path.write_text(json.dumps(report), encoding="utf-8")
            self.assertEqual(public_training_evidence(report_path, path)["parameters"], 100)
            report["history"].pop()
            report_path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "incomplete"):
                public_training_evidence(report_path, path)
            path.write_text(json.dumps({**manifest, "changed": True}), encoding="utf-8")
            with self.assertRaises(ValueError):
                public_training_evidence(report_path, path)

    def test_overlap_excludes_both_sentence_id_and_spelling(self):
        tables = {"train": {"a": {"sentence_id": "1", "text": "Olá, mundo!"}},
                  "validation": {"b": {"sentence_id": "2", "text": "olá mundo"}},
                  "test": {"c": {"sentence_id": "1", "text": "outra frase"}}}
        blocked = overlap_keys(tables)
        self.assertIn("id:1", blocked)
        self.assertIn("text:olá mundo", blocked)
        self.assertNotIn("id:2", blocked)

    def test_conversion_preserves_duration_and_validates_metadata(self):
        import soundfile as sf
        source = io.BytesIO()
        signal = np.zeros(48000, dtype=np.float32)
        signal[:4] = [-1.1, -0.5, 0.5, 1.1]
        sf.write(source, signal, 16000, format="WAV", subtype="FLOAT")
        raw, clipped = canonical_wav(source.getvalue())
        self.assertEqual(pcm_from_wav(raw)[0], 48000)
        self.assertEqual(clipped, 2)
        with wave.open(io.BytesIO(raw)) as wav:
            self.assertEqual(np.frombuffer(wav.readframes(4), dtype="<i2").tolist(),
                             [-32768, -16384, 16384, 32767])
        self.assertEqual(len(wav_features(raw)), 298)
        with self.assertRaises(ValueError):
            parse_table("1\t../escape.wav\tx\tx\tx\t10\tMALE")

    def test_variable_lengths_match_individual_ctc_and_ignore_padding(self):
        torch.manual_seed(41)
        model = CompactCharacterCTC(4, hidden=8).eval()
        items = [(np.random.default_rng(41).normal(size=(31, 32)).astype(np.float32), [1, 1, 2]),
                 (np.random.default_rng(73).normal(size=(19, 32)).astype(np.float32), [2, 3])]
        x, y, lengths, targets = pack_batch(items)
        batched = model(x).log_softmax(-1)
        losses = F.ctc_loss(batched.transpose(0, 1), y, lengths, targets, reduction="none")
        for i, (features, target) in enumerate(items):
            single = model(torch.tensor(features)[None]).log_softmax(-1)
            torch.testing.assert_close(single[0], batched[i, :len(features)], atol=2e-6, rtol=2e-6)
            loss = F.ctc_loss(single.transpose(0, 1), torch.tensor(target),
                              torch.tensor([len(features)]), torch.tensor([len(target)]), reduction="none")
            torch.testing.assert_close(loss[0], losses[i])
        self.assertEqual(required_ctc_frames([1, 1, 1]), 5)
        with self.assertRaises(ValueError):
            pack_batch([(np.zeros((4, 32), dtype=np.float32), [1, 1, 1])])


if __name__ == "__main__":
    unittest.main()
