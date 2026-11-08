#!/usr/bin/env python3
"""Generate source/ui_strings.h and source/ui_strings.c from tools/lang/ui_strings.tsv.

The TSV is the one place the interface text lives. Columns (tab separated, '#' lines are comments):
  id  scale  budget_px  sample  en es fr de it pt nl sv pl da fi
  id         C identifier suffix (UI_<id> in the code)
  scale      text scale the string is drawn at (1 or 2): width = chars * 6 * scale - scale
  budget_px  the widest the string may be in the slot it is drawn in (checked here and by make check)
  sample     what the slots are filled with when the width is measured: one value per slot, '|' separated,
             '-' for none (a %d defaults to 99, a %s to WWWW)
  languages  A-Z 0-9 space and . , : ! ? ' - + / = < > ( ) % only, upper case; printf style slots %d %s %02d,
             positional %2$d allowed; every language of a row has the same slots (as a set)

  python3 tools/lang/gen_ui.py            write the two files (fails on a bad row or an overflow)
  python3 tools/lang/gen_ui.py --check    only validate

The EX_* rows are the guide's example words. Each must be a word of that language's dictionary (data/lang/CODE/dict.bin,
data/dict.bin for English) spelled with letters that have a tile, of the length the page needs (EX_WORD 3 letters, EX_SCORE
3 gems: a Qu language writes QU for one gem, EX_F1..F4 3, 3, 4 and 3 letters); the numbers the pages print are computed in
the game from the language's letter table, so a word that fits here scores as the page says.
"""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import gen_dict

LANGS = "en es fr de it pt nl sv pl da fi".split()
OK = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,:!?'-+/=<>()%_@")
SLOT = re.compile(r"%(?:(\d)\$)?(0?\d?)([ds])")
CODE = re.compile(r"\{([AGFBDW])\}")        # guide colour codes: accent gold fire base dim white (game.h GM_*, bytes 1..6)
CODES = {"A": "\\001", "G": "\\002", "F": "\\003", "B": "\\004", "D": "\\005", "W": "\\006"}
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
TSV = os.path.join(ROOT, "tools", "lang", "ui_strings.tsv")


def slots(s):
    out, seq = {}, 1
    for m in SLOT.finditer(s):
        pos = int(m.group(1)) if m.group(1) else seq
        if not m.group(1):
            seq += 1
        out[pos] = m.group(3)
    return out


def expand(s, samp, lang=0, rows=None):
    s = CODE.sub('', s)
    toks = samp.split("|") if samp != "-" else []
    seq = [0]

    def rep(m):
        pos = int(m.group(1)) if m.group(1) else seq[0] + 1
        if not m.group(1):
            seq[0] += 1
        if pos - 1 < len(toks):
            t = toks[pos - 1]
            if t.startswith("@") and rows is not None:      # another id's text in the same language (a name that goes in this slot)
                return CODE.sub('', rows[t[1:]][lang])
            return t
        return "99" if m.group(3) == "d" else "WWWW"
    return SLOT.sub(rep, s)


def width(s, scale):
    return len(s) * 6 * scale - scale if s else 0


def load():
    rows = []
    for ln, line in enumerate(open(TSV, encoding="ascii"), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) != 4 + len(LANGS):
            sys.exit(f"{TSV}:{ln}: {len(f)} columns, want {4 + len(LANGS)}")
        rows.append((ln, f[0], int(f[1]), int(f[2]), f[3], f[4:]))
    return rows


EX_LEN = {"EX_WORD": 3, "EX_F1": 3, "EX_F2": 3, "EX_F3": 4, "EX_F4": 3}


