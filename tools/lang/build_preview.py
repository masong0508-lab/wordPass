#!/usr/bin/env python3
"""Build RU/UK engineering-preview NitroFS packs from 40 curated seed themes.

Each has only four candidate words per theme plus the earlier test fixtures;
these tiny packs are not sufficient for the real game's language selector.
The full normal ROM never includes these files.
"""
import json
from pathlib import Path
import sys
import dict_v4 as dict4
import tiles_v2 as tiles2
import pools_v2 as pools2
from check_cyr_seeds import check_language

ROOT = Path(__file__).resolve().parents[2]

def build(folder):
    root = Path(folder)
    for code in ("ru", "uk"):
        check_language(code)
        catalog = json.loads((ROOT / "data" / "cyrlex" / f"{code}_themes.json").read_text(encoding="utf-8"))
        themes = {item["title"]: item["words"] for item in catalog}
        assert len(themes) == 40
        target = root / code
        target.mkdir(parents=True, exist_ok=True)
        fixture_words, fixture_common = dict4.FIXTURES[code]
        seed_words = {word for item in catalog for word in item["words"]}
        all_words = set(fixture_words) | seed_words
        # The engineering demo marks its *seed* words as common to make a
        # small exploratory dictionary less frustrating. Actual familiarity
        # tiers must be derived from native-speaker-reviewed frequency lists.
        common = set(fixture_common) | seed_words
        dictionary = dict4.build(code, all_words, common)
        tiles = tiles2.build(code, all_words)
        pools = pools2.build(code, dictionary, themes, preserve_order=True)
        assert dict4.verify(dictionary)["words"] == len(all_words)
        tiles2.verify(tiles, dictionary)
        result = pools2.verify(dictionary, pools)
        assert result["themes"] == 40 and result["words"] == 160
        for name, contents in (("dict.bin", dictionary),
                               ("tiles.bin", tiles), ("pools.bin", pools)):
            (target / name).write_bytes(contents)
        print(f"{code} preview: {len(all_words)} draft words / "
              f"{result['themes']} sample themes / "
              f"{result['words']} draft passes / "
              f"{len(dictionary)+len(tiles)+len(pools)} bytes total")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 tools/lang/build_preview.py nitrofiles/cyrpreview")
    build(sys.argv[1])
