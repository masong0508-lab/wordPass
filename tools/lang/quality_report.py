#!/usr/bin/env python3
"""Deterministic, actionable translation audit for the 11 language UI tables.

Report uses the actual pixel-budget samples, compiled dictionary manifest,
packed ROM theme headings, and references to the English source strings.
It is a REVIEW QUEUE, not automated certification of idiomatic translations.

  python3 tools/lang/quality_report.py build/language-quality.md
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lang"))
import gen_ui
from themes import ORDER, THEME_NAMES


def main():
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "language-quality.md"
    rows = gen_ui.load()
    bad = gen_ui.validate(rows)
    if bad:
        print("\n".join(bad[:50]), file=sys.stderr)
        return 1
    byid = {r[1]: r[5] for r in rows}
    manifest = {}
    for line in (ROOT / "data" / "lang" / "manifest.txt").read_text(encoding="ascii").splitlines():
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if cells[0] != "en":
            manifest[cells[0]] = cells
    lines = [
        "# wordPass language quality / review queue",
        "",
        "Generated deterministically from the game's actual UI source, word manifest, "
        "and packed theme-name source. A green automated result does **not** prove "
        "that every translation is idiomatic or that all dictionary words are correct.",
        "",
        "## Coverage and visible layout",
        "",
        "| Language | UI strings | Translated vs English | Near pixel limit | Dictionary words | Pass words | Themes |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    details = []
    for lang_i, code in enumerate(gen_ui.LANGS):
        tight = []
        same = []
        for ln, id_, scale, budget, sample, translations in rows:
            s = translations[lang_i]
            displayed = gen_ui.expand(s, sample, lang_i, byid)
            rem = budget - gen_ui.width(displayed, scale)
            if rem < 12:
                tight.append((rem, id_, s))
            if code != "en" and s == translations[0] and len(s) >= 8:
                same.append((id_, s))
        tight.sort(key=lambda x: (x[0], x[1]))
        m = manifest.get(code)
        words, pool_words = (m[3], m[5]) if m else ("—", "—")
        theme_count = len(THEME_NAMES[code]) if code != "en" else len(ORDER)
        lines.append(
            f"| {code} | {len(rows)} | "
            f"{len(rows) - len([r for r in rows if r[5][lang_i] == r[5][0]]) if code != 'en' else '—'} | "
            f"{len(tight)} | {words} | {pool_words} | {theme_count} |"
        )
        if code != "en":
            details.append(f"### {code}: review priorities")
            details.append("")
            details.append(f"**Strings matching English:** {len(same)} (not necessarily wrong). "
                           f"**Labels within 11 px of their budget:** {len(tight)}.")
            details.append("")
            if tight:
                details.append("**Tightest rendered labels:**")
                details.append("")
                for remaining, key, translated in tight[:8]:
                    details.append(f"- `{key}` — {remaining} px remaining — `{translated}`")
            if same:
                details.append("")
                details.append("**Long untranslated-looking labels to review:**")
                details.append("")
                for key, translated in same[:8]:
                    details.append(f"- `{key}` — `{translated}`")
            details.append("")
    lines.extend([
        "",
        "## Human review priorities",
        "",
        "These flags are leads, not translation failures. Confirm native grammar, "
        "tone, game mechanic meaning, dialect preferences, and readability on "
        "actual hardware before certifying language quality.",
        "",
    ])
    lines.extend(details)
    lines.extend([
        "## Automated checks",
        "",
        "- `make check` tests all 4,477 locale entries for glyphs, "
        "pixel bounds, formatting and a critical vocabulary subset.",
        "- Every one of the 400 non-English packed ROM theme names must match its source.",
        "- Native checks cover dictionaries, tiles, language switching and corrupt-pack fallback.",
        "- Regular CI tests the actual DS ROM and its language picker in DeSmuME.",
        "",
    ])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"LANGUAGE QUALITY REPORT OK: {len(rows)} UI entries x {len(gen_ui.LANGS)} languages; {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
