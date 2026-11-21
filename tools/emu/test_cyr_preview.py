#!/usr/bin/env python3
"""On-ROM Cyrillic preview test. Runs actual ARM9 game code in DeSmuME.

The preview's 40 *four-word* RU/UK theme seeds are NOT full language packs.
The production ROM has no developer preview and still exposes 11 locales.
"""
from pathlib import Path
import struct
import sys
from wp import Emu, symbol

def get_int(e, name):
    address = symbol(e.rom_elf, name)
    return struct.unpack("<I", bytes(e.mem.unsigned[address:address + 4]))[0]

def check(condition, label):
    if not condition:
        raise AssertionError(label)

def main(rom, output):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    e = Emu(rom)
    e.run(90)
    check(e.state()["state"] == 0, "expected title after selecting English")
    # Entry is intentionally hidden in preview ROMs, not in the public language
    # chooser: the release build never compiles this branch.
    e.hold("L")
    e.hold("R")
    e.press("Y", settle=20)
    e.hold("R", False)
    e.hold("L", False)
    check(e.state()["state"] == 9, "L+R+Y did not open real DS Cyrillic preview")
    check(get_int(e, "scriptPreviewLanguage") == 0, "preview should start in Russian")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Russian preview should contain 40 seeded themes")
    e.shot(str(out / "ru-board.png"))
    check(get_int(e, "scriptPreviewScore") == 0, "initial score is zero")
    # The development fixture guarantees ПРИВЕТ in the first column (six
    # adjacent actual touch-screen gem positions), not merely dictionary data.
    e.chain([(0, r) for r in range(6)])
    e.press("A", settle=24)
    check(get_int(e, "scriptPreviewScore") > 0, "real Cyrillic chain did not score")
    e.shot(str(out / "ru-scored.png"))
    e.hold("L")
    e.tap(*e.center(1, 6), settle=8)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewPeels") == 1, "touch-screen peel did not register")
    e.press("X", settle=20)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "X should switch to Ukrainian without changing the release locale")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Ukrainian preview should contain 40 seeded themes")
    e.shot(str(out / "uk-board.png"))
    e.chain([(0, r) for r in range(6)])
    e.press("A", settle=20)
    check(get_int(e, "scriptPreviewScore") > 0, "Ukrainian ПРИВІТ should score")
    e.press("SELECT", settle=8)
    check(get_int(e, "scriptPreviewTurns") == 0, "new pass did not reset turn counter")
    e.press("START", settle=20)
    check(e.state()["state"] == 0, "START should return to the untouched 11-language title")
    print("CYRILLIC DS PREVIEW OK: Russian and Ukrainian touch chains, score, peel, reset, exit")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_cyr_preview.py wordPass-cyr-preview.nds output-dir")
    main(sys.argv[1], sys.argv[2])
