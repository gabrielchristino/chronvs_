"""Evaluate frozen weights and gates on held-out recording sessions."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from dataset import load_dataset
from features import SPEC
from train import load_model, metrics, normalized, predict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output must be a new file")
    report = json.loads((args.candidate / "report.json").read_text(encoding="utf-8"))
    path = args.candidate / "voice.bin"
    if hashlib.sha256(path.read_bytes()).hexdigest() != report["model_sha256"] or report["features"] != SPEC:
        parser.error("Model hash or feature specification differs from report")
    for name, expected_hash in report["source_sha256"].items():
        if name not in ("dataset.py", "features.py", "train.py"):
            parser.error("Unexpected source in candidate report")
        if hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() != expected_hash:
            parser.error(f"Source differs from training candidate: {name}")
    # Validate all sessions together: detect duplicated audio across splits.
    rows = load_dataset(args.dataset)
    labels = report["labels"]
    evaluation = [r for r in rows if r["split"] == "evaluation" and r["label"] in labels]
    trained = {r["pcm_sha256"] for r in report["recordings"]}
    sessions = {(r["speaker"], r["session"]) for r in report["recordings"]}
    if any(r["pcm_sha256"] in trained or (r["speaker"], r["session"]) in sessions for r in evaluation):
        parser.error("Evaluation audio/session overlaps training or calibration")
    missing = set(labels) - {r["label"] for r in evaluation}
    if missing:
        parser.error(f"Missing evaluation labels: {sorted(missing)}")
    mean, std, layers = load_model(path)
    if len(layers[1][2]) != len(labels):
        parser.error("Model/report class count mismatch")
    expected = np.array([labels.index(r["label"]) for r in evaluation])
    probabilities = predict(normalized(evaluation, mean, std), layers)
    raw = probabilities.argmax(axis=1)
    confusion = np.zeros((len(labels), len(labels)), dtype=int)
    np.add.at(confusion, (expected, raw), 1)
    result = {"model_sha256": report["model_sha256"], "labels": labels,
              "threshold": report["threshold"], "margin": report["margin"],
              "metrics": metrics(probabilities, expected, report["threshold"], report["margin"]),
              "confusion_raw": confusion.tolist(),
              "cases": [{"pcm_sha256": r["pcm_sha256"], "speaker": r["speaker"],
                         "session": r["session"], "expected": r["label"],
                         "raw_predicted": labels[int(pred)]} for r, pred in zip(evaluation, raw)]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["metrics"], indent=2))


if __name__ == "__main__":
    main()
