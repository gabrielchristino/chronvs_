"""Read-only audit of current voice data; export a private workflow snapshot.

Never opens serial, uploads voice, downloads datasets, changes splits or
marks generated IPA as reviewed. Later milestones need actual evidence.
"""
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from dataset import load_dataset, pcm_stats, read_wav
from phonetic_data import load_phonetic_dataset


def audit(word_root, phonetic_root):
    word_root, phonetic_root = Path(word_root), Path(phonetic_root)
    words = load_dataset(word_root) if any(word_root.glob("*/*/manifest.jsonl")) else []
    phonetic = load_phonetic_dataset(phonetic_root)
    rows = words + phonetic
    hashes = [row["pcm_sha256"] for row in rows]
    if len(hashes) != len(set(hashes)):
        raise ValueError("Duplicated audio across word and phonetic datasets")
    sessions = defaultdict(lambda: Counter())
    clipped, quality = [], {}
    for row in rows:
        stats = pcm_stats(read_wav(row["path"]))
        if stats != {"rms": row["rms"], "clipped_fraction": row["clipped_fraction"]}:
            raise ValueError(f"Stored audio statistics differ from WAV: {row['path']}")
        identity = f"{row['speaker']}/{row['session']}"
        sessions[identity][row["split"]] += 1
        quality[row["pcm_sha256"]] = stats
        if stats["clipped_fraction"]:
            clipped.append({"file": str(row["path"]), "label": row["label"], **stats})
    pending = sorted([str(p) for root in (word_root, phonetic_root) for p in root.glob("*/*/pending.wav")])
    context_counts = Counter(unit for row in phonetic for unit in set(row["units"]) if unit != "|")
    tasks = defaultdict(list)
    for row in rows:
        if not row["label"].startswith("_"):
            tasks[row["label"]].append(row)
    review = []
    for text, group in sorted(tasks.items()):
        hypothesis = next((row["units"] for row in group if "units" in row), None)
        review.append({"text_expected": text, "ipa_units_hypothesis": hypothesis,
                       "review_state": "pending-audio-and-transcript-check",
                       "samples": [{"path": str(row["path"]), "pcm_sha256": row["pcm_sha256"],
                                    "split": row["split"], "session": row["session"],
                                    "audio": quality[row["pcm_sha256"]]} for row in group],
                       "instruction": "Keep original labels frozen. Store any reviewed correction in a separate, versioned derivative."})
    snapshot = {"generated_utc": datetime.now(timezone.utc).isoformat(),
                "recordings": len(rows), "wav_seconds": len(rows) * 2,
                "splits": dict(Counter(row["split"] for row in rows)),
                "sessions": {name: dict(counts) for name, counts in sessions.items()},
                "transcribed_speech_recordings": sum(not row["label"].startswith("_") for row in rows),
                "untranscribed_unknown_recordings": sum(row["label"] == "_unknown" for row in rows),
                "silence_recordings": sum(row["label"] == "_silence" for row in rows),
                "phonetic_context_counts": dict(sorted(context_counts.items())),
                "low_coverage_units": {unit: count for unit, count in sorted(context_counts.items()) if count <= 2},
                "phonetic_speech_targets_marked_reviewed": sum(row["reviewed"] for row in phonetic if row["units"]),
                "review_groups": len(review), "clipped_files": clipped, "pending_audio": pending,
                "snapshot_audio_sha256": hashlib.sha256("\n".join(sorted(hashes)).encode("ascii")).hexdigest(),
                "independent_evaluation_recordings": sum(row["split"] == "evaluation" for row in rows),
                "limitations": ["WAV duration includes silence; it is not the duration of spoken phonemes.",
                                "Coverage uses expected automatic labels, not verified phonetic perception.",
                                "This audit does not measure recognizer accuracy or prove deployment readiness."]}
    return snapshot, review


