# Balance report

Measured with `tools/check/bot.c`, a native player that drives the real rules code (`source/board.c`, the dictionaries, the
pass pools, the letter bag) headlessly. Build it with `make bot`, run all languages with `make bot-run` (a minute or two on
four cores), or one language with `build/bot --lang en --n 300`. `tools/check/balance_md.py` lays the output out as the
tables below. A tiny `--selftest` of it runs inside `make check`.

## The players

Both players read only what a human sees: the letters, the flames, the heat meter, the numbers on holes and the diamonds
they have found (never the back layer). Every shelf is played from a fresh seed, 300 attempts per shelf and language
(`--n`), on FIRE NORMAL with the dictionary on FULL; a "clear" is the pass found before the shelf ends.

* **Competent.** Looks for every word on the board (a trie of the whole dictionary) and notices a common word of three or
  four letters 95% of the time, five or more 70%, a rare-tier word 10%. It prefers long words (heat 0 at seven letters),
  and it reads the fire: a flame that will reach the floor in three turns or fewer sends it after a word through that
  flame (92% to notice it), and as a last resort it peels a burning gem no word reaches. It peels like a Minesweeper
  solver: from each hole number it works out which neighbours are safe or certain, it knows the pass is one connected
  cluster buried below the shelf's first pass row and grows from the diamonds it has found, and it peels the most likely
  cell (never one a flame would scorch) whenever no flame is urgent. 10% of turns it looks away from the fire.
* **Weak.** Words of three to five letters only (it notices 55% of the short ones, 20% of the longer), no sense of heat,
  pays attention to a flame 60% of the turns and then only when it is two turns from the floor, and peels beside numbers
  above zero and beside diamonds without any deduction.

Columns: **turns** counts words and peels; **peel hit** is the share of peels that found a pass letter; **guess** is the
share of peels taken with under a 20% chance of finding one; **ignitions** is how many times the meter filled; **stall**
means 100 turns passed with the shelf neither won nor lost (see below).

## Findings, before any change

1. **The ladder is not smooth.** Competent clear rate (English): 100% on shelves 1 to 3, 90 to 95% on shelves 4 to 11, then
   a cliff of 15 points at shelf 12 (Lockdown tier III: two flames per ignition, meter 4, pass in the bottom three rows)
   and a flat 64 to 70% from shelf 15 to 24, because the tiers stop at 15 and the pass length stops at 13. The weak bot
   has a cliff at shelf 6 (62% to 22%: Lockdown) and is dead (about 1%) from shelf 12. Nothing rises after shelf 15.
2. **Shelf 1 to 5 cannot be lost by a competent player (about 100%).** The pass can be won by peeling alone (about 11
   peels for a four-letter pass), the meter of 10 fills once, and the first flame needs 16 turns to arrive.
3. **Peeling alone wins.** Words are only ever needed to put flames out. A competent run uses 0.5 words a shelf on shelves
   1 to 5. Nothing rewards a word beyond its score.
4. **Overtime does not end a shelf, it stalls it.** The meter can light at most one gem (two from tier II) a turn and the
   player puts one flame out per word, so a player who keeps up never dies: 10 to 15% of competent attempts on shelves 12
   and up are still going after 100 turns, and the weak bot stalls on 18 to 20% of shelves 1 and 2.
5. **Flames stack on diamonds and on the pass.** Diamonds are firebreaks, so a flame parks on top of one, the next ignition in
   that column lights the gem above it, and a stack of two to four flames can sit on an unfound pass letter: the gem is
   burning (a peel scorches it), the flame cannot move, and after the word that clears it the next flame of the stack
   steps down onto the cell the same turn. Most of the stalls above are this.
6. **A fifth to a third of FIRE losses were unpreventable on the last look** (English 22%, German 33%): the flame nearest
   the floor had no word through it. German is 15 to 20 points harder than the other languages from shelf 12 on (53% against
   70 to 75%) because its board holds a third as many three-to-five-letter words (a median of 214 paths against 382 to 780).
7. **OUT OF WORDS never happened.** Not once in 36,000 competent and 36,000 weak shelves per language. Fresh boards hold at
   least 13 vowels of 56 and 200 or more word paths in every language (table below), so there is no vowel starvation. The
   fail exists only as a theoretical dead end; it is not worth a rule, but a one-turn warning costs nothing (see the shuffle).
