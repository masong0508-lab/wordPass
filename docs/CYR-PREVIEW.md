# Russian and Ukrainian Nintendo DS engineering previews

**Russian (Русский) and Ukrainian (Українська) are two separate languages.**
Cyrillic is the writing system they share, not a third game language.
This project builds a **separate, experimental DS ROM** alongside the
ordinary 11-language wordPass.nds. The draft lexicons contain about 170
words and all 40 theme headings, but only FOUR starter passwords per theme.
These are NOT complete dictionaries, reviewed translations or finished
Classic/Daily modes, and are not enabled in the production ROM.

## Choose a language on the handheld

Download the wordPass-cyr-preview artifact from GitHub Actions and launch
wordPass-cyr-preview.nds. On the title screen, tap the **LANGUAGE** badge
(or open **OPTIONS > LANGUAGE**) and select one of two explicit entries:

- **РУССКИЙ — DEMO**: Russian word boards and 40 draft themed shelves
- **УКРАЇНСЬКА — DEMO**: Ukrainian word boards and 40 draft themed shelves

The other 11 existing language choices remain unchanged. The two DEMO rows
only appear in the separate experimental ROM. They open the independent
script-ID game mode instead of pretending the normal Classic/Daily engine
has already been translated. They are never stored as the retail language
setting; after leaving a demo, the selected retail locale is unchanged.

The old **hold L+R and press Y** developer shortcut from the title still
opens Russian for debugging, but is no longer needed to find these modes.

- Draw a word chain with the stylus; every generated shelf now plants
  **one real, dictionary-validated 4–6-letter word** from that shelf's theme
  (or a nearby theme for a sparse draft pool). The word and its column
  change with the seed instead of always showing ПРИВЕТ / ПРИВІТ.
  Press **R** for a hint to discover a playable chain.
- Press A to submit, Y to clear the chain, **R for one free word hint per shelf**,
  X to switch the language
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
emulator suite continues to test the standard ROM independently. The
experimental ROM's emulator suite also taps the actual Russian and
Ukrainian DEMO rows, tests both modes and verifies returning to the title.

## Real scoring ledger and combo milestone

The Russian and Ukrainian demo now use the same **eight-word recent history**
principle as the retail Classic game: repeating an accepted word while it
remains in the last eight scored words halves its base score. A streak of
successful words adds 10% per preceding word, capped at +50%. An accepted peel
breaks the streak but does not erase the recent-word history. The current
word's HUD preview includes both modifiers so the score shown is the score
awarded. The preview's tiny WPD4 symbol IDs are stored in a fixed 8×20 buffer
(under 256 bytes total); there are no new images, heap allocations, runtime
dictionary conversions or save-card writes.

A new shelf, a language switch and a new demo clear the history; repeated
Russian words cannot affect Ukrainian scoring or vice versa. Tests cover
repeat penalties, combo cap, ring rollover, invalid words, peel reset and
language isolation in the actual emulator. This is one step toward retail
scoring parity, **not** a claim that the remaining Classic/Daily economy,
password balancing and full 385-message localization are finished.

## Varied, genuinely playable themed shelves

The earlier emulator prototype stamped the same Russian/Ukrainian greeting
onto every board. That made the tiny proof-of-concept playable but created
repetitive, artificial-looking puzzles. The new generator chooses a validated
word **from the current shelf's theme whenever possible**, varies the
4–6-symbol word and its starting column by seed, and plants it along an
adjacent top-row path. If a draft theme contains only longer words, it
falls back to an eligible word from another existing theme rather than
inventing a word or leaving the shelf without a hintable chain.

This runs only when starting a shelf, uses fixed-size buffers, and never
alters hidden pass cells, the tile bag, retail settings, or the date/language
Daily Practice seed. Native tests cover every draft theme in Russian and
Ukrainian across multiple seeds, scoreable hinted chains, deterministic
replays and protection of all hidden password locations. The DS emulator
uses the actual on-board hint path rather than assuming a six-tile word in
column zero. The limited dictionaries and professional translation gates
still apply.

## Recovery when a small draft dictionary leaves no useful chain

After making at least one turn in the Russian or Ukrainian engineering ROM,
**hold L and press R** to place another real, dictionary-validated word
along a legal 7×8 hex chain. This uses the current shelf's theme when
possible. It can be used **once per shelf** and costs one additional
turn and two heat, allowing normal flames and overtime to advance.
It never awards word points, and it breaks any active word combo.
It is a gameplay recovery option, not a free unlimited reshuffle.

Recovery searches other eligible columns and both chain directions if
peels, burns or diamonds obstruct the preferred path. It cannot overwrite
a hidden password tile or an existing fire/hole/diamond. If every path
is blocked, the action fails without modifying the board or spending
a turn. Attempting recovery too early or a second time gives an explicit
Russian/Ukrainian message on the top LCD. A new shelf or replay resets
its one-use allowance; the regular 11-language ROM has no such feature.

