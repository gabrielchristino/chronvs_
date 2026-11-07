"""Text comparison for local ASR auditing; hypotheses never replace labels."""
from collections import defaultdict
import unicodedata


def normalize(text, fold_accents=False):
    text = unicodedata.normalize("NFC", text).casefold()
    if fold_accents:
        text = "".join(c for c in unicodedata.normalize("NFD", text) if not unicodedata.combining(c))
    return " ".join("".join(c if unicodedata.category(c)[0] in "LN" else " " for c in text).split())


def distance(reference, hypothesis):
    previous = list(range(len(hypothesis) + 1))
    for i, expected in enumerate(reference, 1):
        current = [i]
        for j, actual in enumerate(hypothesis, 1):
            current.append(min(current[-1] + 1, previous[j] + 1, previous[j-1] + (expected != actual)))
        previous = current
    return previous[-1]


def summarize(cases):
    known = [case for case in cases if not case["expected"].startswith("_")]
    silence = [case for case in cases if case["expected"] == "_silence"]
    unknown = [case for case in cases if case["expected"] == "_unknown"]
    word_errors = sum(distance(normalize(c["expected"]).split(), normalize(c["hypothesis"]).split()) for c in known)
    word_count = sum(len(normalize(c["expected"]).split()) for c in known)
    char_errors = sum(distance(normalize(c["expected"]), normalize(c["hypothesis"])) for c in known)
    char_count = sum(len(normalize(c["expected"])) for c in known)
    result = {"cases": len(cases), "known_text_cases": len(known),
              "exact_known": sum(normalize(c["expected"]) == normalize(c["hypothesis"]) for c in known),
              "exact_known_without_accents": sum(normalize(c["expected"], True) == normalize(c["hypothesis"], True) for c in known),
              "word_errors": word_errors, "reference_words": word_count,
              "wer": word_errors / word_count if word_count else None,
              "char_errors": char_errors, "reference_chars": char_count,
              "cer": char_errors / char_count if char_count else None,
              "silence_cases": len(silence),
              "silence_nonempty_hypotheses": sum(bool(normalize(c["hypothesis"])) for c in silence),
              "untranscribed_unknown_cases": len(unknown),
              "unknown_nonempty_hypotheses": sum(bool(normalize(c["hypothesis"])) for c in unknown)}
    return result


def review_queue(cases):
    grouped = defaultdict(list)
    for case in cases:
        if case["expected"] == "_unknown":
            continue  # Untranscribed speech is expected to produce text in ASR.
        mismatch = (bool(normalize(case["hypothesis"])) if case["expected"] == "_silence"
                    else normalize(case["expected"]) != normalize(case["hypothesis"]))
        if mismatch:
            grouped[case["expected"]].append(case)
    return [{"expected": text, "status": "needs-listening-not-an-automatic-correction",
             "examples": examples} for text, examples in sorted(grouped.items())]