8. **Daily modifiers.** A competent player clears every Daily (98.7 to 100%): it is a shelf 1 with a bigger meter. For the weak
   bot LONG (60%) and EMBER (65%) are hard and DEEP (80%) is easier than STANDARD (74%), because a pass buried in three rows
   gives the weak solver a smaller area to guess in. DEEP is meant to be the harder one.
9. **Pass placement is not pure guessing.** The pass is a connected cluster, so once a number or a diamond is on the board a
   solver finds the next letter at 50 to 60% per peel; only 1 to 5% of a competent solver's peels from shelf 6 are guesses
   (under 20%), and 30% on shelves 1 and 2, where nothing is known yet (first peels).

### Fresh boards (400 deals per language)

| language | vowels per 56 | fewest vowels | boards under 14 vowels | 3-5 letter word paths | boards under 25 paths | boards without a word |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| en | 23.8 | 16 | 0.0% | 382 | 0.0% | 0.00% |
| es | 24.1 | 16 | 0.0% | 564 | 0.0% | 0.00% |
| fr | 24.1 | 15 | 0.0% | 452 | 0.0% | 0.00% |
| de | 20.3 | 13 | 0.2% | 214 | 0.0% | 0.00% |
| it | 25.9 | 19 | 0.0% | 494 | 0.0% | 0.00% |
| pt | 25.7 | 18 | 0.0% | 779 | 0.0% | 0.00% |
| nl | 21.3 | 16 | 0.0% | 430 | 0.0% | 0.00% |
| sv | 19.3 | 13 | 0.5% | 519 | 0.0% | 0.00% |
| pl | 23.9 | 16 | 0.0% | 407 | 0.0% | 0.00% |
| da | 21.2 | 15 | 0.0% | 517 | 0.0% | 0.00% |
| fi | 27.1 | 19 | 0.0% | 474 | 0.0% | 0.00% |

## Data, before any change

**Competent bot: clear rate (%) by shelf and language**

| shelf | en | es | de | fr | fi |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 100.0 | 99.7 | 99.7 | 100.0 |
| 2 | 100.0 | 100.0 | 100.0 | 100.0 | 100.0 |
| 3 | 100.0 | 100.0 | 100.0 | 99.7 | 99.7 |
| 4 | 99.3 | 99.3 | 99.0 | 99.3 | 100.0 |
| 5 | 99.0 | 99.3 | 98.0 | 99.0 | 99.0 |
| 6 | 95.0 | 94.3 | 89.7 | 96.7 | 96.0 |
| 7 | 95.0 | 97.0 | 94.7 | 97.3 | 98.7 |
| 8 | 94.7 | 95.0 | 90.0 | 96.0 | 93.0 |
| 9 | 91.0 | 92.0 | 84.3 | 90.3 | 91.3 |
| 10 | 93.0 | 94.7 | 88.0 | 93.3 | 91.7 |
| 11 | 90.7 | 94.7 | 86.3 | 92.7 | 92.7 |
| 12 | 76.3 | 81.7 | 60.3 | 80.3 | 81.0 |
| 13 | 76.3 | 80.7 | 67.7 | 82.0 | 80.0 |
| 14 | 76.3 | 78.0 | 58.7 | 70.7 | 82.7 |
| 15 | 70.0 | 72.0 | 53.7 | 70.3 | 73.7 |
| 16 | 69.0 | 71.7 | 58.7 | 74.7 | 73.0 |
| 17 | 69.0 | 73.7 | 54.0 | 72.0 | 74.0 |
| 18 | 69.0 | 74.0 | 58.7 | 70.7 | 71.7 |
| 19 | 69.0 | 74.7 | 53.7 | 76.0 | 72.7 |
| 20 | 70.3 | 70.0 | 52.3 | 71.7 | 74.7 |
| 21 | 66.7 | 70.0 | 53.7 | 70.3 | 72.3 |
| 22 | 69.3 | 71.0 | 49.7 | 75.0 | 71.7 |
| 23 | 67.0 | 72.3 | 54.7 | 69.7 | 76.7 |
| 24 | 64.3 | 67.3 | 53.7 | 71.3 | 73.3 |

**Weak bot: clear rate (%) by shelf and language**

