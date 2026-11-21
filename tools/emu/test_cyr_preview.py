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
    # Localized manual uses only the normal top LCD and pauses gameplay while
    # visible. B dismisses it without exiting or consuming a shelf turn.
    e.hold("L")
    e.press("Y", settle=20)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewHelp") == 1, "L+Y must show Russian rules")
    e.shot(str(out / "russian-controls-help.png"))
    with Image.open(out / "ru-board.png") as normal, Image.open(out / "russian-controls-help.png") as manual:
        delta = ImageChops.difference(normal.convert("RGB").crop((0, 0, 256, 192)),
                                      manual.convert("RGB").crop((0, 0, 256, 192)))
        check(delta.getbbox() is not None, "Russian manual did not redraw the top LCD")
    e.press("B", settle=16)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewHelp") == 0
          and get_int(e, "scriptPreviewTurns") == 0,
          "dismissing Russian manual must resume the untouched board")
    e.press("R", settle=12)
    check(get_int(e, "scriptPreviewHints") == 1,
          "R should find a real seeded Cyrillic word with bounded DFS")
    # Capture the actual bounded-hint path from the ARM9 board memory. Unlike
    # the previous fixed greeting, today's puzzle may plant 4-6 symbols in
    # different columns and one of the 40 actual themed vocabularies.
    board_ptr = symbol(e.rom_elf, "scriptPreviewBoard")
    ru_len = int(e.mem.unsigned[board_ptr + 152])
    check(3 <= ru_len <= 6, "Russian theme seed should have a playable hint")
    russian_path = [(int(e.mem.unsigned[board_ptr + 112 + i]),
                     int(e.mem.unsigned[board_ptr + 132 + i]))
                    for i in range(ru_len)]
    check(all(0 <= col < 7 and 0 <= row < 8 for col, row in russian_path),
          "hint path must be on the 7x8 board")
    e.press("Y", settle=6)  # clear hint, then chain the ACTUAL real word
    e.press("R", settle=5)
    check(get_int(e, "scriptPreviewHints") == 1,
          "a single preview shelf cannot silently grant endless free hints")
    e.chain(russian_path)
    e.press("A", settle=24)
    check(get_int(e, "scriptPreviewScore") > 0, "real themed Russian word did not score")
    check(get_int(e, "scriptPreviewTurns") == 1,
          "a valid Russian themed word must consume exactly one shelf turn")
    e.shot(str(out / "ru-scored.png"))
    # Preview stores an independent, fixed-size eight-word score ledger.
    # Byte offsets: 8*20 word IDs, 8 lengths, 8 uint16 scores, count, head,
    # then aligned uint16 combo. These are diagnostics, not game save fields.
    ledger = symbol(e.rom_elf, "scriptPreviewLedger")
    ledger_bytes = bytes(e.mem.unsigned[ledger:ledger + 194])
    check(ledger_bytes[184] == 1 and struct.unpack_from("<H", ledger_bytes, 186)[0] == 1,
          "first scored Russian word should enter ledger and start combo")
    check(get_int(e, "scriptPreviewLastScore") > 0,
          "awarded Russian score must record a real submitted word")
    e.hold("L")
    e.tap(*e.center(1, 6), settle=8)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewPeels") == 1, "touch-screen peel did not register")
    check(get_int(e, "scriptPreviewTurns") == 2,
          "a successful peel must cost another turn and trigger fire rules")
    ledger_bytes = bytes(e.mem.unsigned[ledger:ledger + 194])
    check(ledger_bytes[184] == 1 and struct.unpack_from("<H", ledger_bytes, 186)[0] == 0,
          "peeling must break combo but keep Russian word history")
    e.press("X", settle=20)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "X should switch to Ukrainian without changing the release locale")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Ukrainian preview should contain 40 seeded themes")
    check(bytes(e.mem.unsigned[ledger:ledger + 194])[184] == 0,
          "switching languages must not leak Russian word history into Ukrainian")
    e.shot(str(out / "uk-board.png"))
    e.hold("L")
    e.press("Y", settle=18)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewHelp") == 1, "L+Y must show Ukrainian rules")
    e.shot(str(out / "ukrainian-controls-help.png"))
    with Image.open(out / "russian-controls-help.png") as russian, Image.open(out / "ukrainian-controls-help.png") as ukrainian:
        delta = ImageChops.difference(russian.convert("RGB").crop((20, 12, 236, 157)),
                                      ukrainian.convert("RGB").crop((20, 12, 236, 157)))
        check(delta.getbbox() is not None, "Ukrainian manual must have translated content")
    # Press A to dismiss; the dismissal may NOT also submit a word.
    turns_before = get_int(e, "scriptPreviewTurns")
    e.press("A", settle=16)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewHelp") == 0 and
          get_int(e, "scriptPreviewTurns") == turns_before,
          "A should close Ukrainian manual without scoring or consuming a turn")
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
    # The Ukrainian first shelf has its own theme-aware playable path,
    # rather than recycling the Russian greeting/column-zero demonstration.
    e.press("R", settle=9)
    uk_len = int(e.mem.unsigned[board_ptr + 152])
    check(3 <= uk_len <= 6, "Ukrainian themed puzzle needs a playable hint")
    ukrainian_path = [(int(e.mem.unsigned[board_ptr + 112 + i]),
                       int(e.mem.unsigned[board_ptr + 132 + i]))
                      for i in range(uk_len)]
    e.press("Y", settle=6)
    e.chain(ukrainian_path)
    e.press("A", settle=20)
    check(get_int(e, "scriptPreviewScore") > 0, "real themed Ukrainian word should score")
    check(get_int(e, "scriptPreviewTurns") == 1,
          "Ukrainian scripted board should share shelf-turn semantics")
    check(bytes(e.mem.unsigned[ledger:ledger + 194])[184] == 1,
          "Ukrainian scored word must have its own fresh ledger")
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
    # L+SELECT offers UNRANKED Daily practice in the separate demo ROM.
    # On an emulator with no valid DS RTC, it must refuse the mode safely.
    e.tap(55, 14, settle=12)
    e.tap(80, 161, settle=20)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 0,
          "Daily practice requires a real Russian menu-selected board")
    e.hold("L")
    e.press("SELECT", settle=12)
    e.hold("L", False)
    if get_int(e, "scriptPreviewDaily"):
        first_date = get_int(e, "scriptPreviewDailyDate")
        first_seed = get_int(e, "scriptPreviewSeed")
        check(first_date >= 20250101 and first_seed,
              "Daily practice should use a nonzero, plausible DS date seed")
        e.shot(str(out / "russian-daily-practice.png"))
        # SELECT is a safe deterministic re-deal, not a new puzzle or a
        # route to farming the same day's cumulative score.
        e.press("SELECT", settle=12)
        check(get_int(e, "scriptPreviewSeed") == first_seed and
              get_int(e, "scriptPreviewScore") == 0,
              "Daily practice retry must preserve seed and clear score")
        e.press("X", settle=18)
        check(get_int(e, "scriptPreviewDaily") == 1 and
              get_int(e, "scriptPreviewDailyDate") == first_date and
              get_int(e, "scriptPreviewSeed") != first_seed,
              "Ukrainian Daily must preserve the day but use its own board")
        e.press("X", settle=18)
        check(get_int(e, "scriptPreviewDaily") == 1 and
              get_int(e, "scriptPreviewSeed") == first_seed,
              "switching back to Russian on the same date restores daily seed")
        e.hold("L")
        e.press("SELECT", settle=12)
        e.hold("L", False)
        check(get_int(e, "scriptPreviewDaily") == 0,
              "L+SELECT must return to ordinary experimental free shelves")
    else:
        check(get_int(e, "scriptPreviewDailyClockError") == 1,
              "invalid RTC must prevent Daily practice and show clock feedback")
    e.press("START", settle=18)
    check(e.state()["state"] == 0, "Daily demo exits safely without changing retail Daily")
    print("RUSSIAN/UKRAINIAN DS OK: menu, hints, scores, combo, Daily practice seed and safe exit")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_cyr_preview.py wordPass-cyr-preview.nds output-dir")
    main(sys.argv[1], sys.argv[2])
