#!/usr/bin/env python3
"""Actual DS checkpoint persistence on a FAT card, including a reboot.
Uses the no-argv NitroFS path so the card must be addressed explicitly.
"""
from pathlib import Path
import re
import sys
from fatrig import FatEmu, patch_dldi, make_card, card_files, read_cfg
from wp import symbol
from elf_abi import member_offsets


def integer(e, name):
    a = symbol(e.rom_elf, name)
    return int.from_bytes(bytes(e.mem.unsigned[a:a + 4]), "little")


def expect(e, language):
    for _ in range(2400):
        if e.state()["state"] == 9 and integer(e, "scriptPreviewLanguage") == language:
            return
        e.run(1)
    raise AssertionError("failed to enter the requested Cyrillic language")


def enter(e, language):
    assert e.state()["state"] == 0, "checkpoint fixture requires title"
    e.tap(55, 14, settle=16)
    assert e.state()["state"] == 8
    e.tap(80, 173 if language else 161, settle=24)
    expect(e, language)


def chord(e, key):
    e.hold("L")
    e.press(key, settle=18)
    e.hold("L", False)


def main(rom, elf, output):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    dldi = patch_dldi(rom, str(out / "wordPass-checkpoint.nds"))
    card = str(out / "checkpoint-card.img")
    make_card(card, [("wordPass.nds", Path(dldi).read_bytes())])
    e = FatEmu(dldi, elf, card, argv=False, dismiss=True)
    assert e.flags()["fatReady"] and e.cwd()[0] == "nitro"
    retail = read_cfg(card)
    assert retail is not None, "baseline English settings must exist"
    enter(e, 0)
    board = symbol(e.rom_elf, "scriptPreviewBoard")
    offsets = member_offsets(e.rom_elf, "scriptPreviewBoard", ("clueC", "clueR"))
    end = board + max(offsets.values()) + 1
    # Play a real first password and advance. Read secret coordinates only
    # in the test fixture; the actual ARM9 peel and scoring paths do the work.
    length = int(e.mem.unsigned[board + 227])
    secret = {}
    for i in range(56):
        n = int(e.mem.unsigned[board + 153 + i])
        if n:
            secret[n] = divmod(i, 8)
    assert sorted(secret) == list(range(1, length + 1))
    e.hold("L")
    for i in range(1, length + 1):
        e.tap(*e.center(*secret[i]), settle=8)
    e.hold("L", False)
    e.press("A", settle=20)
    assert integer(e, "scriptPreviewShelf") == 2
    score = integer(e, "scriptPreviewScore")
    seed = integer(e, "scriptPreviewSeed")
    saved_board = bytes(e.mem.unsigned[board:end])
    assert score > 0
    chord(e, "START")
    assert integer(e, "scriptPreviewCheckpointStatus") == 1, "save failed"
    assert e.state()["state"] == 9
    e.shot(str(out / "ru-checkpoint-saved.png"))

    e.press("X", settle=24)
    expect(e, 1)
    chord(e, "B")
    assert integer(e, "scriptPreviewCheckpointStatus") == 4, "Russian checkpoint leaked to Ukrainian"
    uk_seed = integer(e, "scriptPreviewSeed")
    chord(e, "START")
    assert integer(e, "scriptPreviewCheckpointStatus") == 1
    files = card_files(card)
    records = [n for n, size in files if size == 32 and
               re.fullmatch(r"[RU][0-9A-F]{7}\.C[0-9A-F][01]", n.upper())]
    assert len(records) == 2 and {n[0].upper() for n in records} == {"R", "U"}, files
    assert read_cfg(card) == retail, "checkpoint writes changed retail settings"

    e.power_cycle()
    enter(e, 0)
    assert integer(e, "scriptPreviewShelf") == 1
    chord(e, "B")
    assert integer(e, "scriptPreviewCheckpointStatus") == 2, "restore failed after reboot"
    assert integer(e, "scriptPreviewShelf") == 2
    assert integer(e, "scriptPreviewTurns") == 0
    assert integer(e, "scriptPreviewScore") == score
    assert integer(e, "scriptPreviewSeed") == seed
    assert bytes(e.mem.unsigned[board:end]) == saved_board
    e.shot(str(out / "ru-checkpoint-restored.png"))
    e.press("X", settle=24)
    expect(e, 1)
    chord(e, "B")
    assert integer(e, "scriptPreviewCheckpointStatus") == 2
    assert integer(e, "scriptPreviewSeed") == uk_seed
    assert integer(e, "scriptPreviewShelf") == 1
    assert integer(e, "scriptPreviewScore") == 0
    assert read_cfg(card) == retail
    print("CYR CHECKPOINT FAT OK: separate RU/UK, no-argv, reboot, exact restored shelf and retail isolation")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_cyr_checkpoint.py preview.nds preview.elf output-dir")
    main(*sys.argv[1:])