Native regression tests validate turn/heat accounting, safety around
obstacles and atomic failure. The DS emulator tests the actual L+R
input, score and combo safety, and rejects repeated rescue attempts.

## On-device language-specific controls help

While playing either demo, **hold L and press Y** to open a readable
Russian or Ukrainian control guide on the existing top LCD. It explains
how to chain neighboring tiles, submit, clear a chain, peel/reveal a
password letter, use the once-per-shelf hint, switch languages, start
unranked Daily Practice and understand repeated-word/combination scoring.

Press **B** or **A** to close the guide without exiting the demo or
accidentally submitting the current chain. Press **START** to leave the
game normally. The manual is stored as one small flag, does not allocate
a new framebuffer or texture, and appears only in the experimental ROM.
The emulator tests both native-language pages, redraw, preserved turn
count and safe dismissal.

## Daily Practice (unranked, experimental)

In the Russian or Ukrainian demo press **L+SELECT** to switch between
ordinary test shelves and **Daily Practice**. The practice puzzle uses the DS
calendar date and a different stable seed for each language. On the same
date and language, reloading the ROM, pressing SELECT to retry, or switching
to Ukrainian and back restores the same board and password. The replay score
is reset to zero, so retries cannot inflate it.

This is **NOT** the retail scored Daily. There are no streak records,
achievements, completed-day flags or save writes, and finishing the single
daily shelf does not advance to the next one. Press **SELECT** to retry or
**L+SELECT** to return to experimental Classic-style shelves. A missing/invalid
DS clock refuses entry and displays a clock warning rather than making a
fake date. Russian and Ukrainian do not share the date's board or word ledger.

The small, allocation-free `scriptDailySeed` helper is also exercised
natively for calendar validity (including leap years), deterministic replay,
and per-language isolation. The DeSmuME scenario checks live menu controls,
same-date retry and safe exit; if the emulator's RTC is unset, it instead
checks that the mode is safely refused.

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
6. Only after these gates should Russian/Ukrainian become **production
   language choices** for full Classic/Daily. Their separately marked DEMO
   entries in the engineering ROM do not satisfy this release gate.
   Japanese, Korean and Chinese require separate script models.

This milestone is real DS technical integration, not linguistic completion.

## Bring your own licensed Cyrillic dictionary packs

The built-in preview remains tiny and deterministic for emulator regression.
For a more substantial **experimental DS preview**, run the explicit
`tools/lang/import_cyr_hunspell.py` intake for **both** languages with your
properly licensed source files. Place its output under a single directory,
for example `../wordpass-cyr-sourced/ru/` and `../wordpass-cyr-sourced/uk/`. Both
must include `dict.bin`, `tiles.bin`, `pools.bin`,
`SOURCE-MANIFEST.json` and `LICENSE-SOURCE.txt`.

Place the external packs **outside the build/ directory**, since `make clean` deletes build/.

Then, with devkitARM installed:

```sh
make clean
make WP_CYR_PREVIEW=1 CYR_EXTERNAL_DIR=../wordpass-cyr-sourced
```

This opts into **external** resources only for the preview ROM. It does not
add them to the normal language selector, and the normal `make` still
compiles the original 11-language ROM with no Cyrillic preview assets.

Before installing, the script verifies both packs together: WPD4 dictionary
format and exactly matching language alphabets, WPL2 distribution and
dictionary parity, 40 WPP2 themes and their valid words, the source manifest
and copied license notice, and a 1.5 MiB per-language dictionary RAM cap.
It rejects missing, inconsistent, synthetic-only and altered-attribution
inputs rather than packaging an accidentally broken or unlicensed pack.

**Import does not mean release certification.** Full word quality, broad
word pools, native-speaker review, rights checks, translated UI and complete
Classic/Daily game rules are still required before enabling a real locale.

## Shelf heat, fire and pass bonuses (development milestone)

The Cyrillic preview now includes its **own Classic-like rules loop**:
spelling a valid word and peeling a tile each take a turn; short words add
more heat, while long words produce less. Heat ignites an ordinary gem.
Existing flames descend every few turns, fall through holes, and stop at
revealed password diamonds. A flame reaching the bottom loses the shelf.
Peeling a burning secret (or one directly beneath fire) can scorch it and
also cause a loss. Overtime adds heat when a puzzle takes too many turns.

Revealing every secret letter awards a shelf bonus. Press **A** after winning
to advance to the next shelf, or after losing to retry. Select is still a
manual debug re-deal of the current shelf. The panel shows the shelf number,
turn count, score, and heat meter, and the top screen reports the outcome.

These rules are in `source/script_rules.c` and are **not identical** to the
complete retail Classic/Daily rules (which include lockdown, twists,
saved progress, a word ledger, hint fairness, full difficulty and balancing).
The separate preview is still an engineering build with starter theme
content and should not be presented as a full translated release.
