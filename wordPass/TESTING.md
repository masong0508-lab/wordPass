# Building and testing wordPass in this environment

## Build
The toolchain lives in a Docker image. If `docker info` fails, start the daemon:

    rm -f /var/run/docker.pid; (dockerd --iptables=false --ip6tables=false --bridge=none >/tmp/dockerd.log 2>&1 &); sleep 5

Build (from the repo root; always use this image, libnds 2 ROMs do not boot in DeSmuME):

    docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm:20240511 make 2>&1 | grep -E "error|warning|built"

A clean build must print `built ... wordPass.nds` and no warnings.

## Native rule tests (no toolchain, no emulator)

    make check          # gcc builds board.c, dict.c and music.c with tools/check/stub, runs build/check
                        # (before it, build/lang_check from tools/check/lang_test.c checks data/lang)

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
`tools/check/lang_test.c` loads all thirty files of `data/lang` (ten languages, three files each) with the game's own `dict.c` and `pools.c` pointed at buffers, and checks: records a-z only and sorted, every word found with its tier, the Qu rule, word counts inside 80k..250k, common tier 15k..35k, pool files (40 themes, names A-Z up to 7 letters, every pool word in the dictionary and in one theme only, tile letters only), letters (weights sum 100, values 1..10, Qu flag, vowel set), known words accepted and non-words rejected per language, the manifest, and that the three English files still match their sha256 pin (`python3 tools/lang/build_all.py --manifest` re-pins them after an intended English change). Its last line is `LANG OK: 0 failure(s)`; the existing check still ends the run with `CHECK OK`.
`tools/check/board_test.c` also runs the runtime half for all ten languages through the real loader (`source/langload.c`, reading `data/lang/<code>/` where the ROM reads `nitro:/lang/<code>/`) and `source/lang.c`: English byte-identical (a hash of 2000 bag draws and of 60 whole first boards, pinned from the build before languages existed), loading and tables, known words accepted and non-words rejected, LIBRARY COMMON through the tier bit, SPELLING ignored, pools (names, a-z, in the dictionary, tile letters only), the bag dealing every tile letter and nothing else, 40 whole shelves per language (pass in the pool and the dictionary, tiles from the alphabet), a spelled word scored with the language's letter values, switching back to English restoring everything, and a bad letters image refused. `tools/check/settings_test.c` checks the saved language (round trip, unknown falls back to English). See `docs/LANGUAGES-DATA.md`.
CI runs `make check` and `python3 -m py_compile tools/emu/*.py` in a second job.

## Emulator harness
`tools/emu/wp.py` wraps a headless DeSmuME (py-desmume, already installed) and reads a
fixed-layout `DBG` mirror of game state from the ROM, so it keeps working when `Game` changes.

    python3 tools/emu/smoke.py wordPass.nds /tmp/shots       # full regression, prints SMOKE OK
    python3 tools/emu/test_rules.py wordPass.nds /tmp/shots  # deterministic rule scenarios, RULES OK

Write ad-hoc probes with `from wp import Emu` (see the docstring). Screenshots are
256x384 PNGs: top screen above, touch screen below. Read them with the image viewer to
check visuals. Coordinates: CLASSIC button (52,147), DAILY (120,147), OPTIONS (86,170), the language tag (40,15), GUIDE (140,15),
nine options rows at y = 32 + 13*row, a touch inside a row at +6 (the right half, x >= 86, steps a value forward and the left half back; HAND is row 3: tap (130, 77), LANGUAGE is row 8), BACK (86, 161); the options list is never mirrored, so these do not change with the hand, cell centres
via `e.center(c, r)`, cell rectangles on the screenshot via `e.cell_rect(c, r)`.

