#!/usr/bin/env python3
"""Play the dedicated RU/UK tutorial on both real DS language packs."""
from pathlib import Path
import sys
from wp import Emu, symbol
from elf_abi import member_offsets


def main(rom, output):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    e = Emu(rom)
    e.run(100)
    if e.state()["state"] == 8:
        e.press("A", settle=30)
    assert e.state()["state"] == 0
    def integer(name):
        a = symbol(e.rom_elf, name)
        return int.from_bytes(bytes(e.mem.unsigned[a:a+4]), "little")
    def coaching(code, tag, notice):
        assert integer("scriptPreviewTrainingNotice") == notice, (code, tag)
        px = e.pixels()
        amber = sum(1 for y in range(172, 179) for x in range(13, 243)
                    if px[x, y][0] >= 240 and 170 <= px[x, y][1] <= 220 and px[x, y][2] <= 65)
        assert amber > 20, (code, tag, "missing visible coaching", amber)
        e.shot(str(out / f"{code}-{tag}.png"))

    def expect(language):
        for _ in range(40):
            if e.state()["state"] == 9 and integer("scriptPreviewLanguage") == language:
                return
            e.run(60)
        raise AssertionError("cannot load language")
    for language, code in ((0, "ru"), (1, "uk")):
        e.tap(55, 14, settle=16)
        e.tap(80, 173 if language else 161, settle=24)
        expect(language)
        e.tap(215, 12, settle=18)
        assert integer("scriptPreviewTraining") == 1
        assert integer("scriptPreviewTurns") == 0
        board = symbol(e.rom_elf, "scriptPreviewBoard")
        nc = symbol(e.rom_elf, "scriptPreviewTrainingC")
        nr = symbol(e.rom_elf, "scriptPreviewTrainingR")
        n = integer("scriptPreviewTrainingLen")
        assert 4 <= n <= 6
        e.shot(str(out / f"{code}-01-word.png"))
        e.hold("L")
        e.tap(*e.center(1, 2), settle=8)
        e.hold("L", False)
        assert integer("scriptPreviewTurns") == 0
        coaching(code, "01-early-peel", 4)
        e.chain([(int(e.mem.unsigned[nc+i]), int(e.mem.unsigned[nr+i])) for i in range(n)])
        points = integer("chainScoreNow")
        assert points > 0
        e.press("A", settle=16)
        assert integer("scriptPreviewTurns") == 1 and integer("scriptPreviewScore") == points
        assert integer("scriptPreviewTrainingNotice") == 0
        e.shot(str(out / f"{code}-02-peel.png"))
        e.hold("L")
        e.tap(*e.center(6, 0), settle=8)
        e.hold("L", False)
        assert integer("scriptPreviewTurns") == 1
        coaching(code, "02-wrong-target", 2)
        e.hold("L")
        e.tap(*e.center(1, 2), settle=8)
        e.hold("L", False)
        assert integer("scriptPreviewTurns") == 2
        assert integer("scriptPreviewTrainingNotice") == 0
        assert int(e.mem.unsigned[board + 56 + 1*8 + 2]) == 0
        e.shot(str(out / f"{code}-03-clue.png"))
        e.hold("L")
        e.tap(*e.center(1, 3), settle=8)
        e.hold("L", False)
        assert int(e.mem.unsigned[board + 218]) == 1
        e.shot(str(out / f"{code}-04-password.png"))
        length = int(e.mem.unsigned[board + 227])
        targets = [(3,4),(4,5),(5,6),(6,7),(3,6),(4,7),(6,4),(5,4)]
        e.hold("L")
        for cell in targets[:length-1]:
            e.tap(*e.center(*cell), settle=8)
        e.hold("L", False)
        rules = symbol(e.rom_elf, "scriptPreviewRules")
        status = member_offsets(e.rom_elf, "scriptPreviewRules", ("status", "ignitions"))
        assert int(e.mem.unsigned[rules+status["status"]]) == 1
        assert integer("scriptPreviewTurns") == length+2
        assert integer("scriptPreviewPeels") == length+1
        assert int.from_bytes(bytes(e.mem.unsigned[rules+status["ignitions"]:rules+status["ignitions"]+2]), "little") == 0
        e.shot(str(out / f"{code}-05-complete.png"))
        e.press("A", settle=20)
        assert not integer("scriptPreviewTraining") and integer("scriptPreviewScore") == 0
        assert integer("scriptPreviewShelf") == 1 and integer("scriptPreviewTurns") == 0
        e.press("R", settle=12)
        e.press("A", settle=16)
        points = integer("scriptPreviewScore")
        assert points > 0 and integer("scriptPreviewTurns") == 1
        e.tap(215, 12, settle=12)
        assert not integer("scriptPreviewTraining") and integer("scriptPreviewScore") == points
        e.press("START", settle=16)
        for _ in range(24):
            if e.state()["state"] == 0:
                break
            e.run(60)
        assert e.state()["state"] == 0
    print("CYR TRAINING DS OK: two native-language lessons, real word/peel/clue completion and fresh normal game")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_cyr_training.py rom.nds output-dir")
    main(*sys.argv[1:])
