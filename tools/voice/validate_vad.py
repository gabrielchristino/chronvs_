"""Train-selected WebRTC speech activity gate; diagnostic on existing audio.

The gate is not a recognizer and does not reject unknown vocabulary. Reference
transcriptions are reused locally; no labels, recordings or firmware change.
"""
import argparse
import importlib.metadata
import hashlib
import json
from pathlib import Path
import webrtcvad

from dataset import load_dataset, read_wav
from phonetic_data import load_phonetic_dataset
from reference_metrics import review_queue, summarize

FRAME_BYTES = 640  # 20 ms, 16 kHz, mono signed 16-bit PCM


def longest_run(flags):
    longest = current = 0
    for active in flags:
        current = current + 1 if active else 0
        longest = max(longest, current)
    return longest


def activity(pcm, mode):
    if not pcm or len(pcm) % FRAME_BYTES:
        raise ValueError("Expected whole 20 ms PCM16 frames")
    detector = webrtcvad.Vad(mode)  # Reset adaptive state between recordings.
    flags = [detector.is_speech(pcm[i:i+FRAME_BYTES], 16000) for i in range(0, len(pcm), FRAME_BYTES)]
    return {"frames": len(flags), "speech_frames": sum(flags), "longest_run": longest_run(flags)}


def gate_metrics(cases, mode, minimum):
    speech = [case for case in cases if case["expected"] != "_silence"]
    silence = [case for case in cases if case["expected"] == "_silence"]
    tp = sum(case["vad"][str(mode)]["longest_run"] >= minimum for case in speech)
    fp = sum(case["vad"][str(mode)]["longest_run"] >= minimum for case in silence)
    return {"speech_cases": len(speech), "speech_pass": tp,
            "speech_recall": tp / len(speech) if speech else None,
            "silence_cases": len(silence), "silence_pass": fp}


def choose_gate(cases):
    train = [case for case in cases if case["split"] == "train"]
    candidates = []
    for mode in range(4):
        for minimum in (3, 5, 8, 10, 15, 20):
            value = gate_metrics(train, mode, minimum)
            candidates.append({"mode": mode, "minimum_frames": minimum, "training": value})
    if not train or candidates[0]["training"]["speech_recall"] is None:
        raise ValueError("Need training speech to select the activity gate")
    qualified = [value for value in candidates if value["training"]["speech_recall"] >= .95]
    def key(value):
        metrics = value["training"]
        primary = (-metrics["silence_pass"], metrics["speech_pass"]) if qualified else (metrics["speech_pass"], -metrics["silence_pass"])
        return (*primary, -value["mode"], -value["minimum_frames"])
    selected = max(qualified or candidates, key=key)
    return {**selected, "selection": "training-sessions-only", "training_recall_target": .95,
            "training_recall_target_met": bool(qualified)}, candidates


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--words", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--phonetic", type=Path, default=Path("data/voice-phonetic-local"))
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new output file")
    rows = load_dataset(args.words) + load_phonetic_dataset(args.phonetic)
    reference_report = json.loads((args.reference / "report.json").read_text(encoding="utf-8"))
    if not reference_report["complete"]:
        parser.error("A complete reference audit is required")
    reference = [json.loads(line) for line in (args.reference / "predictions.jsonl").read_text(encoding="utf-8").splitlines()]
    by_hash = {case["pcm_sha256"]: case for case in reference}
    if len(by_hash) != len(rows) or set(by_hash) != {row["pcm_sha256"] for row in rows}:
        parser.error("Reference and VAD must use the same audio snapshot")
    cases = []
    for row in rows:
        pcm = read_wav(row["path"])
        cases.append({"id": by_hash[row["pcm_sha256"]]["id"], "pcm_sha256": row["pcm_sha256"],
                      "expected": row["label"], "split": row["split"], "session": row["session"],
                      "vad": {str(mode): activity(pcm, mode) for mode in range(4)}})
    gate, candidates = choose_gate(cases)
    gated = [{**by_hash[case["pcm_sha256"]], "hypothesis":
              by_hash[case["pcm_sha256"]]["hypothesis"] if case["vad"][str(gate["mode"])]["longest_run"] >= gate["minimum_frames"] else ""}
             for case in cases]
    calibration = [case for case in cases if case["split"] == "calibration"]
    result = {"status": "PC-activity-gate-diagnostic-not-device-validation",
              "reference_commit": reference_report["reference"]["commit"],
              "reference_weights_sha256": reference_report["reference"]["files_sha256"]["model.safetensors"],
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "library": "webrtcvad-wheels", "version": importlib.metadata.version("webrtcvad-wheels"),
              "frame_ms": 20, "gate": gate, "training_candidates": candidates,
              "calibration_activity": gate_metrics(calibration, gate["mode"], gate["minimum_frames"]),
              "all_activity": gate_metrics(cases, gate["mode"], gate["minimum_frames"]),
              "reference_before_gate": summarize(reference), "reference_after_gate": summarize(gated),
              "calibration_reference_after_gate": summarize([case for case in gated if case["split"] == "calibration"]),
              "review_after_gate": review_queue(gated),
              "cases": cases,
              "limitations": ["Speech activity does not identify words or reject speech outside the lexicon.",
                              "Calibration recordings are known development data, not a fresh blind test.",
                              "No device inference or dataset-label corrections were performed."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key not in ("cases", "training_candidates")}, indent=2))


if __name__ == "__main__":
    main()
