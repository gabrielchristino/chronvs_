"""Exercise real training/export/evaluation with synthetic signals, not speech.

Verifies reproducibility and data isolation, not recognition quality.
Requires the optional training dependencies in tools/voice/requirements.txt.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import SAMPLES, read_wav, sha256, vocabulary, write_wav
from features import DIM, extract
from train import load_model, predict


class TrainingTest(unittest.TestCase):
    def run_tool(self, command):
        result = subprocess.run(command, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))

    def test_train_export_evaluate(self):
        with tempfile.TemporaryDirectory(dir=ROOT / ".pio/host-tests") as temporary:
            root = Path(temporary)
            rng = np.random.default_rng(123)
            labels = ["_unknown", "_silence", "Comprar", "Fazer"]
            for session, split in (("one", "train"), ("two", "calibration"), ("three", "evaluation")):
                directory = root / "data/person" / session
                directory.mkdir(parents=True)
                meta = {"speaker": "person", "session": session, "split": split,
                        "vocabulary": vocabulary(), "labels": labels}
                (directory / "session.json").write_text(json.dumps(meta), encoding="utf-8")
                rows = []
                for repeat in range(3):
                    for index, label in enumerate(labels):
                        t = np.arange(SAMPLES) / 16000
                        signal = 4000 * np.sin(2 * np.pi * (250 + index * 500) * t)
                        signal += rng.normal(0, 30, SAMPLES)
                        pcm = signal.astype("<i2").tobytes()
                        name = f"{len(rows):05d}.wav"
                        write_wav(directory / name, pcm)
                        rows.append({"label": label, "file": name, "pcm_sha256": sha256(pcm)})
                (directory / "manifest.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
            feature = extract(read_wav(root / "data/person/one/00000.wav"))
            self.assertEqual(feature.size, DIM)
            self.assertTrue(np.isfinite(feature).all())
            base = [sys.executable, str(ROOT / "tools/voice/train.py"), "--dataset", str(root / "data"),
                    "--words", "Comprar", "Fazer", "--epochs", "4", "--hidden", "8"]
            for name in ("candidate-a", "candidate-b"):
                self.run_tool(base + ["--output", str(root / name)])
            a, b = root / "candidate-a", root / "candidate-b"
            self.assertEqual((a / "voice.bin").read_bytes(), (b / "voice.bin").read_bytes())
            report = json.loads((a / "report.json").read_text(encoding="utf-8"))
            self.assertFalse(any(r["split"] == "evaluation" for r in report["recordings"]))
            self.assertEqual(report["model_sha256"], hashlib.sha256((a / "voice.bin").read_bytes()).hexdigest())
            mean, std, layers = load_model(a / "voice.bin")
            probabilities = predict(((feature - mean) / std).reshape(1, DIM), layers)
            np.testing.assert_allclose(probabilities.sum(axis=1), [1], atol=1e-6)
            evaluate = [sys.executable, str(ROOT / "tools/voice/evaluate.py"), "--dataset", str(root / "data"),
                        "--candidate", str(a), "--output", str(root / "evaluation.json")]
            self.run_tool(evaluate)
            result = json.loads((root / "evaluation.json").read_text(encoding="utf-8"))
            self.assertEqual(result["metrics"]["cases"], 12)
            # Tampering with the candidate is rejected before evaluation.
            (a / "voice.bin").write_bytes((a / "voice.bin").read_bytes() + b"x")
            evaluate[-1] = str(root / "invalid.json")
            self.assertNotEqual(subprocess.run(evaluate, capture_output=True).returncode, 0)


if __name__ == "__main__":
    unittest.main()
