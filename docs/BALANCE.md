# Balance report

Measured with `tools/check/bot.c`, a native player that drives the real rules code (`source/board.c`, the dictionaries, the
pass pools, the letter bag) headlessly. Build it with `make bot`, run English and four other languages with `make bot-run`
(a few minutes on four cores), or one language with `build/bot --lang en --n 300`. `tools/check/balance_md.py` lays the
output out as the tables below. A small `--selftest` of it runs inside `make check`. Every number on this page was produced
by the bot at 300 fresh attempts per shelf and language (so a clear rate is good to about plus or minus 3 points), on FIRE
NORMAL with the dictionary on FULL; "before" is the rules of commit 92faa05 and "after" is this build, both measured by the
same bot (build the old rules by checking out that commit's `source/game.h` and `source/board.c`).

## The players

Both players read only what a human sees: the letters, the flames, the heat meter, the numbers on holes and the diamonds
they have found (never the back layer). A shelf is a clear when the pass is found before the shelf ends.

* **Competent.** Looks for every word on the board (a trie of the whole dictionary) and notices a common word of three or
  four letters 95% of the time, five or more 70%, a rare-tier word 10%. It prefers long words (heat 0 at seven letters), and
  it reads the fire: a flame that will reach the floor in three turns or fewer sends it after a word through that flame (92%
  to notice it), and as a last resort it peels a burning gem no word reaches. It peels like a Minesweeper solver: from each
  hole number it works out which neighbours are safe or certain, it knows the pass is one connected cluster buried below the
  shelf's first pass row and grows from the diamonds it has found, and it peels the most likely cell (never one a flame would
  scorch) whenever no flame is urgent. It looks away from the fire on 10% of turns, and on 5% of turns it slips and peels a
  random gem (counted as a "guess" below).
* **Weak.** Words of three to five letters only (it notices 55% of the short ones, 20% of the longer), no sense of heat,
  pays attention to a flame 60% of the turns and then only when it is two turns from the floor, and peels beside numbers
  above zero and beside diamonds without any deduction.
* **Score-seeking** (a table of its own). The competent player, but it peels only when a cell is at least 60% likely (and on
  12% of other turns), so it spends the turn budget on words.

Columns: **turns** counts words and peels; **peel hit** is the share of peels that found a pass letter; **guess** is the
share of peels taken with under a 20% chance of finding one (the first peels of a shelf and the slips); **ignitions** is
how many times the meter filled; **stall** means 100 turns passed with the shelf neither won nor lost.

## What the data showed, and what changed

| # | finding (before) | change | where the guide quotes it |
| --- | --- | --- | --- |
| 1 | The ladder had a cliff and a plateau: competent English cleared 88% on shelf 11 and 70% on shelf 12 (Lockdown tier III turned on two flames per ignition at once, a drop of 18 points), then 55 to 60% on every shelf from 15 to 24. Nothing rose after shelf 15. | The meter now shrinks a step every shelf or two (10, 10, 9, 8, 7, 7, 6, 6, 5, 5, 5, then 4 from shelf 12) instead of in three jumps. Twin ignitions (two flames) began as a switch at shelf 12; they now ramp: one ignition in eight from shelf 11, one more in eight every two shelves, all of them from shelf 25. | GUIDE 8.2, 8.4, 9.3, 9.4 ladder (new column), README, the in-game SHELVES page and its animated ladder |
| 2 | Flames stacked: a flame stuck on a diamond (a firebreak) was joined by the next ignition of that column, and a stack of two to four could park on an unfound pass letter for good. Most stalls were this. | A new flame goes to a column that has no flame; only when every column burns does one join a column. | GUIDE 8.4 |
| 3 | Overtime did not end a shelf, it stalled it: the meter lit at most one gem (two from tier III) a turn and a player who put one flame out per word never died. 5% of competent attempts (5 to 11% from shelf 12) were still going after 100 turns. | Overtime heat grows twice as fast (+1 for twelve turns, then +2 ...), and in overtime the heat over the meter is kept and lights as many more gems as it fills the meter again. Stalls fall to 1%. | GUIDE 8.6 and its table, README |
| 4 | Words farm a burning shelf: a bot that never peeled made 37,000 points in 100 turns of words on a first-rung shelf (the fire never beat it), against 2,300 for clearing the shelf quickly; even the score-seeking bot, which does peel, made 14,400 a shelf-1 attempt before the change. | Words spelled past the turn limit score a tenth. | GUIDE 8.6, README |
| 5 | A fifth of FIRE losses (English 20%, German 30%) had no word through any flame on the last look. German boards hold a third as many 3-5-letter paths as the other languages. | The re-deal below fixes dead boards; the unreachable-flame share fell to 7% in English once flames stopped stacking. German was 12 to 24 points harder than the other four languages from shelf 12 on; its letter table is retuned (see "German letter table" below) and it now sits within 5 points of them. | |
| 6 | Dead boards (OUT OF WORDS) are rare: 0 in 7,200 competent English shelves, 3 in 7,200 competent German shelves (about 43 chainable gems and 13 holes left), and the German score-seeking bot, which plays more words, re-dealt 0.2 to 0.5% of its early shelves. | The first dead board of a shelf is re-dealt (+3 heat); the second ends the shelf. | GUIDE 8.7, 8.8, README |
| 7 | Daily modifiers were uneven: the weak bot cleared STANDARD 76%, EMBER 60%, LONG 63%, DEEP 78% (English), and in German EMBER 46% against DEEP 72%. A pass buried in three rows is a smaller area to search, so DEEP, meant to be harder, was the easiest. | Meters: STANDARD 6 (was 7), EMBER 9 (was 8), DEEP 9 (was 7) with the fire every turn (was every other), LONG 8 (was 7). The weak bot now clears STANDARD 70%, EMBER 67%, DEEP 63%, LONG 64% in English (a 7 point spread, was 18) and 52 to 55% in German (a 3 point spread, was 26). A competent player clears every Daily 98 to 100% before and after: it is a shelf 4 to 5 in difficulty. | GUIDE 10.3, README |
| 8 | Pass depth is not a clock (the guide said it was): a deeper pass was a little easier for the bot (finding it is one connected cluster in a smaller area). | The guide's claim is corrected; the first pass row stays as it was. | GUIDE 9.4 |
| 9 | Peeling alone wins: a competent shelf on rungs 1 to 5 takes 0.5 words and about 11 peels, and nothing rewarded a word beyond its score. | Combos (below): words in a row without a peel pay up to +50%. The clear rate does not change; the score-seeking table shows the trade. | GUIDE 5.6, README |
| 10 | Pass placement is not pure guessing: once a number or a diamond is on the board the next letter is found 50 to 60% of peels. Only the first peel or two of a shelf, and the slips, are guesses. | none | |
| 11 | Letter bags: no language starves vowels or words (table below, every language has at least 13 vowels of 56 and 200 word paths). | none | |

