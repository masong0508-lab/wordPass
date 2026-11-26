#!/usr/bin/env python3
"""Source-backed main-game integration: real title, preference and launch path."""
from pathlib import Path
import sys
from fatrig import FatEmu, patch_dldi, make_card
from wp import symbol, ST_TITLE, ST_LANGPICK
from elf_abi import member_offsets

def main(rom, elf, output):
    out=Path(output)
    out.mkdir(parents=True,exist_ok=True)
    dldi=patch_dldi(rom,str(out/"wordPass-integrated.nds"))
    card=str(out/"integrated-card.img")
    make_card(card,[("wordPass.nds",Path(dldi).read_bytes())])
    e=FatEmu(dldi,elf,card,argv=False,dismiss=False)
    assert e.flags()["fatReady"] and e.flags()["fsReady"]
    so=member_offsets(elf,"S",("language","languageChosen"))
    addr=symbol(elf,"S")
    def preference():
        return int(e.mem.unsigned[addr+so["language"]])
    def integer(name):
        a=symbol(elf,name)
        return int.from_bytes(bytes(e.mem.unsigned[a:a+4]),"little")
    def wait_state(state, script=None):
        for _ in range(3000):
            if e.state()["state"]==state and (script is None or integer("scriptPreviewLanguage")==script):
                return
            e.run(1)
        raise AssertionError((state,script,e.state()["state"],integer("scriptPreviewOpenError")))
    def select(idx):
        if e.state()["state"]==ST_TITLE:
            e.tap(55,14,settle=12)
        wait_state(ST_LANGPICK)
        e.tap(80,23+idx*12+5,settle=16)
        wait_state(ST_TITLE)
        assert preference()==idx
        assert int(e.mem.unsigned[addr+so["languageChosen"]])==1
    def buffers():
        a=symbol(elf,"images")
        return bytes(e.mem.unsigned[a:a+12])
    assert e.state()["state"]==ST_LANGPICK
    select(11)
    assert not any(buffers()),"menu must not retain Unicode pack heap"
    e.shot(str(out/"ru-title.png"))
    e.tap(86,116,settle=18)
    wait_state(9,0)
    assert integer("scriptPreviewTraining")==1 and integer("scriptPreviewTurns")==0
    assert any(buffers()) and preference()==11
    e.press("R",settle=12)
    e.press("A",settle=12)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewScore")>0
    e.shot(str(out/"ru-tutorial.png"))
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    assert preference()==11 and not any(buffers())
    e.power_cycle()
    wait_state(ST_TITLE)
    assert preference()==11,"Russian selection was lost on reboot"
    e.tap(52,147,settle=18)
    wait_state(9,0)
    assert not integer("scriptPreviewTraining") and not integer("scriptPreviewDaily")
    e.press("R",settle=12)
    e.press("A",settle=12)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewScore")>0
    e.shot(str(out/"ru-classic.png"))
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    select(12)
    e.power_cycle()
    wait_state(ST_TITLE)
    assert preference()==12,"Ukrainian selection was lost on reboot"
    e.shot(str(out/"uk-title.png"))
    e.tap(86,116,settle=18)
    wait_state(9,1)
    assert integer("scriptPreviewTraining")==1
    # Change languages inside the tutorial: preserve the lesson, not score.
    e.press("X",settle=18)
    wait_state(9,0)
    assert preference()==11 and integer("scriptPreviewTraining")==1
    e.press("X",settle=18)
    wait_state(9,1)
    assert preference()==12 and integer("scriptPreviewTraining")==1
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    e.set_clock(20261126)
    e.tap(120,147,settle=18)
    wait_state(9,1)
    assert integer("scriptPreviewDaily")==1 and integer("scriptPreviewDailyDate")==20261126
    e.shot(str(out/"uk-daily.png"))
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    select(6)
    assert e.state()["language"]==6 and not any(buffers())
    e.power_cycle()
    wait_state(ST_TITLE)
    assert preference()==6 and e.state()["language"]==6
    print("INTEGRATED 13 LANGUAGES OK: title, persisted RU/UK, tutorial, Classic, Daily practice, Latin return and FAT reboot",flush=True)

if __name__=="__main__":
    if len(sys.argv)!=4:
        raise SystemExit("usage: test_integrated_languages.py wordPass.nds wordPass.elf output")
    main(*sys.argv[1:])
