#!/usr/bin/env python3
"""Measure shelf-15 pressure factors with the real rule engine and unchanged bots.

This experiment does NOT edit the shipped rules or assert human clear rates.
Four native binaries compile identical current code except for one-shot
shelf-15 pulse / twin-ignition relief in temporary copies of source/board.c.
Every candidate is evaluated against the same board seeds and player RNG.
Shelf 14 is an untouched control: its per-player CSV rows must match exactly.

From the repository root:
  python3 tools/check/balance_pressure.py --attempts 160 --output build/pressure
"""
import argparse
import csv
import io
import json
from pathlib import Path
import tempfile

from balance_sweep import compile_sweep
from benchmark_search import ROOT, run

SCENARIOS = {
    "current": (False, False),
    "pulse-only": (True, False),
    "twin-only": (False, True),
    "both": (True, True),
}
PULSE = "    if (out->pulseEvery < 3) out->pulseEvery = 3;"
TWIN = "    out->twinEighths = boardTwinEighths(shelf);"


def candidate_board(original, ease_pulse, ease_twin):
    """Only alter shelf 15; all other shelves are byte-for-byte equivalent."""
    board = original
    if ease_pulse:
        assert board.count(PULSE) == 1, "shelf pulse formula changed: review experiment"
        board = board.replace(PULSE, PULSE +
                              "\n    if (shelf == 15) out->pulseEvery += 1;  // experimental only")
    if ease_twin:
        assert board.count(TWIN) == 1, "twin ignition formula changed: review experiment"
        board = board.replace(TWIN, TWIN +
                              "\n    if (shelf == 15 && out->twinEighths > 0) out->twinEighths--;  // experimental only")
    return board