| 12 | The weak bot has a cliff at the first Lockdown shelf: 61% on shelf 5, 25% on shelf 6 (English; 31 to 37 points of fall in every bot language), because a peel starts to cost 2 heat there and the weak bot peels fourteen times a shelf. | The meter on shelf 6 is 8 (the ladder's 7 plus one) and Lockdown's overtime starts at turn 24, 22, 20, 18 on shelves 6 to 9 and 16 from shelf 10, instead of 16 from shelf 6. | GUIDE 8.2, 8.6, 9.3 (new table), 9.4 ladder, 12.7, README, the in-game SHELVES page |

Features added (each with a native test, an emulator scenario, a README and GUIDE paragraph, and strings in all ten languages):

* **Shuffle** (above, finding 6). `SHUFFLES_PER_SHELF` 1, `SHUFFLE_HEAT` 3.
* **Combo**: +10% per word of a streak without a peel, to +50% (`COMBO_PCT_PER_WORD`, `COMBO_CAP_PCT`); `COMBO Xn` on the panel, a callout at the third word. The score-seeking bot's streaks reach 15 words and the bonus is worth about 1,400 to 1,800 points an attempt to it (15% of its score) and 0 to 350 to the competent player.
* **Hint**: SELECT traces a common word with the ghost drag (through a flame, if one burns), two a shelf; ten idle seconds offer one for free.
* Not built: **pass-letter streak rewards**. The data does not ask for them: the competent peel hit rate is 40 to 60% and a streak of two hits is common, so a reward would be a bonus to the thing the player already does; the combo already pays the opposite habit (words). **Settings** did not change, so there is no migration.

## Results

### Summary: competent clear rate by stretch of shelves, before / after

| language | clear % shelves 1-5 (before / after) | 6-10 | 11-15 | 16-20 | 21-24 | steepest 3-shelf fall (before / after) | stalls % (before / after) | run: shelves cleared, competent (before / after) | weak (before / after) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| en | 99 / 100 | 92 / 91 | 69 / 73 | 58 / 52 | 56 / 42 | 22 / 20 points | 5.2 / 1.0 | 22.6 / 19.7 | 5.1 / 5.5 |
| es | 99 / 100 | 92 / 91 | 75 / 78 | 64 / 60 | 62 / 49 | 17 / 15 points | 3.1 / 0.8 | 28.6 / 21.0 | 5.2 / 5.8 |
| de | 99 / 99 | 86 / 85 | 58 / 63 | 45 / 43 | 42 / 30 | 27 / 20 points | 1.7 / 0.2 | 15.8 / 16.2 | 4.4 / 4.6 |
| fr | 99 / 100 | 93 / 92 | 73 / 78 | 65 / 59 | 62 / 50 | 21 / 18 points | 4.3 / 0.9 | 26.3 / 21.2 | 5.3 / 5.4 |
| fi | 99 / 100 | 92 / 91 | 74 / 79 | 65 / 62 | 65 / 54 | 18 / 15 points | 3.0 / 0.7 | 27.6 / 22.5 | 5.0 / 5.2 |

The steepest fall is the largest drop between two consecutive three-shelf averages. The cliff (shelf 11 to 12) is gone;
the largest remaining single step is shelf 14 to 15 (Lockdown tier IV: pulse every 3 turns and three ignitions in eight
twins), 12 to 14 points. The competent bot now clears shelf 1 to 5 at 98 to 100% (its 5% slips cannot hurt on a shelf with
no fire to speak of: the first shelf is meant to be nearly unlosable), falls about 3 points a shelf from shelf 6 to shelf 20
and sits at 43% on shelf 20 (English); the target was 95% falling to about 40% around shelf 20. German
was 12 to 24 points under the other four (30% against 42 to 54% at shelf 21 to 24) because its dictionary has fewer short
words; the letter table fixed that (`tools/lang/letters.py`, see "German letter table" below), so the German column of the
tables in this summary is the measurement before that change.

**Competent bot: clear rate (%) by shelf, before and after**

| shelf | en before | en after | es before | es after | de before | de after | fr before | fr after | fi before | fi after |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 100.0 | 100.0 | 100.0 | 100.0 | 100.0 | 99.7 | 99.7 | 99.3 | 99.3 |
| 2 | 100.0 | 100.0 | 100.0 | 100.0 | 99.7 | 99.7 | 99.7 | 99.7 | 99.7 | 100.0 |
| 3 | 100.0 | 100.0 | 99.7 | 100.0 | 99.0 | 99.7 | 100.0 | 99.7 | 99.7 | 100.0 |
| 4 | 98.7 | 99.7 | 99.0 | 99.3 | 98.7 | 99.0 | 98.3 | 99.3 | 98.0 | 100.0 |
| 5 | 98.0 | 98.3 | 98.3 | 98.3 | 97.7 | 97.7 | 99.0 | 99.3 | 99.0 | 99.0 |
| 6 | 93.3 | 94.3 | 92.7 | 92.3 | 89.3 | 92.0 | 92.3 | 94.7 | 91.7 | 96.0 |
| 7 | 93.0 | 94.3 | 95.3 | 93.3 | 87.7 | 90.3 | 94.3 | 93.3 | 96.0 | 93.3 |
| 8 | 91.0 | 90.0 | 91.3 | 91.0 | 87.7 | 87.7 | 95.3 | 94.7 | 93.7 | 92.3 |
| 9 | 90.3 | 86.3 | 89.3 | 87.3 | 82.7 | 78.0 | 90.7 | 87.3 | 87.3 | 87.3 |
| 10 | 90.0 | 89.3 | 91.3 | 90.7 | 83.3 | 79.0 | 94.0 | 89.3 | 92.0 | 88.0 |
| 11 | 88.0 | 81.0 | 92.7 | 86.7 | 81.7 | 79.3 | 90.7 | 83.3 | 90.0 | 84.0 |
| 12 | 69.7 | 83.0 | 72.7 | 82.7 | 53.3 | 70.0 | 68.3 | 88.3 | 71.0 | 84.7 |
| 13 | 67.0 | 74.0 | 78.0 | 76.0 | 57.0 | 62.7 | 73.0 | 77.3 | 72.3 | 77.7 |
| 14 | 64.3 | 71.7 | 71.3 | 80.0 | 55.0 | 60.7 | 71.7 | 76.7 | 73.0 | 81.0 |
| 15 | 57.3 | 57.7 | 61.3 | 67.0 | 42.3 | 44.7 | 59.0 | 62.7 | 65.7 | 66.7 |
| 16 | 57.7 | 58.0 | 64.7 | 67.3 | 50.3 | 53.7 | 66.7 | 64.7 | 69.0 | 64.3 |
| 17 | 61.0 | 52.7 | 63.3 | 59.0 | 47.0 | 40.7 | 65.7 | 61.3 | 65.0 | 67.3 |
| 18 | 55.7 | 57.7 | 65.3 | 65.7 | 41.0 | 44.7 | 63.3 | 55.3 | 62.3 | 62.3 |
| 19 | 55.3 | 48.0 | 64.7 | 58.7 | 43.7 | 38.0 | 62.7 | 55.0 | 66.0 | 58.3 |
| 20 | 58.3 | 43.3 | 63.0 | 50.7 | 42.7 | 37.0 | 68.0 | 59.0 | 62.3 | 57.0 |
| 21 | 55.7 | 42.3 | 63.0 | 50.3 | 42.7 | 28.0 | 62.0 | 52.0 | 67.7 | 56.3 |
| 22 | 58.0 | 49.3 | 63.3 | 54.3 | 43.3 | 35.7 | 63.3 | 51.3 | 64.0 | 54.0 |
| 23 | 58.0 | 36.0 | 64.0 | 49.7 | 44.0 | 30.7 | 63.3 | 49.3 | 64.0 | 54.7 |
| 24 | 53.7 | 40.7 | 57.3 | 42.7 | 39.0 | 25.3 | 59.7 | 48.3 | 63.0 | 51.7 |

**Weak bot: clear rate (%) by shelf, before and after**

| shelf | en before | en after | es before | es after | de before | de after | fr before | fr after | fi before | fi after |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 92.3 | 92.0 | 93.3 | 93.0 | 89.3 | 88.7 | 94.0 | 93.7 | 92.7 | 93.3 |
| 2 | 92.3 | 91.7 | 94.7 | 93.7 | 89.7 | 89.0 | 94.7 | 94.0 | 95.3 | 95.3 |
| 3 | 79.0 | 87.7 | 77.0 | 80.0 | 68.7 | 75.0 | 79.3 | 81.7 | 72.0 | 78.7 |
| 4 | 70.3 | 76.7 | 67.3 | 74.7 | 55.3 | 62.0 | 67.3 | 73.3 | 63.0 | 70.0 |
| 5 | 62.3 | 61.0 | 61.7 | 59.3 | 51.7 | 50.3 | 59.0 | 56.7 | 59.3 | 58.0 |
| 6 | 21.7 | 21.0 | 22.7 | 25.0 | 13.3 | 16.0 | 24.3 | 27.7 | 23.7 | 28.0 |
| 7 | 30.7 | 25.0 | 32.0 | 29.0 | 23.7 | 19.3 | 31.0 | 32.0 | 30.7 | 26.7 |
| 8 | 22.3 | 18.3 | 20.7 | 23.3 | 12.0 | 11.3 | 21.7 | 17.7 | 19.7 | 14.0 |
| 9 | 16.7 | 10.0 | 17.3 | 10.7 | 8.7 | 7.0 | 18.0 | 11.7 | 13.3 | 6.7 |
| 10 | 21.3 | 19.0 | 21.7 | 17.0 | 13.0 | 9.3 | 23.3 | 18.3 | 18.0 | 15.7 |
| 11 | 9.0 | 6.0 | 13.3 | 10.3 | 9.3 | 4.3 | 15.0 | 7.7 | 15.7 | 7.3 |
| 12 | 1.7 | 5.3 | 1.3 | 7.0 | 0.3 | 3.3 | 2.0 | 4.7 | 0.7 | 5.3 |
| 13 | 4.0 | 7.7 | 2.7 | 9.7 | 3.3 | 5.3 | 6.3 | 7.0 | 2.7 | 7.3 |
| 14 | 0.7 | 1.7 | 1.3 | 2.3 | 0.3 | 0.7 | 2.3 | 1.7 | 2.3 | 1.7 |
| 15 | 0.3 | 0.0 | 0.7 | 1.3 | 0.0 | 0.0 | 1.3 | 1.0 | 0.3 | 1.0 |
| 16 | 2.7 | 4.7 | 2.3 | 5.0 | 1.7 | 2.7 | 2.0 | 3.0 | 3.0 | 3.7 |
| 17 | 0.7 | 0.0 | 0.3 | 1.0 | 0.3 | 0.0 | 2.0 | 0.3 | 0.3 | 0.7 |
| 18 | 0.0 | 1.0 | 0.0 | 0.7 | 0.0 | 0.3 | 0.0 | 0.7 | 1.3 | 1.3 |
| 19 | 2.0 | 2.0 | 2.7 | 1.3 | 1.7 | 2.0 | 2.7 | 1.7 | 3.0 | 3.0 |
| 20 | 0.0 | 1.0 | 0.7 | 1.0 | 0.3 | 0.0 | 1.3 | 0.0 | 0.7 | 1.0 |
| 21 | 1.3 | 0.0 | 1.0 | 0.7 | 0.0 | 0.0 | 0.7 | 0.3 | 0.7 | 0.7 |
| 22 | 2.3 | 1.3 | 3.7 | 2.7 | 1.3 | 0.3 | 2.0 | 0.7 | 2.7 | 1.7 |
| 23 | 0.3 | 0.7 | 0.7 | 0.3 | 0.0 | 0.3 | 0.3 | 0.0 | 1.3 | 0.7 |
| 24 | 0.0 | 0.3 | 1.0 | 0.3 | 0.0 | 0.0 | 0.3 | 0.0 | 1.3 | 0.3 |

The weak bot clears 92% of shelves 1 and 2 (it is not dead at shelf 1), 60 to 90% on shelves 3 to 5, 5 to 25% on the Lockdown
shelves 6 to 11, and is gone by shelf 12. It fell off a ledge at shelf 6 (61% to 21%): the shelf where a peel
starts to cost 2 heat, and the weak bot peels fourteen times a shelf. The second pass smooths that step (see "The Lockdown
ramp" below); the two tables above are the measurement before it.

### German letter table (item 1 of the second pass)

German's dictionary holds a third as many words of 3, 4 and 5 letters as English (332 / 1,367 / 3,382 against 999 / 4,006 /
8,894, common tier plus rare), so a board dealt from its natural letter shares held 214 word paths of 3 to 5 tiles against
English's 382, and the competent bot cleared shelves 12 to 24 some 12 to 24 points less often. The fix is in the tile
weights (`TUNED_WEIGHTS` in `tools/lang/letters.py`, written into `data/lang/de/letters.bin`; the other nine languages'
files and English are byte-identical): the bag leans toward the letters that spell the short words and away from the ones
that live in long compounds.

| letter | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | Q | R | S | T | U | V | W | X | Y | Z |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| before | 6 | 2 | 3 | 2 | 16 | 2 | 4 | 5 | 6 | 1 | 2 | 4 | 2 | 9 | 3 | 1 | 1 | 8 | 7 | 7 | 4 | 1 | 1 | 1 | 1 | 1 |
| after | 3 | 1 | 1 | 1 | 24 | 1 | 1 | 1 | 6 | 1 | 1 | 1 | 1 | 9 | 2 | 1 | 1 | 9 | 14 | 14 | 2 | 1 | 1 | 1 | 1 | 1 |

How it was found: a hill climb over the weights (one tile moved from one letter to another at a time) against the bot's
fresh-board measurement (`build/bot --lang de --boards`, 400 boards), then the competent bot's clear rate per band to
confirm. Searches that kept every letter at 2 to 5 tiles reached only 284 paths and fell 3 to 8 points short at shelves
16 to 24, and searches with no floor at all reached 400 paths with E at 30 and A, O, U at one tile, which makes a
board of ESSEN; this table is the compromise: 350 paths, 21 vowels per 56 tiles, no board under 15 vowels. Letter
values (scoring) and the four rare letters are unchanged.

**Competent bot, clear rate (%) by band of shelves** (300 attempts per shelf; the other four languages are the "after"
measurement of the first pass, unchanged because their data and the rules are unchanged)

| band | de before | de after | en | es | fr | fi | others (lowest to highest) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1-5 | 99.2 | 99.2 | 99.6 | 99.5 | 99.5 | 99.7 | 100 to 100 |
| 6-10 | 85.4 | 89.0 | 90.8 | 90.9 | 91.9 | 91.4 | 91 to 92 |
| 11-15 | 63.5 | 73.7 | 73.5 | 78.5 | 77.7 | 78.8 | 73 to 79 |
| 16-20 | 42.8 | 54.9 | 51.9 | 60.3 | 59.1 | 61.8 | 52 to 62 |
| 21-24 | 29.9 | 46.6 | 42.1 | 49.2 | 50.2 | 54.2 | 42 to 54 |

German is now within 5 points of the average of the other four at every band (2 to 3.5 points under it from shelf 6) and
inside the span of the four at 11 to 15 and 21 to 24.

**Competent bot, clear rate (%) by shelf**

| shelf | de before | de after | en | es | fr | fi |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 98.7 | 100.0 | 100.0 | 99.7 | 99.3 |
| 2 | 99.7 | 99.3 | 100.0 | 100.0 | 99.7 | 100.0 |
| 3 | 99.7 | 99.7 | 100.0 | 100.0 | 99.7 | 100.0 |
| 4 | 99.0 | 99.7 | 99.7 | 99.3 | 99.3 | 100.0 |
| 5 | 97.7 | 98.7 | 98.3 | 98.3 | 99.3 | 99.0 |
| 6 | 92.0 | 93.7 | 94.3 | 92.3 | 94.7 | 96.0 |
| 7 | 90.3 | 93.3 | 94.3 | 93.3 | 93.3 | 93.3 |
| 8 | 87.7 | 91.3 | 90.0 | 91.0 | 94.7 | 92.3 |
| 9 | 78.0 | 85.7 | 86.3 | 87.3 | 87.3 | 87.3 |
| 10 | 79.0 | 81.0 | 89.3 | 90.7 | 89.3 | 88.0 |
| 11 | 79.3 | 82.3 | 81.0 | 86.7 | 83.3 | 84.0 |
| 12 | 70.0 | 77.0 | 83.0 | 82.7 | 88.3 | 84.7 |
| 13 | 62.7 | 70.7 | 74.0 | 76.0 | 77.3 | 77.7 |
| 14 | 60.7 | 75.3 | 71.7 | 80.0 | 76.7 | 81.0 |
| 15 | 44.7 | 63.3 | 57.7 | 67.0 | 62.7 | 66.7 |
| 16 | 53.7 | 65.0 | 58.0 | 67.3 | 64.7 | 64.3 |
| 17 | 40.7 | 53.7 | 52.7 | 59.0 | 61.3 | 67.3 |
| 18 | 44.7 | 50.7 | 57.7 | 65.7 | 55.3 | 62.3 |
| 19 | 38.0 | 54.3 | 48.0 | 58.7 | 55.0 | 58.3 |
| 20 | 37.0 | 51.0 | 43.3 | 50.7 | 59.0 | 57.0 |
| 21 | 28.0 | 49.3 | 42.3 | 50.3 | 52.0 | 56.3 |
| 22 | 35.7 | 46.0 | 49.3 | 54.3 | 51.3 | 54.0 |
| 23 | 30.7 | 44.0 | 36.0 | 49.7 | 49.3 | 54.7 |
| 24 | 25.3 | 47.3 | 40.7 | 42.7 | 48.3 | 51.7 |

**Weak bot, clear rate (%) by band of shelves**

| band | de before | de after | en | es | fr | fi |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1-5 | 73.0 | 77.3 | 81.8 | 80.1 | 79.9 | 79.1 |
| 6-10 | 12.6 | 18.8 | 18.7 | 21.0 | 21.5 | 18.2 |
| 11-15 | 2.7 | 3.5 | 4.1 | 6.1 | 4.4 | 4.5 |
| 16-20 | 1.0 | 1.2 | 1.7 | 1.8 | 1.1 | 1.9 |
| 21-24 | 0.1 | 0.8 | 0.6 | 1.0 | 0.2 | 0.8 |


**Fresh boards, German** (400 deals): vowels per 56 21.4 (was 20.3), fewest vowels 15 (was 13), boards under 14 vowels 0.0%
(was 0.2%), 3 to 5 letter word paths 350 (was 214, English 382), boards under 25 paths 0.0%, boards without a word 0.00%.
The other ten rows of the fresh-board table above are unchanged (re-measured: identical).

Other effects (competent, 60 whole runs and 240 dates): a run clears 21.2 shelves on average (was 16.2, the other four 19.7
to 22.5), the Daily modifiers clear 99.6, 100, 100 and 99.6% (weak 63, 62, 58 and 62%, was 55, 52, 55 and 53%), FIRE fails with
no word through any flame fell from 375 to 224, and OUT OF WORDS fails stay at 0. The guard is a native test:
`tools/check/board_test.c` counts the 3 to 5 tile word paths of 60 fresh German boards and fails under 300 (it reads 349).

### The Lockdown ramp (item 2 of the second pass)

The first Lockdown shelf asks the weak bot for a lot at once: a peel costs 2 heat instead of 1, a pulse adds 2 heat every
six turns, overtime starts at turn 16 instead of 24, and the shelf's meter is 7, one step under shelf 5's. The weak bot's
clear rate falls from 61 to 25% (English) between shelves 5 and 6; a competent player barely notices (98 to 94%).

What was tried (English, 1,000 attempts per shelf, the same seeds before and after, so the differences are paired; "weak 6"
is the weak bot's clear rate on shelf 6, "strong" the competent bot's change in points on shelves 6 to 10):

| change | weak 6 | strong, worst shelf | verdict |
| --- | ---: | ---: | --- |
| none | 24.9 | - | the cliff |
| fire every other turn on shelf 6 | 50.2 | +2.8 English, +4.4 Spanish, +4.9 German | too much for the strong bot |
| meter 10 on shelf 6 (+3) | 39.8 | +1.6 English, +3.0 Spanish, +3.8 German | German over the limit |
| meter 9 on shelf 6 (+2), with the overtime ramp | 40.3 | the same +1.6 / +3.0 / +3.8 | German over the limit |
| meter +1 on shelf 7 | 36.2 on shelf 7 (26.2) | +3.6 on shelf 7 | over the limit; shelf 7 is a twist shelf and a good player loses 7% of it to fire, so every step there is worth 3 or 4 points |
| fire every other turn on shelf 6 and 7 | 54.0 on shelf 7 | +4.4 on shelf 7 | over the limit |
| Lockdown pulse every 9, 7 or never on shelf 7 | 27 to 30 on shelf 7 | +0.7 at most | too small to matter |
| **overtime later on shelves 6 to 9 (24, 22, 20, 18) and meter 8 on shelf 6** | **36.1** | **+1.0** (+1.2 downward) | **chosen** |

Overtime is the lever with the best ratio: a competent player spends 15 to 18 turns on a Lockdown shelf, the weak bot 24, so
turns 17 to 24 are almost only ever played by the player who is losing. A meter step on the first shelf is cheap for the
strong bot at +1 and costs it 1.6 to 3.8 points from +2.

**Weak bot, clear rate (%) by shelf**

| shelf | en before | en after | es before | es after | de before | de after | fr before | fr after | fi before | fi after |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 85.5 | 85.5 | 80.9 | 80.9 | 78.8 | 78.8 | 80.4 | 80.4 | 78.1 | 78.1 |
| 4 | 74.7 | 74.7 | 73.1 | 73.1 | 67.4 | 67.4 | 73.7 | 73.7 | 69.9 | 69.9 |
| 5 | 61.5 | 61.5 | 57.1 | 57.1 | 53.0 | 53.0 | 58.5 | 58.5 | 57.0 | 57.0 |
| 6 | 24.9 | 36.1 | 26.3 | 36.8 | 22.7 | 30.8 | 25.6 | 33.2 | 26.9 | 32.7 |
| 7 | 26.2 | 30.2 | 29.5 | 31.9 | 29.3 | 30.5 | 29.4 | 30.8 | 27.9 | 29.6 |
| 8 | 20.1 | 22.8 | 20.4 | 24.1 | 15.8 | 16.3 | 18.7 | 22.4 | 16.1 | 17.7 |
| 9 | 10.1 | 11.5 | 10.9 | 14.7 | 8.6 | 9.2 | 10.3 | 12.7 | 9.0 | 11.7 |
| 10 | 17.8 | 17.8 | 18.1 | 18.1 | 13.4 | 13.4 | 17.2 | 17.2 | 16.4 | 16.4 |

**Competent bot, clear rate (%) by shelf**

Shelves 1 to 5 are identical before and after for both players (the rules do not change there), and shelves 10 and up are
identical (the ramp ends at shelf 9). The weak bot's worst fall between neighbouring shelves is cut by a quarter to a third
in every language (German 30 to 22 points, English 37 to 25); the step from shelf 5 to 6 is still the largest, because the
strong bot's budget (3 points on shelves 6 to 10) is smaller than what would flatten it: the variants that cut the fall to
16 to 21 points raised the German and Spanish competent bot by 3.0 to 4.9 points on shelf 6. The competent bot's change is
+1.0 or less on every shelf and language (shelf 8 moves by -1.2 in German, which is noise at this sample size).

### Daily modifiers, before / after

| modifier | competent clear % (before / after) | weak clear % (before / after), en | weak, es | weak, de | weak, fr | weak, fi |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.3 / 99.3 | 76 / 70 | 72 / 66 | 59 / 55 | 75 / 66 | 68 / 65 |
| EMBER | 99.3 / 99.3 | 60 / 67 | 61 / 69 | 46 / 52 | 65 / 62 | 56 / 66 |
| DEEP | 100.0 / 100.0 | 78 / 63 | 72 / 65 | 72 / 55 | 80 / 71 | 77 / 65 |
| LONG | 98.9 / 99.2 | 63 / 64 | 61 / 65 | 55 / 53 | 61 / 64 | 59 / 63 |

### English, after

**Competent bot, 300 fresh attempts per shelf**

| shelf | clear % | turns | turns (clears) | peels | peel hit % | guess % | words | avg word len | ignitions | score (clears) | FIRE % | SCORCH % | WORDS % | stall % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 11.9 | 11.9 | 11.4 | 35 | 34 | 0.5 | 6.56 | 0.7 | 2304 | 0.0 | 0.0 | 0.0 | 0.0 |
| 2 | 100.0 | 11.9 | 11.9 | 11.3 | 35 | 33 | 0.6 | 6.21 | 0.6 | 4405 | 0.0 | 0.0 | 0.0 | 0.0 |
| 3 | 100.0 | 12.2 | 12.2 | 11.8 | 42 | 20 | 0.4 | 6.86 | 0.9 | 6769 | 0.0 | 0.0 | 0.0 | 0.0 |
| 4 | 99.7 | 12.6 | 12.3 | 11.7 | 45 | 12 | 0.8 | 5.69 | 1.3 | 9162 | 0.0 | 0.0 | 0.0 | 0.3 |
| 5 | 98.3 | 13.4 | 13.0 | 12.1 | 41 | 21 | 1.3 | 5.68 | 1.4 | 11269 | 1.3 | 0.0 | 0.0 | 0.3 |
| 6 | 94.3 | 17.4 | 15.1 | 13.0 | 45 | 9 | 4.4 | 5.29 | 4.6 | 14205 | 4.7 | 0.0 | 0.0 | 1.0 |
| 7 | 94.3 | 17.0 | 15.2 | 12.9 | 48 | 8 | 4.1 | 5.33 | 5.8 | 16583 | 4.0 | 0.0 | 0.0 | 1.7 |
| 8 | 90.0 | 18.3 | 15.9 | 13.1 | 44 | 10 | 5.1 | 5.32 | 5.7 | 18625 | 8.7 | 0.0 | 0.0 | 1.3 |
| 9 | 86.3 | 24.3 | 19.2 | 15.0 | 46 | 11 | 9.3 | 5.16 | 9.0 | 21419 | 11.3 | 0.0 | 0.0 | 2.3 |
| 10 | 89.3 | 20.2 | 17.6 | 14.1 | 51 | 9 | 6.1 | 5.24 | 7.2 | 24073 | 10.0 | 0.0 | 0.0 | 0.7 |
| 11 | 81.0 | 26.6 | 20.4 | 14.9 | 45 | 11 | 11.7 | 5.13 | 9.9 | 25843 | 15.7 | 0.0 | 0.0 | 3.3 |
| 12 | 83.0 | 23.9 | 18.8 | 13.3 | 50 | 11 | 10.6 | 5.33 | 9.5 | 28727 | 16.3 | 0.0 | 0.0 | 0.7 |
| 13 | 74.0 | 27.6 | 19.7 | 14.3 | 55 | 10 | 13.2 | 5.24 | 11.0 | 32292 | 23.7 | 0.3 | 0.0 | 2.0 |
| 14 | 71.7 | 28.7 | 21.5 | 14.3 | 53 | 11 | 14.4 | 5.29 | 11.4 | 34196 | 26.0 | 0.0 | 0.0 | 2.3 |
| 15 | 57.7 | 27.9 | 21.4 | 13.6 | 52 | 13 | 14.4 | 5.37 | 9.7 | 36871 | 39.7 | 0.3 | 0.0 | 2.3 |
| 16 | 58.0 | 27.3 | 19.7 | 13.8 | 55 | 12 | 13.6 | 5.27 | 9.4 | 39567 | 41.0 | 0.0 | 0.0 | 1.0 |
| 17 | 52.7 | 32.6 | 22.1 | 13.9 | 51 | 14 | 18.7 | 5.36 | 11.4 | 41313 | 46.7 | 0.0 | 0.0 | 0.7 |
| 18 | 57.7 | 28.7 | 20.6 | 13.6 | 52 | 12 | 15.1 | 5.40 | 10.1 | 44105 | 42.0 | 0.3 | 0.0 | 0.0 |
| 19 | 48.0 | 29.8 | 21.4 | 13.6 | 54 | 12 | 16.2 | 5.31 | 10.3 | 46391 | 50.7 | 0.7 | 0.0 | 0.7 |
| 20 | 43.3 | 28.7 | 21.3 | 13.3 | 51 | 12 | 15.5 | 5.34 | 10.1 | 48710 | 54.3 | 0.3 | 0.0 | 2.0 |
| 21 | 42.3 | 29.7 | 23.5 | 13.2 | 51 | 13 | 16.5 | 5.41 | 10.4 | 49890 | 57.0 | 0.3 | 0.0 | 0.3 |
| 22 | 49.3 | 28.8 | 19.6 | 13.6 | 55 | 12 | 15.2 | 5.26 | 9.8 | 53861 | 50.0 | 0.3 | 0.0 | 0.3 |
| 23 | 36.0 | 30.1 | 22.4 | 13.0 | 51 | 13 | 17.1 | 5.43 | 10.6 | 54861 | 63.7 | 0.0 | 0.0 | 0.3 |
| 24 | 40.7 | 31.3 | 20.7 | 13.3 | 51 | 14 | 18.0 | 5.40 | 11.1 | 57985 | 58.3 | 0.7 | 0.0 | 0.3 |

**Weak bot, 300 fresh attempts per shelf**

| shelf | clear % | turns | turns (clears) | peels | peel hit % | guess % | words | avg word len | ignitions | score (clears) | FIRE % | SCORCH % | WORDS % | stall % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 92.0 | 24.3 | 23.5 | 14.3 | 27 | 20 | 10.1 | 4.47 | 3.0 | 3163 | 7.7 | 0.0 | 0.0 | 0.3 |
| 2 | 91.7 | 23.5 | 22.6 | 14.3 | 27 | 19 | 9.2 | 4.45 | 2.8 | 4912 | 8.0 | 0.0 | 0.3 | 0.0 |
| 3 | 87.7 | 25.2 | 24.5 | 16.1 | 30 | 16 | 9.1 | 4.42 | 3.3 | 6950 | 11.7 | 0.0 | 0.0 | 0.7 |
| 4 | 76.7 | 22.3 | 22.4 | 14.8 | 33 | 13 | 7.5 | 4.39 | 3.5 | 9007 | 23.3 | 0.0 | 0.0 | 0.0 |
| 5 | 61.0 | 24.9 | 23.9 | 14.8 | 28 | 17 | 10.0 | 4.42 | 4.4 | 10797 | 38.7 | 0.0 | 0.3 | 0.0 |
| 6 | 21.0 | 24.1 | 27.2 | 13.3 | 29 | 18 | 10.8 | 4.36 | 7.5 | 12944 | 78.7 | 0.0 | 0.0 | 0.3 |
| 7 | 25.0 | 23.0 | 21.4 | 13.7 | 32 | 13 | 9.3 | 4.27 | 8.4 | 15576 | 74.7 | 0.0 | 0.0 | 0.3 |
| 8 | 18.3 | 23.8 | 25.3 | 12.8 | 29 | 19 | 11.1 | 4.38 | 8.5 | 17078 | 81.7 | 0.0 | 0.0 | 0.0 |
| 9 | 10.0 | 22.4 | 24.7 | 12.8 | 30 | 15 | 9.6 | 4.35 | 8.3 | 19798 | 89.7 | 0.0 | 0.0 | 0.3 |
| 10 | 19.0 | 21.3 | 22.0 | 13.4 | 35 | 10 | 7.9 | 4.29 | 7.6 | 22634 | 81.0 | 0.0 | 0.0 | 0.0 |
| 11 | 6.0 | 22.3 | 26.7 | 11.6 | 31 | 17 | 10.7 | 4.35 | 8.4 | 23932 | 94.0 | 0.0 | 0.0 | 0.0 |
| 12 | 5.3 | 20.8 | 22.9 | 10.8 | 32 | 19 | 10.0 | 4.34 | 8.1 | 26933 | 94.7 | 0.0 | 0.0 | 0.0 |
| 13 | 7.7 | 21.6 | 23.4 | 12.1 | 38 | 8 | 9.5 | 4.28 | 8.0 | 30360 | 92.3 | 0.0 | 0.0 | 0.0 |
| 14 | 1.7 | 20.0 | 24.8 | 10.3 | 34 | 17 | 9.7 | 4.38 | 7.7 | 31898 | 98.3 | 0.0 | 0.0 | 0.0 |
| 15 | 0.0 | 18.6 | 0.0 | 9.8 | 33 | 17 | 8.8 | 4.38 | 6.6 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 16 | 4.7 | 18.8 | 24.0 | 10.8 | 39 | 9 | 8.0 | 4.32 | 6.1 | 37017 | 95.3 | 0.0 | 0.0 | 0.0 |
| 17 | 0.0 | 18.2 | 0.0 | 9.3 | 34 | 17 | 8.9 | 4.36 | 6.4 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 18 | 1.0 | 18.5 | 21.3 | 9.2 | 35 | 18 | 9.3 | 4.38 | 6.4 | 42450 | 99.0 | 0.0 | 0.0 | 0.0 |
| 19 | 2.0 | 18.0 | 18.8 | 10.4 | 38 | 9 | 7.5 | 4.32 | 5.8 | 45447 | 98.0 | 0.0 | 0.0 | 0.0 |
| 20 | 1.0 | 18.3 | 21.7 | 8.9 | 32 | 19 | 9.4 | 4.40 | 6.4 | 46530 | 99.0 | 0.0 | 0.0 | 0.0 |
| 21 | 0.0 | 19.1 | 0.0 | 8.8 | 34 | 19 | 10.3 | 4.36 | 6.7 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 22 | 1.3 | 17.6 | 17.8 | 10.1 | 38 | 9 | 7.5 | 4.36 | 5.5 | 52819 | 98.7 | 0.0 | 0.0 | 0.0 |
| 23 | 0.7 | 17.8 | 25.5 | 8.7 | 35 | 19 | 9.1 | 4.39 | 6.1 | 51530 | 99.3 | 0.0 | 0.0 | 0.0 |
| 24 | 0.3 | 17.8 | 16.0 | 8.8 | 34 | 19 | 9.0 | 4.39 | 6.2 | 58635 | 99.7 | 0.0 | 0.0 | 0.0 |


### English, competent against score-seeking, after

**Competent against score-seeking** (the score-seeking bot peels only when a cell is likely, so it spells more words; `combo pts` is what the combo bonus added per attempt, `best combo` the longest streak)

| shelf | competent clear % | competent score | competent combo pts | scorer clear % | scorer score | scorer combo pts | scorer best combo | shuffles used |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 2304 | 3 | 84.7 | 9656 | 1751 | 14.8 | 0.000 |
| 3 | 100.0 | 6769 | 1 | 79.3 | 12384 | 1762 | 15.9 | 0.000 |
| 5 | 98.3 | 11147 | 19 | 73.0 | 14980 | 1815 | 15.7 | 0.000 |
| 8 | 90.0 | 17311 | 46 | 60.0 | 16515 | 1388 | 16.2 | 0.000 |
| 10 | 89.3 | 22334 | 70 | 45.0 | 18115 | 1318 | 16.1 | 0.000 |
| 12 | 83.0 | 25462 | 143 | 39.3 | 19494 | 1410 | 16.3 | 0.000 |
| 15 | 57.7 | 26117 | 251 | 25.7 | 21097 | 1481 | 17.1 | 0.000 |
| 18 | 57.7 | 31443 | 290 | 26.0 | 23558 | 1441 | 15.4 | 0.000 |
| 20 | 43.3 | 29557 | 301 | 20.0 | 23527 | 1373 | 14.9 | 0.000 |
| 24 | 40.7 | 34259 | 344 | 15.3 | 24786 | 1324 | 14.7 | 0.000 |


### Runs, Daily and diagnostics, after

**en**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 19.7 shelves on average (score 511942), weak bot 5.5 shelves (score 43867).

Highest shelf cleared, competent: 11:1 13:1 14:6 15:8 16:5 17:10 18:13 19:7 20:9 21:9 22:10 23:8 24:4 25:2 28:4 31:1 32:1 35:1 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.0 | 14.5 | 0.7 | 0.0 | 0.0 | 70.0 |
| EMBER | 99.7 | 14.2 | 0.3 | 0.0 | 0.0 | 66.7 |
| DEEP | 100.0 | 12.0 | 0.0 | 0.0 | 0.0 | 62.9 |
| LONG | 98.7 | 16.6 | 0.3 | 0.0 | 0.0 | 64.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 1875, of which 130 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 13.7% of all peels.



**es**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 21.0 shelves on average (score 609474), weak bot 5.8 shelves (score 50736).

Highest shelf cleared, competent: 12:2 13:3 14:3 15:7 16:7 17:5 18:10 19:7 20:6 21:4 22:8 23:7 24:5 25:6 26:7 27:2 28:4 29:1 31:2 32:2 33:1 34:1 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.0 | 14.0 | 0.7 | 0.0 | 0.0 | 65.7 |
| EMBER | 98.7 | 13.5 | 1.3 | 0.0 | 0.0 | 69.0 |
| DEEP | 100.0 | 11.9 | 0.0 | 0.0 | 0.0 | 64.6 |
| LONG | 99.3 | 15.8 | 0.7 | 0.0 | 0.0 | 65.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 1610, of which 204 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 12.8% of all peels.



**de**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 16.2 shelves on average (score 358992), weak bot 4.6 shelves (score 33436).

Highest shelf cleared, competent: 8:1 9:2 10:5 11:3 12:4 13:5 14:12 15:7 16:12 17:16 18:7 19:8 20:7 21:5 22:2 24:1 25:3 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.7 | 13.6 | 0.3 | 0.0 | 0.0 | 55.0 |
| EMBER | 99.3 | 13.9 | 0.7 | 0.0 | 0.0 | 52.0 |
| DEEP | 100.0 | 11.8 | 0.0 | 0.0 | 0.0 | 55.1 |
| LONG | 99.3 | 15.5 | 0.7 | 0.0 | 0.0 | 53.0 |

**Diagnostics (competent, all shelves)**: FIRE fails 2459, of which 375 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 12.7% of all peels.



**fr**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 21.2 shelves on average (score 626135), weak bot 5.4 shelves (score 45780).

Highest shelf cleared, competent: 12:1 13:2 14:5 15:6 16:5 17:7 18:9 19:7 20:12 21:6 22:7 23:5 24:5 25:6 26:4 27:1 28:1 29:1 30:3 31:2 32:1 34:1 38:1 39:2 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.0 | 14.5 | 0.3 | 0.0 | 0.0 | 66.0 |
| EMBER | 99.7 | 13.8 | 0.0 | 0.0 | 0.0 | 62.3 |
| DEEP | 100.0 | 12.1 | 0.0 | 0.0 | 0.0 | 70.7 |
| LONG | 99.3 | 16.0 | 0.0 | 0.0 | 0.0 | 64.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 1604, of which 162 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 12.9% of all peels.



**fi**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 22.5 shelves on average (score 729546), weak bot 5.2 shelves (score 45077).

Highest shelf cleared, competent: 8:1 10:1 12:2 14:3 15:9 16:6 17:4 18:9 19:4 20:6 21:10 22:1 23:6 24:6 25:7 26:3 27:3 28:2 29:3 30:1 31:3 32:1 39:9 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.7 | 13.9 | 0.0 | 0.0 | 0.0 | 64.7 |
| EMBER | 99.3 | 13.5 | 0.7 | 0.0 | 0.0 | 65.7 |
| DEEP | 100.0 | 11.9 | 0.0 | 0.0 | 0.0 | 64.6 |
| LONG | 99.3 | 15.5 | 0.3 | 0.0 | 0.0 | 62.7 |

**Diagnostics (competent, all shelves)**: FIRE fails 1516, of which 213 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 12.6% of all peels.



### Fresh boards (400 deals per language)

| language | vowels per 56 | fewest vowels | boards under 14 vowels | 3-5 letter word paths | boards under 25 paths | boards without a word |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| en | 23.8 | 16 | 0.0% | 382 | 0.0% | 0.00% |
| es | 24.1 | 16 | 0.0% | 564 | 0.0% | 0.00% |
| fr | 24.1 | 15 | 0.0% | 452 | 0.0% | 0.00% |
| de | 21.4 | 15 | 0.0% | 350 | 0.0% | 0.00% |
| it | 25.9 | 19 | 0.0% | 494 | 0.0% | 0.00% |
| pt | 25.7 | 18 | 0.0% | 779 | 0.0% | 0.00% |
| nl | 21.3 | 16 | 0.0% | 430 | 0.0% | 0.00% |
| sv | 19.3 | 13 | 0.5% | 519 | 0.0% | 0.00% |
| pl | 23.9 | 16 | 0.0% | 407 | 0.0% | 0.00% |
| da | 21.2 | 15 | 0.0% | 517 | 0.0% | 0.00% |
| fi | 27.1 | 19 | 0.0% | 474 | 0.0% | 0.00% |




## Data, before any change

### English in full, before

**Competent bot, 300 fresh attempts per shelf**

| shelf | clear % | turns | turns (clears) | peels | peel hit % | guess % | words | avg word len | ignitions | score (clears) | FIRE % | SCORCH % | WORDS % | stall % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 11.9 | 11.9 | 11.4 | 35 | 34 | 0.5 | 6.56 | 0.7 | 2301 | 0.0 | 0.0 | 0.0 | 0.0 |
| 2 | 100.0 | 11.9 | 11.9 | 11.3 | 35 | 33 | 0.6 | 6.24 | 0.6 | 4411 | 0.0 | 0.0 | 0.0 | 0.0 |
| 3 | 100.0 | 12.3 | 12.3 | 11.8 | 42 | 20 | 0.6 | 6.41 | 1.1 | 6829 | 0.0 | 0.0 | 0.0 | 0.0 |
| 4 | 98.7 | 13.2 | 12.4 | 11.8 | 45 | 13 | 1.4 | 5.48 | 1.7 | 9216 | 0.7 | 0.0 | 0.0 | 0.7 |
| 5 | 98.0 | 13.5 | 12.9 | 12.1 | 41 | 21 | 1.4 | 5.51 | 1.5 | 11270 | 1.7 | 0.0 | 0.0 | 0.3 |
| 6 | 93.3 | 18.1 | 15.8 | 13.0 | 45 | 10 | 5.1 | 5.35 | 5.5 | 14654 | 4.3 | 0.0 | 0.0 | 2.3 |
| 7 | 93.0 | 17.3 | 15.0 | 12.9 | 49 | 9 | 4.5 | 5.28 | 5.7 | 16880 | 5.0 | 0.0 | 0.0 | 2.0 |
| 8 | 91.0 | 19.6 | 16.6 | 13.3 | 44 | 10 | 6.4 | 5.27 | 6.0 | 19089 | 6.0 | 0.0 | 0.0 | 3.0 |
| 9 | 90.3 | 24.6 | 19.5 | 14.9 | 46 | 11 | 9.7 | 5.10 | 8.5 | 22116 | 5.0 | 0.3 | 0.0 | 4.3 |
| 10 | 90.0 | 20.9 | 18.1 | 14.1 | 51 | 9 | 6.8 | 5.18 | 7.0 | 24695 | 6.3 | 0.0 | 0.0 | 3.7 |
| 11 | 88.0 | 25.0 | 20.1 | 15.0 | 45 | 11 | 10.0 | 5.11 | 8.6 | 26685 | 7.0 | 0.0 | 0.0 | 5.0 |
| 12 | 69.7 | 28.2 | 21.3 | 13.0 | 49 | 12 | 15.2 | 5.36 | 9.8 | 30524 | 23.3 | 0.0 | 0.0 | 7.0 |
| 13 | 67.0 | 28.0 | 20.7 | 13.8 | 55 | 11 | 14.2 | 5.25 | 9.7 | 33680 | 25.7 | 0.0 | 0.0 | 7.3 |
| 14 | 64.3 | 30.7 | 24.2 | 14.0 | 52 | 12 | 16.8 | 5.36 | 10.9 | 36161 | 30.3 | 0.0 | 0.0 | 5.3 |
| 15 | 57.3 | 30.5 | 24.7 | 13.5 | 52 | 13 | 17.0 | 5.32 | 8.9 | 38680 | 36.7 | 0.0 | 0.0 | 6.0 |
| 16 | 57.7 | 31.1 | 22.0 | 13.5 | 55 | 12 | 17.6 | 5.24 | 8.4 | 41156 | 33.0 | 0.0 | 0.0 | 9.3 |
| 17 | 61.0 | 32.1 | 23.8 | 13.8 | 52 | 12 | 18.3 | 5.34 | 9.5 | 43000 | 32.0 | 0.0 | 0.0 | 7.0 |
| 18 | 55.7 | 32.2 | 24.1 | 13.5 | 52 | 13 | 18.7 | 5.34 | 9.4 | 45309 | 35.0 | 1.3 | 0.0 | 8.0 |
| 19 | 55.3 | 33.1 | 23.0 | 13.9 | 54 | 13 | 19.2 | 5.16 | 9.1 | 47879 | 35.0 | 0.3 | 0.0 | 9.3 |
| 20 | 58.3 | 33.0 | 23.5 | 13.9 | 51 | 14 | 19.0 | 5.33 | 9.5 | 49463 | 34.7 | 0.3 | 0.0 | 6.7 |
| 21 | 55.7 | 32.4 | 24.0 | 13.7 | 51 | 13 | 18.7 | 5.28 | 9.3 | 51675 | 36.7 | 0.7 | 0.0 | 7.0 |
| 22 | 58.0 | 32.7 | 21.9 | 13.8 | 54 | 13 | 18.9 | 5.17 | 8.9 | 55043 | 31.7 | 0.0 | 0.0 | 10.3 |
| 23 | 58.0 | 34.3 | 24.4 | 13.7 | 52 | 13 | 20.6 | 5.31 | 9.8 | 56182 | 31.7 | 0.0 | 0.0 | 10.3 |
| 24 | 53.7 | 34.8 | 23.4 | 13.9 | 51 | 15 | 20.9 | 5.29 | 9.9 | 58653 | 35.3 | 0.0 | 0.0 | 11.0 |

**Weak bot, 300 fresh attempts per shelf**

| shelf | clear % | turns | turns (clears) | peels | peel hit % | guess % | words | avg word len | ignitions | score (clears) | FIRE % | SCORCH % | WORDS % | stall % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 92.3 | 24.3 | 23.4 | 14.3 | 27 | 20 | 10.1 | 4.45 | 2.8 | 3023 | 7.0 | 0.0 | 0.0 | 0.7 |
| 2 | 92.3 | 23.3 | 22.8 | 14.3 | 27 | 19 | 8.9 | 4.48 | 2.5 | 4802 | 7.3 | 0.0 | 0.3 | 0.0 |
| 3 | 79.0 | 24.7 | 23.4 | 15.7 | 29 | 16 | 9.0 | 4.41 | 4.0 | 6850 | 20.0 | 0.0 | 0.3 | 0.7 |
| 4 | 70.3 | 22.7 | 21.9 | 14.8 | 32 | 13 | 7.9 | 4.38 | 4.1 | 9024 | 29.7 | 0.0 | 0.0 | 0.0 |
| 5 | 62.3 | 25.4 | 24.8 | 15.0 | 28 | 17 | 10.4 | 4.40 | 4.3 | 10806 | 37.3 | 0.0 | 0.0 | 0.3 |
| 6 | 21.7 | 23.2 | 27.1 | 12.6 | 29 | 18 | 10.6 | 4.33 | 8.0 | 13759 | 78.0 | 0.0 | 0.0 | 0.3 |
| 7 | 30.7 | 23.9 | 23.8 | 13.7 | 32 | 12 | 10.1 | 4.17 | 7.1 | 16022 | 67.0 | 0.0 | 0.0 | 2.3 |
| 8 | 22.3 | 25.4 | 27.3 | 13.2 | 29 | 18 | 12.2 | 4.29 | 8.9 | 17617 | 76.3 | 0.0 | 0.0 | 1.3 |
| 9 | 16.7 | 23.2 | 30.4 | 13.1 | 31 | 15 | 10.1 | 4.31 | 8.2 | 20133 | 82.3 | 0.0 | 0.0 | 1.0 |
| 10 | 21.3 | 21.7 | 24.4 | 13.5 | 35 | 10 | 8.2 | 4.23 | 7.3 | 22895 | 78.0 | 0.0 | 0.0 | 0.7 |
| 11 | 9.0 | 24.1 | 26.8 | 12.6 | 30 | 16 | 11.5 | 4.27 | 8.4 | 24573 | 89.3 | 0.0 | 0.0 | 1.7 |
| 12 | 1.7 | 18.8 | 24.0 | 9.4 | 32 | 21 | 9.5 | 4.37 | 6.9 | 27258 | 98.3 | 0.0 | 0.0 | 0.0 |
| 13 | 4.0 | 18.7 | 28.8 | 11.0 | 38 | 8 | 7.7 | 4.30 | 6.6 | 30805 | 96.0 | 0.0 | 0.0 | 0.0 |
| 14 | 0.7 | 18.9 | 20.0 | 9.3 | 34 | 18 | 9.5 | 4.38 | 6.9 | 33522 | 99.3 | 0.0 | 0.0 | 0.0 |
| 15 | 0.3 | 18.9 | 24.0 | 9.3 | 34 | 17 | 9.6 | 4.36 | 6.1 | 34295 | 99.7 | 0.0 | 0.0 | 0.0 |
| 16 | 2.7 | 17.9 | 20.2 | 10.1 | 38 | 9 | 7.8 | 4.34 | 5.2 | 38220 | 97.0 | 0.0 | 0.0 | 0.3 |
| 17 | 0.7 | 17.7 | 25.5 | 9.1 | 36 | 18 | 8.6 | 4.38 | 5.8 | 39090 | 99.3 | 0.0 | 0.0 | 0.0 |
| 18 | 0.0 | 18.0 | 0.0 | 9.0 | 34 | 18 | 9.0 | 4.39 | 5.9 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 19 | 2.0 | 18.3 | 23.3 | 10.3 | 37 | 9 | 8.0 | 4.32 | 5.4 | 44040 | 97.7 | 0.0 | 0.0 | 0.3 |
| 20 | 0.0 | 18.2 | 0.0 | 8.9 | 33 | 19 | 9.4 | 4.40 | 6.0 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 21 | 1.3 | 19.1 | 25.8 | 9.0 | 33 | 19 | 10.1 | 4.36 | 6.2 | 47795 | 98.7 | 0.0 | 0.0 | 0.0 |
| 22 | 2.3 | 18.7 | 23.6 | 10.5 | 38 | 10 | 8.2 | 4.28 | 5.2 | 50811 | 97.7 | 0.0 | 0.0 | 0.0 |
| 23 | 0.3 | 18.3 | 27.0 | 9.0 | 34 | 18 | 9.4 | 4.37 | 6.0 | 51570 | 99.7 | 0.0 | 0.0 | 0.0 |
| 24 | 0.0 | 18.2 | 0.0 | 9.1 | 34 | 18 | 9.0 | 4.38 | 5.9 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |


**en, before**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 22.6 shelves on average (score 752007), weak bot 5.1 shelves (score 37347).

Highest shelf cleared, competent: 11:2 12:1 13:4 14:9 15:7 16:8 17:7 18:3 19:7 20:8 21:3 22:1 23:2 24:2 25:6 26:2 27:1 28:4 29:4 30:1 31:2 32:1 35:1 37:1 39:13 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.3 | 14.2 | 0.3 | 0.0 | 0.0 | 75.7 |
| EMBER | 99.3 | 14.3 | 0.7 | 0.0 | 0.0 | 60.0 |
| DEEP | 100.0 | 11.6 | 0.0 | 0.0 | 0.0 | 77.6 |
| LONG | 98.7 | 17.2 | 0.0 | 0.0 | 0.0 | 63.0 |

**Diagnostics (competent, all shelves)**: FIRE fails 1371, of which 276 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 14.1% of all peels.



**es, before**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 28.6 shelves on average (score 1223242), weak bot 5.2 shelves (score 43170).

Highest shelf cleared, competent: 8:1 9:1 11:1 12:2 13:2 14:1 15:3 16:5 17:4 18:2 19:3 20:2 21:5 22:4 23:2 24:2 25:2 26:2 27:2 28:1 29:1 30:5 32:3 33:3 34:2 35:3 36:2 37:1 38:4 39:29 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.3 | 14.1 | 0.3 | 0.0 | 0.0 | 71.7 |
| EMBER | 99.3 | 13.6 | 0.7 | 0.0 | 0.0 | 61.3 |
| DEEP | 100.0 | 11.8 | 0.0 | 0.0 | 0.0 | 72.4 |
| LONG | 99.3 | 16.3 | 0.0 | 0.0 | 0.0 | 61.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 1269, of which 305 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 13.3% of all peels.



**de, before**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 15.8 shelves on average (score 356389), weak bot 4.4 shelves (score 30978).

Highest shelf cleared, competent: 8:1 9:1 10:1 11:8 12:8 13:14 14:16 15:7 16:9 17:11 18:6 19:2 20:4 21:1 22:3 23:3 24:1 26:2 27:1 38:1 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 100.0 | 13.4 | 0.0 | 0.0 | 0.0 | 58.7 |
| EMBER | 98.7 | 14.1 | 1.0 | 0.0 | 0.0 | 46.0 |
| DEEP | 100.0 | 11.8 | 0.0 | 0.0 | 0.0 | 72.1 |
| LONG | 98.7 | 16.2 | 1.0 | 0.0 | 0.0 | 55.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 2240, of which 664 had no word through any flame near the floor on the last look; OUT OF WORDS fails 3 (average 42.7 chainable gems and 13.3 holes on the board at the end); guess peels (chance under 20%) 13.4% of all peels.



**fr, before**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 26.3 shelves on average (score 1064248), weak bot 5.3 shelves (score 44572).

Highest shelf cleared, competent: 11:3 12:1 13:4 14:4 15:8 16:5 17:4 18:2 19:6 20:7 21:2 22:3 23:3 25:2 27:2 30:2 31:3 32:2 33:4 35:3 36:2 37:2 38:1 39:25 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 97.7 | 14.8 | 1.0 | 0.0 | 0.0 | 75.3 |
| EMBER | 99.3 | 14.5 | 0.3 | 0.0 | 0.0 | 64.7 |
| DEEP | 100.0 | 11.8 | 0.0 | 0.0 | 0.0 | 79.9 |
| LONG | 98.3 | 16.5 | 1.0 | 0.0 | 0.0 | 61.0 |

**Diagnostics (competent, all shelves)**: FIRE fails 1181, of which 275 had no word through any flame near the floor on the last look; OUT OF WORDS fails 1 (average 30.0 chainable gems and 26.0 holes on the board at the end); guess peels (chance under 20%) 13.4% of all peels.



**fi, before**

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 27.6 shelves on average (score 1160983), weak bot 5.0 shelves (score 41567).

Highest shelf cleared, competent: 11:3 13:1 14:7 15:2 16:7 17:2 18:2 19:3 20:5 21:3 22:5 23:6 24:1 25:1 26:3 27:1 28:3 29:2 31:1 32:3 33:4 34:1 36:1 37:2 38:2 39:29 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 100.0 | 13.7 | 0.0 | 0.0 | 0.0 | 67.7 |
| EMBER | 99.7 | 14.1 | 0.0 | 0.0 | 0.0 | 56.0 |
| DEEP | 100.0 | 11.8 | 0.0 | 0.0 | 0.0 | 76.9 |
| LONG | 99.3 | 15.7 | 0.7 | 0.0 | 0.0 | 58.7 |

**Diagnostics (competent, all shelves)**: FIRE fails 1240, of which 371 had no word through any flame near the floor on the last look; OUT OF WORDS fails 5 (average 41.0 chainable gems and 15.0 holes on the board at the end); guess peels (chance under 20%) 13.3% of all peels.


