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
from elf_abi import member_offsets

def get_int(e, name):
    address = symbol(e.rom_elf, name)
    return struct.unpack("<I", bytes(e.mem.unsigned[address:address + 4]))[0]

def check(condition, label):
    if not condition:
        raise AssertionError(label)

def exercise_loss(e, out, scorch):
    """Cause a real loss via a single ordinary peel after arranging fire.
    The emulator-only setup does not alter the game action's rules or status.
    """
    board = symbol(e.rom_elf, "scriptPreviewBoard")
    rules = symbol(e.rom_elf, "scriptPreviewRules")
    off = member_offsets(e.rom_elf, "scriptPreviewRules",
                         ("status", "failReason", "burnEvery", "burnTick"))
    pass_len = int(e.mem.unsigned[board + 227])
    check(4 <= pass_len <= 9, "loss fixture needs a real hidden password")
    def reason():
        return int(e.mem.unsigned[rules + off["failReason"]])
    def status():
        return int(e.mem.unsigned[rules + off["status"]])
    check(status() == 0 and reason() == 0 and
          get_int(e, "scriptPreviewTurns") == 0,
          "loss fixture must begin on a fresh running shelf")
    before = e.emu.screenshot().convert("RGB").crop((26, 70, 230, 84))
    if scorch:
        # Make one genuine hidden secret dangerous, then use actual stylus
        # input to trigger the existing fatal scorch check.
        index = next(i for i in range(56)
                     if int(e.mem.unsigned[board + 153 + i]))
        c, r = divmod(index, 8)
        e.mem.write_byte(board + 56 + index, 2)  # SCRIPT_BURN
    else:
        # A due fire step encountering a bottom-row flame loses the shelf.
        # The top-left ordinary peel spends one real turn to trigger it.
        e.mem.write_byte(board + 56 + 6 * 8 + 7, 2)
        e.mem.write_byte(rules + off["burnTick"],
                         int(e.mem.unsigned[rules + off["burnEvery"]]) - 1)
        c, r = 0, 0
    original_found = bytes(e.mem.unsigned[board + 218:board + 218 + pass_len])
    old_score = get_int(e, "scriptPreviewScore")
    e.hold("L")
    e.tap(*e.center(c, r), settle=14)
    e.hold("L", False)
    check(status() == 2 and reason() == (1 if scorch else 2) and
          get_int(e, "scriptPreviewTurns") == 1 and
          get_int(e, "scriptPreviewScore") == old_score,
          "actual peel must preserve score and distinguish the two loss reasons")
    check(bytes(e.mem.unsigned[board + 218:board + 218 + pass_len]) == original_found,
          "displaying a lost password may not mark any letter found")
    e.run(12)
    after = e.emu.screenshot().convert("RGB").crop((26, 70, 230, 84))
    check(ImageChops.difference(before, after).getbbox() is not None,
          "terminal loss must redraw the password plaque")
    # A fully revealed lost-password plaque is independent of found flags.
    # Temporarily set flags only in this diagnostic, compare its rendered
    # glyph region, then restore the original real game state before retry.
    try:
        for i in range(pass_len):
            e.mem.write_byte(board + 218 + i, 1)
        e.run(12)
        full = e.emu.screenshot().convert("RGB").crop((26, 70, 230, 84))
        check(ImageChops.difference(after, full).getbbox() is None,
              "lost answer must already display every character")
    finally:
        for i, value in enumerate(original_found):
            e.mem.write_byte(board + 218 + i, value)
        e.run(12)
    tag = "ru-scorched-password" if scorch else "uk-bottom-fire"
    e.shot(str(out / (tag + ".png")))
    e.press("A", settle=12)
    check(status() == 0 and reason() == 0 and
          get_int(e, "scriptPreviewTurns") == 0,
          "real retry must clear the terminal reason and start a fresh shelf")
    fresh = e.emu.screenshot().convert("RGB").crop((26, 70, 230, 84))
    check(ImageChops.difference(after, fresh).getbbox() is not None,
          "retry must restore the live masked password")


