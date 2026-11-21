# Cyrillic Nintendo DS engineering preview

This project now builds a **separate, experimental Cyrillic DS ROM** alongside
the ordinary 11-language wordPass.nds. This is **not** a complete Russian or
Ukrainian release: the draft lexicons contain about 170 words and all 40
theme headings, but only FOUR starter passwords per theme. These are not
full dictionaries or production-ready pools and must not appear in the
normal language selector.

## Real handheld controls

Download the wordPass-cyr-preview artifact from GitHub Actions and launch
wordPass-cyr-preview.nds. Choose English on the first-boot language chooser.
On the title screen hold **L+R** and press **Y**.

- Draw a word chain with the stylus; the first column is deliberately seeded
  with **ПРИВЕТ** in Russian or **ПРИВІТ** in Ukrainian to demonstrate scoring
  even with the small fixture dictionary
- Press A to submit, Y to clear the chain, X to switch the language
- Hold L and tap a letter to peel it: hidden pass letters become permanent
  diamonds; a miss leaves a permanent hole
- Press Select for another password, Start or B to leave the preview

The touch screen uses real 7×8 hex-grid coordinates and the same bitmap VRAM
banks as the production game. Cyrillic glyphs render directly on gems without
a new framebuffer, a full gem cache or per-frame allocation. The top screen
shows the masked password, theme name, current Cyrillic chain and preview score.

## Building

Standard: run "make". The ordinary ROM has no hidden preview code or sample
packs. Separate preview: run "make clean" followed by "make WP_CYR_PREVIEW=1".
Its test-only NitroFS assets are generated from the 40-theme draft catalogs
in data/cyrlex/ru_themes.json and data/cyrlex/uk_themes.json, plus the
original tiny word fixtures; every candidate pass is dictionary-validated.
The 40 themes retain the original English shelf order, not an alphabetical
sort order. Files are written at
nitrofiles/cyrpreview/{ru,uk}/{dict,tiles,pools}.bin.

CI publishes both ROMs in separate artifacts. The preview emulator job
tests the actual ARM9 title-screen entry, touch-input word submission,
peeling, language switching and returning to title. The existing 11-language
emulator suite continues to test the standard ROM independently.

## Remaining work before playable new locales

1. Obtain legally redistributable and native-speaker reviewed dictionaries
   for each language with broad vocabulary and inflections.
2. Expand the 40 **starter** themed pools in Russian and Ukrainian from
   four words each to complete reviewed word collections, balancing word
   length and frequency. Every candidate must remain valid in WPD4.
3. Localize and review all 385 UI messages, the guide, score/bonus text,
   examples and interface geometry, including long Cyrillic strings.
4. Integrate WPD4/WPL2/WPP2 into the **full** Classic and Daily game rules
   (fire, lockdown, hints, ledger, saves, progression and scoring).
   The current engineering preview is an interactive but simplified path.
5. Measure RAM, ROM, 60-FPS frame pacing, stylus responsiveness and
   native-resolution legibility on original DS/DSi hardware.
6. Only after these gates should Russian/Ukrainian appear in the language
   picker. Japanese, Korean and Chinese require separate script models.

This milestone is real DS technical integration, not linguistic completion.
