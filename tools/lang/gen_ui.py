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


# Keep positional argument numbers within source/game.h's UI_MAX_SLOTS.
# The generated text is data, but uiFmtV reads varargs based on these slots.
MAX_SLOTS = 8


def slot_errors(s):
    problems, positions = [], set()
    seq = 1
    positional = sequential = False
    for m in SLOT.finditer(s):
        explicit = m.group(1) is not None
        positional |= explicit
        sequential |= not explicit
        pos = int(m.group(1)) if explicit else seq
        if not explicit:
            seq += 1
        if pos > MAX_SLOTS or pos < 1:
            problems.append(f"argument position {pos} outside 1..{MAX_SLOTS}")
        if pos in positions:
            problems.append(f"argument position {pos} used twice")
        positions.add(pos)
    if positions and positions != set(range(1, len(positions) + 1)):
        problems.append(f"argument positions must be contiguous from 1: {sorted(positions)}")
    if positional and sequential:
        problems.append("cannot mix positional and sequential format slots")
    return problems


def sample_errors(sample, known_ids, fmt=None):
    """Samples must match the eight-slot native test and resolve their references."""
    if sample == "-":
        return []
    problems = []
    tokens = sample.split("|")
    if len(tokens) > MAX_SLOTS:
        problems.append(f"{len(tokens)} samples exceeds {MAX_SLOTS}")
    for token in tokens:
        if not token:
            problems.append("empty sample item")
        if len(token) > 15:
            problems.append(f"sample {token!r} exceeds the native test's 15-character slot")
        if token.startswith("@") and token[1:] not in known_ids:
            problems.append(f"unknown referenced UI text {token!r}")
    if fmt is not None:
        kinds = slots(fmt)
        if len(tokens) > len(kinds):
            problems.append(f"{len(tokens)} samples for {len(kinds)} format arguments")
        for pos, kind in kinds.items():
            if kind != "d" or pos > len(tokens):
                continue
            token = tokens[pos - 1]
            if not re.fullmatch(r"[+-]?[0-9]+", token):
                problems.append(f"argument {pos} needs an integer sample, got {token!r}")
            elif not -(1 << 31) <= int(token) < (1 << 31):
                problems.append(f"argument {pos} sample {token!r} exceeds signed 32-bit range")
    return problems


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
                t = CODE.sub('', rows[t[1:]][lang])
        else:
            t = "99" if m.group(3) == "d" else "WWWW"
        # Match uiFmtV: only integers honor field width; sign precedes zero
        # padding. Normalize the sample as an int so +01 is drawn as 1.
        if m.group(3) == "d":
            t = str(int(t))
            count = int(m.group(2)) if m.group(2) else 0
            if count > len(t):
                if m.group(2).startswith("0"):
                    t = t.zfill(count)
                else:
                    t = t.rjust(count)
        return t
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
        except OSError as exc:
            bad.append(f"{lang}: guide example dictionary unavailable: {exc}")
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
        sample_bad = sample_errors(samp, byid, txt[0])
        for problem in sample_bad:
            bad.append(f"{id_} sample: {problem}")
        # Unsupported printf-like syntax may otherwise be mistaken for text.
        # Literal percentages (100%, % PER) stay allowed.
        malformed = re.compile(r"%(?:[0-9$.]+)?[ds]")
        for lang, s in zip(LANGS, txt):
            for m in malformed.finditer(s):
                if not SLOT.fullmatch(m.group()):
                    bad.append(f"{id_} {lang}: malformed format slot {m.group()!r}")
        if id_ in seen:
            bad.append(f"line {ln}: duplicate id {id_}")
        seen.add(id_)
        base = slots(txt[0])
        for lang, s in zip(LANGS, txt):
            for problem in slot_errors(s):
                bad.append(f"{id_} {lang}: {problem}")
            if not s:
                bad.append(f"{id_} {lang}: empty")
            for ch in SLOT.sub('', CODE.sub('', s)):
                if ch not in OK:
                    bad.append(f"{id_} {lang}: character {ch!r} in {s!r}")
                    break
            if slots(s) != base:
                bad.append(f"{id_} {lang}: slots differ from English in {s!r}")
                continue
            # Bad or unresolved samples are already reported; do not crash
            # with ValueError/KeyError while trying to measure them.
            if sample_bad:
                continue
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
    check = "--check" in sys.argv
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
    outs = {"ui_strings.h": "\n".join(h), "ui_strings.c": "\n".join(c)}
    if check:
        # the tsv is valid (above); the committed generated files must also be exactly what it generates
        stale = [n for n, text in outs.items()
                 if not os.path.exists(os.path.join(ROOT, "source", n))
                 or open(os.path.join(ROOT, "source", n)).read() != text]
        if stale:
            print("source/" + ", source/".join(stale) + " differ from ui_strings.tsv: run python3 tools/lang/gen_ui.py")
            sys.exit(1)
        print(f"{len(rows)} ids OK, generated files in sync")
        return
    for n, text in outs.items():
        open(os.path.join(ROOT, "source", n), "w").write(text)
    print(f"wrote {len(rows)} ids")


if __name__ == "__main__":
    main()
