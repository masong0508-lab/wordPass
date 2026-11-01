#!/usr/bin/env python3
"""Expand a Hunspell .dic/.aff pair into every inflected form, for any flag format.

    hunspell.py INDEX.dic INDEX.aff [--caps] > forms.txt

This generalises tools/unmunch.py (which expands the single-character flags of the English
dictionaries and is kept unchanged so the English data stays byte-identical).  Handled here:

  * FLAG ASCII (default), UTF-8 (one Unicode character per flag), long (two characters)
    and num (comma separated decimals), in the dictionary and in affix continuation flags
  * PFX and SFX rules with strip / add / condition, cross products (Y), and continuation
    flags on a rule (a suffix that licenses a further suffix or prefix, two levels deep)
  * NEEDAFFIX / PSEUDOROOT (the bare stem is not a word), ONLYINCOMPOUND (never a word on
    its own), FORBIDDENWORD (stem and forms removed), morphological fields and comments
  * COMPOUNDRULE / COMPOUNDFLAG, REP, ICONV, OCONV, KEEPCASE are read but not expanded:
    compounds are handled by tools/lang/build_lang.py from word-frequency evidence instead

Output is the raw form, case preserved; the caller normalises.  Stems that start with a
capital letter are proper nouns in every language here except German, where all nouns do;
expand() takes a predicate keep_capitalised(stem, forms) for that case: it sees the stem and
the set of forms made from it and says whether it is a common noun.
"""
import re, sys, collections

def parse_aff(path):
    flagtype = "char"
    special = {}
    pfx, sfx = {}, {}      # flag -> (cross, [(strip, add, contflags, cond_regex)])
    lines = open(path, encoding="utf-8", errors="ignore").read().split("\n")
    for line in lines:
        p = line.split("#")[0].split()
        if len(p) >= 2 and p[0] == "FLAG":
            flagtype = p[1].lower()
    def flags_of(s):
        if not s:
            return []
        if flagtype == "long":
            return [s[i:i + 2] for i in range(0, len(s), 2)]
        if flagtype == "num":
            return [x for x in s.split(",") if x]
        return list(s)
    for line in lines:
        if line.lstrip().startswith("#"):
            continue
        p = line.split("\t#")[0].split()
        if len(p) >= 2 and p[0] in ("NEEDAFFIX", "PSEUDOROOT", "ONLYINCOMPOUND", "FORBIDDENWORD",
                                    "CIRCUMFIX", "KEEPCASE", "NOSUGGEST"):
            special[p[0]] = flags_of(p[1])[0] if flags_of(p[1]) else None
        if len(p) < 4 or p[0] not in ("PFX", "SFX"):
            continue
        table = pfx if p[0] == "PFX" else sfx
        if len(p) == 4 and p[2] in ("Y", "N") and p[3].isdigit():
            table[p[1]] = (p[2] == "Y", [])
            continue
        if len(p) < 5:
            continue
        flag, strip, add, cond = p[1], p[2], p[3], p[4]
        cont = ""
        if "/" in add:
            add, _, cont = add.partition("/")
        strip = "" if strip == "0" else strip
        add = "" if add == "0" else add
        try:
            rx = re.compile(("^" if p[0] == "PFX" else "") + (cond if cond != "." else "") +
                            ("" if p[0] == "PFX" else "$")) if cond != "." else None
        except re.error:
            continue
        table.setdefault(flag, (True, []))[1].append((strip, add, flags_of(cont), rx))
    return flagtype, special, pfx, sfx, flags_of

def expand(dic, aff, keep_capitalised=None, verbose=False):
    flagtype, special, pfx, sfx, flags_of = parse_aff(aff)
    need, onlyc, forbid = special.get("NEEDAFFIX") or special.get("PSEUDOROOT"), special.get("ONLYINCOMPOUND"), special.get("FORBIDDENWORD")
    circ = special.get("CIRCUMFIX")
    out, banned = set(), set()

    def apply_sfx(word, flags, depth):
        res = []
        for f in flags:
            ent = sfx.get(f)
            if not ent:
                continue
            for strip, add, cont, rx in ent[1]:
                if strip and not word.endswith(strip):
                    continue
                if rx is not None and not rx.search(word):
                    continue
                form = word[:len(word) - len(strip)] + add
                res.append((form, f, cont))
                if cont and depth < 2:
                    for f2, c2, k2 in apply_sfx(form, [c for c in cont if c in sfx], depth + 1):
                        res.append((f2, f, [c for c in k2] + cont))
        return res

    def apply_pfx(word, flags):
        res = []
        for f in flags:
            ent = pfx.get(f)
            if not ent:
                continue
            for strip, add, cont, rx in ent[1]:
                if strip and not word.startswith(strip):
                    continue
                if rx is not None and not rx.search(word):
                    continue
                res.append((add + word[len(strip):], f, cont))
        return res

    lines = open(dic, encoding="utf-8", errors="ignore").read().split("\n")
    start = 1 if lines and lines[0].strip().isdigit() else 0
    for line in lines[start:]:
        if not line.strip() or line[0] in "#\t /":
            continue
        tok = line.strip().split()[0]
        stem, _, fl = tok.partition("/")
        flags = flags_of(fl)
        if forbid and forbid in flags:
            banned.add(stem.lower())
            continue
        caps = stem[:1].isupper()
        if caps and keep_capitalised is None:
            continue
        if not stem.isalpha():
            continue
        loc = set()
        is_only = bool(onlyc and onlyc in flags)
        bare_ok = not (need and need in flags) and not is_only
        if bare_ok:
            loc.add(stem)
        sfx_forms = apply_sfx(stem, flags, 0)
        pfx_forms = apply_pfx(stem, flags)
        for form, f, cont in sfx_forms:
            if not (circ and circ in cont) and not (onlyc and onlyc in cont) and not is_only and not (need and need in cont):
                loc.add(form)
        for form, f, cont in pfx_forms:
            if not (circ and circ in cont) and not is_only:
                loc.add(form)
        # cross products: a prefix on a suffixed form (both rules marked Y)
        for form, f, cont in sfx_forms:
            if not sfx[f][0] or is_only:
                continue
            pf = [c for c in flags if c in pfx and pfx[c][0]] + [c for c in cont if c in pfx and pfx[c][0]]
            for pform, pf_flag, _ in apply_pfx(form, pf):
                loc.add(pform)
        # circumfix pairs need both halves; the prefix-only half above was skipped, so add the pair
        if circ and circ in flags:
            for form, f, cont in sfx_forms:
                if circ in cont or circ in flags:
                    for pform, pf_flag, pcont in apply_pfx(form, [c for c in flags if c in pfx]):
                        loc.add(pform)
        if caps and not keep_capitalised(stem, loc):
            continue
        out |= loc
    return {w for w in out if w and w.lower() not in banned}

if __name__ == "__main__":
    caps = "--caps" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    for w in sorted(expand(args[0], args[1], (lambda s, f: True) if caps else None)):
        print(w)