def parse_rows(raw):
    rows = list(csv.DictReader(io.StringIO(raw)))
    found = {}
    for row in rows:
        key = (row["player"], int(row["shelf"]))
        if key in found:
            raise RuntimeError(f"duplicate player/shelf row: {key}")
        found[key] = row
    expected = {(p, shelf) for p in ("competent", "weak") for shelf in (14, 15)}
    if found.keys() != expected:
        raise RuntimeError(f"unexpected report keys: {sorted(found)}")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempts", type=int, default=160,
                        help="attempts per player, per shelf, per scenario, per language")
    parser.add_argument("--seed-base", type=int, default=1000)
    parser.add_argument("--languages", nargs="+", default=["en", "de", "fi"],
                        choices="en es fr de it pt nl sv pl da fi".split())
    parser.add_argument("--output", type=Path,
                        help="directory to write machine-readable data and Markdown")
    args = parser.parse_args()
    if not 1 <= args.attempts <= 1000:
        parser.error("--attempts must be 1..1000")
    if not 0 <= args.seed_base <= 0xffffffff:
        parser.error("--seed-base must fit a uint32")
    if len(set(args.languages)) != len(args.languages):
        parser.error("--languages cannot contain duplicates")

    original = (ROOT / "source/board.c").read_text(encoding="utf-8")
    dictionary = (ROOT / "source/dict.c").read_text(encoding="utf-8")
    reports = {}
    with tempfile.TemporaryDirectory(prefix="wordpass-pressure-") as directory:
        tmp = Path(directory)
        for scenario, knobs in SCENARIOS.items():
            binary = compile_sweep(tmp, scenario, candidate_board(original, *knobs), dictionary)
            for language in args.languages:
                raw = run([str(binary), language, "14", "15",
                           str(args.attempts), str(args.seed_base)])
                reports[(scenario, language)] = (parse_rows(raw), raw)

    # The unaffected shelf must have bit-for-bit identical aggregate reports,
    # not merely statistically close numbers.
    for language in args.languages:
        baseline = reports[("current", language)][0]
        for scenario in SCENARIOS:
            for player in ("competent", "weak"):
                if baseline[(player, 14)] != reports[(scenario, language)][0][(player, 14)]:
                    raise AssertionError(
                        f"{language}/{player}: shelf 14 changed in {scenario}; invalid control")

    summary = {
        "purpose": "Isolate pulse and twin-ignition contributions at shelf 15",
        "bot_model": "Unchanged clue-aware competent and weak policies from tools/check/bot.c",
        "scope": "Host simulation, not real human play or native DS FPS",
        "base_rules": "Current retail rules; all temporary variants affect only shelf 15",
        "scenarios": {
            name: {
                "shelf15_pulse_plus_one_turn": pulse,
                "shelf15_one_fewer_twin_ignition_in_eight": twin,
            }
            for name, (pulse, twin) in SCENARIOS.items()
        },
        "languages": args.languages,
        "attempts_per_player_shelf_scenario_language": args.attempts,
        "total_shelves_played": args.attempts * 2 * 2 * len(SCENARIOS) * len(args.languages),
        "seed_base": args.seed_base,
        "seed_formula": "uint32(seed_base + shelf * 100003 + attempt * 7919)",
        "control_shelf14_identical": True,
        "rows": [],
    }
    markdown = [
        "# WordPass shelf 15: controlled pressure study",
        "",
        "Real retail rules and unchanged clue-aware bots on identical seed schedules.",
        "The temporary experiments change **only shelf 15**: +1 turn between",
        "Lockdown heat pulses, one fewer twin ignition per eight, or both.",
        "**These are simulations, not player measurements or a released balance change.**",
        "",
        f"{args.attempts} attempts per player, shelf, language and scenario; "
        f"{summary['total_shelves_played']} simulated shelves in total.",
        "Shelf 14 control rows are exactly identical in every scenario.",
        "",
        "| Language | Bot | Scenario | Shelf 14 clear | Shelf 15 clear | 14-to-15 drop | Fire losses (15) |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for language in args.languages:
        reference = reports[("current", language)][0]
        for player in ("competent", "weak"):
            base14 = int(reference[(player, 14)]["clears"]) * 100 / args.attempts
            for scenario in SCENARIOS:
                current = reports[(scenario, language)][0][(player, 15)]
                rate15 = int(current["clears"]) * 100 / args.attempts
                fire15 = int(current["fire"]) * 100 / args.attempts
                summary["rows"].append({
                    "language": language, "player": player, "scenario": scenario,
                    "shelf14_clears": int(reference[(player, 14)]["clears"]),
                    "shelf15_clears": int(current["clears"]),
                    "shelf15_fire": int(current["fire"]),
                    "shelf15_scorch": int(current["scorch"]),
                    "shelf15_out_of_words": int(current["words"]),
                    "shelf15_timeout": int(current["stall"]),
                    "shelf15_mean_turns": float(current["turns"]),
                    "clear_rate_change_vs_current_pp": (
                        int(current["clears"]) -
                        int(reference[(player, 15)]["clears"])) * 100 / args.attempts,
                })
                markdown.append(
                    f"| {language} | {player} | {scenario} | {base14:.1f}% "
                    f"| {rate15:.1f}% | {base14-rate15:+.1f} pp "
                    f"| {fire15:.1f}% |")
    markdown += [
        "",
        "These differences quantify the existing bot's behavior, not accessibility",
        "or fairness for people. Random sampling uncertainty and player-model",
        "limitations remain. Prefer a follow-up multi-seed/hardware playtest",
        "before changing the shipped fire ladder.",
    ]
    report = "\n".join(markdown) + "\n"
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n",
                                                  encoding="utf-8")
        (args.output / "report.md").write_text(report, encoding="utf-8")
        for (scenario, language), (_rows, raw) in reports.items():
            (args.output / f"{language}-{scenario}.csv").write_text(raw, encoding="utf-8")
    print(report)
    print("PRESSURE STUDY OK: control shelf 14 identical; "
          f"{summary['total_shelves_played']} simulated shelves")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
