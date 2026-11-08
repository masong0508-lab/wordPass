#!/usr/bin/env python3
"""Turn the output of `build/bot --lang xx` (one markdown file per language) into the data tables of docs/BALANCE.md.

    python3 tools/check/balance_md.py DIR [DIR2 ...]      prints the tables for each directory (a label is the directory name)

The bot prints, per language: two per-shelf tables (competent, weak), a run summary, the Daily modifiers table and a
diagnostics line. This script lines the languages up side by side (clear rate by shelf) and repeats the English
tables in full, so the report stays readable.
"""
import os, re, sys

LANGS = ["en", "es", "de", "fr", "fi"]


def parse(path):
    txt = open(path, encoding="utf-8").read()
    out = {"text": txt, "tables": {}}
    for who in ("Competent", "Weak"):
        m = re.search(r"\*\*%s bot[^\n]*\n\n(\|[^\n]*\n\|[^\n]*\n(?:\|[^\n]*\n)+)" % who, txt)
        rows = {}
        if m:
            for line in m.group(1).splitlines()[2:]:
                cells = [c.strip() for c in line.strip("|").split("|")]
                rows[int(cells[0])] = cells
        out["tables"][who] = rows
    return out


def side_by_side(data, who, col, title, unit="%"):
    langs = [l for l in LANGS if l in data]
    lines = ["| shelf | " + " | ".join(langs) + " |", "| ---: |" + " ---: |" * len(langs)]
    for sh in range(1, 25):
        row = [data[l]["tables"][who].get(sh, ["", "-"] + ["-"] * 20)[col] for l in langs]
        lines.append("| %d | %s |" % (sh, " | ".join(row)))
    return "**%s**\n\n%s\n" % (title, "\n".join(lines))


def english_detail(d):
    t = d["text"]
    i = t.index("**Competent bot")
    j = t.index("**Whole runs")
    return t[i:j]


def tail(d):
    t = d["text"]
    return t[t.index("**Whole runs"):]


def report(dirpath):
    data = {}
    for l in LANGS:
        p = os.path.join(dirpath, l + ".md")
        if os.path.exists(p):
            data[l] = parse(p)
    if not data:
        return ""
    out = []
    out.append(side_by_side(data, "Competent", 1, "Competent bot: clear rate (%) by shelf and language"))
    out.append(side_by_side(data, "Weak", 1, "Weak bot: clear rate (%) by shelf and language"))
    if "en" in data:
        out.append("#### English in full\n\n" + english_detail(data["en"]))
    for l in LANGS:
        if l in data and l != "en":
            out.append("#### %s: runs, Daily modifiers, diagnostics\n\n%s" % (l, tail(data[l])))
    if "en" in data:
        out.append("#### en: runs, Daily modifiers, diagnostics\n\n" + tail(data["en"]))
    return "\n".join(out)


if __name__ == "__main__":
    for d in sys.argv[1:]:
        print("<!-- bot data: %s -->" % os.path.basename(os.path.normpath(d)))
        print(report(d))
