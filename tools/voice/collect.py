"""Guided enrollment from the watch's optional vox_capture firmware."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import re
import time

from dataset import PCMReceiver, SPECIAL, pcm_stats, sha256, vocabulary, write_wav
from serial_capture import CaptureDisconnected, DISCONNECT_MESSAGE, SerialCaptureLink


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speaker", required=True)
    parser.add_argument("--session", required=True, help="New recording occasion, e.g. day1-morning")
    parser.add_argument("--split", required=True, choices=["train", "calibration", "evaluation"])
    parser.add_argument("--output", type=Path, default=Path("data/voice-local"))
    parser.add_argument("--words", nargs="+", help="Pilot subset; default: all 200 words")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--background-repetitions", type=int, default=25)
    parser.add_argument("--seed", type=int, default=41)
    parser.add_argument("--playback", action="store_true", help="Listen on Windows before accepting")
    parser.add_argument("--hands-free", action="store_true", help="Recorder taps advance; r repeats, q pauses (Windows)")
    args = parser.parse_args()
    for name in (args.speaker, args.session):
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            parser.error("Speaker/session must use ASCII letters, digits, '_' or '-'")
    words = vocabulary()
    selected = args.words or words
    if not selected or len(set(selected)) != len(selected) or any(w not in words for w in selected):
        parser.error("Use distinct Portuguese words from the Vox vocabulary")
    if args.repetitions < 1 or args.background_repetitions < 1:
        parser.error("Repetitions must be positive")
    labels = selected + SPECIAL
    meta = {"version": 1, "speaker": args.speaker, "session": args.session,
            "split": args.split, "vocabulary": words, "labels": labels,
            "repetitions": args.repetitions, "seed": args.seed, "device": "chronvs-vox_capture"}
    meta["background_repetitions"] = args.background_repetitions
    directory = args.output / args.speaker / args.session
    directory.mkdir(parents=True, exist_ok=True)
    metadata = directory / "session.json"
    if metadata.exists():
        if json.loads(metadata.read_text(encoding="utf-8")) != meta:
            parser.error("Session already exists with different settings; choose a new session")
    else:
        metadata.write_text(json.dumps(meta, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    plan = selected * args.repetitions + SPECIAL * args.background_repetitions
    random.Random(args.seed).shuffle(plan)
    manifest = directory / "manifest.jsonl"
    done = [json.loads(line) for line in manifest.read_text(encoding="utf-8").splitlines()] if manifest.exists() else []
    if len(done) > len(plan):
        parser.error("Session manifest has more recordings than planned")
    for index, row in enumerate(done):
        from dataset import read_wav
        if (row["label"] != plan[index] or row["file"] != f"{index:05d}.wav" or
                sha256(read_wav(directory / row["file"])) != row["pcm_sha256"]):
            parser.error("Cannot resume: manifest/audio differs from recording plan")
    port = SerialCaptureLink(args.port, args.baud)
    with port, manifest.open("a", encoding="utf-8") as output, (directory / "transport.log").open("ab") as transport:
        if args.hands_free:
            from hands_free_capture import collect_hands_free
            def save(index, pcm, stats):
                filename = f"{index:05d}.wav"
                target = directory / filename
                if target.exists():
                    raise RuntimeError(f"Unregistered audio already exists: {target}")
                (directory / "pending.wav").rename(target)
                output.write(json.dumps({"file": filename, "label": plan[index],
                    "pcm_sha256": sha256(pcm), "recorded_utc": datetime.now(timezone.utc).isoformat(),
                    **stats}, ensure_ascii=False) + "\n")
                output.flush()
            def playback(path):
                import winsound
                winsound.PlaySound(str(path), winsound.SND_FILENAME)
            prompts = [{"_unknown": "Diga algo FORA das 200 palavras (varie o conteúdo).",
                        "_silence": "Não fale; grave o ruído real do ambiente."}.get(label, f"Diga: {label}")
                       for label in plan]
            collect_hands_free(port, transport, directory, prompts, len(done), save,
                               playback=playback if args.playback else None)
            return
        for index in range(len(done), len(plan)):
            label = plan[index]
            prompt = {"_unknown": "Diga algo FORA das 200 palavras (varie o conteúdo).",
                      "_silence": "Não fale; grave o ruído real do ambiente."}.get(label, f"Diga: {label}")
            while True:
                choice = input(f"\n[{index + 1}/{len(plan)}] {prompt}\nEnter para preparar; q para sair: ")
                if choice.lower() == "q":
                    return
                try:
                    port.prepare()
                except CaptureDisconnected:
                    print(DISCONNECT_MESSAGE)
                    continue
                print("Toque no microfone do Vox e fale ao aparecer 'Gravando'. Aguarde o envio.", flush=True)
                receiver, pcm = PCMReceiver(), None
                deadline = time.monotonic() + 90
                try:
                    while pcm is None and time.monotonic() < deadline:
                        line = port.readline(1024)
                        if line:
                            transport.write(line)
                            transport.flush()
                            pcm = receiver.feed(line.decode("ascii", errors="replace"))
                    if pcm is None:
                        raise ValueError("Timeout: gravação cancelada, tela apagada ou envio incompleto")
                except CaptureDisconnected:
                    print(DISCONNECT_MESSAGE)
                    continue
                except ValueError as error:
                    print(f"Amostra recusada: {error}")
                    print(f"Diagnóstico salvo em {directory / 'transport.log'}")
                    continue
                stats = pcm_stats(pcm)
                print(f"RMS={stats['rms']:.4f}; saturação={stats['clipped_fraction']:.2%}")
                pending = directory / "pending.wav"
                write_wav(pending, pcm)
                if args.playback:
                    import winsound
                    winsound.PlaySound(str(pending), winsound.SND_FILENAME)
                if input("Enter para aceitar; r para repetir: ").lower() != "":
                    pending.unlink()
                    continue
                filename = f"{index:05d}.wav"
                target = directory / filename
                if target.exists():
                    raise ValueError(f"Unregistered audio already exists; inspect before resuming: {target}")
                pending.rename(target)
                output.write(json.dumps({"file": filename, "label": label,
                    "pcm_sha256": sha256(pcm), "recorded_utc": datetime.now(timezone.utc).isoformat(),
                    **stats}, ensure_ascii=False) + "\n")
                output.flush()
                break
    print("Sessão concluída. Preserve sessões diferentes para calibração e avaliação.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nColeta interrompida. Repita o mesmo comando para continuar.")
