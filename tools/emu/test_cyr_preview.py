#!/usr/bin/env python3
"""Russian/Ukrainian DS menu and gameplay tests against actual ARM9 DeSmuME.

The preview's 40 *four-word* RU/UK theme seeds are NOT full language packs.
The production ROM has no developer preview and still exposes 11 locales.
"""
from pathlib import Path
from PIL import Image, ImageChops
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
    # Tap the actual LANGUAGE badge on the title screen. Only the separate
    # engineering ROM offers two added choices, explicitly labelled DEMO.
    e.tap(55, 14, settle=16)
    check(e.state()["state"] == 8, "title language badge did not open chooser")
    e.shot(str(out / "russian-ukrainian-chooser.png"))
    # Preview-only rows 11 and 12: y=23+11*12+6 and 23+12*12+6.
    e.tap(80, 161, settle=24)
    check(e.state()["state"] == 9, "Русский DEMO option failed to enter DS preview")
    check(get_int(e, "scriptPreviewLanguage") == 0, "Russian menu entry selected wrong board")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Russian preview should contain 40 seeded themes")
    check(get_int(e, "scriptPreviewShelf") == 1, "Cyrillic first shelf should start at 1")
    e.shot(str(out / "ru-board.png"))
    check(get_int(e, "scriptPreviewScore") == 0, "initial score is zero")
    e.press("R", settle=12)
    check(get_int(e, "scriptPreviewHints") == 1,
          "R should find one real Cyrillic word path with bounded DFS")
    e.press("Y", settle=6)  # clear hint, then manually score the greeting
    e.press("R", settle=5)
    check(get_int(e, "scriptPreviewHints") == 1,
          "a single preview shelf cannot silently grant endless free hints")
    # The development fixture guarantees ПРИВЕТ in the first column (six
    # adjacent actual touch-screen gem positions), not merely dictionary data.
    e.chain([(0, r) for r in range(6)])
    e.press("A", settle=24)
    check(get_int(e, "scriptPreviewScore") > 0, "real Cyrillic chain did not score")
    check(get_int(e, "scriptPreviewTurns") == 1,
          "a valid six-letter Russian word must consume exactly one shelf turn")
    e.shot(str(out / "ru-scored.png"))
    e.hold("L")
    e.tap(*e.center(1, 6), settle=8)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewPeels") == 1, "touch-screen peel did not register")
    check(get_int(e, "scriptPreviewTurns") == 2,
          "a successful peel must cost another turn and trigger fire rules")
    e.press("X", settle=20)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "X should switch to Ukrainian without changing the release locale")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Ukrainian preview should contain 40 seeded themes")
    e.shot(str(out / "uk-board.png"))
    # The first two language packs legitimately share the theme "КНИГИ",
    # and hidden passwords can have the same placeholder pattern. Compare
    # the explicit translated language label on the ACTUAL top LCD instead
    # of falsely requiring otherwise identical theme artwork to change.
    with Image.open(out / "ru-board.png") as russian, Image.open(out / "uk-board.png") as ukrainian:
        delta = ImageChops.difference(
            russian.convert("RGB").crop((16, 26, 240, 42)),
            ukrainian.convert("RGB").crop((16, 26, 240, 42)))
        check(delta.getbbox() is not None,
              "Cyrillic top-screen language label did not change")
    e.chain([(0, r) for r in range(6)])
    e.press("A", settle=20)
    check(get_int(e, "scriptPreviewScore") > 0, "Ukrainian ПРИВІТ should score")
    check(get_int(e, "scriptPreviewTurns") == 1,
          "Ukrainian scripted board should share shelf-turn semantics")
    e.press("SELECT", settle=8)
    check(get_int(e, "scriptPreviewTurns") == 0, "new pass did not reset turn counter")
    # Exercise a complete secret-finding shelf through REAL stylus taps,
    # reading hidden pass coordinates from the ARM9 game's independent
    # ScriptBoard memory. This is an emulator-only diagnostic: it does not
    # put solutions on screen or in a production language pack. The first
    # 153 bytes are tile[56], state[56], chainC[20], chainR[20], chainLen[1].
    board = symbol(e.rom_elf, "scriptPreviewBoard")
    pass_len = int(e.mem.unsigned[board + 227])
    check(4 <= pass_len <= 9, "Cyrillic shelf password length is out of range")
    secrets = {}
    for col in range(7):
        for row in range(8):
            index = int(e.mem.unsigned[board + 153 + col * 8 + row])
            if index:
                check(1 <= index <= pass_len and index not in secrets,
                      "hidden Cyrillic password index corrupted or duplicated")
                secrets[index] = (col, row)
    check(len(secrets) == pass_len, "every hidden password symbol must have a tile")
    e.hold("L")
    for index in range(1, pass_len + 1):
        e.tap(*e.center(*secrets[index]), settle=5)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewTurns") == pass_len,
          "every revealed secret must consume a single shelf turn")
    e.shot(str(out / "uk-shelf-won.png"))
    e.press("A", settle=18)
    check(get_int(e, "scriptPreviewShelf") == 2,
          "a completed Cyrillic password must advance to the next shelf")
    check(get_int(e, "scriptPreviewTurns") == 0,
          "new Cyrillic shelf must reset heat/turn pacing")
    e.press("START", settle=20)
    check(e.state()["state"] == 0, "START should return to the untouched 11-language title")
    # The second choice must work independently from the same visible menu,
    # without first launching Russian and pressing X.
    e.tap(55, 14, settle=14)
    check(e.state()["state"] == 8, "returning title badge should reopen chooser")
    e.tap(80, 173, settle=22)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "Українська DEMO option failed to start Ukrainian independently")
    e.shot(str(out / "ukrainian-direct-menu.png"))
    e.press("B", settle=16)
    check(e.state()["state"] == 0, "B must exit Ukrainian DEMO to normal title")
    # Also cover controller-only selection. The normal saved language stays
    # English, so UP from the first entry must wrap to Ukrainian DEMO.
    e.tap(55, 14, settle=12)
    check(e.state()["state"] == 8, "cannot reopen language menu for D-pad test")
    e.press("UP", settle=6)
    e.press("A", settle=18)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "D-pad UP wrapping / A should choose Українська DEMO")
    e.press("START", settle=16)
    check(e.state()["state"] == 0, "controller-selected Ukrainian DEMO must exit safely")
    print("RUSSIAN AND UKRAINIAN DS OPTIONS OK: touch + D-pad picker, bilingual gameplay, full shelf and safe exit")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_cyr_preview.py wordPass-cyr-preview.nds output-dir")
    main(sys.argv[1], sys.argv[2])
