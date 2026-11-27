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
    fields=member_offsets(elf,"cjkGame",("len","turns","score","focus","tile","back","opened"))
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
        e.shot(str(out/f"cjk-{i}-fresh.png"))
        e.tap(215,105,settle=12)
        assert 2<=read("len")<=6
        e.tap(215,49,settle=12)
        assert read("turns",4)==1 and read("score",4)>0
        e.tap(215,133,settle=12);e.tap(18,20,settle=12)
        assert read("turns",4)==2
        e.shot(str(out/f"cjk-{i}-played.png"))
        e.press("B",settle=12);wait(0)
        assert int.from_bytes(bytes(e.mem.unsigned[symbol(elf,"cjkPack"):symbol(elf,"cjkPack")+4]),"little")==0
    e.power_cycle();wait(0)
    assert int(e.mem.unsigned[symbol(elf,"S")+so["language"]])==15
    select(0);e.tap(52,147,settle=18);wait(1)
    print("CJK INTEGRATION OK: 3 real touch launches, words, peels, release, persisted language and English")
if __name__=="__main__":main(*sys.argv[1:])
