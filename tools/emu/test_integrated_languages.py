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
                # scriptPreviewLanguage changes during the blocking load,
                # before the next input poll, preference write and draw.
                # Synchronize to two complete main-loop ticks with no keys.
                frame=e.state()["frame"]
                for _ in range(3000):
                    e.run(1)
                    if e.state()["frame"]>=frame+2: break
                else: raise AssertionError("game did not resume scanning input")
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
    def career(lang):
        a=symbol(elf,"scriptCareer")+32*lang
        return tuple(int.from_bytes(bytes(e.mem.unsigned[a+i:a+i+4]),"little") for i in range(0,32,4))
    assert e.state()["state"]==ST_LANGPICK
    select(11)
    assert not any(buffers()),"menu must not retain Unicode pack heap"
    e.shot(str(out/"ru-title.png"))
    # Options are reachable from the shared title and change actual
    # Cyrillic fire progression. Unsupported common-only filtering is inert.
    e.tap(86,170,settle=12)
    wait_state(5)
    fo=member_offsets(elf,"S",("fire","library"))
    def option(name): return int(e.mem.unsigned[addr+fo[name]])
    assert option("fire")==1
    e.tap(130,32+13*4+6,settle=12)
    assert option("fire")==2
    library=option("library")
    e.tap(130,32+13*6+6,settle=12)
    assert option("library")==library
    e.shot(str(out/"ru-options.png"))
    e.press("B",settle=16)
    wait_state(ST_TITLE)
    e.tap(86,116,settle=18)
    wait_state(9,0)
    assert integer("scriptPreviewTraining")==1 and integer("scriptPreviewTurns")==0
    ro=member_offsets(elf,"scriptPreviewRules",("burnEvery","heatThreshold"))
    ra=symbol(elf,"scriptPreviewRules")
    def rule(name): return int(e.mem.unsigned[ra+ro[name]])
    assert rule("burnEvery")==4 and rule("heatThreshold")==8
    assert any(buffers()) and preference()==11
    e.press("R",settle=12)
    e.press("A",settle=12)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewScore")>0
    e.shot(str(out/"ru-tutorial.png"))
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    assert preference()==11 and not any(buffers())
    assert career(0)==career(1)==(0,)*8,"training must not record a career attempt"
    e.power_cycle()
    wait_state(ST_TITLE)
    assert preference()==11,"Russian selection was lost on reboot"
    e.tap(52,147,settle=18)
    wait_state(9,0)
    assert not integer("scriptPreviewTraining") and not integer("scriptPreviewDaily")
    assert rule("burnEvery")==1 and rule("heatThreshold")==6
    e.press("R",settle=12)
    e.press("A",settle=12)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewScore")>0
    e.shot(str(out/"ru-classic.png"))
    # Play a real terminal shelf. Secret positions are a test-only fixture
    # read; all scored reveals still go through physical DS touch input.
    board=symbol(elf,"scriptPreviewBoard")
    offsets=member_offsets(elf,"scriptPreviewBoard",("back","passLen"))
    length=int(e.mem.unsigned[board+offsets["passLen"]])
    raw=bytes(e.mem.unsigned[board+offsets["back"]:board+offsets["back"]+56])
    for n in range(1,length+1):
        if career(0)[1]: break # a real scorch loss is also a terminal attempt
        c,r=divmod(raw.index(n),8)
        e.hold("L")
        e.tap(*e.center(c,r),settle=12)
        e.hold("L",False)
    assert career(0)[1]==1 and career(0)[3]>=1 and career(0)[6]>0,career(0)
    assert career(1)==(0,)*8
    ru=career(0)
    e.run(120)
    assert career(0)==ru,"terminal shelf must never double count"
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    e.press("X",settle=16)
    wait_state(6)
    e.shot(str(out/"ru-career.png"))
    e.press("B",settle=16)
    wait_state(ST_TITLE)
    select(12)
    e.power_cycle()
    wait_state(ST_TITLE)
    assert career(0)==ru and career(1)==(0,)*8,"career save must survive power cycle separately"
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
    assert rule("burnEvery")==4 and rule("heatThreshold")==8,"Daily must ignore fire preference"
    e.shot(str(out/"uk-daily.png"))
    assert career(0)==ru and career(1)==(0,)*8,"Daily must not contaminate Classic records"
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
