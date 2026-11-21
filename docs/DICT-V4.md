# WPD4: versioned Unicode-alphabet dictionary

**Status: prototyped, not wired into playable game modes.** The 11 existing
languages continue using their original WPD3 / WPP1 / WPL1 formats. Russian
and Ukrainian are **not** available in the language selector.

WPD3 stores 26 Latin symbols in 5-bit records. A Cyrillic alphabet with 33
letters cannot be encoded by it. WPD4 stores up to 64 symbols as explicitly
listed Unicode scalar values, with 1–6 bits per **symbol ID**. Lookup takes a
UTF-8 word or an array of tile IDs; the latter is the proposed interface for
the eventual script-aware board.

## File layout, version 1

| Offset | Type | Description |
|---|---|---|
| 0 | 4 bytes | ASCII magic \`WPD4\` |
| 4 | u8 | version = 1 |
| 5 | u8 | flags = 0 |
| 6 | u8 | symbol count 2–64 |
| 7 | u8 | minimal bits required for count, 1–6 |
| 8 | u8 | minimum playable word length 3 symbols |
| 9 | u8 | maximum length 20 symbols |
| 10 | u16 | reserved zero |
| 12 | u32 LE | exact file byte count |
| 16 | u32 LE | FNV-1a32 checksum of bytes 20..end |
| 20 | n×u32 LE | Unicode scalars, strictly ascending |
| 20+4n | 18×u32 LE | word counts for lengths 3..20 |
| after counts | variable | records by length, sorted by symbol-ID order |

Each record occupies \`ceil((symbol_count_in_word * bits_per_symbol + 1)/8)\`
bytes. Symbol indexes are stored high-bit-first. The final byte's **bit 0**
is the tier (0 common / 1 rare); unused bits are zero. This preserves
the lexicographical binary-search property while allowing 33 Cyrillic
symbols in six bits each, rather than depending on UTF-8 byte counts.
Maximum record width is 16 bytes (20 symbols × 6 bits plus tier).

The file checksum detects accidental damage, not intentional tampering.
The DS loader must still enforce the existing file-size bounds, reject
unsupported versions, and avoid exposing any half-installed language pack.
Runtime validation checks sizes, Unicode scalar metadata and checksum before
storing pointers into the file. The builder also checks every record,
padding bit, symbol index, strict sorting and duplicate words.

## Current implementation

- \`source/dict_v4.c/.h\`: independent, heap-free, bounded WPD4 parser,
  UTF-8 Cyrillic lookup and direct tile-ID lookup. Unknown symbols or malformed
  UTF-8 reject the whole word; there is no silent Latin transliteration.
- \`tools/lang/dict_v4.py\`: deterministic Unicode dictionary packer,
  full verifier and explicit \`ru\` / \`uk\` alphabet tables.
- \`tools/check/dict_v4_test.c\`: C implementation tested against synthetic
  fixtures, both scripts' special letters, word tiers, corruption, truncated
  file lengths and direct symbol-ID chains.
- \`make check\` builds and validates both tiny fixtures before running the
  native regression, independently of external dictionary downloads.

To try the syntax and integrity checks:

\`\`\`sh
python3 tools/lang/dict_v4.py --selftest build/
python3 tools/lang/dict_v4.py --verify build/wpd4_ru.bin
python3 tools/lang/dict_v4.py --verify build/wpd4_uk.bin
\`\`\`

To build a real input after arranging licensing and native review:

\`\`\`sh
python3 tools/lang/dict_v4.py --build ru WORDS_UTF8.txt COMMON_UTF8.txt output_ru.wpd4
\`\`\`

The source files must contain uppercase or lowercase UTF-8 Russian/Ukrainian
words, one per line. The builder NFC-normalizes and uppercases them, rejects
unrepresentable symbols, duplicates after normalization and impossible
lengths, and requires common words to be a dictionary subset.
It does **not** download, license, or certify a dictionary.

## First script-aware board prototype

`source/script_board.c/.h` now connects WPD4 **tile-ID word lookups** and WPL2
**33-letter weighted tile deals** to a fixed 7×8 hex grid, with the same
even/odd column adjacency as the existing DS game. It supports drag-style
chains, backtracking, chain-to-UTF-8 conversion, common/rare word validation,
a simplified fire/diamond scoring preview, submit, gravity and deterministic
refill. A native test creates actual Russian and Ukrainian six-symbol words
on that grid and submits them using dictionary symbol IDs.

This is a **separate simulation**, not a playable DS screen or a replacement
for the live game's fire, secrets, pass pools, ledger, menus, animations or
save rules. The native test is a contract for integrating those systems
safely. The original game continues to call the WPD3/26-tile path exclusively.
The scripted board is under 384 bytes and never allocates a framebuffer.

## Next integration gates

1. **WPL2 groundwork is implemented:** a parallel 33-symbol Unicode
   tile-table format, deterministic 100-tile shuffle, vowel mask, per-symbol
   weights/values, and bounded rarity, with native fixture checks.
   Use `python3 tools/lang/tiles_v2.py --selftest build/` to generate
   `build/wpl2_{ru,uk}.bin` and validate the synthetic fixtures. The
   production game still uses its existing fixed A–Z tile engine.
2. Replace hardwired \`'a' + tile_index\` and 26-glyph caches in the **board**
   only after a symbol-ID bridge and full native regression (fire, pass,
   chain, hints, sound/score and both left/right-hand layouts).
3. Version themed pass pools to store symbol IDs or UTF-8 plus canonical
   codepoint mapping and update the guide's example-word tests.
4. Complete source-licensed Russian and Ukrainian dictionary sets, all 40
   themed passes, 385 localized strings, the guide and native-speaker review.
5. Run full DeSmuME/DS playthroughs and memory/performance measurements;
   only then add a Cyrillic language to the chooser.

Japanese and Chinese need larger alphabets / multigraphs than WPD4's
64-symbol first iteration. Hangul also requires an explicit decision about
syllable blocks versus jamo. None is automatically supported by this format.
