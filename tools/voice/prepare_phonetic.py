"""Generate a provisional PT-BR lexicon and a phonetic coverage recording plan.

Uses eSpeak only as a LOCAL text-to-IPA helper. Does not synthesize training
audio, open a serial port, train a recognizer or touch current voice sessions.
"""
import argparse
from collections import Counter
import hashlib
import importlib.metadata
import json
from pathlib import Path
import sys

from dataset import ROOT, vocabulary
from phonetic_units import UNIT_SPEC, choose_prompts, tokenize, units_and_pairs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phonemizer-path", type=Path, default=ROOT / ".pio/voice-phonetic-tools")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-prompts", type=int, default=40)
    args = parser.parse_args()
    if args.output.exists() or args.max_prompts < 1:
        parser.error("Use a new output directory and positive max-prompts")
    sys.path.insert(0, str(args.phonemizer_path.resolve()))
    from piper.phonemize_espeak import EspeakPhonemizer
    converter = EspeakPhonemizer()
    words = vocabulary()
    source = Path(__file__).with_name("phonetic-prompts.txt")
    candidates = sorted({line.strip() for line in source.read_text(encoding="utf-8").splitlines()
                         if line.strip() and not line.lstrip().startswith("#")} | set(words))
    entries = []
    for text in candidates:
        raw = " ".join("".join(sentence) for sentence in converter.phonemize("pt-br", text))
        units = tokenize(raw)
        if not units:
            raise ValueError(f"No pronunciation for {text}")
        entries.append({"text": text, "ipa_generated": raw, "units": units,
                        "reviewed": False, "source": "piper-espeak-ng-pt-br"})
    lexicon = [entry for entry in entries if entry["text"] in words]
    target_units, target_pairs = set(), set()
    for entry in lexicon:
        units, pairs = units_and_pairs(entry["units"])
        target_units |= units
        target_pairs |= pairs
    chosen, seen_units, seen_pairs = choose_prompts(entries, target_units, target_pairs, args.max_prompts)
    pilot_words = {"Comprar", "Fazer", "Hoje", "Casa", "Pão", "Leite"}
    pilot_units, pilot_pairs = set(), set()
    for entry in lexicon:
        if entry["text"] in pilot_words:
            units, pairs = units_and_pairs(entry["units"])
            pilot_units |= units
            pilot_pairs |= pairs
    tokens = ["<blank>"] + sorted(set().union(*(set(e["units"]) for e in entries)))
    spoken_words = {word.casefold() for entry in chosen for word in entry["text"].split()}
    spoken_sequences = {tuple(entry["units"]) for entry in chosen}
    held_out = [entry for entry in lexicon if entry["text"].casefold() not in spoken_words
                and tuple(entry["units"]) not in spoken_sequences
                and set(entry["units"]) <= seen_units]
    def novel_pairs(entry):
        return len(units_and_pairs(entry["units"])[1] - seen_pairs)
    easy = sorted([e for e in held_out if novel_pairs(e) == 0], key=lambda e: e["text"])[:10]
    harder = sorted([e for e in held_out if novel_pairs(e) > 0],
                    key=lambda e: (-novel_pairs(e), e["text"]))[:10]
    generalization = {"version": 1, "status": "draft-held-out-words-never-use-for-training",
                      "capture_seconds": 2, "unit_spec": UNIT_SPEC, "tokens": tokens,
                      "prompt_count": len(easy + harder), "suggested_initial_repetitions": 3,
                      "prompts": [{**e, "unseen_adjacent_pairs": novel_pairs(e)} for e in easy + harder],
                      "easy_count": len(easy), "novel_context_count": len(harder)}
    coverage = {"target_words": len(lexicon), "target_units": len(target_units),
                "target_pairs": len(target_pairs), "selected_prompts": len(chosen),
                "selected_units_covered": len(target_units & seen_units),
                "selected_pairs_covered": len(target_pairs & seen_pairs),
                "missing_units": sorted(target_units - seen_units),
                "missing_pairs": sorted([list(p) for p in target_pairs - seen_pairs]),
                "pilot_units_covered": len(target_units & pilot_units),
                "pilot_pairs_covered": len(target_pairs & pilot_pairs),
                "pilot_missing_units": sorted(target_units - pilot_units),
                "warning": "Text coverage under generated pronunciations, NOT acoustic coverage or recognition accuracy"}
    count = Counter(unit for entry in chosen for unit in entry["units"])
    plan = {"version": 1, "status": "draft-needs-pronunciation-and-duration-review",
            "capture_seconds": 2, "unit_spec": UNIT_SPEC, "tokens": tokens,
            "prompt_count": len(chosen), "suggested_initial_repetitions": 3,
            "unit_occurrences_per_pass": dict(sorted(count.items())), "prompts": chosen}
    provenance = {"generator": "prepare_phonetic.py", "piper_tts": importlib.metadata.version("piper-tts"),
                  "voice": "pt-br", "unit_spec": UNIT_SPEC,
                  "candidates_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                  "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  "units_sha256": hashlib.sha256(Path(__file__).with_name("phonetic_units.py").read_bytes()).hexdigest(),
                  "notes": ["Generated pronunciation is not a verified transcription of the user's speech.",
                            "No audio was generated or recorded.", "Homophones and pronunciation variants require decoder handling."]}
    args.output.mkdir(parents=True)
    for name, data in (("lexicon.json", {"version": 1, "unit_spec": UNIT_SPEC,
                                         "tokens": tokens, "entries": lexicon, "provenance": provenance}),
                       ("coverage.json", coverage), ("recording-plan.json", plan),
                       ("generalization-plan.json", generalization)):
        (args.output / name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in coverage.items() if key not in ("missing_pairs",)}, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()
