# Performance and responsiveness

wordPass runs at 60 fps in every state (`Emu.fps()` reads 120 in 120 emulated frames). This page records how long the
game takes to answer an input, in frames, measured by `tools/emu/latency.py`, and what was done about it.

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
