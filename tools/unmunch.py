#!/usr/bin/env python3
"""Expand a Hunspell .dic/.aff pair into a plain word list (lower-case a-z words only).

    unmunch.py INDEX.dic INDEX.aff > words.txt

Handles the single-character flags, PFX/SFX rules with strip/add/condition and cross
products that the English dictionaries use.  Capitalised stems (proper nouns) and
anything with an apostrophe or digit are dropped.
"""
import re, sys

def parse_aff(path):
    rules = {"PFX": {}, "SFX": {}}
    cross = {"PFX": {}, "SFX": {}}
    for line in open(path, encoding="utf-8"):
        p = line.split()
        if len(p) >= 4 and p[0] in rules and p[3].isdigit():
            cross[p[0]][p[1]] = (p[2] == "Y")
            rules[p[0]].setdefault(p[1], [])
        elif len(p) >= 5 and p[0] in rules and not line.lstrip().startswith("#"):
            kind, flag, strip, add, cond = p[0], p[1], p[2], p[3], p[4]
            add = add.split("/")[0]
            cond_re = re.compile(("^" + cond) if kind == "PFX" else (cond + "$"))
            rules[kind].setdefault(flag, []).append((0 if strip == "0" else len(strip),
                                                     "" if strip == "0" else strip,
                                                     "" if add == "0" else add, cond_re))
    return rules, cross

def expand(dic, aff):
    rules, cross = parse_aff(aff)
    out = set()
    for line in open(dic, encoding="utf-8", errors="ignore").read().split("\n")[1:]:
        if not line.strip():
            continue
        stem, _, flags = line.strip().partition("/")
        if not stem.isalpha() or not stem.isascii() or not stem.islower():
            continue
        flags = flags.split()[0] if flags else ""
        if flags == "c":
            continue
        out.add(stem)
        sfx, pfx = [], []
        for f in flags:
            for strip_n, strip, add, cond in rules["SFX"].get(f, ()):
                if cond.search(stem) and stem.endswith(strip):
                    sfx.append((stem[: len(stem) - strip_n] + add, f))
            for strip_n, strip, add, cond in rules["PFX"].get(f, ()):
                if cond.search(stem) and stem.startswith(strip):
                    pfx.append((add, stem[strip_n:], f))
        out.update(w for w, _ in sfx)
        out.update(add + rest for add, rest, _ in pfx)
        for add, _, pf in pfx:
            if cross["PFX"].get(pf):
                out.update(add + sw for sw, sf in sfx if cross["SFX"].get(sf))
    return {w for w in out if w.isalpha() and w.isascii() and w.islower()}

if __name__ == "__main__":
    for w in sorted(expand(sys.argv[1], sys.argv[2])):
        print(w)