| shelf | en | es | de | fr | fi |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 65.7 | 63.3 | 59.3 | 61.3 | 63.7 |
| 2 | 69.7 | 67.3 | 63.3 | 66.7 | 66.0 |
| 3 | 79.0 | 79.7 | 64.7 | 81.0 | 76.3 |
| 4 | 67.0 | 64.3 | 58.7 | 62.7 | 66.3 |
| 5 | 61.3 | 60.0 | 50.3 | 61.7 | 62.0 |
| 6 | 21.7 | 22.0 | 14.3 | 25.0 | 20.7 |
| 7 | 31.7 | 30.7 | 21.3 | 28.7 | 33.0 |
| 8 | 25.7 | 23.3 | 11.7 | 22.3 | 18.7 |
| 9 | 16.3 | 13.3 | 9.3 | 17.0 | 13.3 |
| 10 | 28.3 | 24.7 | 9.7 | 20.3 | 19.7 |
| 11 | 14.3 | 15.3 | 8.3 | 16.3 | 10.0 |
| 12 | 2.0 | 3.3 | 0.3 | 1.7 | 2.7 |
| 13 | 4.0 | 5.3 | 1.7 | 6.0 | 4.0 |
| 14 | 1.7 | 1.7 | 0.7 | 1.3 | 1.0 |
| 15 | 0.0 | 1.3 | 1.0 | 1.7 | 1.3 |
| 16 | 2.3 | 0.7 | 1.0 | 2.3 | 2.0 |
| 17 | 1.0 | 0.3 | 0.7 | 0.7 | 1.3 |
| 18 | 0.0 | 0.0 | 0.0 | 1.0 | 1.7 |
| 19 | 2.3 | 0.7 | 2.3 | 2.3 | 2.7 |
| 20 | 0.3 | 0.7 | 0.0 | 0.0 | 1.0 |
| 21 | 0.3 | 1.7 | 0.3 | 1.3 | 0.3 |
| 22 | 1.0 | 2.3 | 0.7 | 4.0 | 3.3 |
| 23 | 0.7 | 0.3 | 0.0 | 0.3 | 0.7 |
| 24 | 0.7 | 0.3 | 0.7 | 1.7 | 1.0 |

#### English in full

**Competent bot, 300 fresh attempts per shelf**

