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
    formatting = (
        ("%03d", "-5", "-05"),
        ("%3d", "-5", " -5"),
        ("%08d", "7", "00000007"),
        ("%d", "+0005", "5"),
        ("%5s", "A", "A"),  # the game's formatter ignores string widths
        ("%2$03d/%1$s", "A|-5", "-05/A"),
        ("%d", "-2147483648", "-2147483648"),
    )
    for fmt, sample, expected in formatting:
        assert not gen_ui.sample_errors(sample, set(), fmt)
        assert gen_ui.expand(fmt, sample) == expected, (fmt, sample)
    for sample, fmt in (("text", "%d"), ("2147483648", "%d"),
                        ("-2147483649", "%d"), ("1|2", "%d")):
        assert gen_ui.sample_errors(sample, set(), fmt), (sample, fmt)
    invalid = [(1, "QA_NUM", 1, 60, "text", ["%d"] * len(gen_ui.LANGS))]
    assert any("integer sample" in e for e in gen_ui.validate(invalid))
    mismatched = ["%s"] * len(gen_ui.LANGS)
    mismatched[1] = "%d"
    invalid = [(1, "QA_KIND", 1, 60, "text", mismatched)]
    assert any("slots differ" in e for e in gen_ui.validate(invalid))
    print(f"UI SLOT TEST OK: {len(accepted)} valid, {len(rejected)} rejected, "
          f"sample type/range checks and {len(formatting)} runtime-format cases")


if __name__ == "__main__":
    main()