### The DBG mirror and its offset table
`source/debug.c` declares `DebugView` and pins every field's byte offset with a
`_Static_assert`; `wp.py` has the same numbers in `OFF = {'state': 8, ..., 'chainR': 442}`
and `state()` reads through it (no hand-walked slices). Adding a field means: append it
to `DebugView`, add its `PIN(...)`, bump the size assert, add it to `OFF`, and read it in
`state()`. `state()` returns the scalars (`language` (LANG_*, 0 English; `wp.LANG_CODES`), `loadMs` (milliseconds the ROM measured for the last language load) and `langMask` (bit i: language i is on the ROM filesystem; bit 31: the raw card path is in use), `peek` (peekHeld or the latch), `armed`, `armC`, `armR` (the armed peel), `curC`, `curR`, `curOn` (the d-pad cursor), `state, mode, shelf, score, fail, turn, heat,
chainLen, peels, lockdown, bestWordScore, dailySeed, passLen, lastWordTier, rareWords,
library, heatThreshold, burnEvery, burnTimer, optRow, pulseEvery, lockTier, overtime,
chainScore, speedBonus, peelBonus, passBonusKind, ledgerN, newBest, lives, passTheme,
twist, calloutLife, pressBits, shownScore, pauseRow, pauseArm`; `shownScore` is the score the HUD shows
(it counts up during the results tally), `pauseRow` the highlighted pause row and `pauseArm` the frames left of a
RESTART/QUIT confirmation; `tutorStep` (0 drag, 1 peek, 2 find, 3 closing tip, 4 off), `tutorDone` (S.tutorDone), `tipIndex` (the title's tip card) and `msgTimer` (frames left of the message line, -1 sticky); `guidePage` (0..7) and `guideFrom` (ST_TITLE or ST_PAUSE) belong to the in-game guide (`ST_GUIDE` is state 7); `msg` mirrors the first 31 characters of the message line; `callout` is the text of the board callout (`calloutLife` frames left;
`pressBits` bit 0 SUBMIT, 1 CLEAR, 2 PEEK, 3 pause pressed, 4 the SUBMIT flash); `chainScore` is `boardChainScore(g)` every frame, the exact gain the current chain
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
gravity, no turn: `boardDebugFireStep`), `set_turn(n)`, `set_score(n)` (command 8), `set_lives(n)` (command 7: bookmarks), `set_pass(n, found)`
(command 12, layout checks only: the pass becomes the first n letters of the alphabet, 1..9, with the bitmask `found`
shown as found; the back layer is left alone, so do not peel afterwards), `set_tutor(done)` (command 13: 1 switches the guided first shelf off, 0 lets the next Classic shelf 1 guide; `test_rules.py` switches it off at the start so the prompts do not own the message line).
`set_language(idx)` (command 15, title or options only) loads a language and waits for the load to finish: the load blocks the ROM for a moment, so it polls the command instead of running a fixed number of frames. Do not run `set_language` mid-shelf (it is ignored there).
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
`test_rules.py` has `fresh_shelf()` (the pause menu's RESTART SHELF on a clean random
board; a restart costs a bookmark, so it pokes `lives` back to 3 first), `pause_do(name)` (START, the d-pad to the
row, A twice for RESTART and QUIT), `to_title()` (the pause menu's QUIT),
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

## Chain input

`boardChainTouch(g, c, r, fresh)` is the one entry point for a touch sample (`fresh` = a new stylus-down, false while dragging); `make check` covers cut-back, bridge, restart-on-tap, the tail-tap submit and the 20-gem cap. `boardTryAddToChain` is a thin drag wrapper. `gfxCellAt` is a 12 px circle hit test over the hex centres. In `test_rules.py` the `chain input` scenarios (both hands) drive real stylus strokes: `touch(x, y)` holds a point, `untouch()` lifts, `e.tap` is a fresh tap. Set `WP_ONLY="chain input|other name"` to run just the scenarios whose name contains one of those words (a development filter; the full suite must still pass).

## Peek input

A peek touch only arms a gem; the peel happens when the stylus lifts (`keysUp() & KEY_TOUCH`). `Emu.peel(c, r)` holds L, touches for 5 frames and lifts, so it keeps working. A test that wants the peel on a frame must lift the stylus first. Avoid pokes that make an all-one-letter board in a scenario that peels: the OUT OF WORDS search over such a board takes many frames. The `peek input` scenarios (both hands) cover arm, slide-off, shoulder release, the X latch, the PEEK button and take the readability screenshots.

## Button play

B steps the chain back one gem (hold it 20 frames to clear all) and Y clears, so a test that wants an empty chain presses `Y`, not `B`. The `d-pad play` scenarios use `goto(c, r)` to walk the cursor and cover the clamps, A to chain and submit, B, Y, X plus A to peel, and a touch hiding the cursor.

## Panel and HUD

The panel scenarios (`panel HUD`, both hands) read the screenshot rather than the mirror: `count_color(px, x0, y0, x1, y1, rgb)` counts pixels near a 5-bit colour in a rectangle, and `lit_outside_panel(px)` lists lit pixels in the margins around the 76 px panel (x 176..251 right-handed, 4..79 left-handed), so a string that grows past the panel edge fails the test. They poke the worst cases (a six-digit score, a nine-letter pass, turn 150, a 20-letter chain, burnEvery 99) and take the screenshots to read. `HUD top row` checks the mode label, that the nine-letter plaque and a 20-letter chain stay left of the fire meter (x 222..) and inside the plaque border, and the `+value` under the big word. The `board callouts` scenarios cover the word tags, `NOT A WORD` with the red rims, `PASS LETTER!` and `SCORCHED` in both hands. Buttons are pressed with `e.tap(..., settle=0)` and read through `pressBits`; leave a few frames between two taps, or the game never sees the stylus lift.

## Overlays

`pause menu` (both hands) drives the menu with the d-pad and with taps (`pause_row_y(i)`), reads `pauseRow` and `pauseArm`, and covers START and B resuming, SELECT doing nothing, the 90-frame confirmation expiring, a moved highlight disarming it, RESTART costing a bookmark, the refusal on the last one, and the chain being dropped when the menu opens. `results card` stops on the frame the last peel passes the shelf (`pass_by_peeling`), reads `shownScore` every frame (it starts at the score before the bonuses, waits for frame 18, never goes backwards, arrives in 10..80 frames and snaps on a tap, including a tap mid-count), and watches the card's top border pixel for the white flash. `fail card` and `results card` poke a 20-letter best word (`long_word_path`) (and, on the fail card, a seven-digit score) and assert that no text touches the two columns inside the card border. `title` checks that the logo band is solid black around the gems while the rain falls above and beside it. The screenshots land in the output folder for reading, one set per hand.

## Options, onboarding and accessibility

`options` (both hands) pokes nothing: it taps the real screen. It flips HAND from the options and compares the pixels of the list before and after (the labels, the highlight and the frame must not move), taps the left and right half of rows (THEME wraps onto CLEAR), watches the chevron flash on the tapped side for about six frames (the screenshot lags the game by a frame or two, so it polls ten frames rather than reading one), walks every row with the d-pad and asserts that the help line keeps clear of the plaque border, checks colours inside the sample board (a ruby, the accent-rimmed hole, the burning gem's light plate, the chained gem's white rim) and that CLEAR adds its own second line. `CLEAR theme` picks the theme through the options and samples a gem of each group on the board (yellow, blue, magenta, teal, grey) and the plate, glyph and bar of two burning gems (a letter and a Qu). `guided first shelf` follows the prompts through `msg` and `msgTimer` (sticky = -1): drag, the first word, a callout that takes the line and gives it back, the first hole's number message (a pass letter is poked beside the cell so its number is 1), the closing tip, and `tutorDone` going to 1 only when the tip has run; a second scenario starts with a peel that finds a pass letter and clears the shelf mid-tip. `title tip cards` presses LEFT, RIGHT, L and R, reads `tipIndex`, checks the dots and that no card text touches the plaque border, and waits out the 420 frame timer. `side-aware hints` tells HOLD L from HOLD R by one pixel of the glyph, on the panel and on the title card. `make check` covers the CLEAR theme in the settings file.

## The in-game guide

`source/guide.c` builds the eight HOW TO PLAY pages as text from the rules' own tables and functions (`boardLenMult`, `boardHeatFor`, `boardWordPoints`, `boardShelfRules`, `boardPassLenFor`, the bonus and shelf macros in `game.h`), and `tools/check/board_test.c` is its test: eight pages, no visible line over 38 characters, no page taller than 139 px (10 px a line, 4 a gap), a glyph for every character, and for each page the numbers it prints are compared with the game (the multiplier ladder, the heat table, the pass-length ladder, the theme count, the bonuses, the Lockdown tiers, and the example words INK = +70 and Qu+I+Z = +210 against `boardChainScore` on a real board). `boardShelfRules` and `boardDailyRules` are compared with what `boardNewShelf` sets up on shelves 1 to 24 and for every Daily modifier. The top-screen captions and diagrams are drawn code, so the emulator scenarios read the pixels instead.

The `guide` scenarios in `test_rules.py` open the guide from the title (the GUIDE button at BX 112..162, y 7..24, and B) and from the pause menu (the third row, GUIDE in `pause_rows()`; the second row in Daily), flip all eight pages with NEXT, PREV, the dots, the d-pad, L and R and A, and check that BACK, B and START return to the right state (BACK sits at (230, 13) right-handed and (26, 13) left-handed; NEXT and PREV at (238, 178) and (18, 178); the dots at x = 79 + 14 i, y 178). For each page they take a screenshot per hand (`rules_guide_{right,left}_N.png`, the top half is the diagram, the bottom the words), check that the diagram moves, `Emu.fps()` is 120/120, and watch a whole four second loop of both screens (one screenshot per four frames) for any lit pixel outside the plaque, in the two columns inside its border, or in the margins and above the buttons of the touch screen. `guide cue` checks that the GUIDE button pulses and the title says NEW HERE? TAP GUIDE until `tutorDone` (`e.set_tutor(0)` makes it false) and that both rest afterwards.

To look at a page, `from wp import Emu`, tap the GUIDE button (`e.tap(137, 15)` right-handed, `e.tap(219, 15)` left-handed), `e.run(60)`, press RIGHT to turn the page and `e.shot('p.png')`. The top screen redraws in 48 row slices at 15 Hz; the loop advances every fourth frame so the four slices of one refresh always show the same moment. A `-DWP_PROFILE` build shows the cost of a slice in `dbgT[7]` (read it a frame late: it holds the slice drawn after the previous vblank); the guide's slices stay under 70k ticks of the 149k a vblank holds.

### Motion tests

While a guide diagram is drawn, `frame` is replaced by the loop's tick (`frame >> 2`, 64 ticks, 256 frames), so every pulse in the board's own helpers loops with it. `guide animation` takes a screenshot of the diagram region every four frames for two loops of every page and asserts that the picture changes (at least 12 different pictures and 20 changes per loop), that the second loop is byte for byte the first (the loop closes exactly) and that `Emu.fps()` is 120/120, then takes start, mid and end screenshots (`rules_guide_anim_{right,left}_N_{start,mid,end}.png`). `guide text` counts the lit pixels of the words 9 frames after a flip (still typing in), checks that pressing X (otherwise unbound in the guide) finishes them at once, and that the finished page matches a page left alone. `guide buttons` uses `POKE_GUIDENAV` (`e.poke(POKE_GUIDENAV)`) to make the PREV/NEXT buttons unused again and watches their border pulse until one is used.

The guided-shelf cues are mirrored as `cue` (`CUE_NONE`, `CUE_GHOST`, `CUE_TAIL`, `CUE_PEEKBTN`, `CUE_ARMED`), `cueAge` (frames) and `hintLen` (gems of the ghost drag). `guided shelf cues` waits for the ghost after idle frames, checks it ends by itself and is not shown again, that a touch cancels it, that a held chain shows no cue and the lifted valid chain points at its tail gem, that scoring moves the cue to the PEEK button (a ring is drawn around it), that holding L stops it, that an armed cell is pointed at, and that a finished guide shows none, with `Emu.fps()` held through every state. `guided prompt` reads the left edge of the sticky prompt over 28 frames: it starts to the right of its place and glides to it. `make check` covers `boardHintChain` (a real, common word on the board, along touching cells).

## Languages (emulator)

The `languages:` scenarios in `test_rules.py` read `data/lang/<code>/pools.bin` and `letters.bin` themselves (`read_pools`, `read_letters`) and compare them with what the ROM does. They cover: all ten languages found on the ROM filesystem (`langMask`) and each loading in under two seconds (`loadMs`, printed as `load ms:`; about 170 to 270 ms in DeSmuME), the title tag drawn and tappable (the GUIDE button beside it still works), a shelf in every language whose pass is in the pool of its theme and whose tiles are all letters the language has a tile for, a known word spelled through the real touch path (chain it, tap the tail gem) whose score equals the preview and enters the ledger, a made-up word refused, the language's own letter values in the preview, the options row cycling forward and back through all ten (with SPELLING reading ENGLISH ONLY and ignoring taps), the LANGUAGE row staying inside the list, English whole again afterwards, worst-case plaques in both hands (seven-letter theme names, a nine-letter pass, a six-digit score) and the results card in every language. Screenshots land as `rules_lang_{title,options,shelf,results}_<code>.png`; `rules_lang_worst_<code>_{right,left}.png` are the worst cases.

Loading speed. NitroFS in card mode (a retail cart, DeSmuME, melonDS) reads each 512 byte page with the slowest card clock and the longest command delay, which measured 4.1 seconds per megabyte in DeSmuME (5 to 7 s for a language). `source/langload.c` therefore finds the three files through the ROM's own file tables and reads them with the header's card control word when that path proves itself at startup (`langMask` bit 31 says it is in use); if it does not, or the program was started from a flashcart's SD card (FAT works there and NitroFS reads the .nds through it), the NitroFS path is used. To exercise the NitroFS-only path, make `rawInit` return early. To check the no-filesystem fallback, build a ROM without the files and look at `langMask` (1: English only) and that the language poke, the tag and the options row leave English in place:

    docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm:20240511 make TARGET=wp_nofs NITRO=
    python3 -c "from wp import Emu; e = Emu('wp_nofs.nds', elf='wp_nofs.elf'); e.run(120); print(e.state()['langMask'])"

## Music

`source/music.c` mirrors what it last sent to the sound hardware into `DebugView.seq` (a `MusicDbg`, 48 bytes at offset 624, so the mirror is 672 bytes): per voice (HOOK, PULSE, BASS, PAD, ALARM, CHIME, DRUM, NOISE2) the Hz, the volume and the duty (the libnds `DutyCycle` value: 0 12.5%, 1 25%, 2 37.5%, 3 50%), plus `step` (the step most recently sequenced, written on the frame its notes were sent; 255 before the first), `section`, `tempo` (frames per step), `drumLevel`, `lastSfx` (the last sound effect that started, -1 none) and `sfxCount`. In Python it is `e.music()`; `e.wait_step(n)` runs frame by frame until step n has been sequenced. Audio is never judged by ear: `make check` compiles `music.c` into the check binary with stub PSG calls that keep the last value per channel (`stubHz`, `stubVol`, `stubDuty`, `stubPan`) and count every hardware write (`stubRedundant` counts writes that changed nothing and must stay 0), so it asserts the register window of every key in major and minor, the hook envelope and vibrato, the stereo table, the effect pitches and priorities and the MUSIC option. The `music` scenarios in `test_rules.py` check the same through the ROM: step 0 onset, the MUSIC option, and every effect from real play (taps, untap, a refused word, peel, scorch, ignite, firestep, menu moves).

The composition checks in `make check` read the sequencer one step at a time (`capture()` in `board_test.c` records what had been sent on the frame each step was sequenced): the A A B A section order, the hook and its harmonic-minor seventh, the pulse arpeggio and its pan, the pad, every drum level (kick, snare, ghost kick, accents, open hat, fill), the bass riff, the alarm's blip rate, volume and length by flame row, and the tempo lift and relax. The `music composition`, `music under fire` and `music: a word brings in the bass riff` scenarios check the same through the ROM.

The event and mix checks in `make check` (cross-fade and pickup bar, a chime per word length 3..20 and the clamps, the pad accent, the steam hiss, the three stingers frame by frame and the bed, the fanfare and the results loop, every game state mapped to a mix, and the attract, pause and peek mixes) set `g.state` themselves and read the mirror, which also carries `mix` (0 play, 1 attract, 2 pause, 3 results, 4 fail), `failPhase`, `fanfare`, `pickup`, `fade` and `wordCount` (`MusicDbg` is 48 bytes, so the mirror is 672). The ROM scenarios `music mirror`, `music words`, `a failed shelf`, `a cleared shelf` and `music mixes` drive the same through real play: the new shelf's fade and pickup bar, the chime of a real word, the fail card's stinger and bed, the results fanfare, and pause, peek and the title.
