"""Pin and validate FLEURS PT-BR metadata only; never downloads speech audio."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
from huggingface_hub import HfApi


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new output file")
    info = HfApi(token=False).dataset_info("google/fleurs")
    card = info.card_data.to_dict()
    config = next(item for item in card["dataset_info"] if item["config_name"] == "pt_br")
    audio = next(item for item in config["features"] if item["name"] == "audio")
    rate = audio["dtype"]["audio"]["sampling_rate"]
    splits = {item["name"]: item["num_examples"] for item in config["splits"]}
    fields = {item["name"] for item in config["features"]}
    if rate != 16000 or set(splits) != {"train", "validation", "test"} or "transcription" not in fields:
        raise ValueError("Public corpus metadata differs from the expected PT-BR ASR schema")
    report = {"checked_utc": datetime.now(timezone.utc).isoformat(), "dataset": "google/fleurs",
              "commit": info.sha, "config": "pt_br", "declared_license": card["license"],
              "declared_sample_rate": rate, "declared_splits": splits,
              "download_size_declared_bytes": config.get("download_size"),
              "dataset_size_declared_bytes": config.get("dataset_size"),
              "status": "metadata-validated-only", "audio_downloaded": False,
              "training_performed": False, "source": "https://huggingface.co/datasets/google/fleurs",
              "pending": ["Audit actual audio and transcripts before training.",
                          "Check duplicates/related sentences across exported splits.",
                          "Implement variable-duration batches and preserve official partitions."]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
