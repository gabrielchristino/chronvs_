"""Preserve local build artifacts before experiments; never upload from this CLI."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

IMAGES = {"bootloader.bin": "0x0", "partitions.bin": "0x10000",
          "firmware.bin": "0x20000", "srmodels/srmodels.bin": "0x420000"}
FILES = tuple(IMAGES) + ("firmware.elf", "config/sdkconfig.h")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(directory):
    directory = Path(directory).resolve()
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("images") != IMAGES:
        raise ValueError("Reference flash map differs from the custom uploader")
    required = set(FILES) | {"sdkconfig", "platformio.ini", "upload_waveshare.py"}
    if not required.issubset(manifest["sha256"]):
        raise ValueError("Incomplete reference")
    for name, expected in manifest["sha256"].items():
        path = (directory / name).resolve()
        if not path.is_relative_to(directory) or digest(path) != expected:
            raise ValueError(f"Reference verification failed: {name}")
    return directory


def preserve(root, environment):
    root = Path(root).resolve()
    if not environment or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in environment):
        raise ValueError("Invalid environment name")
    build = root / ".pio" / "build" / environment
    sources = {name: build / name for name in FILES}
    sources.update({"sdkconfig": root / f"sdkconfig.{environment}",
                    "platformio.ini": root / "platformio.ini",
                    "upload_waveshare.py": root / "scripts/upload_waveshare.py"})
    for source in sources.values():
        if not source.is_file():
            raise FileNotFoundError(source)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    destination = root / "logs" / "firmware-references" / f"{timestamp}-{environment}"
    destination.mkdir(parents=True)
    hashes = {}
    for name, source in sources.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        before = digest(source)
        shutil.copy2(source, target)
        if digest(target) != before or digest(source) != before:
            raise ValueError("Build changed during backup; stop the build and retry")
        hashes[name] = before
    manifest = {
        "environment": environment, "saved_utc": timestamp,
        "workspace_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "workspace_status": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True),
        "provenance": "Existing local artifacts; workspace commit does not prove binary provenance or physical validation.",
        "private": "Local only: firmware/ELF may embed credentials. Do not publish this directory.",
        "images": IMAGES, "sha256": hashes,
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return verify(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--save", metavar="ENVIRONMENT")
    action.add_argument("--verify", type=Path)
    args = parser.parse_args()
    print(preserve(Path(__file__).resolve().parents[1], args.save) if args.save else verify(args.verify))
