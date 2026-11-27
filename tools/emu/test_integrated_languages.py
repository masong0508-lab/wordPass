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
    def daily(lang):
        a=symbol(elf,"scriptDailyCareer")+32*lang
        return tuple(int.from_bytes(bytes(e.mem.unsigned[a+i:a+i+4]),"little") for i in range(0,32,4))
    def enter_daily(lang,date,rank):
        e.set_clock(date)
        e.tap(120,147,settle=18)
        wait_state(9,lang)
        assert integer("scriptPreviewDaily")==1 and integer("scriptPreviewDailyDate")==date
        assert integer("scriptPreviewDailyRanked")==rank,(lang,date,rank,daily(lang))
    def leave_daily():
        e.press("START",settle=20)
        wait_state(ST_TITLE)
    assert e.state()["state"]==ST_LANGPICK
    select(11)
    assert not any(buffers()),"menu must not retain Unicode pack heap"
    e.shot(str(out/"ru-title.png"))
    def guide_pages(tag,return_state):
        wait_state(7)
        assert e.state()["guidePage"]==0
        for page in range(8):
            assert e.state()["guidePage"]==page
            e.run(12)
            e.shot(str(out/f"{tag}-guide-{page+1}.png"))
            if page<7: e.press("RIGHT",settle=8)
        e.press("B",settle=12)
        wait_state(return_state)
    e.press("B",settle=12)
    guide_pages("ru",ST_TITLE)
    assert not any(buffers())
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
    # Actual framebuffer fast path: all 56 fresh ordinary gems use the
    # Unicode atlas, and steady play must not regenerate any composites.
    e.run(12)
    count=integer("scriptCompositeBuilds")
    assert count>=1 and integer("scriptCompositeTilesLast")==56
    e.run(120)
    assert integer("scriptCompositeBuilds")==count
    assert integer("scriptCompositeTilesLast")==56
    board=symbol(elf,"scriptPreviewBoard")
    offsets=member_offsets(elf,"scriptPreviewBoard",("back","passLen","chainLen","chainC","chainR","state","tile"))
    def check_atlas_pixels(tag):
        # The 35 Latin composites precede the 64 Unicode slots. Check the
        # actual touch LCD, not only the reported number of cache hits.
        # Settle the flip and compare interior pixels, avoiding clue badges
        # and selection borders. Fresh boards contain no active overlays.
        e.run(120)
        assert integer("scriptCompositeTilesLast")==56
        pixels=e.pixels()
        atlas=0x06860000+35*960
        ids=bytes(e.mem.unsigned[board+offsets["tile"]:board+offsets["tile"]+56])
        states=bytes(e.mem.unsigned[board+offsets["state"]:board+offsets["state"]+56])
        checked=set()
        for i,id in enumerate(ids):
            if states[i]!=1 or id in checked: continue
            c,r=divmod(i,8)
            x,y,_,_=e.cell_rect(c,r)
            data=bytes(e.mem.unsigned[atlas+id*960:atlas+(id+1)*960])
            assert any(data),(tag,id,"empty VRAM sprite")
            for yy in range(4,17):
                for xx in range(6,15):
                    j=2*((yy-1)*24+xx)
                    v=int.from_bytes(data[j:j+2],"little")
                    expected=(v&31,(v>>5)&31,(v>>10)&31)
                    actual=tuple(ch>>3 for ch in pixels[x+xx,y+yy])
                    assert actual==expected,(tag,c,r,id,xx,yy,actual,expected)
            checked.add(id)
            if len(checked)==6: break
        assert len(checked)>=4,(tag,checked)
        e.shot(str(out/f"{tag}-vram-cache.png"))
    check_atlas_pixels("ru")
    def touch_action(row):
        e.tap(215,118+18*row,settle=12)
    def board_byte(name):
        return int(e.mem.unsigned[board+offsets[name]])
    # Real touch hint, clear and submit share the existing word engine.
    touch_action(2)
    n=board_byte("chainLen")
    assert 3<=n<=20 and integer("scriptPreviewHints")==1
    path=[(int(e.mem.unsigned[board+offsets["chainC"]+i]),
           int(e.mem.unsigned[board+offsets["chainR"]+i])) for i in range(n)]
    touch_action(1)
    assert board_byte("chainLen")==0 and integer("scriptPreviewTurns")==0
    touch_action(2)
    assert board_byte("chainLen")==0 and integer("scriptPreviewHints")==1
    e.chain(path)
    touch_action(0)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewScore")>0
    e.shot(str(out/"ru-classic.png"))
    # Play a real terminal shelf. Secret positions are a test-only fixture
    # read; all scored reveals still go through physical DS touch input.
    length=int(e.mem.unsigned[board+offsets["passLen"]])
    raw=bytes(e.mem.unsigned[board+offsets["back"]:board+offsets["back"]+56])
    for n in range(1,length+1):
        if career(0)[1]: break # a real scorch loss is also a terminal attempt
        c,r=divmod(raw.index(n),8)
        if n==1:
            touch_action(3)
            assert integer("scriptPreviewTouchPeek")==1
            prior=integer("scriptPreviewTurns")
            e.emu.input.touch_set_pos(*e.center(c,r))
            e.run(5)
            assert integer("scriptPreviewTurns")==prior
            e.emu.input.touch_set_pos(*e.center(c,(r+1)%8))
            e.run(5)
            e.emu.input.touch_release()
            e.run(12)
            assert integer("scriptPreviewTurns")==prior
            assert integer("scriptPreviewTouchPeek")==1
            e.tap(*e.center(c,r),settle=12)
            assert integer("scriptPreviewTurns")==prior+1
            assert integer("scriptPreviewTouchPeek")==0
        else:
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
    e.press("B",settle=12)
    guide_pages("uk",ST_TITLE)
    e.tap(86,116,settle=18)
    wait_state(9,1)
    assert integer("scriptPreviewTraining")==1
    # The guide must work in a live tutorial without destroying progress.
    e.press("R",settle=12)
    stable=bytes(e.mem.unsigned[board:board+228])
    e.hold("L")
    e.press("Y",settle=12)
    e.hold("L",False)
    guide_pages("uk-live",9)
    assert bytes(e.mem.unsigned[board:board+228])==stable
    assert integer("scriptPreviewTraining")==1
    # Change languages inside the tutorial: preserve the lesson, not score.
    count=integer("scriptCompositeBuilds")
    e.press("X",settle=18)
    wait_state(9,0)
    assert preference()==11 and integer("scriptPreviewTraining")==1
    e.run(8)
    assert integer("scriptCompositeBuilds")==count+1
    assert integer("scriptCompositeTilesLast")>0
    e.press("X",settle=18)
    wait_state(9,1)
    assert preference()==12 and integer("scriptPreviewTraining")==1
    e.run(8)
    assert integer("scriptCompositeBuilds")==count+2
    e.press("START",settle=20)
    wait_state(ST_TITLE)
    assert daily(0)==daily(1)==(0,)*8
    enter_daily(1,20261126,1)
    check_atlas_pixels("uk")
    assert rule("burnEvery")==4 and rule("heatThreshold")==8,"Daily must ignore fire preference"
    assert daily(1)[1:]==(20261126,0,0,0,0,0,0)
    puzzle=bytes(e.mem.unsigned[board:board+228])
    seed=integer("scriptPreviewSeed")
    e.press("SELECT",settle=16)
    assert bytes(e.mem.unsigned[board:board+228])==puzzle
    assert integer("scriptPreviewSeed")==seed and integer("scriptPreviewDailyRanked")==1
    e.press("R",settle=12)
    e.press("A",settle=16)
    points=integer("scriptPreviewScore")
    assert points>0
    e.shot(str(out/"uk-daily.png"))
    leave_daily()
    first=daily(1)
    assert first[1:]==(20261126,20261126,points,points,20261126,1,1),first
    e.power_cycle()
    wait_state(ST_TITLE)
    assert daily(1)==first
    enter_daily(1,20261126,0)
    assert integer("scriptPreviewSeed")==seed
    assert bytes(e.mem.unsigned[board:board+228])==puzzle
    # A free clue inspection while touch PEEK is active must not peel twice
    # or spend its one-shot latch. Choose a blank on the fresh practice board.
    backs=bytes(e.mem.unsigned[board+offsets["back"]:board+offsets["back"]+56])
    states=bytes(e.mem.unsigned[board+offsets["state"]:board+offsets["state"]+56])
    i=next(i for i,b in enumerate(backs) if b==0 and states[i]==1)
    c,r=divmod(i,8)
    touch_action(3)
    e.tap(*e.center(c,r),settle=12)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewTouchPeek")==0
    touch_action(3)
    e.tap(*e.center(c,r),settle=12)
    assert integer("scriptPreviewTurns")==1 and integer("scriptPreviewTouchPeek")==1
    touch_action(3)
    assert integer("scriptPreviewTouchPeek")==0
    e.shot(str(out/"uk-touch-clue.png"))
    leave_daily()
    assert daily(1)==first,"practice changed scored results"
    enter_daily(1,20261127,1)
    e.power_cycle() # consume the date even when powered off mid-attempt
    wait_state(ST_TITLE)
    assert daily(1)[1]==20261127 and daily(1)[2:]==first[2:]
    entered=daily(1)
    enter_daily(1,20261127,0)
    leave_daily()
    enter_daily(1,20261126,0)
    leave_daily()
    assert daily(1)==entered
    # Russian can score its own Daily on the same date.
    select(11)
    enter_daily(0,20261127,1)
    e.press("R",settle=12)
    e.press("A",settle=16)
    raw=bytes(e.mem.unsigned[board+offsets["back"]:board+offsets["back"]+56])
    length=int(e.mem.unsigned[board+offsets["passLen"]])
    for n in range(1,length+1):
        if integer("scriptPreviewDailyRanked")==2: break
        c,r=divmod(raw.index(n),8)
        e.hold("L")
        e.tap(*e.center(c,r),settle=12)
        e.hold("L",False)
    assert integer("scriptPreviewDailyRanked")==2
    result=daily(0)
    assert result[1]==result[2]==20261127 and result[7]==1
    e.run(60)
    assert daily(0)==result and daily(1)==entered
    e.press("A",settle=24)
    wait_state(ST_TITLE)
    e.press("X",settle=16)
    wait_state(6)
    e.shot(str(out/"ru-daily-ledger.png"))
    e.press("B",settle=16)
    wait_state(ST_TITLE)
    assert career(0)==ru and career(1)==(0,)*8,"Daily must not contaminate Classic records"
    select(6)
    assert e.state()["language"]==6 and not any(buffers())
    e.power_cycle()
    wait_state(ST_TITLE)
    assert preference()==6 and e.state()["language"]==6
    assert daily(0)==result and daily(1)==entered
    # The large Dutch pack is the worst retail RAM case. Exercise a real
    # Latin shelf after returning from both Unicode packs, with the full
    # composite cache in CPU-visible LCDC VRAM D.
    e.start_classic()
    wait_state(1)
    e.set_tutor(1)
    e.press("SELECT",settle=12)
    g=symbol(elf,"G")
    hints=member_offsets(elf,"G",("hintC","hintR"))
    n=e.state()["hintLen"]
    assert 3<=n<=20
    path=[(int(e.mem.unsigned[g+hints["hintC"]+i]),
           int(e.mem.unsigned[g+hints["hintR"]+i])) for i in range(n)]
    e.chain(path)
    assert e.state()["chainScore"]>0
    e.press("A",settle=24)
    assert e.state()["turn"]==1 and e.state()["score"]>0
    e.shot(str(out/"nl-lcd-cache.png"))
    assert any(e.mem.unsigned[0x06860000:0x06860000+26*960]),"LCDC gem cache is empty"
    print("INTEGRATED 13 LANGUAGES OK: persisted selection, guide, career, Classic, Daily and Dutch VRAM rendering",flush=True)

if __name__=="__main__":
    if len(sys.argv)!=4:
        raise SystemExit("usage: test_integrated_languages.py wordPass.nds wordPass.elf output")
    main(*sys.argv[1:])
