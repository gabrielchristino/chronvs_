"""Experimental IPA-derived acoustic units; not a definitive PT-BR inventory.

Nasalization stays attached to its vowel. Stress/length are excluded in v1;
affricates t-sh/d-zh are single units. Spelling and Unicode codepoints are not
the unit inventory. All generated pronunciations still require review.
"""
import re
import unicodedata

UNIT_SPEC = {"version": 1, "normalization": "NFD-with-attached-diacritics",
             "affricates": ["tʃ", "dʒ", "ts", "dz"],
             "ignore": "stress,length,punctuation", "boundary": "|", "blank": "<blank>"}


def tokenize(ipa):
    ipa = ipa.replace("ʤ", "dʒ").replace("ʧ", "tʃ").replace("g", "ɡ")
    ipa = re.sub(r"\([A-Za-z-]+\)", "", ipa)
    ipa = unicodedata.normalize("NFD", ipa).replace("\u0361", "").replace("\u035c", "")
    units = []
    for char in ipa:
        if char in "ˈˌːˑ.,;:!?\"'/[]()":
            continue
        if char.isspace() or char == "|":
            if units and units[-1] != "|":
                units.append("|")
        elif unicodedata.combining(char) or char in "ʲʷ":
            if not units or units[-1] == "|":
                raise ValueError("IPA modifier has no base sound")
            units[-1] += char
        else:
            if not unicodedata.category(char).startswith("L"):
                raise ValueError(f"Unsupported IPA symbol U+{ord(char):04X}")
            units.append(char)
    merged, index = [], 0
    while index < len(units):
        pair = "".join(units[index:index + 2])
        if pair in UNIT_SPEC["affricates"]:
            merged.append(pair)
            index += 2
        else:
            merged.append(units[index])
            index += 1
    if merged and merged[-1] == "|":
        merged.pop()
    return merged


def units_and_pairs(units):
    phones = {unit for unit in units if unit != "|"}
    pairs = {tuple(pair) for pair in zip(units, units[1:]) if "|" not in pair}
    return phones, pairs


def choose_prompts(entries, target_units, target_pairs, limit):
    """Greedy text coverage, not proof of acoustic/contextual coverage."""
    chosen, seen_units, seen_pairs = [], set(), set()
    remaining = list(entries)
    while remaining and len(chosen) < limit:
        def score(entry):
            units, pairs = units_and_pairs(entry["units"])
            gain = 20 * len((units & target_units) - seen_units) + len((pairs & target_pairs) - seen_pairs)
            return (gain / max(1, len(entry["units"])) ** .5, gain, entry["text"])
        best = max(remaining, key=score)
        if score(best)[1] == 0:
            break
        chosen.append(best)
        remaining.remove(best)
        units, pairs = units_and_pairs(best["units"])
        seen_units |= units
        seen_pairs |= pairs
    return chosen, seen_units, seen_pairs
