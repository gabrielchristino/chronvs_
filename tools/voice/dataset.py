"""Shared, dependency-free vocabulary, PCM transport and dataset validation."""
import array
import hashlib
import json
from pathlib import Path
import re
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
RATE, SAMPLES = 16000, 32000
SPECIAL = ["_unknown", "_silence"]


def vocabulary():
    source = (ROOT / "src/services/voice_lab_service.c").read_text(encoding="utf-8")
    table = source.split("static const voice_word_t words[", 1)[1].split("};", 1)[0]
    words = re.findall(r'\{"([^"\\]+)",\s*"[^"\\]+"\}', table)
    if len(words) != 200 or len(set(words)) != 200:
        raise ValueError("Expected the 200 unique Portuguese words from Vox")
    return words


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


class PCMReceiver:
    """Reject missing, reordered or corrupted packets; ignore unrelated logs."""
    def __init__(self):
        self.data = None

    def feed(self, line):
        line = line.strip()
        if line.startswith("VOXPCM BEGIN"):
            self.data = None
            if line != f"VOXPCM BEGIN 1 {RATE} {SAMPLES}":
                raise ValueError("Unsupported PCM format")
            self.data = bytearray()
        elif line.startswith("VOXPCM DATA"):
            if self.data is None:
                return None
            fields = line.split()
            if len(fields) != 4:
                self.data = None
                raise ValueError("Registro PCM contaminado por logs ou dados incompletos")
            _, _, offset, encoded = fields
            chunk = bytes.fromhex(encoded)
            if int(offset) * 2 != len(self.data) or len(chunk) != 256:
                self.data = None
                raise ValueError("PCM packet missing, out of order or invalid")
            self.data.extend(chunk)
            if len(self.data) > SAMPLES * 2:
                self.data = None
                raise ValueError("PCM exceeds capture limit")
        elif line.startswith("VOXPCM END") and self.data is not None:
            data, self.data = bytes(self.data), None
            if len(data) != SAMPLES * 2 or fnv1a(data) != int(line.split()[2], 16):
                raise ValueError("Incomplete PCM or checksum mismatch")
            return data
        return None


def write_wav(path, pcm):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(pcm)


def read_wav(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(),
                source.getnframes(), source.getcomptype()) != (1, 2, RATE, SAMPLES, "NONE"):
            raise ValueError(f"Invalid WAV format: {path}")
        data = source.readframes(SAMPLES)
    if len(data) != SAMPLES * 2:
        raise ValueError(f"Truncated WAV: {path}")
    return data


def pcm_stats(pcm):
    samples = array.array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    rms = (sum(value * value for value in samples) / len(samples)) ** .5 / 32768
    clipped = sum(abs(value) >= 32767 for value in samples) / len(samples)
    return {"rms": rms, "clipped_fraction": clipped}


def load_dataset(root, splits=None):
    """Session is the split unit. Verify audio hashes before any training."""
    root = Path(root).resolve()
    rows, hashes, sessions = [], set(), set()
    for manifest in sorted(root.glob("*/*/manifest.jsonl")):
        meta = json.loads(manifest.with_name("session.json").read_text(encoding="utf-8"))
        identity = (meta["speaker"], meta["session"])
        if identity in sessions or meta["split"] not in ("train", "calibration", "evaluation"):
            raise ValueError(f"Duplicate session or invalid split: {manifest}")
        sessions.add(identity)
        if meta["vocabulary"] != vocabulary():
            raise ValueError(f"Vocabulary differs from firmware: {manifest}")
        if splits is not None and meta["split"] not in splits:
            continue
        for line in manifest.read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            path = (manifest.parent / row["file"]).resolve()
            if path.parent != manifest.parent.resolve():
                raise ValueError("Audio path escapes session directory")
            pcm = read_wav(path)
            digest = sha256(pcm)
            if digest != row["pcm_sha256"] or digest in hashes:
                raise ValueError(f"Changed or duplicated audio: {path}")
            if row["label"] not in meta["labels"]:
                raise ValueError(f"Unknown label: {path}")
            hashes.add(digest)
            rows.append({**row, "path": path, "split": meta["split"],
                         "speaker": meta["speaker"], "session": meta["session"]})
    if not rows:
        raise ValueError("No recorded audio found")
    return rows