def example_words(rows):
    """Check the EX_* rows against the dictionary and the letter table of each language; return the problems."""
    bad = []
    for lang_i, lang in enumerate(LANGS):
        d = os.path.join(ROOT, "data") if lang == "en" else os.path.join(ROOT, "data", "lang", lang)
        try:
            dic = open(os.path.join(d, "dict.bin"), "rb").read()
        except OSError:
            continue
        if lang == "en":
            weight, qu = [1] * 26, True
        else:
            let = open(os.path.join(d, "letters.bin"), "rb").read()
            weight, qu = list(let[16:42]), bool(let[4] & 1)
        for ln, id_, scale, budget, samp, txt in rows:
            if not id_.startswith("EX_"):
                continue
            w = txt[lang_i].lower()
            if gen_dict.lookup(dic, w) == 0:
                bad.append(f"{id_} {lang}: {txt[lang_i]} is not in the dictionary")
            letters = w.replace("qu", "q") if qu else w
            for ch in letters:
                if not ("a" <= ch <= "z") or not weight[ord(ch) - 97]:
                    bad.append(f"{id_} {lang}: {txt[lang_i]} uses {ch!r}, which has no tile")
            if id_ == "EX_SCORE":
                if len(letters) != 3 or (qu and "q" in letters[1:]):
                    bad.append(f"{id_} {lang}: {txt[lang_i]} must be three gems (QU is one)")
                if "q" in w and not qu:
                    bad.append(f"{id_} {lang}: {txt[lang_i]} has a Q and the language has no Qu tile")
            elif len(w) != EX_LEN[id_]:
                bad.append(f"{id_} {lang}: {txt[lang_i]} must have {EX_LEN[id_]} letters")
    return bad


def validate(rows):
    bad, seen = example_words(rows), set()
    byid = {r[1]: r[5] for r in rows}
    for ln, id_, scale, budget, samp, txt in rows:
        if id_ in seen:
            bad.append(f"line {ln}: duplicate id {id_}")
        seen.add(id_)
        base = slots(txt[0])
        for lang, s in zip(LANGS, txt):
            if not s:
                bad.append(f"{id_} {lang}: empty")
            for ch in SLOT.sub('', CODE.sub('', s)):
                if ch not in OK:
                    bad.append(f"{id_} {lang}: character {ch!r} in {s!r}")
                    break
            if slots(s) != base:
                bad.append(f"{id_} {lang}: slots differ from English in {s!r}")
            w = width(expand(s, samp, LANGS.index(lang), byid), scale)
            if w > budget:
                bad.append(f"{id_} {lang}: {w}px > {budget}px: {s!r}")
    return bad


def cstr(s):
    return '"' + CODE.sub(lambda m: CODES[m.group(1)], s.replace("\\", "\\\\").replace('"', '\\"')) + '"'


def main():
    rows = load()
    bad = validate(rows)
    if bad:
        print("\n".join(bad))
        print(f"{len(bad)} problem(s)")
        sys.exit(1)
    if "--check" in sys.argv:
        print(f"{len(rows)} ids OK")
        return
    h = ["// Generated by tools/lang/gen_ui.py from tools/lang/ui_strings.tsv. Do not edit.",
         "#ifndef UI_STRINGS_H", "#define UI_STRINGS_H", "enum {"]
    h += [f"    UI_{r[1]}," for r in rows]
    h += ["    UI_COUNT", "};", "#endif", ""]
    c = ["// Generated by tools/lang/gen_ui.py from tools/lang/ui_strings.tsv. Do not edit.",
         '#include "game.h"', "#include \"ui_strings.h\"", "",
         f"const char *const UI_TEXT[UI_COUNT][{len(LANGS)}] = {{"]
    for r in rows:
        c.append("    { " + ", ".join(cstr(t) for t in r[5]) + " },   // " + r[1])
    c += ["};", "", "const u8 UI_SCALE[UI_COUNT] = { " + ", ".join(str(r[2]) for r in rows) + " };",
          "const u16 UI_BUDGET[UI_COUNT] = { " + ", ".join(str(r[3]) for r in rows) + " };",
          "const char *const UI_SAMPLE[UI_COUNT] = { " + ", ".join(cstr(r[4]) for r in rows) + " };",
          "const char *const UI_IDNAME[UI_COUNT] = { " + ", ".join(cstr(r[1]) for r in rows) + " };", ""]
    open(os.path.join(ROOT, "source", "ui_strings.h"), "w").write("\n".join(h))
    open(os.path.join(ROOT, "source", "ui_strings.c"), "w").write("\n".join(c))
    print(f"wrote {len(rows)} ids")


main()
