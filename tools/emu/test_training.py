#!/usr/bin/env python3
"""Play the actual four-step retail lesson through real touch inputs.
No injection of words, peels, scores or answers. The teaching cues are
the guide; the test reads only their exposed path plus public progress.
"""
from pathlib import Path
import sys
from wp import Emu, ST_TITLE, ST_PLAY, ST_PASSED, ST_OPTIONS, HOLE, LANG_CODES, REPO, symbol
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

    messages = {}
    for line in (Path(REPO) / "tools/lang/ui_strings.tsv").read_text(encoding="utf-8").splitlines():
        row = line.split("\t")
        if row[0] in ("TU_DRAG", "TU_FIND"):
            messages[row[0]] = row[4:]
    def error_feedback(lang, key):
        s = e.state()
        assert s["msgTimer"] > 0 and s["msg"] == messages[key][lang][:31], (lang, key, s["msg"])
        # Check the actually rendered reminder, not just its debug mirror.
        px = e.pixels()
        count = sum(1 for y in range(172, 179) for x in range(12, 244)
                    if px[x, y][0] >= 240 and 170 <= px[x, y][1] <= 220 and px[x, y][2] <= 65)
        assert count > 20, (lang, key, "missing amber reminder", count)

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
        assert s["cell"][5][0][1] == HOLE, code
        e.peel(6, 0)
        assert e.state()["turn"] == 0 and e.state()["peels"] == 0
        error_feedback(lang, "TU_DRAG")
        n = s["hintLen"]
        path = [(int(e.mem.unsigned[game + off["hintC"] + i]),
                 int(e.mem.unsigned[game + off["hintR"] + i])) for i in range(n)]
        # SELECT must retain the numbered teaching word and cost nothing.
        for _ in range(2):
            e.press("SELECT", settle=12)
            hinted = e.state()
            assert hinted["hintLen"] == n and hinted["hints"] == s["hints"], code
            assert hinted["turn"] == 0 and hinted["score"] == 0, code
            assert [(int(e.mem.unsigned[game + off["hintC"] + i]),
                     int(e.mem.unsigned[game + off["hintR"] + i]))
                    for i in range(n)] == path, code
        def prompt_row():
            px = e.pixels()
            return tuple(px[x, y] for y in range(160, 167) for x in range(12, 244))
        before = prompt_row()
        e.chain(path)
        assert e.state()["chainLen"] == n and e.state()["chainScore"] > 0, code
        e.run(8)
        assert prompt_row() != before, (code, "missing ready-to-submit prompt")
        e.shot(str(out / f"{code}-01-ready.png"))
        e.press("A", settle=40)
        s = e.state()
        assert s["turn"] == 1 and s["score"] > 0, code
        e.shot(str(out / f"{code}-02-peel.png"))
        e.peel(0, 7)
        assert e.state()["turn"] == 1 and e.state()["peels"] == 0, code
        error_feedback(lang, "TU_FIND")
        e.peel(6, 0)
        s = e.state()
        assert s["turn"] == 2 and s["cell"][6][0][1] == HOLE, code
        assert not s["msgTimer"] or s["msg"] != messages["TU_FIND"][lang][:31], code
        e.shot(str(out / f"{code}-03-clue.png"))
        e.peel(6, 1)
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
