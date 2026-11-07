"""Train a personal, isolated-word PT-BR classifier in CPU; no evaluation split.

Exports a documented int8-weight MLP candidate, not an ESP-SR model. The current
firmware does not load this format. Do not claim device accuracy from this run.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import random
import struct

import numpy as np
import torch
from torch import nn
from dataset import SPECIAL, load_dataset, read_wav, vocabulary
from features import DIM, SPEC, extract


def metrics(probabilities, expected, threshold, margin):
    order = np.argsort(probabilities, axis=1)
    raw = order[:, -1]
    top = probabilities[np.arange(len(raw)), raw]
    gap = top - probabilities[np.arange(len(raw)), order[:, -2]]
    accepted = (raw >= 2) & (top >= threshold) & (gap >= margin)
    correct = accepted & (raw == expected)
    known = expected >= 2
    count = int(accepted.sum())
    return {"cases": len(expected), "known": int(known.sum()),
            "accepted": count, "correct_accepted": int(correct.sum()),
            "wrong_accepted": int((accepted & (raw != expected)).sum()),
            "background_false_accept": int((accepted & ~known).sum()),
            "precision": float(correct.sum() / count) if count else None,
            "known_coverage": float(correct.sum() / known.sum()) if known.any() else None,
            "raw_accuracy": float((raw == expected).mean())}


def calibrate(probabilities, expected, target):
    best = None
    for threshold in (.4, .5, .6, .7, .8, .9, .95, .98, .99, .995):
        for margin in (.0, .1, .2, .3, .5, .7):
            value = metrics(probabilities, expected, threshold, margin)
            if value["precision"] is not None and value["precision"] >= target:
                key = (value["correct_accepted"], -value["wrong_accepted"], threshold, margin)
                if best is None or key > best[0]:
                    best = (key, threshold, margin, value)
    if best is None:
        return 1.01, 1.01, metrics(probabilities, expected, 1.01, 1.01)
    return best[1:]


def normalized(rows, mean, std):
    return np.stack([((extract(read_wav(row["path"])) - mean) / std).ravel() for row in rows])


def quantize(weight):
    scales = np.maximum(np.abs(weight).max(axis=1) / 127, 1e-12).astype(np.float32)
    quantized = np.clip(np.rint(weight / scales[:, None]), -127, 127).astype(np.int8)
    return quantized, scales


def predict(x, layers):
    for index in (0, 1):
        weights, scales, bias = layers[index]
        x = x @ (weights.astype(np.float32) * scales[:, None]).T + bias
        if index == 0:
            x = np.maximum(x, 0)
    x = np.exp(x - x.max(axis=1, keepdims=True))
    return x / x.sum(axis=1, keepdims=True)


def save_model(path, labels, mean, std, layers):
    # All numbers little-endian. See docs/voice-training.md for the full layout.
    hidden, classes = len(layers[0][2]), len(labels)
    with path.open("wb") as output:
        output.write(struct.pack("<8sIIII", b"CVVOX001", 1, DIM, hidden, classes))
        output.write(np.asarray(mean, dtype="<f4").tobytes())
        output.write(np.asarray(std, dtype="<f4").tobytes())
        for weights, scales, bias in layers:
            output.write(weights.tobytes())
            output.write(np.asarray(scales, dtype="<f4").tobytes())
            output.write(np.asarray(bias, dtype="<f4").tobytes())


def load_model(path):
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError("Truncated model")
    magic, version, dim, hidden, classes = struct.unpack_from("<8sIIII", data)
    if (magic, version, dim) != (b"CVVOX001", 1, DIM) or not (1 <= hidden <= 1024 and 3 <= classes <= 202):
        raise ValueError("Unsupported voice model header")
    offset = 24
    def take(dtype, count):
        nonlocal offset
        size = np.dtype(dtype).itemsize * count
        if offset + size > len(data):
            raise ValueError("Truncated model data")
        values = np.frombuffer(data, dtype=dtype, count=count, offset=offset).copy()
        offset += size
        return values
    mean, std = take("<f4", 32), take("<f4", 32)
    layers = []
    for inputs, outputs in ((DIM, hidden), (hidden, classes)):
        layers.append((take("i1", inputs * outputs).reshape(outputs, inputs),
                       take("<f4", outputs), take("<f4", outputs)))
    if offset != len(data) or not np.isfinite(mean).all() or not np.isfinite(std).all() or (std <= 0).any():
        raise ValueError("Invalid model normalization or size")
    for _, scales, bias in layers:
        if not np.isfinite(scales).all() or (scales <= 0).any() or not np.isfinite(bias).all():
            raise ValueError("Invalid model weights")
    return mean, std, layers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--words", nargs="+", help="Pilot subset; default: all 200")
    parser.add_argument("--epochs", type=int, default=150)
    parser.add_argument("--hidden", type=int, default=96)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("--target-precision", type=float, default=.95)
    args = parser.parse_args()
    words = args.words or vocabulary()
    if len(set(words)) != len(words) or any(word not in vocabulary() for word in words):
        parser.error("Invalid vocabulary subset")
    if args.epochs < 1 or not 1 <= args.hidden <= 1024 or not 0 < args.target_precision <= 1:
        parser.error("Invalid training parameters")
    if args.output.exists():
        parser.error("Output must be a new directory")
    labels = SPECIAL + words
    rows = [r for r in load_dataset(args.dataset, {"train", "calibration"}) if r["label"] in labels]
    train = [r for r in rows if r["split"] == "train"]
    calibration = [r for r in rows if r["split"] == "calibration"]
    for name, split in (("train", train), ("calibration", calibration)):
        counts = Counter(row["label"] for row in split)
        missing = [label for label in labels if not counts[label]]
        if missing:
            parser.error(f"Missing {name} recordings for: {missing}")
    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)
    torch.set_num_threads(2)
    torch.use_deterministic_algorithms(True)
    raw = np.stack([extract(read_wav(r["path"])) for r in train])
    mean = raw.mean(axis=(0, 1))
    std = np.maximum(raw.std(axis=(0, 1)), .1)
    x = torch.from_numpy(((raw - mean) / std).reshape(len(train), DIM))
    y = torch.tensor([labels.index(r["label"]) for r in train])
    cx = normalized(calibration, mean, std)
    cy = np.array([labels.index(r["label"]) for r in calibration], dtype=np.int64)
    model = nn.Sequential(nn.Linear(DIM, args.hidden), nn.ReLU(), nn.Dropout(.2),
                          nn.Linear(args.hidden, len(labels)))
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.01)
    weights = len(train) / (len(labels) * np.bincount(y.numpy(), minlength=len(labels)))
    loss_function = nn.CrossEntropyLoss(weight=torch.tensor(weights, dtype=torch.float32))
    best_loss, best, best_epoch, stale = float("inf"), None, 0, 0
    for epoch in range(1, args.epochs + 1):
        model.train()
        for indices in torch.randperm(len(x)).split(32):
            optimizer.zero_grad()
            loss = loss_function(model(x[indices]), y[indices])
            loss.backward()
            optimizer.step()
        model.eval()
        with torch.no_grad():
            validation_loss = nn.functional.cross_entropy(model(torch.from_numpy(cx)), torch.from_numpy(cy)).item()
        if validation_loss < best_loss:
            best_loss, best_epoch, stale = validation_loss, epoch, 0
            best = {key: value.detach().clone() for key, value in model.state_dict().items()}
        else:
            stale += 1
        if epoch % 10 == 0 or epoch == 1:
            print(f"Epoch {epoch}: calibration loss={validation_loss:.5f}", flush=True)
        if stale >= 25:
            break
    model.load_state_dict(best)
    layers = []
    for layer in (model[0], model[3]):
        quantized, scales = quantize(layer.weight.detach().numpy())
        layers.append((quantized, scales, layer.bias.detach().numpy().copy()))
    probabilities = predict(cx, layers)
    threshold, margin, calibrated = calibrate(probabilities, cy, args.target_precision)
    args.output.mkdir(parents=True)
    save_model(args.output / "voice.bin", labels, mean, std, layers)
    sources = ["dataset.py", "features.py", "train.py"]
    report = {"format": "CVVOX001", "scope": "personal-isolated-words-experimental",
              "labels": labels, "features": SPEC, "hidden": args.hidden,
              "seed": args.seed, "epochs_requested": args.epochs,
              "best_epoch": best_epoch, "calibration_loss_float": best_loss,
              "threshold": threshold, "margin": margin, "calibration_int8_weights": calibrated,
              "target_precision": args.target_precision, "evaluation": "not run",
              "model_sha256": hashlib.sha256((args.output / "voice.bin").read_bytes()).hexdigest(),
              "versions": {"numpy": np.__version__, "torch": torch.__version__},
              "source_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in sources},
              "recordings": [{k: str(v) if isinstance(v, Path) else v for k, v in row.items()} for row in rows]}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(calibrated, indent=2))
    print("Experimental candidate exported. Not installed in the watch. Evaluate on new sessions.")


if __name__ == "__main__":
    main()
