"""Download only public model files; never reads any personal recording.

Pins the public safetensors conversion PR to a commit before downloading.
Model inference is a separate, local-only program.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

MODEL_ID = "jonatasgrosman/wav2vec2-large-xlsr-53-portuguese"
DEFAULT_REVISION = "refs/pr/4"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(".pio/voice-reference-model"))
    parser.add_argument("--revision", default=DEFAULT_REVISION)
    args = parser.parse_args()
    os.environ["HF_HUB_DISABLE_XET"] = "1"
    os.environ["HF_HUB_DISABLE_TELEMETRY"] = "1"
    from huggingface_hub import HfApi, snapshot_download
    info = HfApi().model_info(MODEL_ID, revision=args.revision, files_metadata=True)
    print(f"Public model: {MODEL_ID}; pinned commit: {info.sha}", flush=True)
    wanted = ["config.json", "preprocessor_config.json", "vocab.json", "special_tokens_map.json",
              "tokenizer_config.json", "model.safetensors", "README.md"]
    snapshot_download(MODEL_ID, revision=info.sha, allow_patterns=wanted,
                      local_dir=str(args.output), cache_dir=".pio/voice-hf-cache", max_workers=2)
    required = {"config.json", "preprocessor_config.json", "vocab.json", "model.safetensors"}
    if not all((args.output / name).is_file() for name in required):
        raise ValueError("Incomplete local reference model")
    hashes = {name: digest(args.output / name) for name in wanted if (args.output / name).exists()}
    weights = next(item for item in info.siblings if item.rfilename == "model.safetensors")
    expected = weights.lfs.sha256 if weights.lfs else None
    if expected and hashes["model.safetensors"] != expected:
        raise ValueError("Downloaded safetensors hash differs from repository LFS metadata")
    from safetensors import safe_open
    with safe_open(args.output / "model.safetensors", framework="pt", device="cpu") as tensors:
        tensor_count = len(list(tensors.keys()))
    report = {"model_id": MODEL_ID, "requested_revision": args.revision, "commit": info.sha,
              "conversion_source": f"https://huggingface.co/{MODEL_ID}/discussions/4",
              "weight_format": "safetensors", "tensor_count": tensor_count,
              "files_sha256": hashes, "bytes": {name: (args.output / name).stat().st_size for name in hashes},
              "usage": "local-PC-reference-only; no personal audio uploaded"}
    (args.output / "provenance.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Reference verified locally: {tensor_count} tensors; {sum(report['bytes'].values())} bytes.")


if __name__ == "__main__":
    main()
