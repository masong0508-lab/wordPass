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
