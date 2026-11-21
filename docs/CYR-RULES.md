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
