"""Workflow audit correctness, immutability and cross-dataset contamination."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import SAMPLES, pcm_stats, sha256, vocabulary, write_wav
from workflow_status import audit, reference_evidence


def make_phonetic(root):
    directory = root / "person/occasion"
    directory.mkdir(parents=True)
    plan = {"unit_spec": {"version": 1}, "prompts": [
        {"text": "pato", "units": ["p", "a", "t", "u"], "reviewed": False}]}
    content = json.dumps(plan).encode()
    (directory / "plan.json").write_bytes(content)
    (directory / "session.json").write_text(json.dumps({"format": "chronvs-phonetic-v1",
        "speaker": "person", "session": "occasion", "split": "train", "plan_sha256": sha256(content)}), encoding="utf-8")
    rows = []
    for i, pcm in enumerate((b"\x64\x00" * SAMPLES, bytes(SAMPLES * 2))):
        filename = f"{i:05d}.wav"
        write_wav(directory / filename, pcm)
        rows.append({"file": filename, "text": "pato" if not i else "_silence",
                     "prompt_index": 0 if not i else None, "pcm_sha256": sha256(pcm), **pcm_stats(pcm)})
    (directory / "manifest.jsonl").write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
    return directory


class WorkflowTest(unittest.TestCase):
    def test_counts_and_originals_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = make_phonetic(root / "phonetic")
            manifest = (directory / "manifest.jsonl").read_bytes()
            plan = (directory / "plan.json").read_bytes()
            snapshot, review = audit(root / "words", root / "phonetic")
            self.assertEqual(snapshot["recordings"], 2)
            self.assertEqual(snapshot["wav_seconds"], 4)
            self.assertEqual(snapshot["transcribed_speech_recordings"], 1)
            self.assertEqual(snapshot["silence_recordings"], 1)
            self.assertEqual(snapshot["independent_evaluation_recordings"], 0)
            self.assertEqual(snapshot["phonetic_speech_targets_marked_reviewed"], 0)
            self.assertEqual(len(review), 1)
            self.assertEqual((directory / "manifest.jsonl").read_bytes(), manifest)
            self.assertEqual((directory / "plan.json").read_bytes(), plan)

    def test_duplicate_between_roots_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            make_phonetic(root / "phonetic")
            directory = root / "words/person/legacy"
            directory.mkdir(parents=True)
            pcm = b"\x64\x00" * SAMPLES
            write_wav(directory / "00000.wav", pcm)
            (directory / "session.json").write_text(json.dumps({"speaker": "person", "session": "legacy",
                "split": "train", "vocabulary": vocabulary(), "labels": ["Comprar"]}), encoding="utf-8")
            (directory / "manifest.jsonl").write_text(json.dumps({"file": "00000.wav", "label": "Comprar",
                "pcm_sha256": sha256(pcm), **pcm_stats(pcm)}) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "across"):
                audit(root / "words", root / "phonetic")

    def test_incorrect_statistics_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = make_phonetic(root / "phonetic")
            path = directory / "manifest.jsonl"
            rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            rows[0]["rms"] = 1.0
            path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "statistics"):
                audit(root / "words", root / "phonetic")

    def test_reference_must_match_audio_and_be_complete(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = make_phonetic(root / "phonetic")
            snapshot, _ = audit(root / "words", root / "phonetic")
            predictions = [{"pcm_sha256": json.loads(line)["pcm_sha256"]}
                           for line in (directory / "manifest.jsonl").read_text(encoding="utf-8").splitlines()]
            (root / "predictions.jsonl").write_text("".join(json.dumps(row) + "\n" for row in predictions), encoding="utf-8")
            report = {"complete": True, "status": "PC-reference-audit-not-device-validation", "processed": 2,
                      "reference": {"model_id": "test", "commit": "frozen"}, "metrics": {}}
            path = root / "report.json"
            path.write_text(json.dumps(report), encoding="utf-8")
            self.assertEqual(reference_evidence(path, snapshot)["commit"], "frozen")
            report["complete"] = False
            path.write_text(json.dumps(report), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "incomplete"):
                reference_evidence(path, snapshot)
            report["complete"] = True
            path.write_text(json.dumps(report), encoding="utf-8")
            predictions.pop()
            (root / "predictions.jsonl").write_text(json.dumps(predictions[0]) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "different audio"):
                reference_evidence(path, snapshot)


if __name__ == "__main__":
    unittest.main()
