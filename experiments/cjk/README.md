# WordPass CJK practice preview (experimental)

This standalone DS development ROM begins the Chinese and Japanese expansion without consuming the tiny main-memory reserve of the integrated 13-language ROM.

Modes: Simplified Chinese (Mandarin Pinyin), Traditional Chinese (Mandarin Pinyin), and Japanese (native kana/kanji plus romanized readings). This starter vocabulary is deliberately hand-authored, not a comprehensive or native-speaker-reviewed dictionary. Readings use numeric Pinyin tones and romanized Japanese. English glosses are short aids, not a complete localization.

The portable engine stores actual 16-bit BMP codepoints, accepts two- through six-character words and rejects non-neighboring or repeated tiles. Each fresh board plants real vocabulary across its rows; submit to earn a nearby-password clue, use Open to reveal one hidden location, or use Hint to find a legal word. The practice mode deliberately omits fire, save records, full Classic/Daily and tutorial parity. The DS screen uses locale-specific rasterized 16x16 glyphs, not the existing 5x7 Latin/Cyrillic font. It is a separate build, never a replacement for the verified main ROM.

Build: install Python Pillow and Noto Sans CJK, run python3 gen_assets.py, then make with devkitARM/libnds. Host test: gcc -std=c11 -O2 -Wall -Wextra -Werror -Isource -Igenerated test_core.c source/cjk_core.c -o /tmp/cjk-test && /tmp/cjk-test. No raw fonts are packaged; the generated raster art is derived from Noto CJK with its accompanying copyright/license notice. Future milestone: expand the lexicons with audited source dictionaries, native UI, a shared glyph cache and an integrated language selector, while meeting the existing DS memory gate.

A/B/Y/R: submit/menu/clear/hint. L+touch or Open: reveal a gem on release; dragging away cancels. Menu: select the writing system using touch. The native suite exercises seeded generation, exact word lookup, hints, free clue reinspection and complete passwords. Hardware/emulator verification is required before treating the preview as playable on DS.
