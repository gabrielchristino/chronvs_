"""Transport/dataset regressions; synthetic audio is never personal training data."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/voice"))
from dataset import PCMReceiver, SAMPLES, fnv1a, load_dataset, read_wav, sha256, vocabulary, write_wav


class DatasetTest(unittest.TestCase):
    def test_vocabulary(self):
        words = vocabulary()
        self.assertEqual(len(words), 200)
        self.assertEqual(words[0], "Comprar")
        self.assertEqual(words[-1], "Não")

    def test_actual_c_capture(self):
        receiver, pcm = PCMReceiver(), None
        for line in (ROOT / ".pio/host-tests/voice_capture.txt").read_text().splitlines():
            result = receiver.feed(line)
            if result is not None:
                self.assertIsNone(pcm)
                pcm = result
        self.assertIsNotNone(pcm)
        expected = struct.pack("<256h", 32767, -32768, *range(2, 256)) * 125
        self.assertEqual(pcm, expected)

    def test_missing_packet_and_checksum(self):
        receiver = PCMReceiver()
        receiver.feed("unrelated log")
        receiver.feed("VOXPCM BEGIN 1 16000 32000")
        with self.assertRaises(ValueError):
            receiver.feed("VOXPCM DATA 128 " + "00" * 256)
        receiver.feed("VOXPCM BEGIN 1 16000 32000")
        with self.assertRaises(ValueError):
            receiver.feed("VOXPCM END 00000000")
        receiver.feed("VOXPCM BEGIN 1 16000 32000")
        for offset in range(0, SAMPLES, 128):
            receiver.feed(f"VOXPCM DATA {offset} " + "00" * 256)
        with self.assertRaises(ValueError):
            receiver.feed("VOXPCM END 00000000")
        self.assertEqual(fnv1a(b"hello"), 0x4f9f2cab)

    def test_interleaved_device_log_is_rejected(self):
        receiver = PCMReceiver()
        receiver.feed("VOXPCM BEGIN 1 16000 32000")
        with self.assertRaisesRegex(ValueError, "contaminado"):
            receiver.feed("VOXPCM DATA 0 0000\x1b[0;32mI (470819) display_perf: window_ms=2010\x1b[0m")
        self.assertIsNone(receiver.data)

    def test_integrity_and_session_split(self):
        with tempfile.TemporaryDirectory(dir=ROOT / ".pio/host-tests") as temporary:
            root = Path(temporary)
            for session, split in (("day1", "train"), ("day2", "evaluation")):
                directory = root / "person" / session
                directory.mkdir(parents=True)
                pcm = struct.pack("<h", 11 if split == "train" else 12) * SAMPLES
                write_wav(directory / "00000.wav", pcm)
                self.assertEqual(read_wav(directory / "00000.wav"), pcm)
                (directory / "session.json").write_text(json.dumps({"speaker": "person",
                    "session": session, "split": split, "vocabulary": vocabulary(), "labels": ["Comprar"]}), encoding="utf-8")
                (directory / "manifest.jsonl").write_text(json.dumps({"label": "Comprar",
                    "file": "00000.wav", "pcm_sha256": sha256(pcm)}) + "\n", encoding="utf-8")
            self.assertEqual(len(load_dataset(root)), 2)
            self.assertEqual(len(load_dataset(root, {"train"})), 1)
            write_wav(root / "person/day2/00000.wav", bytes(SAMPLES * 2))
            with self.assertRaises(ValueError):
                load_dataset(root)
            original = read_wav(root / "person/day1/00000.wav")
            write_wav(root / "person/day2/00000.wav", original)
            (root / "person/day2/manifest.jsonl").write_text(json.dumps({"label": "Comprar",
                "file": "00000.wav", "pcm_sha256": sha256(original)}) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicated"):
                load_dataset(root)


if __name__ == "__main__":
    unittest.main()