| shelf | clear % | turns | turns (clears) | peels | peel hit % | guess % | words | avg word len | ignitions | score (clears) | FIRE % | SCORCH % | WORDS % | stall % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 100.0 | 11.4 | 11.4 | 10.9 | 37 | 29 | 0.5 | 6.61 | 0.6 | 2321 | 0.0 | 0.0 | 0.0 | 0.0 |
| 2 | 100.0 | 11.2 | 11.2 | 10.7 | 38 | 30 | 0.5 | 6.65 | 0.5 | 4453 | 0.0 | 0.0 | 0.0 | 0.0 |
| 3 | 100.0 | 12.0 | 12.0 | 11.5 | 44 | 17 | 0.5 | 6.64 | 1.1 | 6844 | 0.0 | 0.0 | 0.0 | 0.0 |
| 4 | 99.3 | 12.1 | 11.8 | 11.1 | 48 | 8 | 1.0 | 5.77 | 1.5 | 9283 | 0.3 | 0.0 | 0.0 | 0.3 |
| 5 | 99.0 | 13.7 | 12.8 | 11.9 | 42 | 17 | 1.8 | 5.50 | 1.6 | 11320 | 0.0 | 0.0 | 0.0 | 1.0 |
| 6 | 95.0 | 18.1 | 15.3 | 12.4 | 48 | 3 | 5.7 | 5.30 | 5.4 | 14691 | 1.7 | 0.0 | 0.0 | 3.3 |
| 7 | 95.0 | 17.9 | 15.0 | 12.3 | 51 | 2 | 5.6 | 5.30 | 5.8 | 17041 | 1.7 | 0.0 | 0.0 | 3.3 |
| 8 | 94.7 | 18.4 | 15.6 | 12.6 | 47 | 3 | 5.8 | 5.37 | 5.6 | 19095 | 2.0 | 0.0 | 0.0 | 3.3 |
| 9 | 91.0 | 24.2 | 18.8 | 13.8 | 50 | 2 | 10.4 | 5.27 | 8.3 | 22347 | 2.3 | 0.0 | 0.0 | 6.7 |
| 10 | 93.0 | 19.9 | 17.4 | 13.4 | 54 | 2 | 6.6 | 5.26 | 6.6 | 24915 | 4.0 | 0.0 | 0.0 | 3.0 |
| 11 | 90.7 | 23.1 | 17.8 | 14.0 | 49 | 3 | 9.2 | 5.32 | 7.9 | 26633 | 2.7 | 0.0 | 0.0 | 6.7 |
| 12 | 76.3 | 29.2 | 20.4 | 12.2 | 54 | 2 | 17.0 | 5.46 | 10.0 | 30540 | 13.7 | 0.0 | 0.0 | 10.0 |
| 13 | 76.3 | 30.4 | 21.1 | 13.1 | 61 | 1 | 17.3 | 5.37 | 10.4 | 34227 | 12.0 | 0.0 | 0.0 | 11.7 |
| 14 | 76.3 | 34.0 | 22.9 | 13.3 | 57 | 1 | 20.8 | 5.42 | 11.6 | 36462 | 12.0 | 0.0 | 0.0 | 11.7 |
| 15 | 70.0 | 33.2 | 24.2 | 12.7 | 58 | 1 | 20.5 | 5.46 | 9.4 | 39250 | 17.3 | 0.0 | 0.0 | 12.7 |
| 16 | 69.0 | 31.4 | 21.2 | 12.9 | 60 | 1 | 18.5 | 5.34 | 8.6 | 41231 | 19.7 | 0.0 | 0.0 | 11.3 |
| 17 | 69.0 | 36.6 | 24.8 | 13.0 | 58 | 2 | 23.7 | 5.45 | 10.5 | 43677 | 15.3 | 0.0 | 0.0 | 15.7 |
| 18 | 69.0 | 34.0 | 24.3 | 13.0 | 57 | 1 | 21.1 | 5.46 | 9.9 | 45783 | 20.0 | 0.0 | 0.0 | 11.0 |
| 19 | 69.0 | 33.4 | 22.3 | 13.0 | 60 | 1 | 20.5 | 5.30 | 9.3 | 48320 | 18.0 | 0.0 | 0.0 | 13.0 |
| 20 | 70.3 | 34.3 | 25.4 | 13.0 | 57 | 1 | 21.3 | 5.41 | 9.9 | 50058 | 18.3 | 0.0 | 0.0 | 11.3 |
| 21 | 66.7 | 33.1 | 23.1 | 12.8 | 57 | 1 | 20.4 | 5.44 | 9.5 | 52330 | 21.7 | 0.0 | 0.0 | 11.7 |
| 22 | 69.3 | 31.9 | 22.3 | 12.7 | 61 | 1 | 19.2 | 5.29 | 8.8 | 55218 | 19.7 | 0.0 | 0.0 | 11.0 |
| 23 | 67.0 | 34.2 | 23.7 | 12.8 | 58 | 2 | 21.4 | 5.43 | 9.8 | 56559 | 19.3 | 0.0 | 0.0 | 13.7 |
| 24 | 64.3 | 35.0 | 23.0 | 12.7 | 57 | 2 | 22.3 | 5.44 | 10.0 | 59501 | 21.0 | 0.0 | 0.0 | 14.7 |

**Weak bot, 300 fresh attempts per shelf**

