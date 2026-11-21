#!/usr/bin/env python3
"""Audit 40-theme Russian and Ukrainian seed catalogs without shipping them.

Four words per theme is NOT a complete playable pass library. This checker
enforces encoding and structure; native-speaker review remains necessary.
"""
import json
from pathlib import Path
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lang"))
from themes import ORDER
from dict_v4 import alphabet

def check_language(code):
    rows = json.loads((ROOT / "data" / "cyrlex" / (code + "_themes.json")).read_text(encoding="utf-8"))
    if not isinstance(rows, list) or len(rows) != 40:
        raise ValueError(f"{code}: expected exactly 40 ordered themes")
    chars = set(alphabet(code))
    titles, words = set(), set()
    for i, row in enumerate(rows):
        if not isinstance(row, dict) or set(row) != {"english", "title", "words"}:
            raise ValueError(f"{code}: theme {i} malformed")
        if row["english"] != ORDER[i]:
            raise ValueError(f"{code}: shelf order mismatch at {i}")
        title = row["title"]
        if (not isinstance(title, str) or title in titles or
            not 0 < len(title.encode("utf-8")) <= 23 or
            title != unicodedata.normalize("NFC", title) or
            any(ord(c) not in chars for c in title)):
            raise ValueError(f"{code}: invalid or duplicated title {title!r}")
        titles.add(title)
        sample = row["words"]
        if not isinstance(sample, list) or len(sample) != 4:
            raise ValueError(f"{code}: {title}: need four unique starter words")
        for word in sample:
            if (not isinstance(word, str) or
                not 4 <= len(word) <= 9 or
                not word.isupper() or
                word != unicodedata.normalize("NFC", word) or
                any(ord(c) not in chars for c in word) or
                word in words):
                raise ValueError(f"{code}: bad word {word!r}")
            words.add(word)
    if len(words) != 160:
        raise ValueError(f"{code}: expected 160 distinct words, got {len(words)}")
    used = set("".join(words))
    if code == "ru" and "Ё" not in used:
        raise ValueError("Russian seeds should cover Ё")
    if code == "uk" and not {"І", "Ї", "Є"}.issubset(used):
        raise ValueError("Ukrainian seeds should cover І, Ї and Є")
    print(f"CYR SEEDS {code}: 40 aligned themes, 160 unique draft words, Unicode and byte budgets valid")

if __name__ == "__main__":
    check_language("ru")
    check_language("uk")
    print("CYR SEEDS OK (not native-speaker review or a finished dictionary)")
