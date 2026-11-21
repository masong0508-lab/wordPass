#!/usr/bin/env python3
"""Check multilingual gameplay meaning and all 40 packed theme names.

gen_ui.py already checks every string's glyphs, format slots and pixel width.
This complementary check catches missing *gameplay concepts* in important
messages, and verifies every translated theme against the actual ROM data.
It is not a substitute for independent native-speaker linguistic review.

Run from any directory: python3 tools/lang/check_translation_quality.py
"""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lang"))
from themes import ORDER, THEME_NAMES  # noqa: E402

LANGS = "en es fr de it pt nl sv pl da fi".split()
IDS_PATH = ROOT / "tools" / "lang" / "ui_strings.tsv"

# High-impact UI meaning that cannot be checked by printf slots or text width.
# Every entry is deliberately tied to the relevant language, not just English.
# These are semantic *anchors*, not an assertion that all prose is perfect.
REQUIRED = {
    "OP_NOSAVE": "CARD TARJETA CARTE KARTE CARD CARTAO KAART KORT KARTY KORT KORT".split(),
    "CO_PASSLETTER": "PASS CLAVE CLE PASS CODICE SENHA CODE LOSEN HASLA KODE SALA".split(),
    "MS_PASSFOUND": "PASS CLAVE CLE PASS CODICE SENHA CODE LOSEN HASLA KODE SALA".split(),
    "F_WHY_SCORCH": "PASS CLAVE CLE PASS CODICE SENHA CODE LOSEN HASLA KODE SALA".split(),
    "G_ONEDAILY": "DAY DIA JOUR TAG GIORNO DIA DAG DAG DZIEN DAG PAIVA".split(),
    "S_WORDSPEEKS": "DAILY DIARIAS JOUR TAGES GIORNALIERE DIARIAS DAGELIJKSE DAG DZIENNE DAG PAIVA".split(),
    "O_BESTDAILY": "DAILY DIARIO JOUR TAGES GIORNO DIARIO DAG DAG DZIEN DAG PAIVA".split(),
    "R_PASSDONE_D": "DAILY DIARIO JOUR TAGES GIORNO DIARIO DAG DAG DZIEN DAG PAIVA".split(),
    "MS_DAILYONE": "TODAY HOY JOUR HEUTE OGGI HOJE VANDAAG IDAG DZIS DAG TANAAN".split(),
    "MS_HINTIDLE": "HINT PISTA INDICE TIPP SUGGERIMENTO DICA TIP TIPS PODPOWIEDZ TIP VIHJE".split(),
}
BUTTONS = {
    "G_C_START": "START",
    "MS_HINTIDLE": "SELECT",
    "TU_PEELHINT": "X",
}


def issue(problems, text):
    problems.append(text)


def read_ui(problems):
    rows = {}
    for line_no, line in enumerate(IDS_PATH.read_text(encoding="ascii").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if len(cells) != 4 + len(LANGS):
            issue(problems, f"UI row {line_no}: {len(cells)} fields, expected {4 + len(LANGS)}")
            continue
        key = cells[0]
        if key in rows:
            issue(problems, f"UI row {line_no}: duplicated key {key}")
        rows[key] = dict(zip(LANGS, cells[4:]))
    if len(rows) < 385:
        issue(problems, f"UI has only {len(rows)} entries, expected at least 385")
    for key, translations in rows.items():
        for lang in LANGS:
            if not translations[lang].strip():
                issue(problems, f"{key} {lang}: missing translation")
    return rows


def check_meaning(rows, problems):
    for key, tokens in REQUIRED.items():
        if key not in rows or len(tokens) != len(LANGS):
            issue(problems, f"{key}: missing row or wrong locale-anchor count")
            continue
        for lang, token in zip(LANGS, tokens):
            if token not in rows[key][lang].upper():
                issue(problems, f"{key} {lang}: missing vital concept {token!r}: {rows[key][lang]!r}")
    for key, button in BUTTONS.items():
        if key not in rows:
            issue(problems, f"{key}: missing button instruction")
            continue
        for lang in LANGS:
            if not re.search(r"(?<![A-Z])" + button + r"(?![A-Z])", rows[key][lang]):
                issue(problems, f"{key} {lang}: controller button {button} is missing")


def check_themes(problems):
    if len(ORDER) != 40:
        issue(problems, f"theme source contains {len(ORDER)} names, not 40")
    for lang in LANGS[1:]:
        expected = THEME_NAMES.get(lang)
        if not expected or len(expected) != 40:
            issue(problems, f"{lang}: expected 40 source theme names")
            continue
        path = ROOT / "data" / "lang" / lang / "pools.bin"
        try:
            raw = path.read_bytes()
        except OSError as exc:
            issue(problems, f"{lang}: cannot load packed pool: {exc}")
            continue
        if len(raw) < 8 + 40 * 20 or raw[:4] != b"WPP1" or struct.unpack_from("<I", raw, 4)[0] != 40:
            issue(problems, f"{lang}: invalid packed themes header")
            continue
        for idx, target in enumerate(expected):
            offset = 8 + idx * 20
            name = raw[offset:offset + 8].split(b"\0", 1)[0].decode("ascii")
            if name != target:
                issue(problems, f"{lang} theme {idx} ({ORDER[idx]}): ROM has {name!r}, translation source says {target!r}")
            if not any(struct.unpack_from("<6H", raw, offset + 8)):
                issue(problems, f"{lang} theme {idx}: no pass words")


def main():
    problems = []
    rows = read_ui(problems)
    check_meaning(rows, problems)
    check_themes(problems)
    for problem in problems:
        print("FAIL", problem)
    if problems:
        print(f"TRANSLATION QUALITY FAILED: {len(problems)} problem(s)")
        return 1
    print(f"TRANSLATION QUALITY OK: {len(rows)} UI entries x {len(LANGS)} languages,"
          f" {len(REQUIRED)} vital-message meanings x {len(LANGS)} languages,"
          f" 40 themes x 10 non-English languages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
