#!/usr/bin/env python3
"""Regression checks for unsafe and ambiguous translated format slots."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lang"))
import gen_ui


def main():
    accepted = [
        "DAILY %02d%02d",
        "DAILY %2$02d%1$02d",
        "BEST %1$d ON %3$02d-%2$02d",
        "%2$s=%1$d",
        "100% %d",
    ]
    rejected = {
        "%9$d": "outside",
        "%2$d": "contiguous",
        "%1$d %1$d": "used twice",
        "%d %1$d": "used twice",
        "%1$d %d": "cannot mix",
        "%d%d%d%d%d%d%d%d%d": "outside",
    }
    for s in accepted:
        assert not gen_ui.slot_errors(s), f"valid: {s}: {gen_ui.slot_errors(s)}"
    for s, expected in rejected.items():
        errors = gen_ui.slot_errors(s)
        assert any(expected in e for e in errors), f"expected {expected!r} for {s!r}, got {errors}"
    for sample, reference in (("@EXISTS|2", {"EXISTS"}), ("-", set()), ("L|R", set())):
        assert not gen_ui.sample_errors(sample, reference)
    for sample, reference in (("@MISSING", {"EXISTS"}), ("ABCDEFGHIJKLMNOP", set()), ("X||Y", set())):
        assert gen_ui.sample_errors(sample, reference), sample
    assert gen_ui.width(gen_ui.expand("%1$02d-%2$02d", "1|3"), 1) == 29
    print(f"UI SLOT TEST OK: {len(accepted)} valid, {len(rejected)} rejected, 6 sample cases, padding regression")


if __name__ == "__main__":
    main()
