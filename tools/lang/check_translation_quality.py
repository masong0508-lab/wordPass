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


# Distinct actions and statuses must never collapse to the same localized
# string. This is an objective check; it does not claim to judge prose quality.
# Use only pairs that are meaningful opposites in the actual DS interface.
CONTRASTS = (
    ("V_ON", "V_OFF"),
    ("V_LEFT", "V_RIGHT"),
    ("V_FULL", "V_COMMON"),
    ("V_AMERICAN", "V_BRITISH"),
    ("V_RELAXED", "V_FAST"),
    ("T_CLASSIC", "T_DAILY"),
    ("OP_SAVED", "OP_SAVEFAIL"),
    ("OP_NOSAVE", "OP_SAVED"),
    ("MS_HINT", "MS_NOHINT"),
    ("V_GOLD", "V_SILVER"),
    ("V_CANDY", "V_EMBER"),
    ("CO_GOOD", "CO_NOTWORD"),
    ("MS_BRIT", "MS_AMER"),
    ("R_TAPRETRY", "R_TAPTITLE"),
)

# Hardware button symbols must remain literal when they introduce a control
# instruction; the translated prose after ':' is free to vary.
CONTROL_LEGENDS = {
    "G_C_A": "A",
    "G_C_B": "B",
    "G_C_X": "X",
    "G_C_Y": "Y",
    "G_C_START": "START",
}
SHOULDER_LEGENDS = ("G_HOLDLR", "G_C_LR")


def check_contrasts(rows, problems):
    for left, right in CONTRASTS:
        if left not in rows or right not in rows:
            issue(problems, f"missing contrast pair {left}/{right}")
            continue
        for lang in LANGS:
            a = " ".join(rows[left][lang].split()).casefold()
            b = " ".join(rows[right][lang].split()).casefold()
            if a == b:
                issue(problems, f"{lang}: {left} and {right} share the same translation")


def check_controls(rows, problems):
    for key, button in CONTROL_LEGENDS.items():
        if key not in rows:
            issue(problems, f"{key}: missing control legend")
            continue
        for lang in LANGS:
            if not rows[key][lang].startswith(button + ":"):
                issue(problems, f"{key} {lang}: controller label must begin {button}:")
    for key in SHOULDER_LEGENDS:
        if key not in rows:
            issue(problems, f"{key}: missing shoulder instruction")
            continue
        for lang in LANGS:
            for button in ("L", "R"):
                if not re.search(r"(?<![A-Z])" + button + r"(?![A-Z])", rows[key][lang]):
                    issue(problems, f"{key} {lang}: missing literal {button} shoulder button")


def selftest():
    # Assert that each negative case is *detected*, not just that existing
    # translation rows happen to pass. Never modify the committed TSV.
    problems = []
    baseline = read_ui(problems)
    check_meaning(baseline, problems)
    check_contrasts(baseline, problems)
    check_controls(baseline, problems)
    if problems:
        print("\\n".join("FAIL " + p for p in problems))
        return 1

    def altered(key, lang, value, check, marker):
        rows = {name: translations.copy() for name, translations in baseline.items()}
        rows[key][lang] = value
        failures = []
        check(rows, failures)
        if not any(marker in item for item in failures):
            raise AssertionError(f"regression missed for {key} {lang}: {marker}")

    altered("V_ON", "fi", baseline["V_OFF"]["fi"], check_contrasts, "V_ON and V_OFF")
    altered("OP_SAVED", "fr", baseline["OP_SAVEFAIL"]["fr"], check_contrasts, "OP_SAVED and OP_SAVEFAIL")
    altered("G_C_A", "fr", "B: WRONG BUTTON", check_controls, "G_C_A fr")
    altered("G_C_LR", "en", "HOLD L: PEEK", check_controls, "missing literal R")
    altered("CO_PASSLETTER", "fr", "SCORE!", check_meaning, "CO_PASSLETTER fr")
    print("TRANSLATION REGRESSION SELFTEST OK: opposite actions, controls and critical meanings")
    return 0


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
    check_contrasts(rows, problems)
    check_controls(rows, problems)
    check_themes(problems)
    for problem in problems:
        print("FAIL", problem)
    if problems:
        print(f"TRANSLATION QUALITY FAILED: {len(problems)} problem(s)")
        return 1
    print(f"TRANSLATION QUALITY OK: {len(rows)} UI entries x {len(LANGS)} languages,"
          f" {len(REQUIRED)} vital-message meanings x {len(LANGS)} languages,"
          f" {len(CONTRASTS)} contrast pairs and {len(CONTROL_LEGENDS) + len(SHOULDER_LEGENDS)} control legends,"
          f" 40 themes x 10 non-English languages")
    return 0


if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else main())