| shelf | clear % | turns | turns (clears) | peels | peel hit % | guess % | words | avg word len | ignitions | score (clears) | FIRE % | SCORCH % | WORDS % | stall % |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 65.7 | 39.2 | 21.1 | 14.4 | 24 | 20 | 24.8 | 3.97 | 8.6 | 2813 | 16.7 | 0.0 | 0.0 | 17.7 |
| 2 | 69.7 | 40.6 | 21.9 | 14.4 | 24 | 20 | 26.2 | 3.96 | 9.1 | 4772 | 10.3 | 0.0 | 0.0 | 20.0 |
| 3 | 79.0 | 24.8 | 23.5 | 15.7 | 29 | 13 | 9.1 | 4.36 | 4.2 | 6846 | 19.7 | 0.0 | 0.0 | 1.3 |
| 4 | 67.0 | 22.1 | 21.6 | 14.5 | 32 | 10 | 7.6 | 4.34 | 4.0 | 8990 | 32.7 | 0.0 | 0.0 | 0.3 |
| 5 | 61.3 | 23.5 | 23.3 | 14.5 | 29 | 15 | 9.0 | 4.37 | 3.9 | 10794 | 37.7 | 0.0 | 0.0 | 1.0 |
| 6 | 21.7 | 21.4 | 24.8 | 12.7 | 29 | 13 | 8.7 | 4.36 | 7.2 | 13704 | 78.0 | 0.0 | 0.0 | 0.3 |
| 7 | 31.7 | 22.5 | 22.4 | 13.5 | 33 | 8 | 9.0 | 4.25 | 7.3 | 15958 | 66.7 | 0.0 | 0.0 | 1.7 |
| 8 | 25.7 | 22.7 | 25.5 | 12.7 | 30 | 14 | 9.9 | 4.31 | 7.6 | 17786 | 73.3 | 0.0 | 0.0 | 1.0 |
| 9 | 16.3 | 21.9 | 25.7 | 12.9 | 31 | 10 | 9.0 | 4.34 | 7.7 | 20265 | 83.0 | 0.0 | 0.0 | 0.7 |
| 10 | 28.3 | 22.6 | 23.8 | 13.9 | 35 | 6 | 8.7 | 4.21 | 7.2 | 22836 | 69.7 | 0.0 | 0.0 | 2.0 |
| 11 | 14.3 | 21.9 | 28.6 | 12.7 | 32 | 10 | 9.2 | 4.32 | 7.7 | 24333 | 84.7 | 0.0 | 0.0 | 1.0 |
| 12 | 2.0 | 18.6 | 26.3 | 9.0 | 33 | 13 | 9.6 | 4.36 | 6.8 | 27288 | 98.0 | 0.0 | 0.0 | 0.0 |
| 13 | 4.0 | 18.4 | 20.3 | 10.9 | 37 | 4 | 7.5 | 4.27 | 6.2 | 31354 | 95.7 | 0.0 | 0.0 | 0.3 |
| 14 | 1.7 | 18.8 | 32.6 | 9.4 | 35 | 11 | 9.4 | 4.35 | 6.8 | 32068 | 98.0 | 0.0 | 0.0 | 0.3 |
| 15 | 0.0 | 17.7 | 0.0 | 9.1 | 34 | 11 | 8.6 | 4.37 | 5.7 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 16 | 2.3 | 17.4 | 21.1 | 10.2 | 38 | 4 | 7.2 | 4.33 | 5.1 | 37721 | 97.7 | 0.0 | 0.0 | 0.0 |
| 17 | 1.0 | 16.9 | 22.0 | 9.0 | 35 | 11 | 7.9 | 4.40 | 5.5 | 39855 | 99.0 | 0.0 | 0.0 | 0.0 |
| 18 | 0.0 | 17.8 | 0.0 | 9.1 | 35 | 11 | 8.7 | 4.39 | 5.7 | 0 | 100.0 | 0.0 | 0.0 | 0.0 |
| 19 | 2.3 | 17.0 | 21.3 | 10.3 | 37 | 4 | 6.7 | 4.33 | 4.9 | 44503 | 97.3 | 0.0 | 0.0 | 0.3 |
| 20 | 0.3 | 17.2 | 29.0 | 8.9 | 34 | 11 | 8.3 | 4.38 | 5.6 | 44525 | 99.7 | 0.0 | 0.0 | 0.0 |
| 21 | 0.3 | 17.2 | 31.0 | 8.9 | 34 | 11 | 8.3 | 4.39 | 5.5 | 44975 | 99.7 | 0.0 | 0.0 | 0.0 |
| 22 | 1.0 | 17.4 | 18.7 | 10.5 | 38 | 4 | 6.9 | 4.31 | 5.1 | 52905 | 98.7 | 0.0 | 0.0 | 0.3 |
| 23 | 0.7 | 18.2 | 30.0 | 8.9 | 35 | 11 | 9.3 | 4.39 | 5.8 | 50485 | 99.0 | 0.0 | 0.0 | 0.3 |
| 24 | 0.7 | 17.7 | 28.5 | 9.1 | 35 | 11 | 8.6 | 4.35 | 5.7 | 53475 | 99.3 | 0.0 | 0.0 | 0.0 |


#### es: runs, Daily modifiers, diagnostics

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 33.1 shelves on average (score 1566135), weak bot 4.6 shelves (score 36260).

Highest shelf cleared, competent: 11:2 15:1 16:2 17:3 18:2 19:1 21:1 22:5 23:3 24:4 25:2 27:2 28:3 29:1 30:1 32:2 33:3 34:3 35:2 36:3 37:1 38:2 39:51 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.3 | 13.3 | 0.7 | 0.0 | 0.0 | 75.3 |
| EMBER | 99.0 | 13.4 | 0.7 | 0.0 | 0.0 | 57.7 |
| DEEP | 100.0 | 11.2 | 0.0 | 0.0 | 0.0 | 78.2 |
| LONG | 99.7 | 15.2 | 0.3 | 0.0 | 0.0 | 62.0 |

