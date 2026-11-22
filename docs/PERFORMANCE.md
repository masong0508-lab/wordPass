# Performance and responsiveness

Earlier DeSmuME measurements below recorded 120 game frames in 120 emulated frames during steady gameplay
(the intended 60 Hz cadence). Language loading blocks separately. These are emulator observations, not a
current blanket hardware guarantee. This page records input latency and the changes that reduce work or
protect the DS memory budget.

## Continued search and regression pass

Retail dead-board detection and word hints now stop following impossible
dictionary prefixes. The WPD3 lookup compares only the prefix's letter bits
in the existing sorted records; it needs no trie, extra index or heap
allocation. The search bounds include Qu's two-letter expansion and
plain-Q diamonds, and the final word still passes the normal common/rare,
library and spelling checks. Traversal order and first eligible hint are
preserved.

`tools/check/benchmark_search.py` compares the old and new implementations
against the same language data and support code. At 100 samples per setting,
2,422 exact result/path cases matched across all 11 locales, library and
English spelling settings, fire/hole/diamond layouts, recent-word history
and all-X/all-Q stress boards. Each search also verifies unchanged board
bytes and RNG state. Across the 2,400 seeded cases, full dictionary lookups
fell from 2,713,366 to 176,651, with 683,214 bounded prefix queries replacing
the discarded branches. Prefix queries also do work; these counts are not
a frame-rate measurement. The separate 7,200-attempt balance sweep produced
identical gameplay reports before and after; see `docs/BALANCE.md`.

Five paired host runs gave these median totals (GCC `-O2`, the same 100
samples per setting):

| Workload | Before | After |
| --- | ---: | ---: |
| 2,400 seeded search/hint cases | 141.988 ms | 113.912 ms |
| All-X boards across 11 locales | 20.830 ms | 3.575 ms |
| All-Q boards across 11 locales | 26.589 ms | 2.449 ms |

The seeded total was about 20% lower, with substantial host scheduling
scatter across the five runs. These timings help assess CPU work; they do
not establish a physical DS frame-rate improvement. Exact search results
and deterministic operation counts are the stronger regression evidence.

Reproduce the isolated search comparison against the prior merged build:

```sh
python3 tools/check/benchmark_search.py --baseline fd4ccd3b092f44c662513cd56059ec65a77e9a75 --samples 100 --output build/search-comparison.json
```

The Cyrillic preview now computes a selected word's score and tier once
before vblank, sharing the pair across top-screen slices. Previously each
clipped slice repeated both dictionary validations during the short LCD
refresh interval. Score, tier and invalid-word visibility participate in
the dirty-slice signatures so the displayed quote updates promptly.

The complete emulator rules/layout/guide suite now runs on every PR, main
build, tag and manual run, replacing its two separately filtered subsets.
Structure offsets are read from the matching built ELF's DWARF with
`pyelftools` or GNU `readelf`, rather than compiling the current checkout
to infer a potentially different build's layout. CI no longer pulls a
toolchain container solely to inspect symbols and member offsets.

## November 21, 2026 performance and reliability pass

### Less work during play

The retail renderer now obtains a chain's validity and score from one `boardChainScore` call. The former
`boardWordOk` call validated the same word a second time on every rendered frame with a valid chain.
Tier, dialect, repeat and overtime rules still run inside the scoring path. This adds no cache, heap
allocation or invalidation state.

The experimental WPD4 dictionary packs each 1..6-bit symbol into a bounded two-byte window rather than
looping over each bit. Its hint DFS now checks prefixes only in the exact word length it is seeking,
skipping empty length tables. This also fixes a real missed hint: a dense board with prefixes that
only reach a 20-letter dictionary word could exhaust the six-letter search budget before finding a
valid six-letter chain in the final column. The regression verifies that the returned path scores
and that searching leaves the board unchanged. Independent packing tests cover all six bit widths
and all 18 supported word lengths, including byte boundaries and prefix masks.

A host-only microbenchmark alternated four million present/absent six-symbol Russian lookups. Five
alternating baseline/current runs with GCC `-O2` returned the same checksum in every run:

