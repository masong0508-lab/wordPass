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
storing pointers into the file. The runtime reader **also scans all records
once at load time**, rejecting out-of-range tile indexes, nonzero packed
padding, duplicate words, and unsorted groups even if an invalid file has
a matching/recomputed checksum. This keeps binary search safe without adding
extra work to each word lookup or any new framebuffer allocations. The
Python builder verifies the same invariants.

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

## WPP2: Unicode-themed hidden password pools

The prototype now has a third versioned resource, **WPP2**, linking password
themes to the exact same Unicode alphabet as WPD4 and WPL2.

- Header (20 bytes): `WPP2`, version 1, flags 0, 1–48 themes, reserved 0,
  2–64 alphabet symbols, pass lengths 4–9, reserved 0, exact file length
  (LE32) and FNV-1a32 of the remaining bytes.
- Unicode alphabet: `symbol_count × u32 LE`; every codepoint must match
  the active WPD4 dictionary exactly.
- Theme headers: each has a 24-byte NUL-padded UTF-8 name and six LE16
  counts for pass lengths 4 through 9 (36 bytes per theme).
- Word payload: sorted unique password words as raw alphabet symbol IDs.
  Every word must be spelled entirely with that alphabet and accepted by
  WPD4; theme names are checked for correct UTF-8 and nonempty content.
- Maximum input file 2 MiB. No runtime allocations: themes and word indexes
  are checked before any pointers into the caller-owned file are published.

`tools/lang/pools_v2.py --selftest build/` builds **three tiny sample themes**
and eight words per language to prove the format and the hidden-password
mechanics. These are **not** complete 40-theme language packs.

Production build syntax after obtaining licensed word sources:

```sh
python3 tools/lang/pools_v2.py --build ru ru.wpd4 themes_ru.json ru.wpp2
python3 tools/lang/pools_v2.py --verify ru.wpd4 ru.wpp2
```

The theme JSON maps localized Cyrillic names to arrays of accepted words.

The independent hex-board model now picks a WPP2 theme and a 4–9-symbol
password deterministically, hides its letters in distinct spots in the
bottom three rows, supports peeling to reveal persistent diamond letters,
and keeps holes/diamonds/firebreaks as gravity barriers when other words
are submitted. `tools/check/script_pass_test.c` checks both Russian and
Ukrainian with the actual WPD4 and WPL2 mock resources. **The DS's regular
live `Game` state, menu and main progression do not yet use this model.**

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
3. **WPP2 pass pools are implemented** in a parallel format, with
   exact-alphabet, UTF-8-name, word-in-dictionary and malformed-file tests.
   The script-board prototype now supports secret placement, peeling,
   permanent revealed letters and segmented gravity.
4. Complete source-licensed Russian and Ukrainian dictionary sets, all 40
   themed passes, 385 localized strings, the guide and native-speaker review.
5. Run full DeSmuME/DS playthroughs and memory/performance measurements;
   only then add a Cyrillic language to the chooser.

Japanese and Chinese need larger alphabets / multigraphs than WPD4's
64-symbol first iteration. Hangul also requires an explicit decision about
syllable blocks versus jamo. None is automatically supported by this format.
