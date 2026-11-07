"""Future guided phonetic-context collection, independent of the active pilot.

Read natural prompts, not IPA symbols. Start only after closing the other
collector: both require exclusive access to the watch's COM port.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import re
import time

from dataset import PCMReceiver, pcm_stats, read_wav, sha256, write_wav
from serial_capture import CaptureDisconnected, DISCONNECT_MESSAGE, SerialCaptureLink


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--speaker", required=True)
    parser.add_argument("--session", required=True)
    parser.add_argument("--split", choices=["train", "calibration", "evaluation"], required=True)
    parser.add_argument("--output", type=Path, default=Path("data/voice-phonetic-local"))
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--silence", type=int, default=15)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("--playback", action="store_true")
    parser.add_argument("--hands-free", action="store_true", help="Recorder taps advance; r repeats, q pauses (Windows)")
    args = parser.parse_args()
    if any(not re.fullmatch(r"[A-Za-z0-9_-]+", value) for value in (args.speaker, args.session)):
        parser.error("Use ASCII letters, numbers, '_' or '-' in identifiers")
    if args.repetitions < 1 or args.silence < 1:
        parser.error("Counts must be positive")
    plan_bytes = args.plan.read_bytes()
    plan = json.loads(plan_bytes)
    if plan["capture_seconds"] != 2 or plan["prompt_count"] != len(plan["prompts"]):
        parser.error("Unsupported recording plan")
    if "held-out" in plan["status"] and args.split != "evaluation":
        parser.error("Held-out words must remain evaluation data")
    meta = {"format": "chronvs-phonetic-v1", "speaker": args.speaker, "session": args.session,
            "split": args.split, "plan_sha256": sha256(plan_bytes), "repetitions": args.repetitions,
            "silence": args.silence, "seed": args.seed}
    directory = args.output / args.speaker / args.session
    directory.mkdir(parents=True, exist_ok=True)
    metadata = directory / "session.json"
    if metadata.exists():
        if json.loads(metadata.read_text(encoding="utf-8")) != meta:
            parser.error("Existing session has different settings")
        if (directory / "plan.json").read_bytes() != plan_bytes:
            parser.error("Frozen plan differs from requested plan")
    else:
        metadata.write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
        (directory / "plan.json").write_bytes(plan_bytes)
    order = list(range(plan["prompt_count"])) * args.repetitions + [None] * args.silence
    random.Random(args.seed).shuffle(order)
    manifest = directory / "manifest.jsonl"
    done = [json.loads(line) for line in manifest.read_text(encoding="utf-8").splitlines()] if manifest.exists() else []
    if len(done) > len(order):
        parser.error("Manifest exceeds planned recordings")
    for i, row in enumerate(done):
        if (row["prompt_index"] != order[i] or row["file"] != f"{i:05d}.wav" or
                sha256(read_wav(directory / row["file"])) != row["pcm_sha256"]):
            parser.error("Cannot resume altered audio/plan")
    port = SerialCaptureLink(args.port)
    print("Leia naturalmente. A pronúncia IPA automática ainda precisa de revisão.")
    with port, manifest.open("a", encoding="utf-8") as output, (directory / "transport.log").open("ab") as log:
        if args.hands_free:
            from hands_free_capture import collect_hands_free
            def save(i, pcm, stats):
                filename = f"{i:05d}.wav"
                if (directory / filename).exists():
                    raise RuntimeError("Audio already exists without a manifest entry")
                (directory / "pending.wav").rename(directory / filename)
                index = order[i]
                text = "_silence" if index is None else plan["prompts"][index]["text"]
                output.write(json.dumps({"file": filename, "text": text, "prompt_index": index,
                    "pcm_sha256": sha256(pcm), "recorded_utc": datetime.now(timezone.utc).isoformat(),
                    **stats}, ensure_ascii=False) + "\n")
                output.flush()
            def playback(path):
                import winsound
                winsound.PlaySound(str(path), winsound.SND_FILENAME)
            prompts = ["Não fale; ruído ambiente." if i is None else f"Diga: {plan['prompts'][i]['text']}" for i in order]
            collect_hands_free(port, log, directory, prompts, len(done), save,
                               playback=playback if args.playback else None)
            return
        for i in range(len(done), len(order)):
            index = order[i]
            text = "_silence" if index is None else plan["prompts"][index]["text"]
            while True:
                instruction = "Não fale; ruído ambiente." if index is None else f"Diga: {text}"
                if input(f"\n[{i+1}/{len(order)}] {instruction}\nEnter para preparar; q para sair: ").lower() == "q":
                    return
                try:
                    port.prepare()
                except CaptureDisconnected:
                    print(DISCONNECT_MESSAGE)
                    continue
                print("Toque no microfone e fale ao aparecer Gravando. Aguarde o envio.", flush=True)
                receiver, pcm = PCMReceiver(), None
                deadline = time.monotonic() + 90
                try:
                    while pcm is None and time.monotonic() < deadline:
                        data = port.readline(1024)
                        if data:
                            log.write(data)
                            log.flush()
                            pcm = receiver.feed(data.decode("ascii", errors="replace"))
                    if pcm is None:
                        raise ValueError("Timeout: captura incompleta")
                except CaptureDisconnected:
                    print(DISCONNECT_MESSAGE)
                    continue
                except ValueError as error:
                    print(f"Amostra recusada: {error}")
                    continue
                stats = pcm_stats(pcm)
                pending = directory / "pending.wav"
                write_wav(pending, pcm)
                if args.playback:
                    import winsound
                    winsound.PlaySound(str(pending), winsound.SND_FILENAME)
                print(f"RMS={stats['rms']:.4f}; saturação={stats['clipped_fraction']:.2%}")
                if input("Enter para aceitar; r para repetir: ") != "":
                    pending.unlink()
                    continue
                filename = f"{i:05d}.wav"
                if (directory / filename).exists():
                    raise ValueError("Audio exists without a manifest entry; inspect before resuming")
                pending.rename(directory / filename)
                output.write(json.dumps({"file": filename, "text": text, "prompt_index": index,
                    "pcm_sha256": sha256(pcm), "recorded_utc": datetime.now(timezone.utc).isoformat(),
                    **stats}, ensure_ascii=False) + "\n")
                output.flush()
                break
    print("Sessão fonética concluída.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrompido. Repita o mesmo comando para retomar.")