def reference_evidence(path, snapshot):
    """Accept completed evidence only for the exact audited audio snapshot."""
    path = Path(path)
    report = json.loads(path.read_text(encoding="utf-8"))
    if not report["complete"] or report["status"] != "PC-reference-audit-not-device-validation":
        raise ValueError("Reference audit is incomplete or unsupported")
    cases = [json.loads(line) for line in path.with_name("predictions.jsonl").read_text(encoding="utf-8").splitlines()]
    hashes = [case["pcm_sha256"] for case in cases]
    digest = hashlib.sha256("\n".join(sorted(hashes)).encode("ascii")).hexdigest()
    if (len(hashes) != len(set(hashes)) or len(hashes) != snapshot["recordings"] or
            report["processed"] != len(hashes) or digest != snapshot["snapshot_audio_sha256"]):
        raise ValueError("Reference evidence belongs to a different audio snapshot")
    return {"path": str(path), "model_id": report["reference"]["model_id"],
            "commit": report["reference"]["commit"], "metrics": report["metrics"],
            "meaning": "transcription audit only; labels not automatically reviewed or corrected"}


def public_training_evidence(report_path, manifest_path):
    """Describe a completed development run, never mark it device-ready."""
    report = json.loads(Path(report_path).read_text(encoding="utf-8"))
    raw = Path(manifest_path).read_bytes()
    manifest = json.loads(raw)
    if (report["status"] != "public-character-CTC-development-baseline-not-device-ready" or
            report["fingerprint"]["manifest_sha256"] != hashlib.sha256(raw).hexdigest() or
            report["personal_audio_used"] or report["test_audio_used"] or report["device_exported"] or
            manifest["dataset"] != "google/fleurs" or manifest["config"] != "pt_br"):
        raise ValueError("Unsupported public training evidence or changed manifest")
    for split in ("train", "validation"):
        expected = sum(row["split"] == split for row in manifest["records"])
        if not expected or report[split]["cases"] != expected:
            raise ValueError("Public training evidence has different partition sizes")
    if len(report["history"]) != report["fingerprint"]["epochs"]:
        raise ValueError("Public development run is incomplete")
    return {"report": str(report_path), "manifest": str(manifest_path),
            "manifest_sha256": report["fingerprint"]["manifest_sha256"],
            "selected_epoch": report["selected_epoch"], "parameters": report["parameters"],
            "validation": {key: value for key, value in report["validation"].items() if key != "predictions"},
            "meaning": "completed public development experiment; no personal adaptation or device approval"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--words", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--phonetic", type=Path, default=Path("data/voice-phonetic-local"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference-report", type=Path)
    parser.add_argument("--public-corpus-report", type=Path)
    parser.add_argument("--public-training-report", type=Path)
    parser.add_argument("--public-manifest", type=Path)
    args = parser.parse_args()
    if bool(args.public_training_report) != bool(args.public_manifest):
        parser.error("Public training report requires its manifest, and vice versa")
    if args.output.exists():
        parser.error("Use a new snapshot directory")
    snapshot, review = audit(args.words, args.phonetic)
    policy = json.loads(Path(__file__).with_name("workflow.json").read_text(encoding="utf-8"))
    stages = [{**stage, "status": "pending-evidence"} for stage in policy["stages"]]
    stages[0]["status"] = "integrity-verified-with-pending-audio" if snapshot["pending_audio"] else "integrity-verified"
    if args.reference_report:
        snapshot["reference_evidence"] = reference_evidence(args.reference_report, snapshot)
        stages[1]["status"] = "transcriptions-generated-needs-review"
    if args.public_corpus_report:
        evidence = json.loads(args.public_corpus_report.read_text(encoding="utf-8"))
        if evidence["status"] != "metadata-validated-only" or evidence["config"] != "pt_br" or evidence["dataset"] != "google/fleurs":
            raise ValueError("Unsupported public corpus evidence")
        snapshot["public_corpus_metadata"] = evidence
        stages[2]["status"] = "metadata-verified-training-not-started"
    if args.public_training_report:
        snapshot["public_training_evidence"] = public_training_evidence(args.public_training_report, args.public_manifest)
        stages[2]["status"] = "public-development-run-complete-quality-not-approved"
    snapshot["workflow"] = stages
    snapshot["proposed_pilot_targets"] = policy["proposed_pilot_targets"]
    args.output.mkdir(parents=True)
    for name, value in (("snapshot.json", snapshot), ("review-queue.json", review), ("workflow.json", policy)):
        (args.output / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: snapshot[key] for key in ("recordings", "wav_seconds", "sessions", "review_groups",
                                                   "independent_evaluation_recordings", "snapshot_audio_sha256")}, indent=2))
    print("Snapshot exported locally. No session or pronunciation label was changed.")


if __name__ == "__main__":
    main()