| Lookup implementation | Median host time |
| --- | ---: |
| Baseline `a888b449` | 155.534 ms |
| Bounded symbol packing | 104.570 ms |

This isolated test used about 32.8% less elapsed time. It is not a DS FPS or end-to-end hint timing claim.
Reproduce with `make check` to create `build/wpd4_ru.bin`, then:

```sh
gcc -std=gnu11 -O2 -I source tools/check/dict_v4_bench.c source/dict_v4.c -o build/lookup-new
git show a888b449:source/dict_v4.c > build/dict-v4-before.c
gcc -std=gnu11 -O2 -I source tools/check/dict_v4_bench.c build/dict-v4-before.c -o build/lookup-before
build/lookup-before build/wpd4_ru.bin
build/lookup-new build/wpd4_ru.bin
```

### Linked memory and active packs

The baseline retail ELF left 1,831,700 bytes between `__end__` and `__eheap_end`. The largest shipped
pack is Dutch: 1,749,435 payload bytes, leaving only 82,265 bytes before loader padding, allocator,
filesystem and other runtime costs. This is a static upper bound, not measured free RAM.

The pass removes the general `snprintf` dependency from save-path assembly and uses newlib's bounded
integer formatter in the Cyrillic HUD. The build reports check that full floating printf helpers
are absent. When entering the experimental board, the runtime releases the previous retail pack;
it closes all preview images before restoring that pack on exit. The saved language preference is
unchanged. Emulator pointer checks exercise the largest retail pack, both entry routes, title/options
returns, language switches and failed loads.

`tools/check/rom_budget.py` writes a separate JSON report for each linked ROM. CI requires at least
64 KiB of linker heap span after the largest active pack and loader padding, checks ITCM/DTCM bounds,
caps ROM size at 18 MiB, and rejects preview symbols in the retail build. The reserve still has to
cover filesystem buffers and all other dynamic allocations; it does not certify runtime memory or
hardware speed. The reports are retained with the build artifacts so future changes have an explicit
budget. Measurements on physical DS/DS Lite/DSi hardware remain a release gate.

### Faster, reproducible verification

Emulator symbol lookup reads and caches the matching ELF's data/BSS symbols with local `nm`. It used
to launch a toolchain container for each lookup. Replacement, modification or deletion of the ELF
invalidates the cache; a pinned container remains a fallback when local binutils cannot read it.
This speeds host test execution without changing the ROM.

Standard soundtrack previews now use one canonical oscillator phase recurrence whether NumPy is
installed or not. NumPy's former vectorized phase arithmetic could flip samples at square-wave duty
boundaries and incorrectly report changes to all ten committed WAV files. Backend parity and all
20 existing WAV/MIDI comparisons cover this; the game sequencer and tracked recordings are unchanged.
The optional untracked HQ renderer retains its explicitly separate accelerated path.

## How it is measured

    python3 tools/emu/latency.py wordPass.nds [--left] [--only "gem|submit"] [--fps] [--json out.json]
    python3 tools/emu/latency.py --table before.json after.json      # markdown, no emulator

Each scenario is reached by ordinary input and saved as an emulator savestate. It is then run twice from that state,
once with no input (the control) and once with the input applied before the first frame. DeSmuME is deterministic,
so a pixel that differs between the two runs after k frames was caused by the input. The latency is the smallest k
that shows a difference on:

- `touch`: the touch (bottom) screen, `top`: the top screen
- `sfx`: the sound sequencer's mirror (`music.c`) reports a new effect, word or fanfare
- `state`: the game's own state changed (the DBG mirror), `done`: the touch screen reached its final brightness
  (fades, iris and the board falling in are finished; transitions only)
- `fps`: with `--fps`, `Emu.fps()` over 120 frames after the action, the gate for any change (120 is 60 fps)

