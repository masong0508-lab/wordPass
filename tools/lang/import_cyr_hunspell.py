#!/usr/bin/env python3
"""Offline, attributable intake of externally sourced Russian/Ukrainian Hunspell.

This does NOT automatically certify or distribute a new playable locale.
Input dictionaries and license notices must be obtained from upstream by
the operator. We keep those resources OUT of the default 11-language ROM.

Example:
  python3 tools/lang/import_cyr_hunspell.py --code ru \
    --dic path/to/ru_RU.dic --aff path/to/ru_RU.aff \
    --notice path/to/README_ru_RU.txt --origin https://... \
    --out build/ru-experiment

  python3 tools/lang/import_cyr_hunspell.py --selftest build/cyr-intake
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unicodedata

import dict_v4 as d4
import tiles_v2 as t2
import pools_v2 as p2
from check_cyr_seeds import check_language
from hunspell import expand

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUDGET = 1536 * 1024  # practical per-language dictionary RAM budget


def normalize(token, index):
    word = unicodedata.normalize("NFC", token.strip()).upper()
    if (not d4.MIN_LEN <= len(word) <= d4.MAX_LEN or
        any(ord(ch) not in index for ch in word)):
        return None
    return word


def read_catalog(code):
    check_language(code)
    rows = json.loads((ROOT / "data" / "cyrlex" /
                       (code + "_themes.json")).read_text(encoding="utf-8"))
    themes = {row["title"]: row["words"] for row in rows}
    return themes, {word for row in rows for word in row["words"]}


def rank_words(path, index):
    if path is None:
        return []
    seen, ranked = set(), []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        word = normalize(fields[0], index)
        if word is not None and word not in seen:
            ranked.append(word)
            seen.add(word)
    return ranked


def build(code, dic, aff, notice, origin, out,
          rank=None, max_words=0, max_dict_bytes=DEFAULT_BUDGET):
    if code not in ("ru", "uk"):
        raise ValueError("supported Cyrillic source codes: ru, uk")
    if not origin or not origin.startswith(("https://", "http://")):
        raise ValueError("a nonblank origin URL is mandatory for attribution")
    notice_bytes = Path(notice).read_bytes()
    if len(notice_bytes) < 12:
        raise ValueError("license/attribution notice missing or implausibly short")
    dic = Path(dic)
    aff = Path(aff)
    if not dic.is_file() or not aff.is_file():
        raise ValueError("both the spelling dictionary and its affix rules are required")
    themes, seeds = read_catalog(code)
    index = {cp: i for i, cp in enumerate(d4.alphabet(code))}

    # Expand Hunspell affix flags rather than pretending the root .dic entries
    # are all playable inflections. Default Hunspell expander drops capitalized
    # proper names and forbids marked ONLYINCOMPOUND / FORBIDDENWORD roots.
    forms = expand(str(dic), str(aff))
    words = set()
    for form in forms:
        word = normalize(form, index)
        if word is not None:
            words.add(word)
    if not words:
        raise ValueError("no safe Cyrillic words survived parsing")
    missing = seeds - words
    if missing:
        raise ValueError(f"{len(missing)} candidate passwords absent from source dictionary: "
                         + ", ".join(sorted(missing)[:18])
                         + " (edit/review catalog, do not inject unsupported words)")

    ranked = rank_words(rank, index)
    if max_words and len(words) > max_words:
        if not ranked:
            raise ValueError("word budget requires an independently sourced frequency ranking; "
                             "lexicographic cutoff would bias the dictionary")
        selected = set(seeds)
        for word in ranked:
            if len(selected) >= max_words:
                break
            if word in words:
                selected.add(word)
        if len(selected) < max_words:
            # No invented frequency for non-ranked words: remaining source
            # entries are retained only in deterministic codepoint order and
            # remain RARE, not incorrectly promoted to common.
            selected.update(sorted(words - selected)[:max_words - len(selected)])
        words = selected
    common = {word for word in ranked[:15000] if word in words}
    if not common:
        # Preview-only fallback. No assertion that these 160 seed words are
        # statistically common or native-editor-approved.
        common = seeds
    dictionary = d4.build(code, words, common)
    if len(dictionary) > max_dict_bytes:
        raise ValueError(f"dictionary is {len(dictionary)} bytes > {max_dict_bytes} budget; "
                         "supply a reviewed frequency ranking and --max-words")
    tiles = t2.build(code, words)
    pools = p2.build(code, dictionary, themes, preserve_order=True)
    d4.verify(dictionary)
    t2.verify(tiles, dictionary)
    report = p2.verify(dictionary, pools)
    if report["themes"] != 40 or report["words"] != 160:
        raise ValueError("40-theme seed library incomplete")

    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for filename, payload in (("dict.bin", dictionary), ("tiles.bin", tiles),
                              ("pools.bin", pools)):
        (out / filename).write_bytes(payload)
    (out / "LICENSE-SOURCE.txt").write_bytes(notice_bytes)
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    manifest = {
        "status": "EXPERIMENTAL: not certified for release",
        "language": code, "origin": origin,
        "dictionary_source_sha256": sha(dic),
        "affix_source_sha256": sha(aff),
        "license_notice_sha256": hashlib.sha256(notice_bytes).hexdigest(),
        "source_valid_word_count": len(forms),
        "retained_word_count": len(words),
        "common_tier_count": len(common),
        "starter_theme_count": 40, "starter_password_count": 160,
        "dictionary_bytes": len(dictionary),
        "tile_bytes": len(tiles), "pool_bytes": len(pools),
        "frequency_source": str(rank) if rank else "NONE: candidate seeds marked common for engineering only",
        "warnings": [
            "Native-speaker review, mature frequency tiers and content filtering still required",
            "The bundled 160 candidate passwords are not enough for full Classic/Daily play",
            "Verify source and derivative redistribution obligations for all components",
        ],
    }
    (out / "SOURCE-MANIFEST.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"CYR INTAKE {code}: {len(words)} source words, {len(dictionary)} dictionary bytes, "
          f"40 starter themes, {len(pools)} pool bytes; status EXPERIMENTAL")
    return manifest


def selftest(folder):
    root = Path(folder)
    root.mkdir(parents=True, exist_ok=True)
    notice = root / "NOTICE.synthetic.txt"
    notice.write_text("Synthetic fixtures generated by wordPass for testing only.\n",
                      encoding="utf-8")
    aff = root / "test.aff"
    aff.write_text("SET UTF-8\n", encoding="utf-8")
    for code in ("ru", "uk"):
        themes, seeds = read_catalog(code)
        fixtures, _ = d4.FIXTURES[code]
        words = sorted(set(fixtures) | seeds)
        dic = root / (code + ".dic")
        dic.write_text(str(len(words)) + "\n" +
                       "\n".join(word.lower() for word in words) + "\n",
                       encoding="utf-8")
        dest = root / code
        result = build(code, dic, aff, notice, "https://example.invalid/synthetic",
                       dest, max_dict_bytes=DEFAULT_BUDGET)
        assert result["starter_theme_count"] == 40
        assert result["starter_password_count"] == 160
        dictionary = (dest / "dict.bin").read_bytes()
        pools = (dest / "pools.bin").read_bytes()
        assert p2.verify(dictionary, pools)["words"] == 160
        assert (dest / "LICENSE-SOURCE.txt").read_bytes() == notice.read_bytes()
    print("CYR INTAKE OK: 40 themes, trusted source words, verified binary + attribution")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--selftest", metavar="FOLDER")
    p.add_argument("--code", choices=("ru", "uk"))
    p.add_argument("--dic")
    p.add_argument("--aff")
    p.add_argument("--notice")
    p.add_argument("--origin")
    p.add_argument("--out")
    p.add_argument("--rank")
    p.add_argument("--max-words", type=int, default=0)
    p.add_argument("--max-dict-bytes", type=int, default=DEFAULT_BUDGET)
    args = p.parse_args()
    if args.selftest:
        selftest(args.selftest)
    elif all((args.code, args.dic, args.aff, args.notice, args.origin, args.out)):
        if args.max_words < 0 or args.max_dict_bytes < 1024:
            p.error("word and byte budgets must be positive")
        build(args.code, args.dic, args.aff, args.notice, args.origin, args.out,
              rank=args.rank, max_words=args.max_words,
              max_dict_bytes=args.max_dict_bytes)
    else:
        p.error("provide --selftest or --code, --dic, --aff, --notice, --origin and --out")

if __name__ == "__main__":
    main()
