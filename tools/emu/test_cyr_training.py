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

    def stage_strip(step):
        e.run(8)
        px=e.pixels()
        colors=[px[20+58*i,144] for i in range(4)]
        if step<4:
            assert len(set(colors[step:]))==1, ("inconsistent unstarted stages",step,colors)
            assert colors[step-1]!=colors[step], ("active stage not highlighted",step,colors)
        if step>1:
            assert len(set(colors[:step-1]))==1 and colors[step-2]!=colors[step-1], (
                "completed stages not distinguished",step,colors)
        return colors
    def evidence():
        e.run(8)
        px=e.pixels()
        return tuple(px[x,y] for y in range(114,128) for x in range(96,177))

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
        state_off = member_offsets(e.rom_elf, "scriptPreviewBoard", ("state",))["state"]
        def flames():
            return sum(int(e.mem.unsigned[board + state_off + c*8 + r]) == 2
                       for c in range(7) for r in range(8))
        assert flames() == 1, code
        strip=stage_strip(1)
        e.shot(str(out / f"{code}-01-word.png"))
        e.hold("L")
        e.tap(*e.center(6, 0), settle=8)
        e.hold("L", False)
        assert integer("scriptPreviewTurns") == 0
        coaching(code, "01-early-peel", 4)
        path = [(int(e.mem.unsigned[nc+i]), int(e.mem.unsigned[nr+i])) for i in range(n)]
        offsets = member_offsets(e.rom_elf, "scriptPreviewBoard", ("chainLen", "chainC", "chainR"))
        def prompt_row():
            px = e.pixels()
            return tuple(px[x, y] for y in range(160, 167) for x in range(12, 244))
        before = prompt_row()
        for _ in range(2):
            e.press("R", settle=12)
            assert integer("scriptPreviewTurns") == 0 and integer("scriptPreviewHints") == 0
            assert int(e.mem.unsigned[board+offsets["chainLen"]]) == n
            assert [(int(e.mem.unsigned[board+offsets["chainC"]+i]),
                     int(e.mem.unsigned[board+offsets["chainR"]+i]))
                    for i in range(n)] == path
        assert prompt_row() != before, (code, "missing ready-to-submit prompt")
        e.shot(str(out / f"{code}-01-ready.png"))
        e.press("Y", settle=8)
        e.chain(path)
        points = integer("chainScoreNow")
        assert points > 0
        e.press("A", settle=16)
        assert integer("scriptPreviewTurns") == 1 and integer("scriptPreviewScore") == points
        assert flames() == 0, code
        assert integer("scriptPreviewTrainingNotice") == 0
        assert stage_strip(2)!=strip, code
        e.shot(str(out / f"{code}-02-peel.png"))
        e.hold("L")
        e.tap(*e.center(0, 7), settle=8)
        e.hold("L", False)
        assert integer("scriptPreviewTurns") == 1
        coaching(code, "02-wrong-target", 2)
        e.hold("L")
        e.tap(*e.center(6, 0), settle=8)
        e.hold("L", False)
        assert integer("scriptPreviewTurns") == 2
        assert integer("scriptPreviewTrainingNotice") == 0
        assert int(e.mem.unsigned[board + 56 + 6*8]) == 0
        stage_strip(3)
        initial_evidence=evidence()
        assert len(set(initial_evidence))>2, (code,"deduction readout missing")
        e.shot(str(out / f"{code}-03-clue.png"))
        e.hold("L")
        e.tap(*e.center(6, 1), settle=8)
        e.hold("L", False)
        assert int(e.mem.unsigned[board + 218]) == 1
        stage_strip(4)
        e.shot(str(out / f"{code}-04-password.png"))
        length = int(e.mem.unsigned[board + 227])
        clue_off = member_offsets(e.rom_elf, "scriptPreviewBoard", ("clueC","clueR"))
        def tile_state(c,r):
            return int(e.mem.unsigned[board + state_off + c*8 + r])
        e.hold("L")
        e.tap(*e.center(0,7), settle=8)
        e.hold("L", False)
        assert tile_state(0,7) == 0 and integer("scriptPreviewTurns") == 4
        assert evidence()!=initial_evidence, (code,"stale 1/1 readout after free focus change")
        before = (integer("scriptPreviewTurns"),integer("scriptPreviewScore"),
                  integer("scriptPreviewPeels"),integer("scriptPreviewHints"))
        for c,r,peek in ((3,5,False),(4,6,True)):
            if peek: e.hold("L")
            e.tap(*e.center(c,r), settle=12)
            if peek: e.hold("L",False)
            assert before == (integer("scriptPreviewTurns"),integer("scriptPreviewScore"),
                              integer("scriptPreviewPeels"),integer("scriptPreviewHints")), code
            assert int(e.mem.unsigned[board + clue_off["clueC"]])==c, code
            assert int(e.mem.unsigned[board + clue_off["clueR"]])==r, code
        for _ in range(length-1):
            before = (integer("scriptPreviewTurns"), integer("scriptPreviewScore"),
                      integer("scriptPreviewHints"))
            e.press("R", settle=12)
            assert before == (integer("scriptPreviewTurns"), integer("scriptPreviewScore"),
                              integer("scriptPreviewHints"))
            c = int(e.mem.unsigned[board + clue_off["clueC"]])
            r = int(e.mem.unsigned[board + clue_off["clueR"]])
            assert tile_state(c,r)==0
            covered = [(cc,rr) for cc,rr in e.neighbors(c,r) if tile_state(cc,rr)==1]
            assert covered, code
            cc,rr = covered[-1]
            e.hold("L")
            e.tap(*e.center(cc,rr), settle=8)
            e.hold("L", False)
            assert tile_state(cc,rr)==3, code
        rules = symbol(e.rom_elf, "scriptPreviewRules")
        status = member_offsets(e.rom_elf, "scriptPreviewRules", ("status", "ignitions"))
        assert int(e.mem.unsigned[rules+status["status"]]) == 1
        assert integer("scriptPreviewTurns") == length+3
        assert integer("scriptPreviewPeels") == length+2
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
