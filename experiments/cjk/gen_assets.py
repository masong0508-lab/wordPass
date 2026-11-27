#!/usr/bin/env python3
"""Build bounded CJK starter vocabulary and optional 16px raster glyphs."""
import argparse, json, unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LOCALES = ("zh-Hans", "zh-Hant", "ja")
LABELS = ("简体中文", "繁體中文", "日本語")
FACES = ("Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans CJK JP")

def read_words():
    sets = {x: [] for x in LOCALES}
    seen = {x: set() for x in LOCALES}
    for lineno, line in enumerate((ROOT / "words.tsv").read_text(encoding="utf-8").splitlines(), 1):
        lang, word, reading, gloss = line.split("\t")
        assert lang in sets and word == unicodedata.normalize("NFC", word), lineno
        assert 2 <= len(word) <= 6 and all(0x3000 <= ord(c) <= 0xffff for c in word), lineno
        assert word not in seen[lang] and reading.isascii() and gloss.isascii(), lineno
        assert 0 < len(reading) <= 30 and 0 < len(gloss) <= 30, lineno
        seen[lang].add(word)
        sets[lang].append((word, reading, gloss))
    assert all(len(sets[x]) >= 50 for x in LOCALES)
    return sets

def q(s):
    return json.dumps(s, ensure_ascii=True)

def words_header(sets):
    out = ['#ifndef CJK_WORDS_GENERATED_H', '#define CJK_WORDS_GENERATED_H', '#include "cjk_core.h"']
    for i, lang in enumerate(LOCALES):
        out.append(f'static const CjWord words{i}[] = {{')
        for word, reading, gloss in sets[lang]:
            ids = ','.join(hex(ord(c)) for c in word)
            out.append(f'    {{{{{ids}}},{len(word)},{q(reading)},{q(gloss)}}},')
        out.append('};')
    out.append('static const CjLexicon lexicons[3] = {')
    for i in range(3):
        out.append(f'    {{words{i},sizeof words{i}/sizeof words{i}[0]}},')
    out.extend(('};', '#endif'))
    return '\n'.join(out) + '\n'

def bitmap(font, ch, w, h):
    from PIL import Image, ImageDraw
    im = Image.new("L", (w, h))
    box = font.getbbox(ch)
    x = (w - box[2] + box[0]) // 2 - box[0]
    y = (h - box[3] + box[1]) // 2 - box[1]
    ImageDraw.Draw(im).text((x, y), ch, fill=255, font=font)
    return [sum((1 << (w-1-x)) for x in range(w) if im.getpixel((x,y)) >= 90) for y in range(h)]

def glyph_header(sets, path):
    from PIL import ImageFont
    fonts = {}
    for idx in range(5):
        font = ImageFont.truetype(str(path), 16, index=idx)
        fonts[font.getname()[0]] = font
    out = ['#ifndef CJK_GLYPHS_GENERATED_H','#define CJK_GLYPHS_GENERATED_H',
           '#include <stdint.h>',
           'typedef struct { uint16_t code, rows[16]; } CjGlyph;']
    for i, lang in enumerate(LOCALES):
        chars = sorted(set(''.join(w[0] for w in sets[lang]) + LABELS[i]))
        out.append(f'static const CjGlyph glyph{i}[] = {{')
        for ch in chars:
            rows = bitmap(fonts[FACES[i]], ch, 16, 16)
            assert any(rows), (lang, ch)
            out.append('    {' + hex(ord(ch)) + ',{' + ','.join(hex(x) for x in rows) + '}},')
        out.append('};')
    ascii_font = ImageFont.load_default()
    out.append('static const uint8_t ascii8[95][10] = {')
    for cp in range(32,127):
        rows = bitmap(ascii_font, chr(cp), 8, 10)
        out.append('    {' + ','.join(hex(x) for x in rows) + '},')
    out.extend(('};','#endif'))
    return '\n'.join(out) + '\n'

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--words-only', action='store_true')
    p.add_argument('--font', default='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
    args=p.parse_args()
    sets=read_words()
    out=ROOT/'generated'
    out.mkdir(exist_ok=True)
    (out/'words_generated.h').write_text(words_header(sets), encoding='utf-8')
    if not args.words_only:
        (out/'glyphs_generated.h').write_text(glyph_header(sets, Path(args.font)), encoding='utf-8')
    print('CJK starter records:', {k:len(v) for k,v in sets.items()})

if __name__ == '__main__':
    main()