k = 1 is the next frame. The harness injects input at the start of a frame, so a key can be at best on screen at
k = 2 (the logic sees it in frame 1, the picture it makes is flipped at the vblank that ends the frame and scanned out
in frame 2) and a stylus touch at k = 3: libnds' ARM7 polls the stylus once a frame and hands it over through shared
memory, which costs one more frame (`state` for a touch reads 2, for a key 1). Those are the floors of the pipeline;
`touch` and `top` in the tables are measured against them.

## Before

The first numbers, taken before any change (the loop read the pad right after the flip, drew the frame, and refreshed
the top screen one 48-row slice per frame, 15 Hz round robin):

| action | touch | top | sfx | state | done |
|---|---|---|---|---|---|
| touch down on a gem | 4 | 6 | 2 |  |  |
| drag onto the next gem | 3 | 6 | 1 |  |  |
| tail tap (submit) | 4 | 5 | 2 |  |  |
| A button (submit) | 3 | 3 | 1 |  |  |
| SUBMIT button tap | 4 | 5 | 2 |  |  |
| CLEAR button tap | 4 | 7 |  |  |  |
| peel (stylus lift) | 3 | 3 |  |  |  |
| peek arm (touch in peek) | 4 |  |  |  |  |
| PEEK button tap | 4 |  |  |  |  |
| X latch | 3 |  |  |  |  |
| L held (peek) | 3 |  |  |  |  |
| d-pad cursor move | 3 |  |  |  |  |
| pause button tap | 4 |  |  | 2 |  |
| START (pause) | 3 |  |  | 1 |  |
| last pass letter peel -> results | 3 | 4 | 2 | 2 | 25 |
| fire reaches the bottom -> fail card | 3 | 3 | 1 | 1 | 40 |
| pause menu: d-pad DOWN | 3 |  | 1 |  |  |
| pause menu: resume (START) | 3 |  |  | 1 | 3 |
| title: CLASSIC tap -> play | 4 | 4 | 2 | 2 | 36 |
| title: A (start) | 3 | 3 | 1 | 1 | 35 |
| title: OPTIONS tap | 4 | 4 | 2 | 2 |  |
| title: LEDGER tap | 4 | 4 | 2 | 2 |  |
| title: GUIDE tap | 4 | 4 |  | 2 |  |
| title: language tag tap | 4 |  |  | 2 |  |
| title: tip card flip (RIGHT) |  | 3 | 1 |  |  |
| options: value row tap (THEME) | 5 | 5 | 3 |  |  |
| options: d-pad DOWN | 3 |  | 1 |  |  |
| options: BACK tap | 4 | 4 | 2 | 2 |  |
| guide: NEXT tap (page flip) | 4 | 4 |  |  |  |
| guide: RIGHT (page flip) | 3 | 3 |  |  |  |
| language picker: row tap (DEUTSCH) -> title | 4 | 3 |  | 16 | 28 |


## What changed, and after

1. **Late input sampling** (`gfxLateSample`, main loop). The loop used to read the pad right after the flip and spend the
   frame drawing, so the picture came out one vblank later and the CPU idled. It now waits until the logic and drawing it
   did over the last eight frames (measured in scanlines, plus a quarter and 16 lines) just fit before vblank, then reads
   the pad. The sample is never earlier than line 94, because libnds' ARM7 polls the stylus at line 80 (an earlier sample
   would see a touch a frame late). A draw that ends inside vblank (up to line 256) flips at once instead of waiting for
   the next one. Result: keys 3 to 2, stylus 4 to 3: the floors.
2. **Dirty top-screen slices.** The top screen is drawn in 48-row slices straight into VRAM. A per-slice signature
   (chain word and score, plaque, score line, message, ledger, tip card) marks a slice dirty the frame its content changes,
   and dirty slices are drawn in that vblank (two at most, plus the rotation slice), skipping any slice the beam would
   reach before it finishes. The chain word, the score, a callout and the plaque are now on screen with the touch screen
   (top 5 to 7 before, equal to touch now).
3. **Transitions.** Fade-in steps are 4 per frame (it was 2), the guide's page dip is shallower, the iris opens in nine
   frames, and results/fail cards accept a tap at 24/36 frames (it was 30/45) with one earlier tap (after frame 10) queued
   and applied when the gate opens. Language loads stay a single `read()` per file: 170 to 270 ms in the emulator.

