# 20-language candidate

Base: verified 16-language PR #93 head 716ce4791fab563e42afa710b6317582215e66e0. Stable language IDs 0–15, dictionary assets, original CJK word ordering and historical Daily rules are retained.

| ID | Language | Starter records | Tile system |
|---|---|---:|---|
| 16 | Korean | 90 | NFC Hangul syllable blocks |
| 17 | Hindi | 90 | Hand-segmented Devanagari aksharas, shaped as a unit with Pillow RAQM |
| 18 | Greek | 93 | NFC Greek letters, including accents and final sigma |
| 19 | Turkish | 107 | NFC Turkish letters, including dotted/dotless I and diacritics |

The four additions are curated, limited 2–6-tile puzzle vocabularies with ASCII reading aids and English glosses. Not full dictionaries or fully translated main-menu shells. Main gameplay uses localized controls, tutorial prompts, classic fire and recovery, Daily scoring and independent career files. Hindi tiles are not individual Unicode scalars; locale-local PUA IDs index 16-pixel raster glyphs. Do not normalize away accents or strip vowel signs. Preserve old word order and pin new historical Daily prefixes if expanding later.

Generate original packs with experiments/cjk/gen_assets.py and extras with experiments/cjk/gen_extra_assets.py. Noto CJK KR, Noto Sans Devanagari and Noto Sans fonts require licenses shipped in the CI artifact. The existing CJK1 ABI and 64 KiB free-memory gate are unchanged. Only one pack is loaded at a time; Korean/Hindi/Greek/Turkish use new record files WPKO, WPHI, WPEL and WPTR.

Release gate: original native tests and seven-locale emulator lifecycle, existing 13-language emulator regression, 7 pack files, original heap reserve, verified SHA-256 and exact commit marker. Review screenshots on actual hardware if possible. No claim of full dictionary coverage, physical hardware FPS or release readiness until all required checks pass.
