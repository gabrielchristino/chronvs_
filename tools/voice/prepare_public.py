"""Export a small, pinned FLEURS PT-BR subset without executing dataset code.

Streams public archives; never uploads audio or extracts archive paths. Entire
utterances are retained. Test audio is not downloaded. Originals stay untouched.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import tarfile
import unicodedata
import wave

import requests
import numpy as np
import soundfile as sf
from huggingface_hub import hf_hub_download, hf_hub_url

REVISION = "70bb2e84b976b7e960aa89f1c648e09c59f894dd"
REPO = "google/fleurs"


def sentence_key(text):
    return " ".join("".join(c if c.isalnum() else " " for c in
                            unicodedata.normalize("NFC", text).casefold()).split())


def parse_table(text):
    records = {}
    for row in csv.reader(io.StringIO(text), delimiter="\t", quoting=csv.QUOTE_NONE):
        if len(row) != 7 or Path(row[1]).name != row[1] or row[1] in records:
            raise ValueError("Unexpected or duplicate FLEURS metadata row")
        records[row[1]] = {"sentence_id": row[0], "filename": row[1],
                           "text": row[3], "raw_text": row[2],
                           "declared_samples": int(row[5]), "gender": row[6]}
    return records


def overlap_keys(tables):
    """Conservatively exclude cross-partition sentence IDs OR normalized texts."""
    owners = {}
    for split, rows in tables.items():
        for row in rows.values():
            for key in ("id:" + row["sentence_id"], "text:" + sentence_key(row["text"])):
                owners.setdefault(key, set()).add(split)
    return {key for key, splits in owners.items() if len(splits) > 1}


def pcm_from_wav(raw):
    with wave.open(io.BytesIO(raw)) as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) != (1, 2, 16000, "NONE"):
            raise ValueError("Expected mono PCM16 at 16 kHz")
        count = wav.getnframes()
        pcm = wav.readframes(count)
    if len(pcm) != count * 2:
        raise ValueError("Truncated public WAV")
    return count, hashlib.sha256(pcm).hexdigest()


def canonical_wav(raw):
    signal, rate = sf.read(io.BytesIO(raw), dtype="float32", always_2d=True)
    if rate != 16000 or signal.shape[1] != 1 or not np.isfinite(signal).all():
        raise ValueError("Expected finite mono audio at 16 kHz")
    clipped = int(np.count_nonzero((signal < -1) | (signal > 32767 / 32768)))
    pcm = np.clip(np.rint(signal[:, 0] * 32768), -32768, 32767).astype("<i2").tobytes()
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav:
        wav.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
        wav.writeframes(pcm)
    return buffer.getvalue(), clipped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--train", type=int, default=128)
    parser.add_argument("--validation", type=int, default=32)
    args = parser.parse_args()
    if args.output.exists() or min(args.train, args.validation) < 1:
        parser.error("Use a new output directory and positive counts")
    args.output.mkdir(parents=True)
    tables, table_hashes = {}, {}
    for split, source in (("train", "train"), ("validation", "dev"), ("test", "test")):
        path = Path(hf_hub_download(REPO, f"data/pt_br/{source}.tsv", repo_type="dataset",
                                   revision=REVISION, cache_dir=".pio/voice-hf-cache", token=False))
        raw = path.read_bytes()
        tables[split] = parse_table(raw.decode("utf-8"))
        table_hashes[split] = hashlib.sha256(raw).hexdigest()
        (args.output / f"{split}.tsv").write_bytes(raw)
    blocked = overlap_keys(tables)
    records, pcm_owners, sources = [], {}, {}
    for split, source, limit in (("train", "train", args.train), ("validation", "dev", args.validation)):
        url = hf_hub_url(REPO, f"data/pt_br/audio/{source}.tar.gz", repo_type="dataset", revision=REVISION)
        destination = args.output / split
        destination.mkdir()
        count = 0
        with requests.get(url, stream=True, timeout=(30, 120)) as response:
            response.raise_for_status()
            sources[split] = {"url": url, "etag": response.headers.get("ETag")}
            with tarfile.open(fileobj=response.raw, mode="r|gz") as archive:
                for member in archive:
                    name = Path(member.name).name
                    row = tables[split].get(name)
                    if not member.isfile() or row is None:
                        continue
                    if ("id:" + row["sentence_id"] in blocked or
                            "text:" + sentence_key(row["text"]) in blocked):
                        continue
                    # Preserve full text/audio; skip long utterances rather than crop.
                    if not 8000 <= row["declared_samples"] <= 240000:
                        continue
                    if member.size > 1000000:
                        raise ValueError("Unexpected WAV size")
                    original = archive.extractfile(member).read()
                    raw, clipped = canonical_wav(original)
                    samples, digest = pcm_from_wav(raw)
                    if samples != row["declared_samples"]:
                        raise ValueError("Audio length differs from public metadata")
                    if digest in pcm_owners:
                        raise ValueError("Duplicate PCM in selected public data")
                    pcm_owners[digest] = split
                    relative = f"{split}/{name}"
                    (args.output / relative).write_bytes(raw)
                    records.append({**row, "split": split, "path": relative,
                                    "samples": samples, "pcm_sha256": digest,
                                    "source_wav_sha256": hashlib.sha256(original).hexdigest(),
                                    "pcm_conversion_clipped_samples": clipped,
                                    "wav_sha256": hashlib.sha256(raw).hexdigest()})
                    count += 1
                    if count % 16 == 0:
                        print(f"{split}: {count}/{limit}", flush=True)
                    if count == limit:
                        break
        if count != limit:
            raise ValueError(f"Insufficient eligible audio for {split}: {count}/{limit}")
    manifest = {"status": "public-subset-exported-not-trained", "dataset": REPO,
                "revision": REVISION, "config": "pt_br", "license": "CC-BY-4.0",
                "attribution": "FLEURS (Google), https://huggingface.co/datasets/google/fleurs",
                "selection": "first eligible archive members; full 0.5-15s utterances; cross-split sentence IDs/text excluded",
                "audio_conversion": "float32 decode, round(x*32768), saturate int16; no resampling, trimming or gain adjustment",
                "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "metadata_sha256": table_hashes, "sources": sources,
                "excluded_cross_split_keys": len(blocked), "test_audio_downloaded": False,
                "records": records}
    (args.output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Exported {len(records)} complete utterances to {args.output}", flush=True)


if __name__ == "__main__":
    main()
