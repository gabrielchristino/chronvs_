"""Train a reproducible public character CTC baseline; never deploys firmware.

Epoch-boundary checkpoint supports --resume after a PC interruption. Validation
selects a development checkpoint; no public test or personal audio is consumed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import time

import numpy as np
import torch
from torch.nn import functional as F

from phonetic_ctc import edit_distance, greedy_ctc
from public_ctc import CompactCharacterCTC, SPEC, pack_batch, wav_features


def source_hash():
    return hashlib.sha256(b"".join((Path(__file__).parent / name).read_bytes() for name in
                                  ("train_public.py", "public_ctc.py", "phonetic_ctc.py", "features.py"))).hexdigest()


def atomic_checkpoint(path, state):
    temporary = path.with_suffix(".tmp")
    torch.save(state, temporary)
    temporary.replace(path)


def evaluate(model, items, rows, tokens, batch_size):
    model.eval()
    cases, errors, chars, losses = [], 0, 0, []
    with torch.no_grad():
        for start in range(0, len(items), batch_size):
            batch = items[start:start + batch_size]
            x, y, lengths, target_lengths = pack_batch(batch)
            logp = model(x).log_softmax(-1)
            loss = F.ctc_loss(logp.transpose(0, 1), y, lengths, target_lengths, zero_infinity=False)
            losses.append((float(loss), len(batch)))
            for offset, (_, target) in enumerate(batch):
                hypothesis = greedy_ctc(logp[offset, :lengths[offset]].numpy())
                distance = edit_distance(target, hypothesis)
                errors += distance
                chars += len(target)
                cases.append({"path": rows[start + offset]["path"],
                              "expected": rows[start + offset]["text"],
                              "hypothesis": "".join(tokens[t] for t in hypothesis),
                              "character_errors": distance})
    return {"cases": len(items), "character_errors": errors, "characters": chars,
            "cer": errors / chars, "exact": sum(c["expected"] == c["hypothesis"] for c in cases),
            "ctc_loss": sum(v * n for v, n in losses) / len(items), "predictions": cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--batch-size", type=int, default=4)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if min(args.epochs, args.batch_size) < 1:
        parser.error("Positive epochs and batch size required")
    if args.output.exists() and not args.resume:
        parser.error("Use a new output directory, or --resume")
    torch.set_num_threads(2)
    torch.manual_seed(args.seed)
    random.seed(args.seed)
    np.random.seed(args.seed)
    manifest_raw = (args.corpus / "manifest.json").read_bytes()
    manifest = json.loads(manifest_raw)
    rows = {split: [r for r in manifest["records"] if r["split"] == split]
            for split in ("train", "validation")}
    if not all(rows.values()) or any(r["split"] not in rows for r in manifest["records"]):
        raise ValueError("Require nonempty training/validation only")
    # Fixed alphabet includes digits and common borrowed-name spelling. It is
    # declared independently of validation; never fit vocabulary on held-out text.
    alphabet = " abcdefghijklmnopqrstuvwxyz0123456789àáâãéêíóôõúüçñ'-"
    train_chars = set(alphabet) | set("".join(r["text"] for r in rows["train"]))
    tokens = ["<blank>"] + sorted(train_chars)
    unseen = sorted(set("".join(r["text"] for r in rows["validation"])) - train_chars)
    if unseen:
        raise ValueError(f"Validation characters absent from training: {unseen}; do not silently drop")
    ids = {token: i for i, token in enumerate(tokens)}
    all_features, seen, sentence_splits = {}, set(), {}
    for split, records in rows.items():
        all_features[split] = []
        for row in records:
            raw = (args.corpus / row["path"]).read_bytes()
            if hashlib.sha256(raw).hexdigest() != row["wav_sha256"] or row["pcm_sha256"] in seen:
                raise ValueError("Corrupt or duplicate public audio")
            seen.add(row["pcm_sha256"])
            key = row["sentence_id"]
            if key in sentence_splits and sentence_splits[key] != split:
                raise ValueError("Sentence crosses public partitions")
            sentence_splits[key] = split
            all_features[split].append(wav_features(raw))
    valid_frames = np.concatenate(all_features["train"])
    mean = valid_frames.mean(axis=0)
    scale = np.maximum(valid_frames.std(axis=0), 1e-4)
    del valid_frames
    items = {split: [((x - mean) / scale, [ids[c] for c in r["text"]])
                     for x, r in zip(all_features[split], records)] for split, records in rows.items()}
    model = CompactCharacterCTC(len(tokens))
    optimizer = torch.optim.AdamW(model.parameters(), lr=0.001)
    fingerprint = {"manifest_sha256": hashlib.sha256(manifest_raw).hexdigest(),
                   "source_sha256": source_hash(), "seed": args.seed,
                   "batch_size": args.batch_size, "epochs": args.epochs, "tokens": tokens,
                   "alphabet_policy": "fixed PT/borrowed-name alphabet plus training characters only"}
    args.output.mkdir(parents=True, exist_ok=True)
    start, history, best = 0, [], float("inf")
    if args.resume:
        state = torch.load(args.output / "latest.pt", weights_only=True)
        if state["fingerprint"] != fingerprint:
            raise ValueError("Resume configuration/source/data changed")
        model.load_state_dict(state["model"])
        optimizer.load_state_dict(state["optimizer"])
        start, history, best = state["epoch"], state["history"], state["best_loss"]
        torch.set_rng_state(state["rng"])
    since = time.perf_counter()
    for epoch in range(start, args.epochs):
        model.train()
        order = list(range(len(items["train"])))
        random.Random(args.seed + epoch).shuffle(order)
        losses = []
        for start_index in range(0, len(order), args.batch_size):
            batch = [items["train"][i] for i in order[start_index:start_index + args.batch_size]]
            x, y, lengths, target_lengths = pack_batch(batch)
            optimizer.zero_grad()
            logp = model(x).log_softmax(-1)
            loss = F.ctc_loss(logp.transpose(0, 1), y, lengths, target_lengths, zero_infinity=False)
            if not torch.isfinite(loss):
                raise ValueError("Nonfinite CTC loss; do not hide impossible alignments")
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5)
            optimizer.step()
            losses.append(float(loss.detach()))
        validation = evaluate(model, items["validation"], rows["validation"], tokens, args.batch_size)
        entry = {"epoch": epoch + 1, "train_batch_mean_loss": float(np.mean(losses)),
                 "validation_loss": validation["ctc_loss"], "validation_cer": validation["cer"]}
        history.append(entry)
        improved = validation["ctc_loss"] < best
        best = min(best, validation["ctc_loss"])
        state = {"fingerprint": fingerprint, "model": model.state_dict(), "optimizer": optimizer.state_dict(),
                 "epoch": epoch + 1, "history": history, "best_loss": best, "rng": torch.get_rng_state(),
                 "mean": torch.tensor(mean), "scale": torch.tensor(scale), "spec": SPEC}
        atomic_checkpoint(args.output / "latest.pt", state)
        if improved:
            atomic_checkpoint(args.output / "best.pt", state)
        print(json.dumps(entry), flush=True)
    selected = torch.load(args.output / "best.pt", weights_only=True)
    model.load_state_dict(selected["model"])
    report = {"status": "public-character-CTC-development-baseline-not-device-ready",
              "fingerprint": fingerprint, "selected_epoch": selected["epoch"], "history": history,
              "spec": SPEC, "parameters": sum(p.numel() for p in model.parameters()),
              "float32_weight_bytes": sum(p.numel() * 4 for p in model.parameters()),
              "elapsed_this_invocation_seconds": time.perf_counter() - since,
              "train": evaluate(model, items["train"], rows["train"], tokens, args.batch_size),
              "validation": evaluate(model, items["validation"], rows["validation"], tokens, args.batch_size),
              "empty_hypothesis_cer_baseline": 1.0, "personal_audio_used": False,
              "test_audio_used": False, "device_exported": False,
              "limitations": ["Small subset, development evaluation, no blind personal test.",
                              "No phonetic comparison or personal adaptation yet.",
                              "Weight bytes exclude activations, runtime and feature extraction."]}
    (args.output / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Saved report to {args.output}", flush=True)


if __name__ == "__main__":
    main()