**Diagnostics (competent, all shelves)**: FIRE fails 744, of which 201 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 5.1% of all peels.


#### de: runs, Daily modifiers, diagnostics

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 20.0 shelves on average (score 601476), weak bot 3.8 shelves (score 25580).

Highest shelf cleared, competent: 7:1 11:4 12:5 13:5 14:7 15:9 16:11 17:5 18:5 19:9 20:7 22:5 23:3 24:1 25:1 26:2 27:2 28:1 29:5 30:1 31:2 32:2 33:1 35:1 39:5 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 100.0 | 12.9 | 0.0 | 0.0 | 0.0 | 64.3 |
| EMBER | 99.0 | 13.7 | 0.7 | 0.0 | 0.3 | 48.3 |
| DEEP | 100.0 | 11.4 | 0.0 | 0.0 | 0.0 | 69.4 |
| LONG | 99.7 | 15.1 | 0.3 | 0.0 | 0.0 | 49.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 1627, of which 543 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 5.3% of all peels.


#### fr: runs, Daily modifiers, diagnostics

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 32.1 shelves on average (score 1500990), weak bot 4.6 shelves (score 35539).

Highest shelf cleared, competent: 11:2 13:2 14:3 15:2 16:1 17:1 18:3 19:7 20:2 22:2 23:1 24:2 25:2 26:1 27:2 29:2 31:3 32:1 33:2 34:2 36:1 37:1 39:55 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.3 | 13.6 | 0.3 | 0.0 | 0.0 | 70.7 |
| EMBER | 98.7 | 13.8 | 0.7 | 0.0 | 0.0 | 61.0 |
| DEEP | 100.0 | 11.2 | 0.0 | 0.0 | 0.0 | 76.9 |
| LONG | 99.7 | 15.6 | 0.0 | 0.0 | 0.0 | 62.7 |

**Diagnostics (competent, all shelves)**: FIRE fails 707, of which 160 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 5.2% of all peels.


#### fi: runs, Daily modifiers, diagnostics

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 34.2 shelves on average (score 1660991), weak bot 4.3 shelves (score 32409).

Highest shelf cleared, competent: 14:1 15:2 16:1 18:3 19:2 20:2 21:1 22:3 23:4 25:1 26:2 28:3 29:4 30:3 31:1 32:1 34:2 35:1 36:1 38:1 39:61 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.7 | 13.0 | 0.0 | 0.0 | 0.0 | 66.3 |
| EMBER | 100.0 | 13.1 | 0.0 | 0.0 | 0.0 | 58.3 |
| DEEP | 100.0 | 11.4 | 0.0 | 0.0 | 0.0 | 75.5 |
| LONG | 99.7 | 15.1 | 0.3 | 0.0 | 0.0 | 57.7 |

**Diagnostics (competent, all shelves)**: FIRE fails 734, of which 223 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 5.1% of all peels.


#### en: runs, Daily modifiers, diagnostics

**Whole runs (three bookmarks, 100 runs)**: competent bot clears 27.5 shelves on average (score 1105028), weak bot 4.6 shelves (score 33318).

Highest shelf cleared, competent: 11:3 13:3 14:8 15:3 16:1 17:6 18:3 19:7 20:2 21:2 22:1 23:5 24:1 25:5 27:1 28:1 29:3 30:1 31:2 32:3 33:1 34:2 35:1 36:1 37:2 38:2 39:30 

**Daily Pass by modifier** (300 dates each)

| modifier | competent clear % | turns | FIRE % | SCORCH % | WORDS % | weak clear % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD | 99.7 | 13.1 | 0.0 | 0.0 | 0.0 | 73.7 |
| EMBER | 99.7 | 14.2 | 0.0 | 0.0 | 0.0 | 64.7 |
| DEEP | 100.0 | 11.2 | 0.0 | 0.0 | 0.0 | 79.6 |
| LONG | 98.7 | 16.1 | 0.0 | 0.0 | 0.0 | 60.3 |

**Diagnostics (competent, all shelves)**: FIRE fails 728, of which 162 had no word through any flame near the floor on the last look; OUT OF WORDS fails 0 (average 0.0 chainable gems and 0.0 holes on the board at the end); guess peels (chance under 20%) 5.1% of all peels.


