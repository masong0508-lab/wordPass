# Shelf 14–15 Lockdown pressure experiment

This study is **host-only evidence**, not a rule change, a human playtest, or a
Nintendo DS FPS benchmark. The shipped rules stay the same.

On Classic shelf 15, the game currently increases two pressures together:
Lockdown heat pulses advance from every fourth turn to every third turn, and
the share of ignitions lighting two columns rises from two of eight to three
of eight. Historic bot results show a sharp drop, but the bot now incorporates
word-earned clues, so historic clear rates must not be treated as current ones.

## Four counterfactuals

The runner compiles four temporary board implementations against the exact same
current dictionaries and competent/weak simulated-player logic:

- **current** — unchanged retail rules.
- **pulse-only** — adds one turn between heat pulses on shelf 15.
- **twin-only** — removes one double-column ignition in eight on shelf 15.
- **both** — applies both changes on shelf 15.

Every other shelf is unchanged. Each scenario is run against exactly the same
board RNG and simulated player RNG seeds. Shelf 14 acts as a strict control:
every shelf-14 report row, including turns and failure categories, must be
identical to the untouched game or the experiment aborts.

## Reproduce

From the repository root with Python 3 and a host GCC compiler:

    python3 tools/check/balance_pressure.py --languages en de fi --attempts 160 --seed-base 1000 --output build/pressure-study

The GitHub Actions workflow named **WordPass fire-pressure study** runs on
relevant rule / bot / study pull requests and main changes, as well as on
manual dispatch. It uploads a Markdown report and the source CSV and JSON
summaries. The workflow also runs an independent 80-attempt-per-case seed batch
across all eleven retail languages, to check whether a first batch's results
hold across other vocabulary and board distributions. The same command can be
run locally with more attempts or languages.

## How to use the evidence

Compare shelf 15 clear percentages and failure causes while holding the player
models, board seeds, language packs and compilation conditions fixed. The
experiment can attribute an outcome change in *this simulation* to a specific
pressure setting; it cannot measure human understanding, enjoyment or ability.
A small difference on 160 attempts is noisy, and even a large difference needs
another seed batch and real playtesting. No retail balance change should be
selected solely from these data.

Once a change is supported, check it against all currently playable languages,
Daily rules, memory/ROM budgets, save compatibility, the full DeSmuME scenarios
and native-hardware observations. This experiment is intentionally separate
from the release rules and cannot change the ROM by itself.

## Initial results (November 22, 2026; first seed batch)

[Successful CI run #45055393299](https://github.com/masong0508-lab/wordPass/actions/runs/45055393299):
**7,680 completed simulated shelves**, 160 attempts per player/shelf/language/
scenario in English, German and Finnish. All shelf-14 control CSV rows matched
exactly. The following are *competent bot* shelf-15 clear percentages, not
human win rates:

| Locale | Current | Pulse-only relief | Twin-only relief | Both changes |
| --- | ---: | ---: | ---: | ---: |
| English | 61.2% | 72.5% | 65.6% | 75.0% |
| German | 70.0% | 70.0% | 65.6% | 75.6% |
| Finnish | 73.8% | 75.0% | 75.6% | 81.9% |

Shelf-14 competent bot clear percentages were 75.0% English, 76.2% German and
79.4% Finnish. The weak bot's shelf-15 clear percentage remained very low
(1.2–6.2% across the 12 scenario/locale combinations), indicating that a
single rule adjustment is unlikely to make later Lockdown accessible to the
current weak simulated player.

The counterintuitive German twin-only result and other non-monotonic cases
are plausible in a stochastic game: changing when fire appears can change
subsequent board and player decisions, even under matched initial seeds. A
160-attempt comparison is too small and too model-dependent to choose a rule.
The independent all-language replication, additional seeds, and actual player
testing are needed before any gameplay retune. No shipping rules are changed
by this work.
