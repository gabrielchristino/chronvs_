"""Calibration diagnostic: MFCC templates and time alignment, no new recordings.

This word-template control is NOT a phonetic recognizer, does not evaluate
unseen words and has no calibrated acceptance gate. It tests whether timing
and feature preparation explain the weak neural pilots.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import numpy as np

from dataset import SAMPLES, load_dataset, read_wav
from features import mel_filters

SPEC = {"version": 1, "window": 400, "hop": 160, "fft": 512, "mel_bins": 32,
        "mfcc": "orthonormal-DCT-II-coefficients-1-through-13", "time_bins": 48,
        "trim": "power-q20+0.08*(q95-q20),8-frame-margin", "delta": "first-difference",
        "scale": "train-only-per-coefficient", "alignment": "DTW-squared-euclidean,48-frames"}


def features(pcm):
    signal = np.frombuffer(pcm, dtype="<i2").astype(np.float64) / 32768
    if len(signal) != SAMPLES:
        raise ValueError("Expected 2 s PCM16")
    frames = np.lib.stride_tricks.sliding_window_view(signal, 400)[::160]
    power = (frames ** 2).mean(axis=1)
    low, high = np.quantile(power, [.2, .95])
    active = np.flatnonzero(power > low + .08 * (high - low))
    start = max(0, int(active[0]) - 8) if len(active) else 0
    end = min(len(frames), int(active[-1]) + 9) if len(active) else len(frames)
    spectrum = np.abs(np.fft.rfft(frames * np.hanning(400), n=512)) ** 2 / 512
    logmel = np.log(np.maximum(spectrum @ mel_filters().T, 1e-10))
    k = np.arange(1, 14)[:, None]
    dct = np.cos(np.pi / 32 * k * (np.arange(32) + .5)) * np.sqrt(2 / 32)
    cepstra = (logmel @ dct.T)[start:end]
    grid = np.linspace(0, len(cepstra) - 1, 48)
    resized = np.stack([np.interp(grid, np.arange(len(cepstra)), cepstra[:, i]) for i in range(13)], axis=1)
    return np.concatenate([resized, np.gradient(resized, axis=0)], axis=1).astype(np.float32)


def dtw_distance(a, b):
    if a.ndim != 2 or b.ndim != 2 or a.shape[1] != b.shape[1] or not len(a) or not len(b):
        raise ValueError("Invalid DTW feature sequences")
    cost = ((a[:, None, :] - b[None, :, :]) ** 2).mean(axis=2)
    grid = np.full((len(a) + 1, len(b) + 1), np.inf)
    grid[0, 0] = 0
    for diagonal in range(2, len(a) + len(b) + 1):
        i = np.arange(max(1, diagonal - len(b)), min(len(a), diagonal - 1) + 1)
        j = diagonal - i
        grid[i, j] = cost[i - 1, j - 1] + np.minimum(np.minimum(grid[i - 1, j], grid[i, j - 1]), grid[i - 1, j - 1])
    return float(grid[-1, -1] / (len(a) + len(b)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new output file")
    rows = load_dataset(args.dataset, {"train", "calibration"})
    train = [row for row in rows if row["split"] == "train"]
    calibration = [row for row in rows if row["split"] == "calibration"]
    if not train or not calibration:
        parser.error("Both train and calibration are required")
    templates = np.stack([features(read_wav(row["path"])) for row in train])
    mean = templates.mean(axis=(0, 1))
    std = np.maximum(templates.std(axis=(0, 1)), .1)
    templates = (templates - mean) / std
    classes = sorted({row["label"] for row in train})
    cases = []
    for row in calibration:
        query = (features(read_wav(row["path"])) - mean) / std
        distances = [dtw_distance(query, template) for template in templates]
        scores = {label: float(np.mean(sorted(distance for template, distance in zip(train, distances)
                                            if template["label"] == label)[:3])) for label in classes}
        ranked = sorted(scores, key=scores.get)
        words = [label for label in ranked if not label.startswith("_")]
        cases.append({"file": row["file"], "session": row["session"], "label": row["label"],
                      "pcm_sha256": row["pcm_sha256"], "predicted": ranked[0],
                      "forced_word": words[0], "distances": scores})
    speech = [case for case in cases if not case["label"].startswith("_")]
    result = {"status": "calibration-diagnostic-only-no-acceptance-gate", "spec": SPEC,
              "speech_cases": len(speech),
              "raw_word_correct": sum(case["predicted"] == case["label"] for case in speech),
              "forced_word_correct": sum(case["forced_word"] == case["label"] for case in speech),
              "all_classes_correct": sum(case["predicted"] == case["label"] for case in cases),
              "cases_total": len(cases), "counts": dict(Counter(case["label"] for case in cases)),
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "training_audio": [row["pcm_sha256"] for row in train], "cases": cases}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key not in ("cases", "counts", "training_audio")}, indent=2))


if __name__ == "__main__":
    main()
