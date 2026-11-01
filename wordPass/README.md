# wordPass

**Bookworm × Minesweeper for the Nintendo DS.**
Chain letters into words on a hex grid of gems. Behind the board hides a
password. Hold a shoulder button to *peek*: peel away letters you can spare
and uncover the pass. Spell weak words and the shelf catches fire.

![build](https://github.com/masong0508-lab/wordPass/actions/workflows/build.yml/badge.svg)

## How it plays

- **Spell.** Drag the stylus across touching gems to chain a word (3+ letters),
  then tap **SUBMIT** or press **A**. Scored tiles shatter and the column drops.
  Every word is a turn.
- **Peek.** Hold **L** or **R** and tap a gem to peel it off the board.
  Behind it is either a **pass letter** (a diamond you can still chain through)
  or a **number**: how many of the six neighbouring cells hide a pass letter.
  A peeled cell is a hole for the rest of the shelf; the gems below it still
  refill. Every peel is a turn too, and warms the shelf like a short word.
- **Fire.** Short words build **heat**: three letters add 3, four add 2, five
  or six add 1, seven and up are free; a peel adds 1 (2 in Lockdown). When the
  meter fills, the topmost gem of a column ignites. A new flame waits one turn
  before it starts to creep, then burns one row down per turn (every other
  turn on early shelves). Put it out by spelling a word through it. A flame
  hangs where it is when the gems under it are cleared. Holes are chimneys:
  fire falls straight through them. Diamonds are firebreaks.
- **Overtime.** Past turn 24 on a shelf (16 in Lockdown) every turn adds heat
  of its own, so a shelf you camp on eventually burns.
- **Scorching.** Peel a burning gem, or the gem directly under one, and the
  flame hits the back layer. If a pass letter was hiding there, it is gone.

### Scoring

- **Words.** Letter values (Bookworm weights, Qu = 10) summed, times a length
  multiplier (x1 up to four letters, x2 for five and six, x3 for seven and
  eight, x4 beyond), times 10. Seven letters and up add a flat 200. A word
  through a burning gem adds 30 per letter. Each diamond (revealed pass
  letter) in the chain adds half again, up to two, so a word through two pass
  letters scores double. Spelling a word you already spelled on this shelf
  halves it (the shelf remembers its last eight words: the ledger on the top
  screen shows the last five). The panel previews exactly what the chain is
  worth before you submit.
- **Pass letters.** Every pass letter found pays 100 x shelf on the spot.
- **Clearing a shelf.** Shelf bonus 1000 x shelf, a speed bonus of
  25 x shelf for every turn under 40, and a peel bonus: **PERFECT PASS**
  (+400 x shelf) when every peel found a pass letter, **CLEAN PASS**
  (+150 x shelf) with at most two wasted peels.

The curve pays roughly twice what earlier builds did, so best scores saved
by older versions will be overtaken quickly: they were set under the old
numbers.

### Fail states

| Screen | Cause |
| --- | --- |
| LIBRARY LOST | a flame was still on the bottom row when the fire advanced |
| PASS DESTROYED | you scorched a cell that held a pass letter |
| OUT OF WORDS | no 3- to 5-letter word is left on the board |

### Modes

- **Classic**: climb the shelves. Fire pressure and pass depth rise as you go,
  with a twist every third shelf from shelf 4 and three bookmarks per run.
- **Daily Pass**: one fixed shelf for everyone, seeded from the DS clock date.
  Same board, same pass, one attempt per run. Pick it from the title screen or
  press SELECT there.

### Options

From the title (OPTIONS button or Y). Tap a row to cycle it, or use the
d-pad.

| Setting | Values |
| --- | --- |
| Theme | Jewel, Ink (slate gems with a tinted edge), Candy (pastels), Ember (warm) |
| Frame | Gold, Silver, Accent |
| Accent | Auto (changes per shelf) or one of eight fixed colours |
| Hand | Right or Left: mirrors the grid and panel for left-handed stylus play |
| Fire | Relaxed, Normal, Fast |
| Music | On, Off |
| Library | Full (every ENABLE word) or Common (rejects obscure words with TOO OBSCURE) |

Settings, best Classic score and shelf, and best Daily result are saved to
`wordPass.cfg` on the card when a FAT filesystem is available (flashcarts
with DLDI, DSi SD). Without one they last for the session and the options
screen says so.

### Shelves

Every shelf draws its pass from a themed pool that matches the shelf's accent
colour, and the plaque on the top screen names it (PASS - BINDERY). The eight
pools cycle as you climb: **Bindery** (book, page, spine, jacket ...), **Ink**
(nib, font, quill, scribe, parchment ...), **Verse** (poem, rhyme, sonnet,
epigram ...), **Tales** (myth, fable, novel, chapter, epilogue ...), **Atlas**
(map, globe, compass, gazetteer ...), **Archive** (codex, ledger, lexicon,
glossary ...), **Lamp** (wick, candle, lantern, sconce ...) and **Study**
(desk, shelf, lectern, footnote, bookmark ...). The pass gets longer with the
shelf: four letters on shelves 1-2, five on 3-5, six on 6-8, seven on 9-12 and
eight from shelf 13 (a pool without that length lends its nearest), and the
last eight passes of the run are not repeated while the pool has a choice.
The letters themselves come from a shuffled bag of 100 drawn without
replacement, so a shelf never starves of vowels or drowns in them, J, Q, X and
Z appear at most twice a bag, and a column that refills with nothing but
consonants gets a vowel at the bottom. Daily boards use a separate random
stream from the sparks and shakes, so two players on the same date see the
same board, the same refills and the same fire.

Shelves 1-3 are **Classic**: fire moves every other turn, the pass sits high
and the heat meter holds 10. Shelves 3-5 tighten it to 7. From shelf 6 on it
is **Lockdown**: fire every turn, a heat pulse every six turns, the pass
buried low, and the music drops into a minor key. Lockdown climbs in tiers
every three shelves (the panel reads LOCKDOWN, LOCKDOWN II, III, IV): the
pulse comes faster, the meter shrinks, and from tier two (shelf 12) every
ignition lights two columns at once.

From shelf 4, every third shelf (4, 7, 10 ...) has a **twist**, announced as
you arrive and never the same one twice running:

| Twist | Effect |
| --- | --- |
| INK SPILL | three cells are already holes when the shelf opens |
| DAMP | the fire creeps one turn slower but the meter is two shorter |
| BURIED | the pass is one letter longer and lies entirely in the bottom three rows |

A Classic run starts with three **bookmarks**, shown as small books on the
top screen. A failed shelf costs one and you retry the shelf; with none left
the run is over. Restarting a shelf from the pause menu also costs one (it is
refused on your last), and clearing a Lockdown shelf earns one back, up to
three. Daily Pass has no bookmarks: one attempt.

## Controls

| Input | Action |
| --- | --- |
| Stylus drag | chain letters |
| A / SUBMIT button | submit word |
| B / CLEAR button | clear chain |
| Hold L or R | peek mode (tap a gem to peel it) |
| START | pause (then START resume, SELECT restart shelf for one bookmark, B quit to title) |
| Y (title) | options |

![wordPass](screenshot.png) ![fire](screenshot-fire.png) ![results](screenshot-results.png)

## Interface

Bejeweled on the front, N+ behind. Letter gems are faceted and glossy, colour
coded by letter group (vowels ruby, common consonants sapphire, rare letters
amber). The frame and pass plaque are gold. Everything else, including the
whole back layer you see in peek mode, is flat black with thin lines and a
single accent colour. Holding peek dims the gems to outlines so the layer
you are reading is always the one in colour.

Everything that happens gets juice: scored gems shatter into shards and the
score floats up from the chain, a found pass letter cracks open in a ring of
stars and pops into the plaque on the top screen, burning gems pulse with
flame tongues and shed sparks, callouts pop in at double size, shelves fade
in, and a failed run flashes and shakes the screen. The gold frames carry a
travelling shimmer and the submit gem breathes while a valid word is chained.
The title screen's top half cycles three illustrated tip cards, a first-shelf
nudge appears if you haven't peeked after three words, rejected words shake
red, the chain rope turns the shelf accent once the word is valid, and a red
glow creeps up from the floor as fire gets low. Columns fall under gravity and
squash when they land, each shelf has its own
accent colour, and clearing a shelf slides up a results card with the pass,
your best word, words spelled (and how many were unique), peels used, turns,
and the shelf, speed and perfect/clean pass bonuses. Finding a pass letter
pops its reward over the gem, a word through a diamond calls out PASS LINK,
a repeat says AGAIN, and the top screen keeps a fading ledger of the last
five words.

## Music

No samples: the soundtrack is sequenced live on the DS PSG channels so stems
can be mixed by game state.

- A fixed four-bar **hook** that never stops. Each shelf transposes it to a
  new key; Lockdown shelves play it in minor.
- **Pulse** arpeggio always on. **Bass** enters while you keep spelling and
  fades after three quiet bars. **Drums** layer up by word length and decay
  back. **Pad** swells one chord tone per pass letter found.
- **Alarm**: a clashing tone that climbs as the fire descends.
- **Peek** ducks everything but the pulse; each reveal plays the next note of
  the hook, so peeling a path plays the tune.
- Word chimes are quantized to the beat. Finishing a pass hands the hook to a
  bright octave-up voice for one loop. Failing cuts to the alarm sliding down.

## Performance

Both screens are software rendered straight into VRAM, with no RAM back
buffer and no per-frame copy. The touch screen uses banks A and B as a full
16-bit bitmap pair and the main background flips between them at vblank. The
top screen HUD has no back buffer, so it is drawn into bank C inside vblank,
one 48-row slice per frame: every primitive honours a vertical clip band, so
each region refreshes at 15 Hz without ever tearing or flickering, and a
change of layout (title/options to in-game or back) redraws all four slices
over two frames. Every state, including the results-card slide-in, the fail
shake and a pause pressed mid-cascade, runs at 60 fps: the title and options
screens skip the board entirely, and pause, results and fail screens render
the live board and let the 2D engine halve its brightness (a brightness-
decrease colour effect on the bitmap layer, windowed to the board strip with
the card cut out), so no CPU time goes into dimming or snapshotting. The hot
blitters (fills, gem blits, glyphs,
outlines, particles, backdrop, board, panel) live in ITCM and the gem sprite
cache, particles and popups in DTCM; every letter is pre-composited onto its
gem (and onto a diamond) once per theme, so a resting gem is twenty 48-byte
row moves, the backdrop is skipped under gems that are fully redrawn, glyphs
and gems are written directly after a single bounds test, and fills and blits
use word-aligned 32-bit stores (the left-hand layout starts the grid on an
even x for the same reason). The floor glow is painted into the backdrop and
only added over the three gems that reach into it, the note table is built
from one octave of integers (no libm), and the HUD counters use a local itoa
with the integer-only `siprintf` for the rest. Build
with `-DWP_PROFILE` to expose per-stage timings in `dbgT[]`.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
`nds-dev` group installed.

```bash
make
```

Produces `wordPass.nds`. Run it in melonDS or DeSmuME, or on hardware with a
flashcart.

Without a local toolchain, the devkitPro Docker image works:

```bash
docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm:20240511 make
```

The release ROM and CI use the `20240511` image (libnds 1.8). Current
devkitPro (libnds 2 with calico) builds the same source without changes, but
ROMs built with it do not boot in DeSmuME; use melonDS or real hardware.

### Dictionary

`data/dict.bin` is generated from the public-domain ENABLE list (words of
3–10 letters, 125k entries) in the `WPD2` format: a 4-byte magic, eight
`u32` counts for lengths 3..10, then one sorted group of fixed-width records
per length. Letters are packed 5 bits each, big-endian and left aligned, so
`memcmp()` order is alphabetical; the record width follows the length (4 bytes
for 3–6 letters, 5 for 7, 6 for 8, 7 for 9–10, 717 KB in all) and bit 0 of
the last byte is the **tier**: 0 common, 1 rare. The device binary-searches
the group for the word's length (one 32-bit compare per step for 3–6 letter
words) and returns the tier, which drives the RARE WORD callout and the
Library option. Rebuild it from any word list with one word per line, plus an
optional list of the words to mark common:

