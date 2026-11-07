"""Compare frozen word/CTC pilots on calibration data, not a blind evaluation.

CTC word ranking is forced among the six known words and has no acceptance
gate. Keep that diagnostic distinct from the calibrated word classifier.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import numpy as np
import torch

from dataset import load_dataset, pcm_stats, read_wav
from phonetic_ctc import GAIN_NORMALIZED_SPEC, SEQUENCE_SPEC, SmallPhoneticCTC, edit_distance, greedy_ctc, rank_words, sequence_features
from train import load_model, metrics, normalized, predict


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--word-model", type=Path, required=True)
    parser.add_argument("--phonetic-model", type=Path, required=True)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--all-words", action="store_true", help="Also rank all 200 lexicon words for speech cases")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output must be a new file")
    word = json.loads((args.word_model / "report.json").read_text(encoding="utf-8"))
    phonetic = json.loads((args.phonetic_model / "report.json").read_text(encoding="utf-8"))
    if phonetic["sequence_spec"] not in (SEQUENCE_SPEC, GAIN_NORMALIZED_SPEC):
        parser.error("Unsupported phonetic feature specification")
    for path, digest in ((args.word_model / "voice.bin", word["model_sha256"]),
                         (args.phonetic_model / "weights-float32.npz", phonetic["weights_sha256"]),
                         (args.lexicon, phonetic["lexicon_sha256"])):
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            parser.error(f"Artifact differs from training report: {path}")
    all_rows = load_dataset(args.dataset)
    rows = [row for row in all_rows if row["split"] == "calibration"]
    if not rows:
        parser.error("No calibration recordings")
    phonetic_train = {row["pcm_sha256"] for row in phonetic["recordings"]}
    if any(row["pcm_sha256"] in phonetic_train for row in rows):
        parser.error("Phonetic training overlaps calibration")
    labels, tokens = word["labels"], phonetic["tokens"]
    known = labels[2:]
    lexicon = json.loads(args.lexicon.read_text(encoding="utf-8"))
    entries = [entry for entry in lexicon["entries"] if entry["text"] in known]
    by_text = {entry["text"]: entry for entry in entries}
    training_words = {word.casefold() for row in phonetic["recordings"] for word in row["label"].split()}
    indices = {token: index for index, token in enumerate(tokens)}
    mean, std, layers = load_model(args.word_model / "voice.bin")
    word_p = predict(normalized(rows, mean, std), layers)
    torch.set_num_threads(2)
    model = SmallPhoneticCTC(len(tokens), phonetic["hidden"])
    with np.load(args.phonetic_model / "weights-float32.npz", allow_pickle=False) as archive:
        model.load_state_dict({key: torch.from_numpy(archive[key].copy()) for key in model.state_dict()})
        mean, std = archive["mean"].copy(), archive["std"].copy()
    gain_normalize = phonetic["sequence_spec"] == GAIN_NORMALIZED_SPEC
    features = np.stack([(sequence_features(read_wav(row["path"]), gain_normalize) - mean) / std for row in rows])
    model.eval()
    with torch.no_grad():
        logp = model(torch.from_numpy(features)).log_softmax(-1).numpy()
    cases, unit_errors, unit_count = [], 0, 0
    for i, row in enumerate(rows):
        case = {"file": row["file"], "session": row["session"], "label": row["label"],
                "pcm_sha256": row["pcm_sha256"], "audio": pcm_stats(read_wav(row["path"])),
                "word_raw": labels[int(word_p[i].argmax())],
                "word_forced_six": known[int(word_p[i, 2:].argmax())]}
        units = greedy_ctc(logp[i])
        case["ctc_greedy_units"] = [tokens[index] for index in units]
        if row["label"] in known:
            target = [indices[unit] for unit in by_text[row["label"]]["units"]]
            errors = edit_distance(target, units)
            unit_errors += errors
            unit_count += len(target)
            case["ctc_unit_errors"] = errors
            ranking = rank_words(logp[i], entries, tokens, limit=2)
            case["ctc_forced_six"] = ranking[0][0]
            case["ctc_rank_log_scores"] = ranking
            case["word_present_in_phonetic_training"] = row["label"].casefold() in training_words
            if args.all_words:
                full_ranking = rank_words(logp[i], lexicon["entries"], tokens, limit=5)
                case["ctc_all_lexicon_top5"] = [entry[0] for entry in full_ranking]
        cases.append(case)
    speech = [case for case in cases if case["label"] in known]
    expected = np.array([labels.index(row["label"]) for row in rows])
    result = {"status": "calibration-diagnostic-not-independent-evaluation",
              "word_model_sha256": word["model_sha256"], "ctc_weights_sha256": phonetic["weights_sha256"],
              "classes": dict(Counter(row["label"] for row in rows)),
              "word_calibrated": metrics(word_p, expected, word["threshold"], word["margin"]),
              "speech_samples": len(speech),
              "word_raw_correct": sum(case["word_raw"] == case["label"] for case in speech),
              "word_forced_six_correct": sum(case["word_forced_six"] == case["label"] for case in speech),
              "ctc_forced_six_correct": sum(case["ctc_forced_six"] == case["label"] for case in speech),
              "speech_words_absent_from_phonetic_training": sum(not case["word_present_in_phonetic_training"] for case in speech),
              "ctc_greedy_unit_error_rate": unit_errors / unit_count if unit_count else None,
              "ctc_silence_nonblank": sum(bool(case["ctc_greedy_units"]) for case in cases if case["label"] == "_silence"),
              "warning": "CTC forced ranking has no rejection gate; it is not end-to-end accuracy or unseen-word generalization.",
              "cases": cases}
    if args.all_words:
        result["ctc_full_lexicon_words"] = len(lexicon["entries"])
        result["ctc_full_lexicon_top1_correct"] = sum(case["ctc_all_lexicon_top5"][0] == case["label"] for case in speech)
        result["ctc_full_lexicon_top5_correct"] = sum(case["label"] in case["ctc_all_lexicon_top5"] for case in speech)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key not in ("cases", "classes")}, indent=2))


if __name__ == "__main__":
    main()
