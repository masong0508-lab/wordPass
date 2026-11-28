#!/usr/bin/env python3
"""Touch launch, native rendering, hint/word/peel, persistence and return to Latin."""
from pathlib import Path
import sys
from fatrig import FatEmu, patch_dldi, make_card
from wp import symbol
from elf_abi import member_offsets

def main(rom,elf,output,flavor="16"):
    twenty=flavor=="20"
    last=20 if twenty else 16
    out=Path(output);out.mkdir(parents=True,exist_ok=True)
    dldi=patch_dldi(rom,str(out/"wordPass-integrated.nds"))
    card=str(out/"integrated-card.img")
    make_card(card,[("wordPass.nds",Path(dldi).read_bytes())])
    e=FatEmu(dldi,elf,card,argv=False,dismiss=False)
    assert e.flags()["fatReady"] and e.flags()["fsReady"]
    so=member_offsets(elf,"S",("language",))
    def records(name,lang):
        a=symbol(elf,name)+32*lang
        return tuple(int.from_bytes(bytes(e.mem.unsigned[a+i:a+i+4]),"little") for i in range(0,32,4))
    def ranked():
        a=symbol(elf,"cjkDailyRanked")
        return int.from_bytes(bytes(e.mem.unsigned[a:a+4]),"little")
    ga=symbol(elf,"cjkGame")
    fields=member_offsets(elf,"cjkGame",("len","turns","score","focus","tile","back","opened","heat","heatLimit","burnEvery","burnTick","flame","loss","shelf","passLen","foundMask","dealSeed","bankedScore","words"))
    def read(name,n=1):
        a=ga+fields[name]
        return int.from_bytes(bytes(e.mem.unsigned[a:a+n]),"little")
    def wait(state):
        for _ in range(3000):
            if e.state()["state"]==state:
                e.run(16);return
            e.run(1)
        raise AssertionError((state,e.state()["state"]))
    def select(i):
        if e.state()["state"]==0:e.tap(55,14,settle=12)
        wait(8)
        if i>=13:e.tap(213,(23+23*(i-13)+10) if twenty else (28+40*(i-13)+12),settle=12)
        else:e.tap(80,23+i*12+5,settle=12)
        wait(0)
        assert int(e.mem.unsigned[symbol(elf,"S")+so["language"]])==i
    wait(8)
    expected={}
    for i in range(13,last):
        lang=i-11
        assert records("scriptCareer",lang)==(0,)*8
        select(i);e.tap(52,147,settle=18);wait(10)
        cp=read("tile",2)
        if i==16:assert 0xac00<=cp<=0xd7a3
        elif i==17:assert 0xe000<=cp<0xf000
        elif i>=18:assert 0x61<=cp<=0x17f
        else:assert cp>255
        # Both cached VRAM banks must display the same settled board.
        e.run(20);first=e.emu.screenshot().convert("RGB").tobytes()
        e.run(1);second=e.emu.screenshot().convert("RGB").tobytes()
        assert first==second,"stable CJK frame differs between VRAM banks"
        assert read("heatLimit")==10 and read("burnEvery")==4 and read("shelf",2)==1
        e.shot(str(out/f"cjk-{i}-fresh.png"))
        e.tap(215,105,settle=12)
        assert 2<=read("len")<=6
        e.tap(215,49,settle=12)
        assert read("turns",4)==1 and read("score",4)>0 and 0<=read("heat")<=3
        e.tap(215,133,settle=12);e.tap(18,20,settle=12)
        assert read("turns",4)==2
        e.shot(str(out/f"cjk-{i}-played.png"))
        # Controlled public fire fixture: gameplay still uses actual touch.
        # Place one flame at (0,1), then attempt to open directly below.
        e.mem.unsigned[ga+fields["flame"]+1]=1
        before=read("turns",4)
        e.tap(215,133,settle=12);e.tap(18,70,settle=12)
        assert read("turns",4)==before and read("loss")==0
        e.shot(str(out/f"cjk-{i}-fire-warning.png"))
        # Move the flame to the bottom and schedule the next spread tick.
        e.mem.unsigned[ga+fields["flame"]+1]=0
        e.mem.unsigned[ga+fields["flame"]+35]=1
        e.mem.unsigned[ga+fields["burnTick"]]=read("burnEvery")-1
        # Choose a safe non-password covered cell, solely as a test fixture.
        raw=e.mem.unsigned[ga+fields["back"]:ga+fields["back"]+36]
        p=next(p for p in range(1,30) if not raw[p])
        x=18+28*(p//6);y=20+25*(p%6)+12*((p//6)&1)
        e.tap(215,133,settle=12);e.tap(x,y,settle=12)
        assert read("loss")==1 and read("turns",4)==before+1
        lost=records("scriptCareer",lang)
        assert lost[1:4]==(1,0,1),lost
        e.run(60)
        assert records("scriptCareer",lang)==lost,"terminal fire loss double counted"
        e.shot(str(out/f"cjk-{i}-fire-loss.png"))
        old_seed=read("dealSeed",4)
        e.tap(215,161,settle=12)
        assert read("loss")==0 and read("turns",4)==0 and read("score",4)==0 and read("dealSeed",4)==old_seed
        # Open the actual hidden coordinates using real touch input. At most
        # six peels at normal difficulty cannot ignite a fresh shelf.
        raw=bytes(e.mem.unsigned[ga+fields["back"]:ga+fields["back"]+36])
        for p in range(36):
            if raw[p]:
                e.tap(215,133,settle=12)
                e.tap(18+28*(p//6),20+25*(p%6)+12*((p//6)&1),settle=12)
        assert read("foundMask")==((1<<read("passLen"))-1) and not read("loss")
        bank=read("score",4)
        career=records("scriptCareer",lang)
        assert career[1]==2 and career[2]==1 and career[6]>=bank and career[7]==1,career
        e.tap(215,161,settle=12)
        assert read("shelf",2)==2 and read("turns",4)==0 and read("score",4)==bank
        e.press("B",settle=12);wait(0)
        e.tap(86,116,settle=18);wait(10)
        assert read("heatLimit")==0
        e.run(12);lesson1=e.emu.screenshot().convert("RGB").crop((0,171,256,190)).tobytes()
        e.tap(215,105,settle=12);e.tap(215,49,settle=12)
        assert read("words",4)==1
        e.run(12);lesson2=e.emu.screenshot().convert("RGB").crop((0,171,256,190)).tobytes()
        assert lesson1!=lesson2,"training prompt did not progress"
        e.shot(str(out/f"cjk-{i}-training.png"))
        raw=bytes(e.mem.unsigned[ga+fields["back"]:ga+fields["back"]+36])
        for p in range(36):
            if raw[p]:
                e.tap(215,133,settle=12)
                e.tap(18+28*(p//6),20+25*(p%6)+12*((p//6)&1),settle=12)
        assert read("foundMask")==((1<<read("passLen"))-1)
        e.press("B",settle=12);wait(0)
        assert records("scriptCareer",lang)==career,"training or abandoned shelf changed career"
        assert int.from_bytes(bytes(e.mem.unsigned[symbol(elf,"cjkPack"):symbol(elf,"cjkPack")+4]),"little")==0
        e.set_clock(20261127)
        e.tap(120,147,settle=18);wait(10)
        assert ranked()==1
        assert records("scriptDailyCareer",lang)[1]==20261127
        board=bytes(e.mem.unsigned[ga+fields["tile"]:ga+fields["tile"]+72])
        e.tap(215,105,settle=12);e.tap(215,49,settle=12)
        dailyScore=read("score",4);assert dailyScore>0
        e.press("B",settle=12);wait(0)
        daily=records("scriptDailyCareer",lang)
        assert daily[1:4]==(20261127,20261127,dailyScore) and daily[7]==1,daily
        e.tap(120,147,settle=18);wait(10)
        assert ranked()==0 and bytes(e.mem.unsigned[ga+fields["tile"]:ga+fields["tile"]+72])==board
        e.tap(215,105,settle=12);e.tap(215,49,settle=12)
        e.press("B",settle=12);wait(0)
        assert records("scriptDailyCareer",lang)==daily
        assert records("scriptCareer",lang)==career
        e.press("X",settle=12);wait(6);e.shot(str(out/f"cjk-{i}-career.png"))
        e.press("B",settle=12);wait(0)
        expected[lang]=(career,daily)
    # Reserving the next date survives power loss without manufacturing a result.
    e.set_clock(20261128)
    e.tap(120,147,settle=18);wait(10)
    assert ranked()==1
    e.power_cycle();wait(0)
    for lang,(career,daily) in expected.items():
        assert records("scriptCareer",lang)==career
        actual=records("scriptDailyCareer",lang)
        if lang==last-12:
            assert actual[1]==20261128 and actual[2:]==daily[2:]
        else:assert actual==daily
    e.set_clock(20261128);e.tap(120,147,settle=18);wait(10)
    assert ranked()==0
    e.press("B",settle=12);wait(0)
    assert int(e.mem.unsigned[symbol(elf,"S")+so["language"]])==last-1
    select(0);e.tap(52,147,settle=18);wait(1)
    print("CJK INTEGRATION OK: words, peels, fire, retry, progression, training, independent careers, ranked Daily reservation, replay and reboot")
if __name__=="__main__":main(*sys.argv[1:])