```bash
make dict WORDS=tools/enable1.txt COMMON=tools/common_words.txt
```

`make dict` runs `gen_dict.py --verify` afterwards, which re-reads the binary
with a Python port of the device lookup and checks every input word (with its
tier) and 20 random non-words per length. `tools/common_words.txt` is the
ENABLE words that appear in the top 30,000 of the
[FrequencyWords](https://github.com/hermitdave/FrequencyWords) 2018 English
list (hermitdave, CC-BY-SA 4.0) or in
[google-10000-english](https://github.com/first20hours/google-10000-english)
(first20hours).

## Layout

```
source/main.c    state machine, input
source/board.c   hex grid, chaining, scoring, gravity, fire, peeling
source/dict.c    packed dictionary lookup
source/gfx.c     software renderer for both screens
source/music.c   PSG sequencer and adaptive stems
source/font.c    5x7 bitmap font
source/settings.c options, records, FAT save
source/debug.c   fixed-layout state mirror and poke channel for the test harness
tools/gen_dict.py  WPD2 dictionary builder and --verify round trip
tools/check/     native rule tests: make check (gcc, no toolchain)
tools/emu/       headless DeSmuME harness, smoke.py and test_rules.py (see TESTING.md)
tools/common_words.txt
data/dict.bin
```

## License

MIT. Word list: ENABLE, public domain. Common-word tier derived from
hermitdave/FrequencyWords (CC-BY-SA 4.0) and first20hours/google-10000-english.
