# Cyrillic preview: turn, heat and fire contracts

This milestone narrows the gap between the real Classic rules and the
Unicode-script prototype without changing the released 11-language engine.

## Turn outcomes

- An accepted word consumes one turn. Its base heat is 3 for three-letter
  words, 2 for four, 1 for five/six, and 0 for seven or more letters.
- A valid peel consumes one turn and 1 heat, even when it misses.
- Heat thresholds start at 8; later shelves lower the threshold to 5.
  An ignition lights one topmost eligible gem, preferring columns that do
  not already burn. The flame only starts descending at a later burn tick.
- Flames descend one *ordinary tile* per tick, skipping permanent holes.
  Revealed pass diamonds and other flames stop them. A flame on the
  bottom row, or one that escapes the board through holes, fails the shelf.
- A burning secret cannot be revealed safely. Its scorched peel causes an
  immediate failure. A successful final pass letter wins the shelf before
  the fire advances again.
- Overtime adds escalating heat after the soft turn budget. A completed
  shelf awards a deterministic bonus for shelf, speed and peel economy.
- Repeated word rules, ledger, lockdown, twists, save/resume and Daily
  modifiers are **not** implemented by this preliminary rules module.
  Enabling these languages in Classic/Daily remains blocked on full parity.

## Accidental-peel protection (experimental DS previews)

The Russian/Ukrainian demo now uses the same deliberate peek gesture as the
regular game: **hold L and touch a gem to arm it, then lift on that SAME gem
to peel**. Merely touching does not spend a turn. Moving the stylus off the
armed gem (including onto a neighboring gem) permanently cancels the peel for
that stroke; releasing L before lifting cancels too. A cancelled peek cannot
silently turn into a normal word chain. Starting a new stroke is required to
arm another gem, and the regular non-peek word drag is unchanged.

This protects the hidden-password search from accidental taps without
granting extra peels, reveals or hints. The small gesture state lives only
inside the optional preview's input handler, requires no heap or board
schema changes, and never runs a dictionary search per frame. The DeSmuME
scenario checks no score, heat, turn, board or word-chain mutation on a
cancelled gesture and retains the existing valid-peel-on-release checks.

## Deterministic themed password lengths

The experimental Russian and Ukrainian preview used to ask the WPP2
selector for five-letter passwords on every shelf even when sourced
dictionaries included substantial four-, six-, seven-, eight- and
nine-letter options. The preview now varies the requested password
length as the challenge progresses: shelves 1–3 target 4–5 letters;
4–9 target 5–6; 10–19 target 5–7; 20–34 target 6–8; 35+ target
6–9 letters. The actual pass is still chosen **only** from dictionary-
validated WPP2 words in the active localized theme, with the nearest
available length if no word of the requested length exists. Equal-
distance alternatives prefer the shorter word. No artificial spelling,
extra gem type or secret location is generated.

The length calculation is deterministic and needs no new board memory,
RNG state, frame-time work or new asset. For unranked Daily Practice
the date/language seed remains the same on every replay. The early
and late bands are engineering defaults, **not** evidence of a
native-player-tested difficulty curve or Classic/Daily parity.

## Word-earned and peel-earned numbered clues (RU/UK demo)

Both experimental language engines now use the genuine shifted-hex
Minesweeper clue rule: peeling a blank tile exposes a permanent number 0–6,
while successfully submitting any dictionary-accepted word surveys the fixed
board location where its chain ended. A published count includes adjacent
hidden password positions **and** diamonds that were already revealed; it
never includes the center tile. Refills, flames and front-letter movement do
not relocate the back-layer count.

Previously, the demo drew gold markers directly on secret positions, spoiling
the password. Those marks are removed. A code-level visibility gate ensures
unsurveyed covered cells return **no clue**; a deterministic native test checks
the gate independently of screen rendering, and a real-ROM screenshot test
changes a hidden back value without permitting the on-screen tile to change.
Where a published clue logically forces all/none of the remaining covered
neighbors, the DS marks them with +/- cues while retaining red fire danger.
This adds no automatic reveal, hint resource, turn, heap allocation or texture
asset. It does **not** make Ukrainian/Russian complete retail languages.

## Refill corruption safety (RU/UK engineering ROM)

The validated 100-tile WPL2 bag now gets a cheap preflight weight sum **before**
any shuffle writes to the caller's 100-byte output buffer. A damaged in-RAM
weight table totaling 99 or 101 tiles, or an impossible symbol count, fails
closed without changing the output buffer. The normal checksum/loader already
rejects malformed files; this additionally protects refills after loading.

Submitting a legitimate word now stages its affected 7×8 board in one small
stack copy. The word, falling tiles, clue survey and bag RNG are committed
**together** only when refill succeeds. A failed refill cannot remove half a
word, spend the tile bag, reveal a clue, or report a scored word. Native
regressions force a valid chain to encounter a damaged next bag, then verify
that its chain and all other board bytes remain unchanged. Neither change
adds heap, a framebuffer or per-frame search work; normal, valid dictionaries
use the identical deterministic shuffle, score and clue result. The 11-language
retail engine, save files and daily puzzles are untouched.

## Memory and rendering

The rules state is a few dozen bytes, uses stack-bounded searches and has
no heap allocation or framebuffer dependency. The board keeps 7x8 symbol
IDs, a 100-tile bag and fixed password metadata. Both independent games
share the same 33-symbol codepoint dictionaries and tile weights.

A critical visual regression is prevented by hashing the Cyrillic preview
data into the four 48-pixel top-screen slice signatures. Previously the
top screen used the unrelated retail `Game` values and could miss
updates to its password and currently chained word.

## Validation

`make check` now runs `script_fire_check` on synthetic RU/UK resources.
It checks deterministic ignition and movement, immutability of diamonds,
hole chimneys, fire reaching the bottom, scorched pass secrets, word heat,
overtime, shelf difficulty, loss states and the single-award pass bonus.

The opt-in ROM has a separate real DeSmuME scenario that checks turn
accounting and that the actual rendered top screen changes when switching
from Russian to Ukrainian, while the normal 11-language ROM keeps its own
emulator suite.

**Do not mistake technical parity for completed language content.** The
40 starter themes still need native proofreading, extensive vocabulary,
licensed source review and complete translation before a release.

## Unicode prefix-aware hint (preview only)

Press **R** during an active Cyrillic shelf to automatically select a real
3–6-symbol adjacent word path. Only one free hint is allowed per preview
shelf, and the user still needs to submit the word. It neither reveals a
hidden password position nor skips turns.

The solver uses heap-free, 12,000-node-capped depth-first search of the
7×8 hex-grid. It prunes impossible paths with WPD4's new
`v4DictHasPrefixIds()` binary search over the already validated, sorted
packed word groups. The search is only run on a key press, never per frame.
The standard game's paid-hint economy and dictionary remain unchanged.
Native tests cover Russian and Ukrainian hint paths, corrupt prefix IDs and
empty boards; the optional DeSmuME scenario exercises R and the one-hint
limit on ARM9.
