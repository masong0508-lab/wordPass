#!/usr/bin/env python3
"""Generate on-demand Korean, Hindi, Greek and Turkish puzzle packs.
Tile IDs are locale-local PUA values, never Unicode scalar values: Hindi
aksharas are authored as indivisible units and rasterized with RAQM.
The CJK1 disk ABI, original three packs, and memory limits remain unchanged.
"""
import argparse
import json
import struct
import subprocess
import unicodedata as ud
from pathlib import Path
from PIL import ImageFont, features
from gen_assets import UI, bitmap

ROOT = Path(__file__).resolve().parent
LOCALES = ("ko", "hi", "el", "tr")
FAMILIES = ("Noto Sans CJK KR", "Noto Sans Devanagari", "Noto Sans", "Noto Sans")
BUTTONS = {"SCORE", "ACCEPT", "CLEAR", "HINT", "OPEN", "CANCEL", "NEXT", "MENU", "RECOVER", "RETRY"}
HUD = {"PASS", "MOVES", "SHELF", "HEAT"}

def normalized(s):
    return ud.normalize("NFC", s)

def read():
    result = {k: [] for k in LOCALES}
    seen = {k: set() for k in LOCALES}
    for line_no, line in enumerate((ROOT / "extra_words.tsv").read_text(encoding="utf-8").splitlines(), 1):
        lang, units, reading, gloss = line.split("\t")
        tiles = tuple(units.split("|"))
        assert lang in result and 2 <= len(tiles) <= 6, line_no
        assert all(t and t == normalized(t) and t[0].isalpha() and
                   not any(c.isspace() or ud.category(c).startswith("C") for c in t) for t in tiles), line_no
        if lang == "ko":
            assert all(len(t) == 1 and 0xac00 <= ord(t) <= 0xd7a3 for t in tiles), line_no
        elif lang == "hi":
            assert all(0x900 <= ord(c) <= 0x97f for t in tiles for c in t), line_no
            assert all(not t.endswith("्") for t in tiles), line_no
        else:
            assert all(len(t) == 1 for t in tiles), line_no
        word = "".join(tiles)
        assert word not in seen[lang] and 0 < len(reading) <= 30 and 0 < len(gloss) <= 30, line_no
        assert reading.isascii() and gloss.isascii(), line_no
        seen[lang].add(word)
        result[lang].append((tiles, reading, gloss))
    for lang, words in result.items():
        assert len(words) >= 64 and len(words[0][0]) == 2, lang
        assert sum(len(w[0]) == 2 for w in words) >= 3, lang
    return result

def font_for(family, size):
    match = subprocess.check_output(["fc-match", "-f", "%{file}\n%{index}\n%{family}", family], text=True).splitlines()
    assert family in match[2].split(","), (family, match)
    return ImageFont.truetype(match[0], size, index=int(match[1]), layout_engine=ImageFont.Layout.RAQM)

def raster(family, text, narrow, fonts):
    if text == " ":
        return [0] * 16
    for size in range(13 if narrow else 16, 9, -1):
        if (family, size) not in fonts:
            fonts[family, size] = font_for(family, size)
        font = fonts[family, size]
        box = font.getbbox(text)
        if box[2]-box[0] <= (12 if narrow else 16) and box[3]-box[1] <= 16:
            rows = bitmap(font, text, 16, 16)
            assert any(rows), (family, text)
            return rows
    raise ValueError("unreadable tile: " + repr((family, text)))

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--pack-dir", type=Path, required=True)
    args = p.parse_args()
    assert features.check("raqm"), "Hindi requires Pillow RAQM/HarfBuzz shaping"
    words = read()
    raw_ui = json.loads((ROOT / "extra_ui.json").read_text(encoding="utf-8"))
    dest = args.pack_dir
    dest.mkdir(exist_ok=True, parents=True)
    fonts = {}
    ascii_font = ImageFont.load_default()
    ascii_bytes = bytes(v for cp in range(32, 127) for v in bitmap(ascii_font, chr(cp), 8, 10))
    out = ['#ifndef EXTRA_WORDS_GENERATED_H', '#define EXTRA_WORDS_GENERATED_H', '#include "cjk_core.h"']
    for i, lang in enumerate(LOCALES):
        narrow = lang in ("el", "tr")
        family = FAMILIES[i]
        assert len(raw_ui[lang]) == len(UI)
        labels = [tuple(normalized(s).split("|")) if lang == "hi" else tuple(normalized(s)) for s in raw_ui[lang]]
        for key, label in zip(UI, labels):
            assert 1 <= len(label) <= 14 and all(label), (lang, key, label)
            if key in BUTTONS:
                assert len(label) <= (6 if narrow else 4), (lang, key, label)
            if key in HUD:
                assert len(label) <= (3 if narrow else 2), (lang, key, label)
            if key == "CLUE":
                assert len(label) <= (12 if narrow else 9), (lang, label)
        units = sorted({t for tiles, _, _ in words[lang] for t in tiles} | {t for label in labels for t in label})
        assert len(units) <= 1024
        ids = {t: 0xe000 + j for j, t in enumerate(units)}
        payload = bytearray()
        out.append(f"static const CjWord extra{i}[] = {{")
        for tiles, reading, gloss in words[lang]:
            codes = [ids[t] for t in tiles]
            payload.extend(struct.pack("<6HBx31s31s", *(codes + [0]*(6-len(codes))), len(codes), reading.encode("ascii"), gloss.encode("ascii")))
            out.append("    {{" + ",".join(hex(c) for c in codes) + "}," + str(len(codes)) + "," + json.dumps(reading) + "," + json.dumps(gloss) + "},")
        out.append("};")
        for unit in units:
            payload.extend(struct.pack("<17H", ids[unit], *raster(family, unit, narrow, fonts)))
        for label in labels:
            codes = [ids[t] for t in label]
            payload.extend(struct.pack("<15H", *(codes + [0]*(15-len(codes)))))
        payload.extend(ascii_bytes)
        checksum = 2166136261
        for b in payload:
            checksum = ((checksum ^ b) * 16777619) & 0xffffffff
        data = struct.pack("<4sHHHHI", b"CJK1", len(words[lang]), len(units), len(UI), 0, checksum) + payload
        assert len(data) <= 65536 and len(words[lang])*24 + len(data) <= 131072
        (dest / f"{i+3}.bin").write_bytes(data)
        print(f"{lang}: {len(words[lang])} words, {len(units)} shaped tiles, {len(data)} packed bytes")
    out.append("static const CjLexicon lexicons[4] = {")
    for i in range(4):
        out.append(f"    {{extra{i},sizeof extra{i}/sizeof extra{i}[0]}},")
    out.extend(("};", "#endif"))
    generated = ROOT / "generated"
    generated.mkdir(exist_ok=True)
    (generated / "extra_words_generated.h").write_text("\n".join(out) + "\n", encoding="ascii")

if __name__ == "__main__":
    main()
