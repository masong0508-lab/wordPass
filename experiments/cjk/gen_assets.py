#!/usr/bin/env python3
"""Build bounded CJK starter vocabulary and optional 16px raster glyphs."""
import argparse, json, unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent
LOCALES = ("zh-Hans", "zh-Hant", "ja")
LABELS = ("简体中文", "繁體中文", "日本語")
FACES = ("Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans CJK JP")

UI = {
    "SCORE": ("分数", "分數", "得点"),
    "ACCEPT": ("确认", "確認", "決定"),
    "CLEAR": ("清除", "清除", "消去"),
    "HINT": ("提示", "提示", "ヒント"),
    "OPEN": ("揭开", "揭開", "開く"),
    "CANCEL": ("取消", "取消", "取消"),
    "NEXT": ("继续", "繼續", "次へ"),
    "MENU": ("菜单", "選單", "メニュー"),
    "PASS": ("密码", "密碼", "暗号"),
    "MOVES": ("步数", "步數", "手数"),
    "DONE": ("找到密码", "找到密碼", "暗号発見"),
    "NEXT_BOARD": ("继续下一局", "繼續下一局", "次の盤面へ"),
    "LINK_TWO": ("至少连接两个字", "至少連接兩個字", "二文字以上つなぐ"),
    "UNKNOWN": ("词库中没有这个词", "詞庫中沒有這個詞", "語彙集にない単語"),
    "MAKE_WORD": ("连接文字组成词语", "連接文字組成詞語", "文字をつないで単語に"),
    "CLUE": ("待找字/未开格", "待找字/未開格", "残り字/未開封"),
    "OPEN_PROMPT": ("点按并松开以揭开", "點按並放開以揭開", "マスに触れて離す"),
    "WORD_PROMPT": ("连词取线索，揭格找密码", "連詞取線索，揭格找密碼", "単語で手掛かりを得る"),
    "NO_HINT": ("没有找到词", "沒有找到詞", "単語が見つからない"),
}

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
    assert all(len(sets[x]) >= 130 for x in LOCALES)
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
    out.append('enum {' + ','.join('UI_' + key for key in UI) + ',UI_COUNT};')
    out.append('static const uint16_t cjk_ui[3][UI_COUNT][15] = {')
    for i in range(3):
        out.append('    {')
        for key, texts in UI.items():
            label = texts[i]
            assert 1 <= len(label) <= 14, (key, label)
            if key in ('ACCEPT','CLEAR','HINT','OPEN','CANCEL','NEXT','MENU','SCORE'):
                assert len(label) <= 4, (key, label)
            out.append('        {' + ','.join(hex(ord(ch)) for ch in label) + ',0},')
        out.append('    },')
    out.append('};')
    for i, lang in enumerate(LOCALES):
        chars = sorted(set(''.join(w[0] for w in sets[lang]) + LABELS[i] +
                           ''.join(texts[i] for texts in UI.values())))
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
