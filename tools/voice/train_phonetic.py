"""Train-only CTC feasibility experiment on completed isolated-word sessions.

Does not read calibration/evaluation, accept unknown speech, or export firmware.
The train-set error is an optimization diagnostic, not recognition quality.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import torch

from dataset import load_dataset, read_wav
from phonetic_ctc import GAIN_NORMALIZED_SPEC, SEQUENCE_SPEC, SmallPhoneticCTC, edit_distance, greedy_ctc, sequence_features
from phonetic_data import load_phonetic_dataset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--phonetic-dataset", type=Path, help="Additional transcribed phonetic-context sessions")
    parser.add_argument("--phonetic-only", action="store_true", help="Exclude previous whole-word training sessions")
    parser.add_argument("--gain-normalize", action="store_true", help="Remove overall log-mel intensity per frame (v2 frontend)")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=60)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("--allow-unreviewed", action="store_true", help="Research only: use automatic IPA hypotheses")
    args = parser.parse_args()
    if args.output.exists() or args.epochs < 1 or not 4 <= args.hidden <= 128:
        parser.error("Use a new output directory, positive epochs and hidden 4..128")
    if args.phonetic_only and not args.phonetic_dataset:
        parser.error("--phonetic-only requires --phonetic-dataset")
    lexicon = json.loads(args.lexicon.read_text(encoding="utf-8"))
    entries = {entry["text"]: entry for entry in lexicon["entries"]}
    tokens = lexicon["tokens"]
    if tokens[0] != "<blank>" or len(tokens) != len(set(tokens)):
        parser.error("Invalid CTC token inventory")
    indices = {token: index for index, token in enumerate(tokens)}
    source_rows = load_dataset(args.dataset, {"train"}) if not args.phonetic_only and any(args.dataset.glob("*/*/manifest.jsonl")) else []
    if args.phonetic_dataset:
        source_rows += load_phonetic_dataset(args.phonetic_dataset, {"train"})
    if len({row["pcm_sha256"] for row in source_rows}) != len(source_rows):
        parser.error("Duplicate audio across training sources")
    rows, targets = [], []
    ignored_unknown = 0
    for row in source_rows:
        if row["label"] == "_unknown":
            ignored_unknown += 1
            continue  # Content was not transcribed; it is not a blank target.
        if row["label"] == "_silence":
            target = []
        elif "units" in row or row["label"] in entries:
            entry = row if "units" in row else entries[row["label"]]
            if "unit_spec" in row and row["unit_spec"] != lexicon["unit_spec"]:
                parser.error("Phonetic data and lexicon use different unit specifications")
            if not entry["reviewed"] and not args.allow_unreviewed:
                parser.error("Pronunciation requires review; --allow-unreviewed is a research-only override")
            if any(unit not in indices for unit in entry["units"]):
                parser.error("Recording contains units outside the fixed model inventory")
            target = [indices[unit] for unit in entry["units"]]
        else:
            parser.error(f"No pronunciation for training label {row['label']}")
        if 0 in target:
            parser.error("Blank must not occur in pronunciation targets")
        rows.append(row)
        targets.append(target)
    if not any(targets):
        parser.error("No transcribed speech in training data")
    torch.set_num_threads(2)
    torch.manual_seed(args.seed)
    torch.use_deterministic_algorithms(True)
    raw = np.stack([sequence_features(read_wav(row["path"]), args.gain_normalize) for row in rows])
    mean, std = raw.mean(axis=(0, 1)), np.maximum(raw.std(axis=(0, 1)), .1)
    x = torch.from_numpy((raw - mean) / std)
    model = SmallPhoneticCTC(len(tokens), args.hidden)
    optimizer = torch.optim.AdamW(model.parameters(), lr=.002, weight_decay=.01)
    criterion = torch.nn.CTCLoss(blank=0, zero_infinity=False)
    history = []
    for epoch in range(1, args.epochs + 1):
        model.train()
        losses = []
        for batch in torch.randperm(len(rows)).split(16):
            optimizer.zero_grad()
            probabilities = model(x[batch]).log_softmax(-1).transpose(0, 1)
            expected = torch.tensor([unit for i in batch.tolist() for unit in targets[i]], dtype=torch.long)
            lengths = torch.tensor([len(targets[i]) for i in batch.tolist()], dtype=torch.long)
            input_lengths = torch.full((len(batch),), probabilities.shape[0], dtype=torch.long)
            loss = criterion(probabilities, expected, input_lengths, lengths)
            if not torch.isfinite(loss):
                raise ValueError("Nonfinite CTC loss; inspect labels, durations and repeated units")
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 5)
            optimizer.step()
            losses.append(loss.item())
        history.append(float(np.mean(losses)))
        if epoch == 1 or epoch % 10 == 0:
            print(f"Epoch {epoch}: train-only CTC loss={history[-1]:.5f}", flush=True)
    model.eval()
    with torch.no_grad():
        predictions = model(x).softmax(-1).numpy()
    hypotheses = [greedy_ctc(probabilities) for probabilities in predictions]
    errors = sum(edit_distance(target, hypothesis) for target, hypothesis in zip(targets, hypotheses))
    units = sum(len(target) for target in targets)
    observed = {unit for target in targets for unit in target}
    args.output.mkdir(parents=True)
    arrays = {"mean": mean, "std": std,
              **{name: value.detach().numpy() for name, value in model.state_dict().items()}}
    np.savez(args.output / "weights-float32.npz", **arrays)
    used = [{"label": row["label"], "pcm_sha256": row["pcm_sha256"],
             "speaker": row["speaker"], "session": row["session"]} for row in rows]
    parameters = sum(p.numel() for p in model.parameters())
    report = {"status": "train-only-feasibility-not-deployable", "deployable": False,
              "phonetic_only": args.phonetic_only,
              "frames": raw.shape[1], "sequence_spec": GAIN_NORMALIZED_SPEC if args.gain_normalize else SEQUENCE_SPEC,
              "unit_spec": lexicon["unit_spec"], "tokens": tokens, "hidden": args.hidden,
              "parameters": parameters, "float32_parameter_bytes": parameters * 4,
              "seed": args.seed, "epochs": args.epochs, "train_loss": history,
              "train_only_unit_error_rate": errors / units, "reference_units": units,
              "speech_samples": sum(bool(t) for t in targets), "silence_samples": sum(not t for t in targets),
              "excluded_untranscribed_unknown_samples": ignored_unknown,
              "observed_nonblank_units": len(observed),
              "unobserved_units": [token for i, token in enumerate(tokens) if i and i not in observed],
              "automatic_pronunciations_allowed": args.allow_unreviewed,
              "lexicon_sha256": hashlib.sha256(args.lexicon.read_bytes()).hexdigest(),
              "weights_sha256": hashlib.sha256((args.output / "weights-float32.npz").read_bytes()).hexdigest(),
              "source_sha256": {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                for name in ("train_phonetic.py", "phonetic_ctc.py", "phonetic_units.py", "phonetic_data.py", "features.py", "dataset.py")},
              "recordings": used, "evaluation": "not run",
              "versions": {"numpy": np.__version__, "torch": torch.__version__}}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("status", "parameters", "speech_samples", "silence_samples",
                                                 "observed_nonblank_units", "train_only_unit_error_rate")}, indent=2))


if __name__ == "__main__":
    main()
