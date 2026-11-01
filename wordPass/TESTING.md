# Building and testing wordPass in this environment

## Build
The toolchain lives in a Docker image. If `docker info` fails, start the daemon:

    rm -f /var/run/docker.pid; (dockerd --iptables=false --ip6tables=false --bridge=none >/tmp/dockerd.log 2>&1 &); sleep 5

Build (from the repo root; always use this image, libnds 2 ROMs do not boot in DeSmuME):

    docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm:20240511 make 2>&1 | grep -E "error|warning|built"

A clean build must print `built ... wordPass.nds` and no warnings.

## Native rule tests (no toolchain, no emulator)

    make check          # gcc builds board.c, dict.c and music.c with tools/check/stub, runs build/check

`tools/check/board_test.c` loads `data/dict.bin` and exercises the rules directly: fire
over holes and secrets, gravity with a hole, Qu+I+Z, chain backtracking, peeling (scorch,
reveal, OUT OF WORDS on an all-x board), dictionary tiers (`zax` rare, `book` common) and
the note table (`musicBuildNotes`: C4 = 262, A4 = 440), the letter bag (100 a cycle, two
of J/Q/X/Z, the vowel floor, seed determinism), the pass pools (shelf 2 is a 4-letter INK
word for every `recentPass` state, lengths by shelf), Daily determinism, the Daily modifiers, the lifetime stats and the twists. `tools/check/stub/nds.h` stands in
for libnds (types, `ARGB16`, empty placement attributes, the PSG calls) and
`tools/check/stubs.c` defines `S`, `dict_bin` and the sound stubs. When board.c or music.c
starts calling a new gfx*/music*/sound* symbol, add it to stubs.c or the link will say so.
`make check` first runs `tools/check/settings_test.c`, which compiles `source/settings.c` against a stub `fat.h` and tests the pure encode/decode (CRC, WPS1 migration, clamping), the streak arithmetic and the temp-file-and-rename save on a scratch file in `build/`.
CI runs `make check` and `python3 -m py_compile tools/emu/*.py` in a second job.

## Emulator harness
`tools/emu/wp.py` wraps a headless DeSmuME (py-desmume, already installed) and reads a
fixed-layout `DBG` mirror of game state from the ROM, so it keeps working when `Game` changes.

    python3 tools/emu/smoke.py wordPass.nds /tmp/shots       # full regression, prints SMOKE OK
    python3 tools/emu/test_rules.py wordPass.nds /tmp/shots  # deterministic rule scenarios, RULES OK

Write ad-hoc probes with `from wp import Emu` (see the docstring). Screenshots are
256x384 PNGs: top screen above, touch screen below. Read them with the image viewer to
check visuals. Coordinates: CLASSIC button (52,147), DAILY (120,147), OPTIONS (86,170),
options rows at y = 36 + 14*row (HAND is row 3, tap (86,84)), BACK (86,170), cell centres
via `e.center(c, r)`, cell rectangles on the screenshot via `e.cell_rect(c, r)`.

### The DBG mirror and its offset table
`source/debug.c` declares `DebugView` and pins every field's byte offset with a
`_Static_assert`; `wp.py` has the same numbers in `OFF = {'state': 8, ..., 'chainR': 442}`
and `state()` reads through it (no hand-walked slices). Adding a field means: append it
to `DebugView`, add its `PIN(...)`, bump the size assert, add it to `OFF`, and read it in
`state()`. `state()` returns the scalars (`state, mode, shelf, score, fail, turn, heat,
chainLen, peels, lockdown, bestWordScore, dailySeed, passLen, lastWordTier, rareWords,
library, heatThreshold, burnEvery, burnTimer, optRow, pulseEvery, lockTier, overtime,
chainScore, speedBonus, peelBonus, passBonusKind, ledgerN, newBest, lives, passTheme,
twist`; `chainScore` is `boardChainScore(g)` every frame, the exact gain the current chain
would submit for; `passTheme` indexes `wp.PASS_THEMES`, `twist` is 0 none, 1 INK SPILL,
2 DAMP, 3 BURIED),
`pass`, `found`, `cell[c][r] =
(letter, state)`, `back`, `scorched`, `msg`, `lastWord`, `bestWord`, `settings`
(theme/hand/fire/music) and `chain` as a list of `(c, r)`.

