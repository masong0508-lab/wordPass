#!/usr/bin/env python3
"""Touch launch, native rendering, hint/word/peel, persistence and return to Latin."""
from pathlib import Path
import sys
from fatrig import FatEmu, patch_dldi, make_card
from wp import symbol
from elf_abi import member_offsets

def main(rom,elf,output):
    out=Path(output);out.mkdir(parents=True,exist_ok=True)
    dldi=patch_dldi(rom,str(out/"wordPass-integrated.nds"))
    card=str(out/"integrated-card.img")
    make_card(card,[("wordPass.nds",Path(dldi).read_bytes())])
    e=FatEmu(dldi,elf,card,argv=False,dismiss=False)
    so=member_offsets(elf,"S",("language",))
    ga=symbol(elf,"cjkGame")
    fields=member_offsets(elf,"cjkGame",("len","turns","score","focus","tile","back","opened","heat","heatLimit","burnEvery","burnTick","flame","loss","shelf","passLen","foundMask","dealSeed","bankedScore"))
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
        if i>=13:e.tap(213,28+40*(i-13)+12,settle=12)
        else:e.tap(80,23+i*12+5,settle=12)
        wait(0)
        assert int(e.mem.unsigned[symbol(elf,"S")+so["language"]])==i
    wait(8)
    for i in range(13,16):
        select(i);e.tap(52,147,settle=18);wait(10)
        assert read("tile",2)>255
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
        e.tap(215,161,settle=12)
        assert read("shelf",2)==2 and read("turns",4)==0 and read("score",4)==bank
        e.press("B",settle=12);wait(0)
        e.tap(86,116,settle=18);wait(10)
        assert read("heatLimit")==0
        e.press("B",settle=12);wait(0)
        assert int.from_bytes(bytes(e.mem.unsigned[symbol(elf,"cjkPack"):symbol(elf,"cjkPack")+4]),"little")==0
    e.power_cycle();wait(0)
    assert int(e.mem.unsigned[symbol(elf,"S")+so["language"]])==15
    select(0);e.tap(52,147,settle=18);wait(1)
    print("CJK INTEGRATION OK: 3 real touch launches, words, peels, fire, loss, retry, next shelf, training, persisted language and English")
if __name__=="__main__":main(*sys.argv[1:])
