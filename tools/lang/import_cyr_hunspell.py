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
from themes import ORDER
from hunspell import expand

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUDGET = 1536 * 1024  # practical per-language dictionary RAM budget

# Both ordinary keyboard and typographic Ukrainian apostrophes are the same
# orthographic separator for auditing. Do not simply discard them or silently
# strip them out of passwords: WPD4's current DS tile alphabet lacks a
# non-tile apostrophe representation, so these forms remain unsupported.
UK_APOSTROPHES = "'’ʼ"


def canonical_uk_apostrophe(form):
    word = unicodedata.normalize("NFC", form.strip()).upper()
    return word.replace("'", "ʼ").replace("’", "ʼ")



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


def expand_themes_from_source(code, path, themes, starters, source_words):
    """Accept explicit language-specific, unreviewed, source-backed additions.

    English theme keys are identifiers, NEVER English/Russian/Ukrainian words
    to put into the playable pack. The original 40 translated titles remain
    fixed. No semantic matching or automatic theming is invented by software.
    """
    if path is None:
        return set()
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    if (not isinstance(raw, dict) or set(raw) !=
            {"language", "review_status", "themes"} or
            raw["language"] != code or
            raw["review_status"] != "unreviewed-candidates" or
            not isinstance(raw["themes"], dict) or not raw["themes"]):
        raise ValueError(f"{code}: expansion must specify its language, "
                         "unreviewed status and nonempty theme mapping")
    titles = dict(zip(ORDER, themes))
    if len(titles) != 40 or any(key not in titles for key in raw["themes"]):
        raise ValueError(f"{code}: unknown or missing canonical theme identity")
    index = {cp: i for i, cp in enumerate(d4.alphabet(code))}
    added = set()
    for english, entries in raw["themes"].items():
        if not isinstance(entries, list) or not 1 <= len(entries) <= 64:
            raise ValueError(f"{code}/{english}: provide 1..64 editorial candidates")
        for word in entries:
            if (not isinstance(word, str) or
                not 4 <= len(word) <= 9 or
                word != unicodedata.normalize("NFC", word) or
                not word.isupper() or
                any(ord(ch) not in index for ch in word)):
                raise ValueError(f"{code}/{english}: invalid spelling or alphabet: {word!r}")
            if word in starters or word in added:
                raise ValueError(f"{code}/{english}: duplicate password {word}")
            if word not in source_words:
                raise ValueError(f"{code}/{english}: {word} absent from licensed source dictionary")
            added.add(word)
            themes[titles[english]].append(word)
    return added


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
          rank=None, max_words=0, max_dict_bytes=DEFAULT_BUDGET,
          extra_themes=None):
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
    forms = expand(str(dic), str(aff),
                   allow_internal_apostrophes=(code == "uk"))
    apostrophized = set()
    words = set()
    for form in forms:
        if code == "uk" and any(ch in form for ch in UK_APOSTROPHES):
            canonical = canonical_uk_apostrophe(form)
            parts = canonical.split("ʼ")
            if len(parts) > 1 and all(part and part.isalpha() for part in parts):
                apostrophized.add(canonical)
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
    extras = expand_themes_from_source(code, extra_themes, themes, seeds, words)
    required = seeds | extras
    if max_words and max_words < len(required):
        raise ValueError(f"--max-words={max_words} would discard mandatory "
                         f"themed passwords ({len(required)})")

    ranked = rank_words(rank, index)
    if max_words and len(words) > max_words:
        if not ranked:
            raise ValueError("word budget requires an independently sourced frequency ranking; "
                             "lexicographic cutoff would bias the dictionary")
        selected = set(required)
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
    if report["themes"] != 40 or report["words"] != 160 + len(extras):
        raise ValueError("40-theme source-backed candidate library incomplete")
    if len(pools) > 256 * 1024:
        raise ValueError("expanded 40-theme pool exceeds Nintendo DS preview budget")

    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for filename, payload in (("dict.bin", dictionary), ("tiles.bin", tiles),
                              ("pools.bin", pools)):
        (out / filename).write_bytes(payload)
    (out / "LICENSE-SOURCE.txt").write_bytes(notice_bytes)
    extras_copy = out / "THEME-EXTRAS.json"
    if extra_themes is not None:
        extras_copy.write_bytes(Path(extra_themes).read_bytes())
    elif extras_copy.exists():
        extras_copy.unlink()
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    manifest = {
        "status": "EXPERIMENTAL: not certified for release",
        "language": code, "origin": origin,
        "dictionary_source_sha256": sha(dic),
        "affix_source_sha256": sha(aff),
        "license_notice_sha256": hashlib.sha256(notice_bytes).hexdigest(),
        "source_valid_word_count": len(forms),
        "source_tile_eligible_word_count": len({normalize(w, index) for w in forms} - {None}),
        "ukrainian_apostrophe_forms_unrepresented": len(apostrophized),
        "ukrainian_apostrophe_example_forms": sorted(apostrophized)[:12],
        "apostrophe_policy": "Preserve source spelling; reject apostrophe forms until a non-tile separator is implemented",
        "retained_word_count": len(words),
        "common_tier_count": len(common),
        "starter_theme_count": 40, "starter_password_count": 160,
        "extra_password_count": len(extras),
        "total_password_count": report["words"],
        "theme_expansion_status": ("unreviewed-candidates" if extra_themes
                                   else "none"),
        "theme_expansion_sha256": (sha(extra_themes) if extra_themes else None),
        "dictionary_bytes": len(dictionary),
        "tile_bytes": len(tiles), "pool_bytes": len(pools),
        "frequency_source": str(rank) if rank else "NONE: candidate seeds marked common for engineering only",
        "warnings": [
            "Native-speaker review, mature frequency tiers and content filtering still required",
            "The starter 160 passwords and any unreviewed expansions are not release-ready",
            "Theme assignments and extra vocabulary require independent native-speaker review",
            "Verify source and derivative redistribution obligations for all components",
            "Ukrainian apostrophe spellings are excluded, never silently stripped; add a true non-tile separator before locale release",
        ],
    }
    (out / "SOURCE-MANIFEST.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"CYR INTAKE {code}: {len(words)} source words, {len(dictionary)} dictionary bytes, "
          f"40 themes, {report['words']} passwords ({len(extras)} additional), "
          f"{len(pools)} pool bytes; "
          f"Ukrainian apostrophe forms excluded={len(apostrophized)}; status EXPERIMENTAL")
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
        # The extra entry is SYNTHETIC (not a real lexical candidate) and
        # appears only in this temporary source-coverage test.
        synthetic = "".join(chr(cp) for cp in d4.alphabet(code)[:4])
        assert synthetic not in seeds
        words = sorted(set(fixtures) | seeds | {synthetic})
        dic = root / (code + ".dic")
        # Exercise all three real Ukrainian apostrophe encodings, which
        # must be accounted for but NEVER turned into misspelled DS words.
        if code == "uk":
            words += ["п'ять", "м’ясо", "звʼязок"]
        dic.write_text(str(len(words)) + "\n" +
                       "\n".join(word.lower() for word in words) + "\n",
                       encoding="utf-8")
        extras = root / (code + ".extra-themes.json")
        extras.write_text(json.dumps({
            "language": code, "review_status": "unreviewed-candidates",
            "themes": {"BINDERY": [synthetic]},
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        # Default builds remain unchanged even if a larger source has words
        # not explicitly assigned to the 40 original themes.
        baseline = root / (code + "-starter-only")
        base_result = build(code, dic, aff, notice,
                            "https://example.invalid/synthetic",
                            baseline, max_dict_bytes=DEFAULT_BUDGET)
        assert base_result["total_password_count"] == 160
        assert not (baseline / "THEME-EXTRAS.json").exists()
        dest = root / code
        result = build(code, dic, aff, notice, "https://example.invalid/synthetic",
                       dest, max_dict_bytes=DEFAULT_BUDGET,
                       extra_themes=extras)
        assert result["starter_theme_count"] == 40
        assert result["starter_password_count"] == 160
        assert result["extra_password_count"] == 1
        assert result["total_password_count"] == 161
        assert result["theme_expansion_status"] == "unreviewed-candidates"
        assert result["theme_expansion_sha256"] == hashlib.sha256(
            extras.read_bytes()).hexdigest()
        assert (dest / "THEME-EXTRAS.json").read_bytes() == extras.read_bytes()
        try:
            build(code, dic, aff, notice, "https://example.invalid/synthetic",
                  root / "unsafe-budget", max_words=160, rank=extras,
                  extra_themes=extras)
        except ValueError as err:
            assert "mandatory" in str(err)
        else:
            raise AssertionError("truncated source word budget dropped themed passwords")
        # An extra password absent from the licensed dictionary, a repeated
        # original seed, and cross-locale JSON must all be rejected.
        for candidate in (
            {"language": code, "review_status": "unreviewed-candidates",
             "themes": {"BINDERY": ["ЖЖЖЖ"] if code == "ru" else ["ЇЇЇЇ"]}},
            {"language": code, "review_status": "unreviewed-candidates",
             "themes": {"BINDERY": [next(iter(seeds))]}},
            {"language": "uk" if code == "ru" else "ru",
             "review_status": "unreviewed-candidates",
             "themes": {"BINDERY": [synthetic]}},
        ):
            invalid = root / (code + ".invalid-extra.json")
            invalid.write_text(json.dumps(candidate, ensure_ascii=False),
                               encoding="utf-8")
            try:
                expand_themes_from_source(code, invalid, dict(themes), seeds,
                                          set(words))
            except ValueError:
                pass
            else:
                raise AssertionError("invalid source/theme mapping was accepted")
        if code == "uk":
            assert result["ukrainian_apostrophe_forms_unrepresented"] == 3
            assert set(result["ukrainian_apostrophe_example_forms"]) == {
                "ПʼЯТЬ", "МʼЯСО", "ЗВʼЯЗОК"
            }, "all Ukrainian apostrophe variants should normalize identically"
        else:
            assert result["ukrainian_apostrophe_forms_unrepresented"] == 0
        dictionary = (dest / "dict.bin").read_bytes()
        pools = (dest / "pools.bin").read_bytes()
        assert p2.verify(dictionary, pools)["words"] == 161
        assert p2.verify((baseline / "dict.bin").read_bytes(),
                         (baseline / "pools.bin").read_bytes())["words"] == 160
        assert (dest / "LICENSE-SOURCE.txt").read_bytes() == notice.read_bytes()
    print("CYR INTAKE OK: 40 themes, 160 starter vs 161 source-backed "
          "test passwords, attribution and invalid-expansion rejection; "
          "Ukrainian apostrophes preserved in audit")


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
    p.add_argument("--extra-themes", help="JSON: unreviewed, source-backed themed candidates")
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
              max_dict_bytes=args.max_dict_bytes,
              extra_themes=args.extra_themes)
    else:
        p.error("provide --selftest or --code, --dic, --aff, --notice, --origin and --out")

if __name__ == "__main__":
    main()
