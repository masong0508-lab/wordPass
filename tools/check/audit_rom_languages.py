#!/usr/bin/env python3
"""Audit actual shipped ROM dictionaries, pools, UI glyphs, and resource bounds.

This verifies structural, gameplay and encoding consistency, not idiomatic
translations or complete lexical accuracy. Uses the matching unstripped ELF
only to locate the built-in English data; those bytes are read from the ROM.
"""
import argparse,json,struct,sys,subprocess
from pathlib import Path
from collections import Counter
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'lang'))
import dict_v4,pools_v2,tiles_v2
from sanitize_retail import WIDTHS,unpack_word,unreachable,passwords

LANGS=('en','es','fr','de','it','pt','nl','sv','pl','da','fi')
EXT=('zh-Hans','zh-Hant','ja','ko','hi','id','tr')
def u32(d,o):return struct.unpack_from('<I',d,o)[0]

def nitro(rom):
    fntoff,fnsize,fatoff,fatsize=struct.unpack_from('<4I',rom,0x40)
    assert fntoff+fnsize<=len(rom) and fatoff+fatsize<=len(rom)
    fnt=rom[fntoff:fntoff+fnsize];out={};visited=set()
    def walk(did,prefix):
        assert did not in visited;visited.add(did)
        off,fid,_=struct.unpack_from('<IHH',fnt,8*(did&4095))
        while fnt[off]:
            flags=fnt[off];length=flags&127;off+=1
            assert length and off+length<=len(fnt)
            name=fnt[off:off+length].decode('ascii');off+=length
            if flags&128:
                child=struct.unpack_from('<H',fnt,off)[0];off+=2
                walk(child,prefix+name+'/')
            else:
                assert 8*fid+8<=fatsize
                a,b=struct.unpack_from('<II',rom,fatoff+8*fid)
                assert 0<=a<=b<=len(rom) and prefix+name not in out
                out[prefix+name]=rom[a:b];fid+=1
    walk(0xf000,'');return out

def english(rom,elf,nm):
    names={'dict_bin','dict_bin_end','pools_bin','pools_bin_end'}
    symbols={}
    for line in subprocess.check_output([nm,'--defined-only',str(elf)],text=True).splitlines():
        p=line.split()
        if len(p)==3 and p[-1] in names:symbols[p[-1]]=int(p[0],16)
    arm9off,_,ram,size=struct.unpack_from('<4I',rom,0x20)
    def get(name):
        a,b=symbols[name]-ram,symbols[name+'_end']-ram
        assert 0<=a<b<=size
        return rom[arm9off+a:arm9off+b]
    return get('dict_bin'),get('pools_bin')

def latin(code,d,p,letters):
    assert d[:4]==b'WPD3' and p[:4]==b'WPP1'
    weights=bytes([1]*26) if code=='en' else letters[16:42]
    qu=True if code=='en' else bool(letters[4])
    counts=struct.unpack_from('<18I',d,4);off=76;keys={};bad=Counter()
    for i,count in enumerate(counts):
        length=i+3;stride=WIDTHS[i];shift=8*stride-5*length;prev=-1;group=set()
        for _ in range(count):
            record=d[off:off+stride];off+=stride;assert len(record)==stride
            v=int.from_bytes(record,'big');k=v>>shift
            assert k>prev and not v&((1<<shift)-2),(code,length,'record order/padding')
            prev=k;group.add(k);word=unpack_word(record,length)
            assert all('a'<=c<='z' and weights[ord(c)-97] for c in word),(code,word,'unavailable tile')
            if qu and unreachable(word):bad['unreachable_qu']+=1
        keys[length]=group
    assert off==len(d)
    pc=0
    for word in passwords(p):
        assert word.isascii() and word.isalpha()
        value=0
        for ch in word:value=(value<<5)|(ord(ch)-97)
        assert value in keys[len(word)],(code,word,'password missing from dictionary')
        assert all(weights[ord(c)-97] for c in word) and not(qu and unreachable(word))
        pc+=1
    assert not bad,(code,dict(bad))
    return {'words':sum(counts),'passwords':pc,'themes':u32(p,4),'issues':{}}

def native(data,locale):
    magic,n,ng,nu,res,h=struct.unpack_from('<4sHHHHI',data)
    assert magic==b'CJK1' and not res and len(data)==16+76*n+34*ng+30*nu+950
    check=2166136261
    for b in data[16:]:check=((check^b)*16777619)&0xffffffff
    assert check==h
    glyphs={};off=16+76*n;prev=-1
    for i in range(ng):
        cp,*bitmap=struct.unpack_from('<17H',data,off+34*i)
        assert cp>prev;prev=cp;glyphs[cp]=any(bitmap)
    seen=set()
    def code_ok(cp):
        if locale=='ko':return 0xac00<=cp<=0xd7a3
        if locale=='hi':return 0xe000<=cp<0xf000
        if locale=='id':return 97<=cp<=122
        if locale=='tr':return chr(cp) in 'abcçdefgğhıijklmnoöprsştuüvyzâîû'
        return 0x3000<=cp<=0xffff and not 0xd800<=cp<=0xdfff
    for j in range(n):
        *codes,length,r,g=struct.unpack_from('<6HBx31s31s',data,16+76*j)
        key=tuple(codes[:length]);assert 2<=length<=6 and not any(codes[length:]) and key not in seen
        assert all(code_ok(cp) and glyphs.get(cp,False) for cp in key),(locale,key,'unrenderable tile')
        seen.add(key)
        for s in (r,g):
            assert b'\0' in s and s[0] and all(32<=x<=126 for x in s.split(b'\0')[0])
    for j in range(nu):
        label=struct.unpack_from('<15H',data,off+34*ng+30*j)
        assert label[0] and not label[-1] and all(cp in glyphs for cp in label if cp)
        # Shaped text may contain a blank interior glyph chunk; not a missing tile.
        assert any(glyphs[cp] for cp in label if cp)
    return {'words':n,'glyphs':ng,'ui_labels':nu,'issues':{}}

def main():
    p=argparse.ArgumentParser();p.add_argument('rom',type=Path);p.add_argument('elf',type=Path)
    p.add_argument('--nm',default='nm');p.add_argument('--expected',type=int,choices=(16,20),default=20)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    rom=a.rom.read_bytes();files=nitro(rom);enD,enP=english(rom,a.elf,a.nm);report={}
    for code in LANGS:
        if code=='en':report[code]=latin(code,enD,enP,None)
        else:
            folder='lang/'+code+'/'
            report[code]=latin(code,files[folder+'dict.bin'],files[folder+'pools.bin'],files[folder+'letters.bin'])
        print(code,report[code],flush=True)
    for code in ('ru','uk'):
        folder='cyrpreview/'+code+'/'
        d=files[folder+'dict.bin'];pools=files[folder+'pools.bin'];tiles=files[folder+'tiles.bin']
        dict_v4.verify(d);pools_v2.verify(d,pools);tiles_v2.verify(tiles,d)
        manifest=json.loads(files[folder+'SOURCE-MANIFEST.json'])
        report[code]={'words':manifest['retained_word_count'],'passwords':manifest['total_password_count'],'issues':{}}
        print(code,report[code],flush=True)
    for i,locale in enumerate(EXT[:a.expected-13]):
        report[locale]=native(files[f'cjk/{i}.bin'],locale);print(locale,report[locale],flush=True)
    assert len(report)==a.expected
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print('PACKED LANGUAGE AUDIT OK:',len(report),'languages;',sum(x['words'] for x in report.values()),'records')
if __name__=='__main__':main()
