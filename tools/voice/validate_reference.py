"""Offline, PC-only reference transcription of existing Chronvs recordings.

Requires an already downloaded, hashed local model. No serial, network calls,
label changes or firmware actions. Checkpoints each audio for safe resumption.
"""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import time

import numpy as np
from dataset import load_dataset, pcm_stats, read_wav, sha256
from phonetic_data import load_phonetic_dataset
from reference_metrics import normalize, review_queue, summarize


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, default=Path(".pio/voice-reference-model"))
    parser.add_argument("--words", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--phonetic", type=Path, default=Path("data/voice-phonetic-local"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--max-new", type=int, default=0, help="0 processes all remaining audio")
    parser.add_argument("--threads", type=int, default=2)
    args = parser.parse_args()
    if args.max_new < 0 or args.threads < 1:
        parser.error("Invalid processing limits")
    os.environ["HF_HOME"] = str(Path(".pio/voice-hf-cache").resolve())
    os.environ["HF_HUB_OFFLINE"] = "1"
    os.environ["TRANSFORMERS_OFFLINE"] = "1"
    os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
    provenance = json.loads((args.model / "provenance.json").read_text(encoding="utf-8"))
    if provenance["weight_format"] != "safetensors":
        parser.error("Only a verified local safetensors reference is supported")
    for name, expected in provenance["files_sha256"].items():
        if Path(name).name != name or file_digest(args.model / name) != expected:
            parser.error("Local model files differ from download provenance")
    rows = load_dataset(args.words) if any(args.words.glob("*/*/manifest.jsonl")) else []
    rows += load_phonetic_dataset(args.phonetic)
    if not rows or len({r["pcm_sha256"] for r in rows}) != len(rows):
        parser.error("No audio or duplicate audio across roots")
    ids = [f"{r['speaker']}/{r['session']}/{r['file']}" for r in rows]
    if len(set(ids)) != len(ids):
        parser.error("Duplicate case identity")
    run_config = {"model_commit": provenance["commit"],
                  "model_sha256": provenance["files_sha256"]["model.safetensors"],
                  "input_sha256": sha256("\n".join(f"{i}:{r['pcm_sha256']}:{r['label']}:{r['split']}" for i, r in zip(ids, rows)).encode()),
                  "rate": 16000, "decoder": "greedy-CTC-no-language-model", "threads": args.threads,
                  "source_sha256": {name: file_digest(Path(__file__).with_name(name))
                                    for name in ("validate_reference.py", "reference_metrics.py", "dataset.py", "phonetic_data.py")}}
    if args.output.exists():
        if not args.resume:
            parser.error("Output exists; use --resume with unchanged inputs/model/code")
        if json.loads((args.output / "run.json").read_text(encoding="utf-8")) != run_config:
            parser.error("Cannot resume: inputs, model, code or configuration changed")
    else:
        args.output.mkdir(parents=True)
        (args.output / "run.json").write_text(json.dumps(run_config, indent=2) + "\n", encoding="utf-8")
    predictions = args.output / "predictions.jsonl"
    cases = [json.loads(line) for line in predictions.read_text(encoding="utf-8").splitlines()] if predictions.exists() else []
    if len(cases) > len(rows) or any(c["id"] != ids[i] or c["pcm_sha256"] != rows[i]["pcm_sha256"] for i, c in enumerate(cases)):
        parser.error("Existing predictions do not match the frozen input order")
    if len(cases) < len(rows):
        import torch
        from transformers import Wav2Vec2ForCTC, Wav2Vec2Processor
        torch.set_num_threads(args.threads)
        print(f"Loading local PC reference; {len(cases)}/{len(rows)} cases already saved.", flush=True)
        processor = Wav2Vec2Processor.from_pretrained(str(args.model.resolve()), local_files_only=True)
        model = Wav2Vec2ForCTC.from_pretrained(str(args.model.resolve()), local_files_only=True,
            use_safetensors=True, low_cpu_mem_usage=True, trust_remote_code=False)
        model.eval()
        stop = min(len(rows), len(cases) + args.max_new) if args.max_new else len(rows)
        with predictions.open("a", encoding="utf-8") as output:
            for i in range(len(cases), stop):
                row = rows[i]
                pcm = read_wav(row["path"])
                if sha256(pcm) != row["pcm_sha256"]:
                    raise ValueError("Audio changed during reference validation")
                audio = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768
                inputs = processor(audio, sampling_rate=16000, return_tensors="pt")
                started = time.perf_counter()
                with torch.inference_mode():
                    logits = model(**inputs).logits
                hypothesis = processor.batch_decode(logits.argmax(dim=-1))[0]
                case = {"id": ids[i], "path": str(row["path"]), "pcm_sha256": row["pcm_sha256"],
                        "session": row["session"], "split": row["split"], "expected": row["label"],
                        "hypothesis": hypothesis, "hypothesis_normalized": normalize(hypothesis),
                        "audio": pcm_stats(pcm), "inference_ms": (time.perf_counter() - started) * 1000,
                        "recorded_utc": datetime.now(timezone.utc).isoformat()}
                output.write(json.dumps(case, ensure_ascii=False) + "\n")
                output.flush()
                cases.append(case)
                print(f"Saved reference transcription {i+1}/{len(rows)}", flush=True)
    groups = defaultdict(list)
    for case in cases:
        groups[case["session"]].append(case)
    timings = [case["inference_ms"] for case in cases]
    report = {"status": "PC-reference-audit-not-device-validation", "complete": len(cases) == len(rows),
              "processed": len(cases), "expected_cases": len(rows), "reference": provenance,
              "metrics": summarize(cases), "by_session": {name: summarize(group) for name, group in groups.items()},
              "median_inference_ms": float(np.median(timings)) if timings else None,
              "versions": {name: importlib.metadata.version(name) for name in ("torch", "transformers", "safetensors", "huggingface-hub", "accelerate")},
              "limitations": ["Reference hypotheses are not ground-truth corrections.",
                              "Untranscribed unknown speech has no accuracy score.",
                              "Short isolated words differ from the reference model's published benchmark.",
                              "This model is PC-only; no ESP32 feasibility is implied."]}
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (args.output / "review.json").write_text(json.dumps(review_queue(cases), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"complete": report["complete"], **report["metrics"]}, indent=2))


if __name__ == "__main__":
    main()
