#!/usr/bin/env python3
"""Summarize a paired multi-shelf WordPass pressure sweep and enforce controls.

Inputs are from tools/check/balance_sweep.py --baseline REV --first 14 --last 23.
The old/current builds share the same player model, dictionary and seed schedule.
This does not estimate human win rates or Nintendo DS frame rates.
"""
import argparse
import csv
import json
from pathlib import Path


def load(path):
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"empty report: {path}")
    out = {}
    for row in rows:
        key = (row["player"], int(row["shelf"]))
        if key in out:
            raise ValueError(f"duplicate {key} in {path}")
        out[key] = row
    return out


def percent(n, denom):
    return 100.0 * n / denom


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--languages", nargs="+", required=True)
    p.add_argument("--first", type=int, default=14)
    p.add_argument("--last", type=int, default=23)
    p.add_argument("--output", type=Path, help="Markdown report path")
    args = p.parse_args()
    if args.first >= args.last or args.first > 14 or args.last < 23:
        p.error("study must cover unchanged control shelves 14 and 23")
    if len(set(args.languages)) != len(args.languages):
        p.error("languages must be unique")
    summary = json.loads((args.input / "summary.json").read_text(encoding="utf-8"))
    if summary["first_shelf"] != args.first or summary["last_shelf"] != args.last:
        raise ValueError("requested shelves do not match sweep summary")
    if summary["languages"] != args.languages:
        raise ValueError("requested languages do not match sweep summary")
    n = int(summary["attempts_per_player_shelf"])
    if not 1 <= n <= 1000:
        raise ValueError("invalid sample count")
    lines = [
        "# Staged Lockdown IV: paired progression test",
        "",
        f"{n} seeds per language, player and shelf; baseline {summary['baseline']}.",
        "The baseline and candidate share the current clue-aware bot, packs and",
        "seed schedule. **These are bot-policy outcomes, not human clear rates.**",
        "Shelves 14 and 19 are unchanged controls and must have identical full CSV rows.",
        "",
        "| Language | Player | Baseline 14-23 clear rates | Staged 14-23 clear rates | Largest consecutive drop: baseline / staged |",
        "| --- | --- | --- | --- | --- |",
    ]
    comparisons = []
    for lang in args.languages:
        a = load(args.input / f"baseline-{lang}.csv")
        b = load(args.input / f"current-{lang}.csv")
        for player in ("competent", "weak"):
            shelves = range(args.first, args.last + 1)
            keys = [(player, shelf) for shelf in shelves]
            if any(key not in a or key not in b for key in keys):
                raise ValueError(f"{lang}/{player}: missing shelf in paired CSV")
            for shelf in (14, 23):
                if a[(player, shelf)] != b[(player, shelf)]:
                    raise AssertionError(
                        f"{lang}/{player}: unchanged control shelf {shelf} differs")
            old = [percent(int(a[key]["clears"]), n) for key in keys]
            new = [percent(int(b[key]["clears"]), n) for key in keys]
            worst_old = max([0.0] + [x - y for x, y in zip(old, old[1:])])
            worst_new = max([0.0] + [x - y for x, y in zip(new, new[1:])])
            comparisons.append({
                "language": lang, "player": player, "baseline_clear_pct": old,
                "staged_clear_pct": new, "baseline_largest_drop_pp": worst_old,
                "staged_largest_drop_pp": worst_new,
            })
            lines.append(
                "| " + " | ".join([
                    lang, player,
                    ", ".join(f"{v:.1f}%" for v in old),
                    ", ".join(f"{v:.1f}%" for v in new),
                    f"{worst_old:.1f} / {worst_new:.1f} pp",
                ]) + " |"
            )
    lines.extend([
        "",
        "A smaller largest-drop figure is suggestive, not proof that the player",
        "experience is smooth. A comparison can get worse on an individual shelf,",
        "and adjusted early hazards can change later RNG paths. Inspect every",
        "language and failure category, then repeat with more seeds.",
        "",
        "The candidate does **not** change classic shelves 1-14 or 23+,",
        "Daily rules, save files, tile dictionaries, music or renderer allocations.",
        "",
    ])
    report = "\n".join(lines)
    target = args.output or args.input / "report.md"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(report, encoding="utf-8")
    (args.input / "analysis.json").write_text(
        json.dumps({"baseline": summary["baseline"], "n": n, "controls_match": True,
                    "comparisons": comparisons}, indent=2) + "\n",
        encoding="utf-8")
    print(report)
    print(f"LOCKDOWN RAMP OK: {len(comparisons)} player/language profiles; controls 14 and 23 match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