def main(rom, output):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    e = Emu(rom)
    sourced = "sourced" in Path(rom).name
    def await_preview(language):
        # Reading + structurally validating the large 100k-word WPD4 image
        # is a synchronous ARM9 operation; game language/state globals are
        # updated only when that load and board setup have completed.
        # The tiny built-in engineering demo keeps its original short gate.
        if sourced:
            for _ in range(40):
                if (e.state()["state"] == 9 and
                    get_int(e, "scriptPreviewLanguage") == language):
                    break
                if get_int(e, "scriptPreviewOpenError"):
                    break
                e.run(60)

    def await_title():
        if sourced:
            for _ in range(24):
                if e.state()["state"] == 0:
                    break
                e.run(60)

    e.run(90)
    check(e.state()["state"] == 0, "expected title after selecting English")
    # Tap the actual LANGUAGE badge on the title screen. Only the separate
    # engineering ROM offers two added choices, explicitly labelled DEMO.
    e.tap(55, 14, settle=16)
    check(e.state()["state"] == 8, "title language badge did not open chooser")
    e.shot(str(out / "russian-ukrainian-chooser.png"))
    # Preview-only rows 11 and 12: y=23+11*12+6 and 23+12*12+6.
    # Source-backed 100k-word WPD4 images take longer to read and validate
    # synchronously on ARM9 than the tiny built-in demo. While that loader
    # is executing the menu state remains 8 and open-error is still zero.
    # Give it bounded emulator cycles to finish BEFORE interpreting an
    # unchanged state as a missed touch. Only retry a truly idle picker,
    # and never conceal a real loader failure.
    e.tap(80, 161, settle=24)
    if "sourced" in Path(rom).name:
        for _ in range(24):  # at most 24 * 60 simulated frames
            if e.state()["state"] != 8 or get_int(e, "scriptPreviewOpenError"):
                break
            e.run(60)
    if e.state()["state"] == 8 and not get_int(e, "scriptPreviewOpenError"):
        e.tap(80, 161, hold=18, settle=60)
        if "sourced" in Path(rom).name:
            for _ in range(24):
                if e.state()["state"] != 8 or get_int(e, "scriptPreviewOpenError"):
                    break
                e.run(60)
    if e.state()["state"] != 9:
        e.shot(str(out / "ru-sourced-launch-failed.png"))
    check(e.state()["state"] == 9,
          f"Русский DEMO launch failed: state={e.state()['state']}, "
          f"resource-stage={get_int(e, 'scriptPreviewOpenError')}")
    check(get_int(e, "scriptPreviewLanguage") == 0, "Russian menu entry selected wrong board")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Russian preview should contain 40 seeded themes")
    check(get_int(e, "scriptPreviewShelf") == 1, "Cyrillic first shelf should start at 1")
    check(1 <= get_int(e, "scriptPreviewOpeningWords") <= 2,
          "Russian shelf must start with one or two real playable word chains")
    e.shot(str(out / "ru-board.png"))
    check(get_int(e, "scriptPreviewScore") == 0, "initial score is zero")
    # The glyph cache is built when the first Russian frame is drawn and
    # must not rebuild merely because a 56-tile board is drawn each frame.
    glyph_builds_ru = get_int(e, "scriptGlyphCacheBuilds")
    check(glyph_builds_ru >= 1, "Russian glyphs were never cached")
    check(e.fps() == 120, "Russian preview must retain 120/120 frames while rendering")
    check(get_int(e, "scriptGlyphCacheBuilds") == glyph_builds_ru,
          "Russian renderer rebuilt the alphabet on every frame")
    board_ptr = symbol(e.rom_elf, "scriptPreviewBoard")
    clues_offset = member_offsets(e.rom_elf, "scriptPreviewBoard",
                                   ("surveyed", "clueC", "clueR"))
    check(not any(bytes(e.mem.unsigned[
          board_ptr + clues_offset["surveyed"]:
          board_ptr + clues_offset["surveyed"] + 7])),
          "fresh Russian shelf starts without any word-earned clues")
    # Retail-style peek safety: merely touching a gem is NOT a peel.
    # A drift onto the neighboring gem must disarm without consuming a
    # turn, changing the board or accidentally beginning a word chain.
    board_ptr = symbol(e.rom_elf, "scriptPreviewBoard")
    untouched = bytes(e.mem.unsigned[board_ptr:board_ptr + 336])
    e.hold("L")
    e.emu.input.touch_set_pos(*e.center(0, 0))
    e.run(5)
    check(get_int(e, "scriptPreviewPeels") == 0,
          "initial L+touch must only arm a peel until stylus release")
    e.emu.input.touch_set_pos(*e.center(0, 1))
    e.run(5)
    e.emu.input.touch_release()
    e.run(8)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewPeels") == 0 and
          get_int(e, "scriptPreviewTurns") == 0 and
          bytes(e.mem.unsigned[board_ptr:board_ptr + 336]) == untouched,
          "sliding from an armed gem must cancel with zero peel/turn/mutation")
    # Releasing the shoulder before lifting also cancels. The touch
    # must not be reinterpreted as a regular front-letter word stroke.
    e.hold("L")
    e.emu.input.touch_set_pos(*e.center(0, 0))
    e.run(5)
    e.hold("L", False)
    e.run(4)
    e.emu.input.touch_release()
    e.run(8)
    check(get_int(e, "scriptPreviewPeels") == 0 and
          get_int(e, "scriptPreviewTurns") == 0 and
          bytes(e.mem.unsigned[board_ptr:board_ptr + 336]) == untouched,
          "shoulder release mid-peek must cancel without a chain or heat")
    # Controller-only demo inputs use real ARM9 D-pad and button events:
    # move focus, choose a front gem with A, undo with B without exiting,
    # and keep the entire hidden-password/heat/turn state untouched.
    initial_controller_board = bytes(e.mem.unsigned[board_ptr:board_ptr + 336])
    e.press("RIGHT", settle=6)
    check(get_int(e, "scriptPreviewCursorActive") == 1 and
          get_int(e, "scriptPreviewCursorCol") == 4 and
          get_int(e, "scriptPreviewCursorRow") == 4,
          "RIGHT should show a bounded Cyrillic D-pad cursor")
    e.shot(str(out / "russian-controller-cursor.png"))
    with Image.open(out / "ru-board.png") as no_cursor, Image.open(
            out / "russian-controller-cursor.png") as with_cursor:
        # cell_rect() already returns coordinates in the combined
        # 256x384 image, including the touch LCD's +192 row offset.
        # Do NOT add 192 again: the previous test cropped empty space.
        px, py, x1, y1 = e.cell_rect(4, 4)
        region = (px, py, x1, y1)
        check(ImageChops.difference(
            no_cursor.convert("RGB").crop(region),
            with_cursor.convert("RGB").crop(region)).getbbox() is not None,
            "D-pad focus must be visible on the actual bottom LCD")
    e.press("A", settle=6)
    check(int(e.mem.unsigned[board_ptr + 152]) == 1 and
          get_int(e, "scriptPreviewTurns") == 0,
          "controller A should select exactly one front gem at no turn cost")
    e.press("B", settle=6)
    check(e.state()["state"] == 9 and
          int(e.mem.unsigned[board_ptr + 152]) == 0 and
          get_int(e, "scriptPreviewTurns") == 0 and
          bytes(e.mem.unsigned[board_ptr:board_ptr + 112]) ==
          initial_controller_board[:112],
          "B must undo a controller word, not quit or change board tiles")
    e.press("Y", settle=5)
    # A fast stylus drag between two video samples must pick up every
    # intervening adjacent letter, not leave a two-cell gap in the chain.
    # The upper five rows are always ordinary front gems at shelf start.
    untouched = bytes(e.mem.unsigned[board_ptr:board_ptr + 336])
    e.emu.input.touch_set_pos(*e.center(0, 0))
    e.run(5)
    e.emu.input.touch_set_pos(*e.center(0, 3))  # jump three gem rows in 1 frame
    e.run(5)
    check(int(e.mem.unsigned[board_ptr + 152]) == 4 and
          [int(e.mem.unsigned[board_ptr + 112 + i]) for i in range(4)] == [0] * 4 and
          [int(e.mem.unsigned[board_ptr + 132 + i]) for i in range(4)] == [0, 1, 2, 3] and
          get_int(e, "scriptPreviewTurns") == 0,
          "fast vertical Cyrillic drag must bridge every real intervening gem")
    e.emu.input.touch_release()
    e.run(6)
    e.press("B", settle=5)
    check(e.state()["state"] == 9 and
          int(e.mem.unsigned[board_ptr + 152]) == 3,
          "B must undo a STYLUS-chosen letter without exiting the demo")
    # Clear during an ongoing touch and continue moving: a simultaneous
    # Y action must cancel the entire stroke rather than silently start a
    # second chain as the stylus remains on the screen.
    e.emu.input.touch_set_pos(*e.center(0, 0))
    e.run(5)
    e.press("Y", settle=5)
    e.emu.input.touch_set_pos(*e.center(0, 4))
    e.run(5)
    check(int(e.mem.unsigned[board_ptr + 152]) == 0 and
          get_int(e, "scriptPreviewTurns") == 0,
          "Y during a held touch must clear once and suppress phantom word segments")
    e.emu.input.touch_release()
    e.run(6)
    check(bytes(e.mem.unsigned[board_ptr:board_ptr + 112]) == untouched[:112],
          "fast drag and backtrack change only the public word selection")
    # Switching to peek while a normal word stroke is still down must
    # freeze that stroke, even if L is released before the stylus lifts.
    e.emu.input.touch_set_pos(*e.center(0, 0))
    e.run(5)
    e.hold("L")
    e.emu.input.touch_set_pos(*e.center(0, 3))
    e.run(5)
    e.hold("L", False)
    e.emu.input.touch_set_pos(*e.center(0, 4))
    e.run(5)
    e.emu.input.touch_release()
    e.run(8)
    check(int(e.mem.unsigned[board_ptr + 152]) == 1 and
          get_int(e, "scriptPreviewPeels") == 0 and
          get_int(e, "scriptPreviewTurns") == 0,
          "pressing and releasing L mid-word must not trigger a peel or extra word cells")
    e.press("Y", settle=6)
    # The old preview painted a gold pixel under EVERY hidden pass cell,
    # leaking the entire secret. Changing one still-covered back value
    # must not change any pixel on the touch-screen board without a peel.
    # A top-row cell can never be an actual seeded pass location.
    x, y, _, _ = e.cell_rect(0, 0)
    crop = (x, y, x + 24, y + 22)
    before_pixels = e.emu.screenshot().convert("RGB").crop(crop)
    back_addr = board_ptr + 153      # back[0][0] (legacy debug offsets)
    original_back = int(e.mem.unsigned[back_addr])
    check(original_back == 0, "top row unexpectedly hides a pass letter")
    e.mem.write_byte(back_addr, 1)
    try:
        e.run(6)
        after_pixels = e.emu.screenshot().convert("RGB").crop(crop)
        check(ImageChops.difference(before_pixels, after_pixels).getbbox() is None,
              "Cyrillic preview rendered an unopened secret location (spoiler)")
    finally:
        e.mem.write_byte(back_addr, original_back)
        e.run(6)
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
    initial_board = bytes(e.mem.unsigned[board_ptr:board_ptr + 336])
    e.hold("L")
    e.press("R", settle=12)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewRescueError") == 1 and
          get_int(e, "scriptPreviewRescues") == 0 and
          get_int(e, "scriptPreviewTurns") == 0 and
          bytes(e.mem.unsigned[board_ptr:board_ptr + 336]) == initial_board,
          "too-early L+R must show feedback without changing the board or spending rescue")
    e.shot(str(out / "ru-rescue-too-early.png"))
    e.press("R", settle=12)
    check(get_int(e, "scriptPreviewHints") == 1 and
          get_int(e, "scriptPreviewRescueError") == 0,
          "R should find a real seeded Cyrillic word with bounded DFS")
    # Capture the actual bounded-hint path from the ARM9 board memory. Unlike
    # the previous fixed greeting, today's puzzle may plant 4-6 symbols in
    # different columns and one of the 40 actual themed vocabularies.
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
    quoted = get_int(e, "chainScoreNow")
    quoted_tier = get_int(e, "scriptQuoteTierNow")
    check(quoted > 0 and quoted_tier in (1, 2),
          "Russian renderer must cache a real score and tier for the selected word")
    e.shot(str(out / "ru-selected-word.png"))
    check(e.fps() == 120, "selected Cyrillic word must retain 120/120 emulator frame cadence")
    # Match retail's one-hand submit gesture: lift after selecting the word,
    # then tap its final gem AGAIN. Unlike hovering during a held drag, a
    # second fresh contact on a dictionary-valid tail submits exactly once.
    e.tap(*e.center(*russian_path[-1]), settle=24)
    # A submitted word publishes a genuine fixed-position clue at its tail.
    # Temporarily remove only that public-bit marker: the numbered badge
    # should vanish, while scores and secret data remain unchanged.
    tail_c, tail_r = russian_path[-1]
    surveyed_at = board_ptr + clues_offset["surveyed"] + tail_c
    survey_bits = int(e.mem.unsigned[surveyed_at])
    check(survey_bits & (1 << tail_r),
          "scored Russian word did not publish a location-anchored clue")
    check(int(e.mem.unsigned[board_ptr + clues_offset["clueC"]]) == tail_c and
          int(e.mem.unsigned[board_ptr + clues_offset["clueR"]]) == tail_r,
          "last word-clue focus is not its true tail")
    x, y, _, _ = e.cell_rect(tail_c, tail_r)
    crop = (x + 15, y + 12, x + 24, y + 22)
    with_clue = e.emu.screenshot().convert("RGB").crop(crop)
    e.mem.write_byte(surveyed_at, survey_bits & ~(1 << tail_r))
    try:
        e.run(6)
        without_clue = e.emu.screenshot().convert("RGB").crop(crop)
        check(ImageChops.difference(with_clue, without_clue).getbbox() is not None,
              "scored Russian clue numeral is not visible on the DS screen")
    finally:
        e.mem.write_byte(surveyed_at, survey_bits)
        e.run(6)
    check(get_int(e, "scriptPreviewScore") > 0, "real themed Russian word did not score")
    check(get_int(e, "scriptPreviewTurns") == 1,
          "a valid Russian themed word must consume exactly one shelf turn")
    e.shot(str(out / "ru-scored.png"))
    check(get_int(e, "scriptPreviewLastScore") > 0,
          "awarded Russian score must record a real submitted word")
    check(get_int(e, "scriptPreviewLastScore") == quoted and
          get_int(e, "scriptPreviewLastTier") == quoted_tier,
          "Russian submitted points and tier must match the displayed quote")
    # L+R repairs a thin draft vocabulary board once after the first turn.
    # It must spend a real turn, avoid granting points, and reset combo.
    scored = get_int(e, "scriptPreviewScore")
    e.hold("L")
    e.press("R", settle=14)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewRescues") == 1 and
          get_int(e, "scriptPreviewTurns") == 2 and
          get_int(e, "scriptPreviewScore") == scored and
          get_int(e, "scriptPreviewHints") == 1,
          "one L+R rescue must cost one turn without awarding score or refilling hints")
    e.shot(str(out / "ru-rescued-board.png"))
    e.hold("L")
    e.press("R", settle=9)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewRescueError") == 2 and
          get_int(e, "scriptPreviewTurns") == 2,
          "repeated L+R must not bypass the one-use rescue limit")
    # Preview stores an independent, fixed-size eight-word score ledger.
    # Byte offsets: 8*20 word IDs, 8 lengths, 8 uint16 scores, count, head,
    # then aligned uint16 combo. These are diagnostics, not game save fields.
    ledger = symbol(e.rom_elf, "scriptPreviewLedger")
    ledger_bytes = bytes(e.mem.unsigned[ledger:ledger + 194])
    check(ledger_bytes[184] == 1 and struct.unpack_from("<H", ledger_bytes, 186)[0] == 0,
          "rescue must keep Russian word history and break the earned combo")
    check(get_int(e, "scriptPreviewLastScore") == 0,
          "rescue should clear the stale last-word score display")
    e.hold("L")
    e.tap(*e.center(1, 6), settle=8)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewPeels") == 1 and
          get_int(e, "scriptPreviewRescueError") == 0,
          "touch-screen peel must register and dismiss old rescue feedback")
    check(get_int(e, "scriptPreviewTurns") == 3,
          "a successful peel after word+rescue must cost a third fire turn")
    ledger_bytes = bytes(e.mem.unsigned[ledger:ledger + 194])
    check(ledger_bytes[184] == 1 and struct.unpack_from("<H", ledger_bytes, 186)[0] == 0,
          "peeling must break combo but keep Russian word history")
    e.press("SELECT", settle=9)
    check(get_int(e, "scriptPreviewScore") == 0 and
          get_int(e, "scriptPreviewTurns") == 0 and
          get_int(e, "scriptPreviewShelf") == 1,
          "Russian first-shelf replay must discard its attempt score")
    exercise_loss(e, out, scorch=True)
    e.press("X", settle=20)
    await_preview(1)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "X should switch to Ukrainian without changing the release locale")
    check(get_int(e, "scriptPreviewThemeCount") == 40, "Ukrainian preview should contain 40 seeded themes")
    check(1 <= get_int(e, "scriptPreviewOpeningWords") <= 2,
          "Ukrainian shelf must independently seed real playable words")
    check(bytes(e.mem.unsigned[ledger:ledger + 194])[184] == 0,
          "switching languages must not leak Russian word history into Ukrainian")
    e.shot(str(out / "uk-board.png"))
    check(get_int(e, "scriptGlyphCacheBuilds") == glyph_builds_ru + 1,
          "Ukrainian glyph cache did not refresh exactly once on switching alphabet")
    check(e.fps() == 120, "Ukrainian preview must retain 120/120 frames while rendering")
    check(get_int(e, "scriptGlyphCacheBuilds") == glyph_builds_ru + 1,
          "Ukrainian renderer rebuilt the alphabet during unchanged frames")
    check(not any(bytes(e.mem.unsigned[
          board_ptr + clues_offset["surveyed"]:
          board_ptr + clues_offset["surveyed"] + 7])),
          "switching Russian to Ukrainian leaked earned word clues")
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
    uk_tail_c, uk_tail_r = ukrainian_path[-1]
    check(int(e.mem.unsigned[board_ptr + clues_offset["surveyed"] + uk_tail_c]) &
          (1 << uk_tail_r),
          "Ukrainian word must earn its own clue independently of Russian")
    check(get_int(e, "scriptPreviewScore") > 0, "real themed Ukrainian word should score")
    check(get_int(e, "scriptPreviewTurns") == 1,
          "Ukrainian scripted board should share shelf-turn semantics")
    check(bytes(e.mem.unsigned[ledger:ledger + 194])[184] == 1,
          "Ukrainian scored word must have its own fresh ledger")
    e.press("SELECT", settle=8)
    check(get_int(e, "scriptPreviewTurns") == 0 and
          get_int(e, "scriptPreviewScore") == 0,
          "Ukrainian re-deal must reset both the attempt clock and its points")
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
    # Peel the first actual hidden symbol using D-pad selection + L+A,
    # then finish with the stylus. No mock peel or code-level reveal is used.
    # Both input paths must consume one real turn per password letter.
    e.press("RIGHT", settle=6)
    dest_col, dest_row = secrets[1]
    while get_int(e, "scriptPreviewCursorCol") != dest_col:
        step = "RIGHT" if get_int(e, "scriptPreviewCursorCol") < dest_col else "LEFT"
        e.press(step, settle=2)
    while get_int(e, "scriptPreviewCursorRow") != dest_row:
        step = "DOWN" if get_int(e, "scriptPreviewCursorRow") < dest_row else "UP"
        e.press(step, settle=2)
    e.hold("L")
    e.press("A", settle=5)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewTurns") == 1 and
          get_int(e, "scriptPreviewPeels") == 1 and
          int(e.mem.unsigned[board + 218]) == 1,
          "controller L+A must reveal the first real secret for exactly one turn")
    e.hold("L")
    for index in range(2, pass_len + 1):
        e.tap(*e.center(*secrets[index]), settle=5)
    e.hold("L", False)
    check(get_int(e, "scriptPreviewTurns") == pass_len,
          "every revealed secret must consume a single shelf turn")
    e.shot(str(out / "uk-shelf-won.png"))
    banked = get_int(e, "scriptPreviewScore")
    check(banked > 0, "completed Ukrainian shelf should award its real bonus")
    e.press("A", settle=18)
    check(get_int(e, "scriptPreviewShelf") == 2,
          "a completed Cyrillic password must advance to the next shelf")
    check(get_int(e, "scriptPreviewTurns") == 0,
          "new Cyrillic shelf must reset heat/turn pacing")
    check(get_int(e, "scriptPreviewScore") == banked,
          "advancing must retain the completed shelf score")
    e.press("R", settle=9)
    e.press("A", settle=14)
    check(get_int(e, "scriptPreviewScore") > banked,
          "next shelf should add real word points before a retry")
    e.press("SELECT", settle=9)
    check(get_int(e, "scriptPreviewScore") == banked and
          get_int(e, "scriptPreviewShelf") == 2 and
          get_int(e, "scriptPreviewTurns") == 0,
          "retrying shelf two must retain only the already completed shelf")
    e.press("START", settle=20)
    await_title()
    check(e.state()["state"] == 0, "START should return to the untouched 11-language title")
    # The second choice must work independently from the same visible menu,
    # without first launching Russian and pressing X.
    e.tap(55, 14, settle=14)
    check(e.state()["state"] == 8, "returning title badge should reopen chooser")
    e.tap(80, 173, settle=22)
    await_preview(1)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "Українська DEMO option failed to start Ukrainian independently")
    e.shot(str(out / "ukrainian-direct-menu.png"))
    e.press("B", settle=16)
    await_title()
    check(e.state()["state"] == 0, "B must exit Ukrainian DEMO to normal title")
    # Also cover controller-only selection. The normal saved language stays
    # English, so UP from the first entry must wrap to Ukrainian DEMO.
    e.tap(55, 14, settle=12)
    check(e.state()["state"] == 8, "cannot reopen language menu for D-pad test")
    e.press("UP", settle=6)
    e.press("A", settle=18)
    await_preview(1)
    check(e.state()["state"] == 9 and get_int(e, "scriptPreviewLanguage") == 1,
          "D-pad UP wrapping / A should choose Українська DEMO")
    exercise_loss(e, out, scorch=False)
    e.press("START", settle=16)
    await_title()
    check(e.state()["state"] == 0, "controller-selected Ukrainian DEMO must exit safely")
    # L+SELECT offers UNRANKED Daily practice in the separate demo ROM.
    # On an emulator with no valid DS RTC, it must refuse the mode safely.
    e.tap(55, 14, settle=12)
    e.tap(80, 161, settle=20)
    await_preview(0)
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
        await_preview(1)
        check(get_int(e, "scriptPreviewDaily") == 1 and
              get_int(e, "scriptPreviewDailyDate") == first_date and
              get_int(e, "scriptPreviewSeed") != first_seed,
              "Ukrainian Daily must preserve the day but use its own board")
        e.press("X", settle=18)
        await_preview(0)
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
    await_title()
    check(e.state()["state"] == 0, "Daily demo exits safely without changing retail Daily")
    print("RUSSIAN/UKRAINIAN DS OK: menu, hints, score/retry banking, combo, Daily practice seed and safe exit")
    from cyr_lifecycle import exercise
    exercise(e, rom, out)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_cyr_preview.py wordPass-cyr-preview.nds output-dir")
    main(sys.argv[1], sys.argv[2])
