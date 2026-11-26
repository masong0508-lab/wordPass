#!/usr/bin/env python3
"""Play the actual four-step retail lesson through real touch inputs.
No injection of words, peels, scores or answers. The teaching cues are
the guide; the test reads only their exposed path plus public progress.
"""
from pathlib import Path
import sys
from wp import Emu, ST_TITLE, ST_PLAY, ST_PASSED, ST_OPTIONS, HOLE, LANG_CODES, symbol
from elf_abi import member_offsets


def main(rom, output):
    out = Path(output)
    out.mkdir(parents=True, exist_ok=True)
    e = Emu(rom)
    e.run(100)
    if e.state()["state"] == 8:
        e.press("A", settle=30)
    assert e.state()["state"] == ST_TITLE
    game = symbol(e.rom_elf, "G")
    off = member_offsets(e.rom_elf, "G", ("training", "hintC", "hintR"))
    def training():
        return bool(e.mem.unsigned[game + off["training"]])

    # A full real-input playthrough in all retail languages, with both
    # hand layouts exercised and before/after saved-record comparisons.
    for lang, code in enumerate(LANG_CODES):
        assert e.state()["state"] == ST_TITLE
        e.set_language(lang, frames=16)
        assert e.state()["language"] == lang
        left = bool(lang & 1)
        e.tap(e.bx + 86, 170, settle=16)
        assert e.state()["state"] == ST_OPTIONS
        if bool(e.state()["settings"]["hand"]) != left:
            e.press("DOWN", settle=4)
            e.press("DOWN", settle=4)
            e.press("DOWN", settle=4)
            e.press("RIGHT", settle=4)
        e.press("B", settle=16)
        e.set_hand(left)
        assert bool(e.state()["settings"]["hand"]) == left
        records = e.records()
        start = e.state()
        e.tap(e.bx + 86, 116, settle=24)
        e.run(100)
        s = e.state()
        assert s["state"] == ST_PLAY and training() and s["practice"], code
        assert s["turn"] == 0 and 3 <= s["hintLen"] <= 20, code
        e.shot(str(out / f"{code}-01-word.png"))
        # A premature peel is a harmless mistake, not a lost turn.
        e.peel(1, 2)
        assert e.state()["turn"] == 0 and e.state()["peels"] == 0
        n = s["hintLen"]
        path = [(int(e.mem.unsigned[game + off["hintC"] + i]),
                 int(e.mem.unsigned[game + off["hintR"] + i])) for i in range(n)]
        e.chain(path)
        assert e.state()["chainLen"] == n and e.state()["chainScore"] > 0, code
        e.press("A", settle=40)
        s = e.state()
        assert s["turn"] == 1 and s["score"] > 0, code
        e.shot(str(out / f"{code}-02-peel.png"))
        e.peel(6, 0)
        assert e.state()["turn"] == 1 and e.state()["peels"] == 0, code
        e.peel(1, 2)
        s = e.state()
        assert s["turn"] == 2 and s["cell"][1][2][1] == HOLE, code
        e.shot(str(out / f"{code}-03-clue.png"))
        e.peel(1, 3)
        s = e.state()
        assert s["found"][0] == 1 and s["turn"] == 3, code
        e.shot(str(out / f"{code}-04-password.png"))
        # The visible lesson target order is fixed by the practice design;
        # these coordinates do not read or write any ordinary shelf secret.
        targets = [(3,4),(4,5),(5,6),(6,7),(3,6),(4,7),(6,4),(5,4)]
        for c, rr in targets[:s["passLen"]-1]:
            e.peel(c, rr)
        s = e.state()
        assert s["state"] == ST_PASSED and all(s["found"]) and not s["fail"], code
        assert s["turn"] == s["passLen"] + 2 and s["ignitions"] == 0, code
        assert e.records() == records and s["statWords"] == start["statWords"] and s["statGames"] == start["statGames"], code
        e.run(60)
        e.shot(str(out / f"{code}-05-complete.png"))
        if lang == len(LANG_CODES)-1:
            e.press("A", settle=30)
            assert e.state()["state"] == ST_PLAY and not training() and not e.state()["practice"], "completion must enter real Classic"
            assert e.state()["score"] == 0 and e.state()["turn"] == 0
        else:
            e.press("B", settle=16)
            assert e.state()["state"] == ST_TITLE and not training(), code
            assert e.records() == records, code
    print("TRAINING DS OK: 11 languages, both hands, real word/clue/peel lesson, record isolation and Classic transition")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_training.py wordPass.nds output-dir")
    main(*sys.argv[1:])