### Poke channel
`DebugView` carries `cmd, a0..a3` (offsets 336..352). A test writes the arguments then the
command; the ROM's next `debugSync()` applies it and clears `cmd`. `e.poke(cmd, a0, a1,
a2, a3)` does the writes and runs two frames, and asserts the command was consumed.
Daily and persistence: `set_clock(ymd)` makes the game's clock read a date (0 = real; call it on the title), `set_daily(played, last, streak)` sets the records, and `state()` also returns `practice, dailyMod, clockOk, statWords, statGames, streak, dailyPlayed, dailyLast, dailyLastScore`. DeSmuME has no FAT card, so saving is covered natively; the emulator keeps records in memory for the session.
Helpers: `set_cell(c, r, state, letter)` (state EMPTY/TILE/BURN/HOLE/SECRET = 0..4,
letter `'a'..'z'` or 0..25; motion and flash cleared), `set_back(c, r, v)` (0 or pass
index + 1), `set_heat(h)`, `set_burn(timer, every)`, `fire_step()` (one fire advance plus
gravity, no turn: `boardDebugFireStep`), `set_turn(n)`, `set_score(n)` (command 8), `set_lives(n)` (command 7: bookmarks).
Pokes bypass the game's own invariants, so set up whole columns explicitly.

Recipes: `set_burn(0, 99)` keeps the fire still across submits, `set_burn(0, 1)` makes it
advance on the next word. OUT OF WORDS deterministically: `set_cell` every cell to `'x'`,
then peel one cell with `back == 0` (a peel is a turn, so its `boardHasWords` check runs,
but a peel vacates nothing, so nothing refills; a submit would refill random letters).
Every word and peel is a turn (`turn++`, heat by `Emu.heat_for(len)` or 1 per peel),
and a turn's heat crossing `heatThreshold` lights the topmost gem of a random column,
so a scenario that must stay flame-free keeps `set_heat(0)` fresh. `set_turn(0)` keeps
shelf overtime (turn 24, 16 in Lockdown) out of a long scenario. `burn_cells(g)`,
`lowest_fire_row(g)` and `heat_for(n)` read the fire state the way board.c computes it.
`test_rules.py` has `fresh_shelf()` (pause + SELECT restarts the shelf on a clean random
board; a restart costs a bookmark, so it pokes `lives` back to 3 first), `to_title()`,
`clear_shelf()` (peels the pass in order with the fire held off and taps through the
results card: the way to reach a given shelf) and `play_turn(pick)` with the `shortest`
and `longest_fire_aware` strategies. `letter_mix(g)` counts vowels and J/Q/X/Z on a board.
Board randomness is board.c's own xorshift (`boardRand`, seeded per shelf from the Daily
date or libc `rand()`); gfx keeps libc `rand()` for sparks and shakes, so a Daily board and
its refills are identical between runs whatever the frame timing. `make check` exercises
the letter bag directly through `boardSeed`, `boardBagReset` and `boardRandomLetter`.
`Emu.word_score(word, burn, diamonds, repeat)` and `Emu.pass_bonus(shelf, turn, peels,
passLen)` are Python ports of the scoring curve (board.c `chainScoreDetail`, main.c pass
completion) for exact score assertions.

## Rules
- Keep play at 60 fps: `Emu.fps()` must return 120 for 120 cycles in play. Build a copy of
  the tree with `-DWP_PROFILE` (add to CFLAGS) and run `python3 tools/emu/prof.py <copy>`
  to read the per-stage timings in `dbgT` (board stage: ~73k idle, effects ~6k with fire).
- `debugSync()` in `source/debug.c` must be kept in sync if you add state the tests need.
- Commit after each verified change with a descriptive message.

## Long words and spelling

`make check` covers words of 11 to 20 letters, a 20-gem chain that spells a 20-letter word, and the SPELLING setting (BOTH, AMERICAN, BRITISH, the shared and -ize/-ise words, `boardWordSpelling`). The emulator suite cycles the SPELLING row on the options screen and uses `POKE_SPELL`. The check binary loads `data/variants.bin` as its fourth argument, after dict, pools and variants default to `data/`.
