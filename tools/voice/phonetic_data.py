"""Independent transcribed-utterance data format; never changes word sessions."""
import json
from pathlib import Path
from dataset import read_wav, sha256


def load_phonetic_dataset(root, splits=None):
    rows, hashes, sessions = [], set(), set()
    for manifest in sorted(Path(root).glob("*/*/manifest.jsonl")):
        meta = json.loads(manifest.with_name("session.json").read_text(encoding="utf-8"))
        if meta["format"] != "chronvs-phonetic-v1" or meta["split"] not in ("train", "calibration", "evaluation"):
            raise ValueError(f"Invalid phonetic session: {manifest}")
        identity = (meta["speaker"], meta["session"])
        if identity in sessions:
            raise ValueError("Duplicate phonetic session identity")
        sessions.add(identity)
        if splits is not None and meta["split"] not in splits:
            continue
        plan = json.loads(manifest.with_name("plan.json").read_text(encoding="utf-8"))
        if sha256(manifest.with_name("plan.json").read_bytes()) != meta["plan_sha256"]:
            raise ValueError("Phonetic recording plan changed")
        for line in manifest.read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            path = (manifest.parent / row["file"]).resolve()
            if path.parent != manifest.parent.resolve():
                raise ValueError("Phonetic audio escapes its session")
            digest = sha256(read_wav(path))
            if digest != row["pcm_sha256"] or digest in hashes:
                raise ValueError("Changed or duplicated phonetic audio")
            if row["prompt_index"] is None:
                expected_text, expected_units, reviewed = "_silence", [], True
            else:
                if type(row["prompt_index"]) is not int or not 0 <= row["prompt_index"] < len(plan["prompts"]):
                    raise ValueError("Invalid phonetic prompt index")
                entry = plan["prompts"][row["prompt_index"]]
                expected_text, expected_units, reviewed = entry["text"], entry["units"], entry["reviewed"]
            if row["text"] != expected_text:
                raise ValueError("Phonetic transcription differs from recording plan")
            hashes.add(digest)
            rows.append({**row, "path": path, "label": row["text"], "units": expected_units,
                         "reviewed": reviewed, "speaker": meta["speaker"],
                         "session": meta["session"], "split": meta["split"], "unit_spec": plan["unit_spec"]})
    return rows
