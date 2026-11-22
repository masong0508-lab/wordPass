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
summaries. The same command can be run locally with more attempts or languages.

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
