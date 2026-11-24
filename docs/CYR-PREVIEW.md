# Russian and Ukrainian Nintendo DS engineering previews

**Russian (Русский) and Ukrainian (Українська) are two separate languages.**
Cyrillic is the writing system they share, not a third game language.
This project builds a **separate, experimental DS ROM** alongside the
ordinary 11-language wordPass.nds. The **compact demo** has small draft
lexicons (about 170 words per language) and four starter passwords in each
of 40 themes. A **different, source-backed optional demo** tests 100,000
words per language and 347 Russian / 333 Ukrainian unreviewed themed
password candidates. Both remain engineering experiments: neither provides
complete reviewed translations, full Classic/Daily parity or languages
selectable in the regular 11-language production ROM.

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

### Controller-only play (no stylus required)

In the **separate Russian/Ukrainian demo ROM**, the D-pad moves a visible,
bounded 7×8 cursor over the actual gems and numbered holes. Press **A** to
select a front letter at that cursor. Continue moving to adjacent letters;
press **A again on the last selected letter** once the chain has at least
three symbols to submit the accepted dictionary word. **B** removes the
last selected letter without spending a turn, whether it came from the
D-pad or stylus. With no active chain to undo, **B** returns to the title.
**Y** clears the whole chain; **R** selects one free word hint and allows a plain **A**
to submit it immediately.

Hold **L+A** to peel the selected gem at the cursor; the same fire, secret,
score, clue and turn rules apply as with the stylus. Invalid positions,
unearned clues and holes are never converted into free hints. Pressing a
stylus tile hides the controller focus and restores normal drag or
hold-L-to-peel operation, including its slide-away-to-cancel safeguard.
The cursor exposes only its public coordinate and never the hidden pass
locations. It consumes no turn/heat when moved and adds just two outlines
to the active cursor frame; normal game saves, 11 retail languages and
Daily scores are unchanged.

- Draw a word chain with the stylus. Quick swipes interpolate across
  neighboring hexes rather than silently skipping letters between touch
  samples. **After lifting the stylus, tap the final selected gem again**
  to submit a valid word without pressing A; tapping a short, invalid or
  different chain never scores. The A button still submits normally.
  Only strokes starting and ending on the board are bridged;
  switching to L mid-drag cancels that word gesture until a new touch.
  At most 32 touch samples are checked per moved frame, without new
  allocations or secret-location reads. Peels remain deliberate
  **L + touch, then lift on the same gem**.
  Each new shelf seeds **up to two
  different, dictionary-validated 4–6-letter words** on independent columns,
  from its theme whenever possible (or a nearby theme for a sparse pool).
  Both chains are genuine board words, not repeated fixed greetings. Clearing
  one chain cannot destroy the other through gravity. The second opening word
  is optional when no distinct eligible source word exists; there is still at
  least one playable chain. Press **R** for your one free path hint per shelf.
  The separate **L+R** recovery still plants exactly one word at its normal
  heat and turn cost, never two.
- Press A to submit, Y to clear the chain, **R for one free word hint per shelf**,
  X to switch the language
- Hold L and tap a letter to peel it: hidden pass letters become permanent
  diamonds; a miss leaves a permanent **numbered hole** (0–6) showing how many
  nearby hex cells hold pass letters, including already revealed diamonds
- Every accepted word also earns a **numbered clue on its final hex cell**.
  The number stays at that board position even after front gems fall or refill.
  Its six neighboring cells are outlined; when a clue logically proves all
  still-covered neighbors must hide letters they receive a gold +, or a teal
  - when all remaining neighbors are empty. Burning tiles stay red: a
  logically empty neighbor can still cost a dangerous peel under fire
- Press Select for a fresh attempt on the current shelf, Start or B to leave the preview

Password lengths now vary with shelf progression rather than requesting
five letters forever: shelves 1–3 use a four/five-letter target, shelves
4–9 five/six, shelves 10–19 five through seven, shelves 20–34 six through
eight, and later shelves six through nine. A theme without an exact length
uses the nearest actual sourced word; small draft packs never manufacture
missing passwords. These are preliminary difficulty windows, **not yet
native-player-balanced**, and Daily Practice remains deterministic per date
and language.

The top screen makes the goal explicit with **НАЙДЕНО x/y** (Russian) or
**ЗНАЙДЕНО x/y** (Ukrainian): the number of hidden password letters already
uncovered out of the total. These are not the ordinary front-letter gems used
to spell words. Press **L+Y** during the demo for a short localized rules card:
form words to earn numbered neighbor clues, then hold **L** and tap a gem to
peel its back and uncover password letters or a numbered hole. The card also
shows that **A** submits a word; **Y** cancels a chain on the board panel.
This is explanation-only UI, with no automatic reveals or change to scoring.

Replaying a free-play shelf discards the **current attempt's** word points
and completion bonus. Points from previously completed shelves stay banked.
Press **A after completing the password** to bank that shelf and advance;
pressing Select on the completed shelf instead replays it without banking
the same bonus repeatedly. Retrying after fire follows the same rule.
Daily Practice always retries from zero. If a replacement board cannot be
generated, the current board, selection, score and mode remain intact.

**Fair clue reading:** An earlier engineering renderer painted a tiny gold
underline on every still-hidden password tile, unintentionally revealing the
solution. That marker is removed. The preview renderer now reads the hidden
back layer **only** to count neighboring pass positions when a number is
publicly earned by a word or revealed by a peel. An unopened, unsurveyed cell
never gets its own secret-indicating glyph. The player must still choose and
perform peels; the new cues never grant a free turn, hint or automatic reveal.