Frame rate after every action in the table: 120 in 120 frames, except the language picker row (it blocks while the files
load, 110, as before: 109).

Before > after, frames (`-` is no change in the 45-frame window; `done` includes the board falling in):

| action | touch before > after | top before > after | sfx before > after | state before > after | done before > after |
|---|---|---|---|---|---|
| touch down on a gem | 4 > 3 | 6 > 3 | 2 > 2 |  |  |
| drag onto the next gem | 3 > 2 | 6 > 2 | 1 > 1 |  |  |
| tail tap (submit) | 4 > 3 | 5 > 3 | 2 > 2 |  |  |
| A button (submit) | 3 > 2 | 3 > 2 | 1 > 1 |  |  |
| SUBMIT button tap | 4 > 3 | 5 > 3 | 2 > 2 |  |  |
| CLEAR button tap | 4 > 3 | 7 > 3 |  |  |  |
| peel (stylus lift) | 3 > 2 | 3 > 2 |  |  |  |
| peek arm (touch in peek) | 4 > 3 |  |  |  |  |
| PEEK button tap | 4 > 3 |  |  |  |  |
| X latch | 3 > 2 |  |  |  |  |
| L held (peek) | 3 > 2 |  |  |  |  |
| d-pad cursor move | 3 > 2 |  |  |  |  |
| pause button tap | 4 > 3 |  |  | 2 > 2 |  |
| START (pause) | 3 > 2 |  |  | 1 > 1 |  |
| last pass letter peel -> results | 3 > 2 | 4 > 3 | 2 > 1 | 2 > 1 | 25 > 28 |
| fire reaches the bottom -> fail card | 3 > 2 | 3 > 2 | 1 > 1 | 1 > 1 | 40 > 39 |
| pause menu: d-pad DOWN | 3 > 2 |  | 1 > 1 |  |  |
| pause menu: resume (START) | 3 > 2 |  |  | 1 > 1 | 3 > 2 |
| title: CLASSIC tap -> play | 4 > 3 | 4 > 3 | 2 > 2 | 2 > 2 | 36 > 35 |
| title: A (start) | 3 > 2 | 3 > 2 | 1 > 1 | 1 > 1 | 35 > 34 |
| title: OPTIONS tap | 4 > 3 | 4 > 3 | 2 > 2 | 2 > 2 |  |
| title: LEDGER tap | 4 > 3 | 4 > 3 | 2 > 2 | 2 > 2 |  |
| title: GUIDE tap | 4 > 3 | 4 > 3 |  | 2 > 2 |  |
| title: language tag tap | 4 > 3 |  |  | 2 > 2 |  |
| title: tip card flip (RIGHT) |  | 3 > 2 | 1 > 1 |  |  |
| options: value row tap (THEME) | 5 > 4 | 5 > 4 | 3 > 3 |  |  |
| options: d-pad DOWN | 3 > 2 |  | 1 > 1 |  |  |
| options: BACK tap | 4 > 3 | 4 > 3 | 2 > 2 | 2 > 2 |  |
| guide: NEXT tap (page flip) | 4 > 3 | 4 > 3 |  |  |  |
| guide: RIGHT (page flip) | 3 > 2 | 3 > 2 |  |  |  |
| language picker: row tap (DEUTSCH) -> title | 4 > 3 | 3 > 2 |  | 16 > 15 | 28 > 26 |


## Targets, honestly

- Touch feedback: 3 for a stylus, 2 for a key, which is the floor of the pipeline (see above); not 1. The touched
  gem is drawn in the same frame the input is read, and that frame is the next one shown.
- Top-screen feedback: equal to the touch screen (2 to 3), inside the 3-frame target.
- Menu taps: 2 for keys, 3 for the stylus (the options value row reads 4 because it rebuilds the gem cache).
- The profile build (`-DWP_PROFILE`, see TESTING.md) shows the touch screen at about 175k to 230k ticks of a 558k frame and
  one top slice at 26k to 41k; two slices in a vblank fit.
