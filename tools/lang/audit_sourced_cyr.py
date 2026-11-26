#!/usr/bin/env python3
"""Produce a reviewer-facing, reproducible RU/UK sourced-content QA report.

No translation engine, machine-generated inflections, or made-up frequency
rankings. Source verification means *spelling provenance*, not that a word
fits a specific theme, is common, or has passed native-speaker review.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lang"))
import dict_v4 as d4
import pools_v2 as p2
import tiles_v2 as t2
from themes import ORDER

CODES = ("ru", "uk")
DEFAULTS = ROOT / "data" / "cyrlex"


def read_catalog(code):
    base = json.loads((DEFAULTS / f"{code}_themes.json").read_text(encoding="utf-8"))
    extras = json.loads((DEFAULTS / f"{code}_source_candidates.json").read_text(
        encoding="utf-8"))
    if (len(base) != 40 or [t["english"] for t in base] != list(ORDER) or
            extras.get("language") != code or
            extras.get("review_status") != "unreviewed-candidates" or
            set(extras.get("themes", {})) != set(ORDER)):
        raise ValueError(f"{code}: 40-theme identity or language gate failed")
    titles, all_words = set(), set()
    rows = []
    idx = {cp for cp in d4.alphabet(code)}
    for theme in base:
        key = theme["english"]
        title = theme["title"]
        if title in titles:
            raise ValueError(f"{code}: duplicated theme title {title}")
        titles.add(title)
        originals = theme["words"]
        candidates = extras["themes"][key]
        if len(originals) != 4 or not 1 <= len(candidates) <= 64:
            raise ValueError(f"{code}/{key}: unsafe starter or candidate count")
        for word in originals + candidates:
            if (not isinstance(word, str) or word != unicodedata.normalize("NFC", word)
                    or not word.isupper()):
                raise ValueError(f"{code}/{key}: invalid {word!r}")
            path = word
            if code == "uk" and any(ch in d4.APOSTROPHES for ch in word):
                path, mask, canonical = d4.project_uk(word)
                if not mask or canonical != word or word in originals:
                    raise ValueError(f"{code}/{key}: invalid separator candidate {word!r}")
            if not 4 <= len(path) <= 9 or any(ord(c) not in idx for c in path) or path in all_words:
                raise ValueError(f"{code}/{key}: invalid or repeated {word!r}")
            all_words.add(path)
        rows.append({"key": key, "title": title,
                     "starter": originals, "candidate": candidates,
                     "total": len(originals) + len(candidates)})
    return rows, extras, all_words


def verify_pack(root, code, rows, extras, words, min_source_words):
    folder = root / code
    manifest = json.loads((folder / "SOURCE-MANIFEST.json").read_text(
        encoding="utf-8"))
    dictionary = (folder / "dict.bin").read_bytes()
    tiles = (folder / "tiles.bin").read_bytes()
    pools = (folder / "pools.bin").read_bytes()
    details = d4.verify(dictionary)
    t2.verify(tiles, dictionary)
    p = p2.verify(dictionary, pools)
    expected_extra = sum(len(r["candidate"]) for r in rows)
    expected_total = len(words)
    expected_apostrophe = sum("ʼ" in word for row in rows for word in row["candidate"])
    raw = (DEFAULTS / f"{code}_source_candidates.json").read_bytes()
    if (details["words"] < min_source_words or
            details["symbols"] != 33 or p["themes"] != 40 or
            p["words"] != expected_total or
            p["apostrophe_words"] != expected_apostrophe or
            manifest.get("apostrophe_password_count", 0) != expected_apostrophe or
            manifest.get("language") != code or
            manifest.get("retained_word_count") != details["words"] or
            manifest.get("starter_password_count") != 160 or
            manifest.get("extra_password_count") != expected_extra or
            manifest.get("total_password_count") != expected_total or
            manifest.get("theme_expansion_sha256") != hashlib.sha256(raw).hexdigest() or
            manifest.get("theme_expansion_status") != "unreviewed-candidates" or
            (folder / "THEME-EXTRAS.json").read_bytes() != raw):
        raise ValueError(f"{code}: source-derived assets/manifests differ from checked-in candidates")
    if tuple(d4.alphabet(code)) != tuple(
            __import__("struct").unpack_from("<33I", dictionary, 20)):
        raise ValueError(f"{code}: source dictionary is a different alphabet")
    if len(dictionary) > 1536 * 1024 or len(pools) > 256 * 1024:
        raise ValueError(f"{code}: derived dictionary exceeds preview RAM budget")
    # The core packed dictionary verifier checks format integrity, while
    # the WPP2 verifier checks that all candidate words are in the real WPD4.
    # This does not equate to editorial approval of their theme meanings.
    return {"dictionary_count": details["words"],
            "dictionary_bytes": len(dictionary),
            "theme_pool_bytes": len(pools),
            "source_forms": manifest.get("source_valid_word_count"),
            "apostrophe_retained": details["apostrophe_words"],
            "apostrophe_passwords": p["apostrophe_words"],
            "apostrophe_excluded": manifest.get("ukrainian_apostrophe_forms_unrepresented", 0),
            "theme_source_sha256": hashlib.sha256(raw).hexdigest(),
            "origin": manifest.get("origin"),
            "frequency_source": manifest.get("frequency_source")}


def analyze(rows, code):
    length_bins = {str(n): 0 for n in range(4, 10)}
    for row in rows:
        for word in row["starter"] + row["candidate"]:
            length_bins[str(len(word.replace("ʼ", "")))] += 1
    counts = [row["total"] for row in rows]
    return {"language": code, "themes": len(rows),
            "starters": sum(len(r["starter"]) for r in rows),
            "extra": sum(len(r["candidate"]) for r in rows),
            "total": sum(counts), "min_per_theme": min(counts),
            "max_per_theme": max(counts),
            "lengths": length_bins}


def report(entries, output):
    lines = [
        "# WordPass RU/UK: source-backed candidate review queue",
        "",
        "This is an **editorial queue**, not native-speaker validation or release",
        "certification. Every candidate has passed codepoint/format checks and,",
        "for sourced builds, WPD4/WPP2 membership and pinned-source manifest",
        "verification. Dictionary presence **does not prove** a good category",
        "match, conversational frequency, suitable inflection, age suitability,",
        "or redistribution clearance.",
        "",
        "| Language | WPD4 words | Starter passes | New candidate passes | Total | Smallest theme |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for code, info in entries.items():
        summary = info["summary"]
        derived = info.get("pack", {})
        lines.append(
            f"| {code} | {derived.get('dictionary_count', 'not built')} "
            f"| {summary['starters']} | {summary['extra']} "
            f"| {summary['total']} | {summary['min_per_theme']} |")
    lines += ["", "## Review instructions", "",
              "Review each line for semantic theme fit, natural spelling,",
              "singular/plural/inflection suitability, register, regional",
              "usage and offensive/unwanted forms. Do not auto-approve based",
              "on Hunspell membership. Ukrainian orthographic word-chain support",
              "uses stored non-tile separators for words and explicitly tagged passwords.",
              ""]
    for code, info in entries.items():
        lines += [f"## {code.upper()} — 40 editorial categories", "",
                  "| Theme key | Localized title | Starter | New | All | Candidate words awaiting review |",
                  "| --- | --- | ---: | ---: | ---: | --- |"]
        for row in info["rows"]:
            lines.append(
                f"| {row['key']} | {row['title']} | {len(row['starter'])} "
                f"| {len(row['candidate'])} | {row['total']} "
                f"| {', '.join(row['candidate'])} |")
        lines += ["", "**Length distribution (password candidates, 4–9 letters):** "
                  + ", ".join(f"{n}: {v}" for n, v in info["summary"]["lengths"].items()),
                  ""]
        if "pack" in info:
            pack = info["pack"]
            lines += [f"**Attested dictionary:** {pack['dictionary_count']} words; "
                      f"{pack['dictionary_bytes']} bytes; "
                      f"pool {pack['theme_pool_bytes']} bytes.",
                      f"**Apostrophe spellings retained:** {pack['apostrophe_retained']}; "
                      f"**excluded:** {pack['apostrophe_excluded']}.",
                      f"**Apostrophe passwords:** {pack['apostrophe_passwords']}.",
                      f"**Upstream:** {pack['origin']}",
                      "**Frequency:** not certified; technical common/rare tiers "
                      "must not be described as statistical popularity.",
                      ""]
    output.mkdir(parents=True, exist_ok=True)
    (output / "RU-UK-REVIEW.md").write_text("\n".join(lines) + "\n",
                                           encoding="utf-8")
    (output / "RU-UK-REVIEW.json").write_text(
        json.dumps({"release_status": "UNREVIEWED: human editorial and rights gates open",
                    "languages": entries}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packs", type=Path,
                        help="root containing paired ru/ and uk/ sourced packs")
    parser.add_argument("--min-source-words", type=int, default=100000)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    entries = {}
    for code in CODES:
        rows, extras, words = read_catalog(code)
        summary = analyze(rows, code)
        record = {"rows": rows, "summary": summary}
        if args.packs:
            record["pack"] = verify_pack(args.packs, code, rows, extras,
                                         words, args.min_source_words)
        entries[code] = record
    report(entries, args.out)
    for code, item in entries.items():
        s = item["summary"]
        print(f"RU/UK REVIEW {code}: {s['themes']} themes, {s['starters']} "
              f"starters + {s['extra']} candidates = {s['total']}; "
              f"minimum {s['min_per_theme']} per theme")
    print("CYR EDITORIAL QUEUE OK: candidate provenance check only, human review outstanding")


if __name__ == "__main__":
    main()