The experimental Russian/Ukrainian board also now reorders its **existing**
weighted 100-letter bag at shelf creation to avoid consonant-only columns.
When the bag contains at least 14 vowels, each of the seven initial
columns starts with two or more; even a legal 10-vowel bag spreads at
least one vowel into every column. No tile values, symbol counts, hidden
password positions, or later refill inventory change. The same date +
language + seed always creates the same board. This occurs only on shelf
creation, not during rendering, word tracing or hints. The 11 retail
language engines and their saved puzzles are unaffected.

The touch screen uses real 7×8 hex-grid coordinates and the same bitmap VRAM
banks as the production game. Cyrillic glyphs render directly on gems without
a new framebuffer, a full gem cache or per-frame allocation. A small static
alphabet-to-bitmap pointer table is generated only once per successful
Russian/Ukrainian language load; normal frames reuse it rather than resolving
all 56 Unicode tile glyphs repeatedly. The cache does not pre-render images,
read unrevealed password locations or share the two languages' alphabets. The top screen
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
awarded. Its score and dictionary tier are computed once before vblank and
shared by the top-screen slices, avoiding repeated dictionary searches
during the LCD's refresh interval. The preview's tiny WPD4 symbol IDs are stored in a fixed 8×20 buffer
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

On a moving-fire turn, the game settles fire and gravity before placing the
new word, so they cannot immediately break the recovered chain. This uses
a fixed-size board/rules transaction: if this turn's new flame blocks the
last usable route, recovery leaves the original board, RNG, heat, turn and
allowance unchanged. An otherwise eligible recovery still loses normally
if a bottom flame escapes; it cannot undo that loss or revive a finished
shelf.

The numbered clue system adds seven row masks and two focus coordinates to
the small experimental board structure, with no separate 56-byte number map,
new framebuffer, image atlas or dynamic allocation. Its read-only inference
uses the displayed clue count and revealed diamonds/holes, not the identity of
unopened password cells. Native tests and the real DS emulator verify the
hidden-position anti-spoiler invariant in Russian and Ukrainian.

The preview and a translated retail pack never occupy the heap together.
Entering either demo temporarily releases the active retail dictionary,
pool and tile-table buffers, retaining the saved language choice. Leaving
or a failed preview load closes all preview images before restoring that
retail language and its original title/options screen. If the retail pack
has become unavailable, the game returns to a playable English board with
the existing load-failure message and preserves the saved preference.

Native regression tests validate turn/heat accounting, safety around
obstacles and atomic failure. A separate native test calls the actual
preview action with both complete 40-theme draft packs, checking recovery
after fire/gravity, blocked-new-flame refusal, fatal fire, finished shelves,
preservation of score, word history and hint allowance, exact read-only
score/tier quotes, replay score banking and failed board generation.
The DS emulator tests the actual L+R
input, score and combo safety, and rejects repeated rescue attempts.
It also checks allocator pointers around Dutch (the largest retail pack),
both entry controls, title/options returns, RU/UK switching, damaged preview
files and an unavailable retail pack on return.

## On-device language-specific controls help

While playing either demo, **hold L and press Y** to open a readable
Russian or Ukrainian control guide on the existing top LCD. It explains
how to chain neighboring tiles, submit, clear a chain, peel/reveal a
password letter, use the once-per-shelf hint, switch languages, start
unranked Daily Practice and understand repeated-word/combination scoring.
The recovery entry explicitly shows that it spends a turn and heat;
overtime can add to its normal two-heat cost. The Select description also
distinguishes a new free-play attempt from replaying the same Daily puzzle.

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

Ukrainian dictionary imports now report apostrophe-containing words separately
rather than silently changing their spelling. Those forms are **not playable
in the 33-letter demo yet**: the engine still needs a proper word-internal
apostrophe separator instead of a bogus tile. Russian and Ukrainian remain
independent language modes. See [CYR-SOURCES.md](CYR-SOURCES.md) for the
exact manifest fields and source-review limitations.

An optional source-verified thematic expansion pipeline now allows
independent, editorially selected Russian and Ukrainian candidates beyond
the built-in four passwords per theme. Each addition must be present in its
licensed source dictionary, explicitly mapped to a canonical theme, and
included in the external manifest for verification. The two existing demo
ROMs remain limited to the original starter catalogs unless you opt into
external packs and select extra candidates yourself. This is **not** a
substitute for translation review or complete retail gameplay parity.

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

## Optional larger Russian/Ukrainian source-backed build

A dedicated `sourced-cyr-preview.yml` GitHub Actions job now assembles an
**additional** opt-in Nintendo DS ROM with 100,000 licensed-source-derived
candidate words in **each** language, plus 347/333 source-headword-verified,
unreviewed thematic password candidates across all 40 themes. This is not
the ordinary mini demonstration ROM and is not enabled in the retail
11-language game.

Choose the Actions artifact `wordPass-real-RU-UK-experimental-ROM`
(`wordPass-ru-uk-sourced-preview.nds`). The separate
`wordPass-ru-uk-sourced-lexicons` artifact retains original LibreOffice
Russian/Ukrainian dictionary and affix source files, original rights notices,
the exact source commit, generated WPD4/WPL2/WPP2 data, attribution manifests
and the source candidate files. The ROM itself still labels the modes
`РУССКИЙ — DEMO` and `УКРАЇНСЬКА — DEMO`.

The builder validates source integrity and keeps the DS dictionary under
the 1.5 MiB/language cap, rather than making unavailable or inaccurate words
appear. Larger dictionaries are sampled by word length without a genuine
frequency ranking; the supplied themed words have not received professional
semantic review. Hardware save compatibility, physical DS FPS, Ukrainian
apostrophe gameplay, complete Classic/Daily modes and the full UI translation
remain unfinished. Do not present this build as a certified Russian/Ukrainian
release.
