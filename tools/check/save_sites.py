#!/usr/bin/env python3
"""A static guard for the settingsSave() call sites in source/main.c.

main.c is not compiled by any native test and the emulator harness has no FAT card, so nothing can observe a
save. This script is the seam that is possible without one: it fails when a call site that the game relies on
is deleted (the Daily attempt marked as played, a new best, leaving OPTIONS, picking a successfully loaded language (not a failed one), the tutorial
flag, the end of a run). It does not prove the save works (settings_test.c does that for the save itself) or
that it runs at the right moment; an end-to-end check needs a FAT image in the harness (see TESTING.md).

If a site is moved or renamed on purpose, update SITES below in the same change.
"""
import os, re, sys

MAIN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "source", "main.c")

# (what the save is for, a regular expression that must match main.c, text spanning at most one statement list)
SITES = [
    ("tutorial finished",             r"S\.tutorDone\s*=\s*1;\s*settingsSave\(\)"),
    ("Daily attempt marked played",   r"S\.dailyPlayedDate\s*=\s*G\.dailySeed;\s*settingsSave\(\)"),
    ("new best (the run-end save is separate)", r"if \(best\) \{ G\.newBest\s*=\s*1;\s*settingsSave\(\);\s*\}"),
    ("successful language pick saved", r"gameSetLanguage\(G\.langPick\);\s*if\s*\(langLoadFailed\(\)\)\s*\{[^}]*\}\s*else\s+settingsSave\(\)"),
    ("failed language pick stays visible", r"if\s*\(langLoadFailed\(\)\)\s*\{[^}]*G\.state\s*=\s*ST_LANGPICK"),
    ("leaving OPTIONS (BACK or B)",   r"KEY_B\)\)\s*\{\s*settingsSave\(\)"),
]
MIN_CALLS = 9          # all call sites in main.c at the time of writing


def main():
    src = open(MAIN, encoding="utf-8").read()
    bad = [name for name, rx in SITES if not re.search(rx, src)]
    n = len(re.findall(r"\bsettingsSave\(\)", src))
    if bad or n < MIN_CALLS:
        print("SAVE SITES FAILED: missing " + (", ".join(bad) or "none") + f"; {n} settingsSave() calls (want at least {MIN_CALLS})")
        return 1
    print(f"SAVE SITES OK: {len(SITES)} named sites, {n} calls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
