#!/usr/bin/env python3
"""Generate additional native-script packs for the opt-in 20-language ROM.
Uses full-string RAQM shaping for Hindi and all new UI labels. Does not
normalize away Turkish letters, or split a Hindi virama conjunct on tiles.
Only raster assets, source vocabulary, and font notices enter the ROM.
"""
import argparse
import json
import struct
import unicodedata as ud
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, features
import gen_assets as base

ROOT=Path(__file__).resolve().parent
NEW=('ko','hi','id','tr')
UI_NEW={
  "ko": [
    "점수",
    "확인",
    "지우기",
    "힌트",
    "열기",
    "취소",
    "다음",
    "메뉴",
    "암호",
    "횟수",
    "암호 찾음",
    "다음 판으로",
    "두 글자 이상 연결",
    "목록에 없는 단어",
    "글자를 이어 단어 만들기",
    "남은 암호/닫힌 칸",
    "칸을 누르고 떼기",
    "단어로 단서를 찾으세요",
    "단어를 찾지 못함",
    "재배치",
    "다시 누르면 한 턴 소모",
    "인접한 닫힌 칸 없음",
    "단계",
    "열기",
    "불을 끈 뒤 열기",
    "불이 바닥에 닿음",
    "재도전",
    "힌트 후 확인",
    "열기를 누르고 칸 선택",
    "숫자는 주변 암호 수",
    "암호 글자를 모두 찾기",
    "한국어"
  ],
  "hi": [
    "अंक",
    "मानें",
    "मिटाएँ",
    "संकेत",
    "खोलें",
    "रद्द",
    "आगे",
    "मेनू",
    "कोड",
    "चाल",
    "कोड मिला",
    "अगला स्तर",
    "दो अक्षर या अधिक जोड़ें",
    "शब्द सूची में नहीं",
    "अक्षर जोड़कर शब्द बनाएँ",
    "बाकी कोड/बंद खाने",
    "खाना छूकर छोड़ें",
    "शब्द बनाकर संकेत पाएँ",
    "शब्द नहीं मिला",
    "फिर सजाएँ",
    "फिर दबाएँ, एक चाल लगेगी",
    "पास में बंद खाने नहीं",
    "स्तर",
    "ताप",
    "पहले आग बुझाएँ",
    "आग नीचे पहुँच गई",
    "फिर खेलें",
    "संकेत लें, फिर मानें",
    "खोलें दबाकर खाना चुनें",
    "संख्या पास के कोड बताती है",
    "सारे कोड अक्षर खोजें",
    "हिंदी"
  ],
  "id": [
    "Skor",
    "Terima",
    "Hapus",
    "Petunjuk",
    "Buka",
    "Batal",
    "Lanjut",
    "Menu",
    "Sandi",
    "Giliran",
    "Sandi ditemukan",
    "Papan berikutnya",
    "Hubungkan dua huruf atau lebih",
    "Kata tidak ada di daftar",
    "Rangkai huruf menjadi kata",
    "Sisa sandi/kotak tertutup",
    "Sentuh lalu lepas kotak",
    "Buat kata untuk mencari petunjuk",
    "Tidak ada kata ditemukan",
    "Susun lagi",
    "Tekan lagi: satu giliran",
    "Tak ada kotak tertutup bersebelahan",
    "Level",
    "Panas",
    "Padamkan api sebelum membuka",
    "Api mencapai dasar",
    "Ulangi",
    "Pilih petunjuk, lalu terima",
    "Pilih Buka, lalu sentuh kotak",
    "Angka menghitung sandi di sekitar",
    "Temukan semua huruf sandi",
    "Indonesia"
  ],
  "tr": [
    "Puan",
    "Onayla",
    "Sil",
    "İpucu",
    "Aç",
    "İptal",
    "İleri",
    "Menü",
    "Şifre",
    "Hamle",
    "Şifre bulundu",
    "Sonraki tahta",
    "İki veya daha çok harf bağla",
    "Sözcük listede yok",
    "Harfleri birleştir, sözcük kur",
    "Kalan şifre/kapalı kare",
    "Kareye dokun ve bırak",
    "Sözcük kur, ipucu bul",
    "Sözcük bulunamadı",
    "Yenile",
    "Tekrar bas: bir hamle",
    "Bitişik kapalı kare yok",
    "Seviye",
    "Isı",
    "Açmadan önce ateşi söndür",
    "Ateş en alta ulaştı",
    "Tekrar",
    "İpucu seç, sonra onayla",
    "Aç seçeneğine ve kareye dokun",
    "Sayı çevredeki şifreleri sayar",
    "Tüm şifre harflerini bul",
    "Türkçe"
  ]
}
MAX_LABEL={'SCORE':4,'ACCEPT':4,'CLEAR':4,'HINT':4,'OPEN':4,'CANCEL':4,
           'NEXT':4,'MENU':4,'RETRY':4,'RECOVER':4,'PASS':2,'MOVES':2,
           'SHELF':2,'HEAT':2,'CLUE':9,'LOCALE_TITLE':4}
def hindi_tiles(word):
    # A base plus nukta, vowel signs, anusvara/chandrabindu, and linked
    # virama+consonant sequences constitute one orthographic unit.
    # Explicit grammar: no standalone marks, viramas or Latin substitutions.
    units=[]
    def consonant(c):return '\u0915'<=c<='\u0939'
    def is_base(c):return '\u0904'<=c<='\u0914' or consonant(c)
    marks=set(chr(i) for i in list(range(0x0900,0x0904))+[0x093a,0x093b,0x093c]+list(range(0x093e,0x094d))+[0x094e,0x094f,0x0955,0x0956,0x0957,0x0962,0x0963])
    i=0
    while i<len(word):
        if not is_base(word[i]):raise ValueError(('Hindi base',word,i))
        start=i;i+=1
        while i<len(word):
            c=word[i]
            if c in marks:i+=1
            elif c=='\u094d':
                if i+1>=len(word) or not consonant(word[i+1]):raise ValueError(('Hindi virama',word,i))
                i+=2
            else:break
        units.append(word[start:i])
    assert ''.join(units)==word
    return units

def split_word(locale,word):
    assert word==ud.normalize('NFC',word)
    if locale=='hi':return hindi_tiles(word)
    if locale=='ko':assert all(0xac00<=ord(c)<=0xd7a3 for c in word)
    if locale=='id':assert all('a'<=c<='z' for c in word)
    if locale=='tr':assert all(c in 'abcçdefgğhıijklmnoöprsştuüvyzâîû' for c in word)
    return list(word)

def rows_for_new():
    rows={l:[] for l in NEW}
    seen={l:set() for l in NEW}
    rejected=[]
    for n,line in enumerate((ROOT/'new_languages.tsv').read_text(encoding='utf8').splitlines(),1):
        locale,word,reading,gloss=line.split('\t')
        assert locale in NEW and word not in seen[locale],(n,word)
        assert reading.isascii() and gloss.isascii() and 0<len(reading)<=30 and 0<len(gloss)<=30,(n,word)
        try:units=split_word(locale,word)
        except ValueError as e:raise ValueError((n,e)) from e
        if not 2<=len(units)<=6:
            rejected.append((locale,word,len(units)))
            continue
        seen[locale].add(word)
        rows[locale].append((word,units,reading,gloss))
    assert all(len(rows[l])>=100 for l in NEW),{l:len(v) for l,v in rows.items()}
    assert all(any(len(x[1])==2 for x in rows[l]) for l in NEW)
    print('Excluded outside 2..6 playable units:',rejected)
    return rows

def font_at(path,size,index=0):
    return ImageFont.truetype(str(path),size,index=index,layout_engine=ImageFont.Layout.RAQM)

def raster_text(text,path,index,max_tiles):
    # Shape the entire phrase once so vowel reordering and conjuncts work.
    # Slice the finished raster only AFTER shaping. Never shrink horizontally.
    for size in range(16,9,-1):
        f=font_at(path,size,index);box=f.getbbox(text)
        w,h=box[2]-box[0],box[3]-box[1]
        if 0<w<=16*max_tiles and 0<h<=16:
            tiles=(w+15)//16
            im=Image.new('L',(16*tiles,16))
            ImageDraw.Draw(im).text(((16*tiles-w)//2-box[0],(16-h)//2-box[1]),text,font=f,fill=255)
            masks=[]
            for tile in range(tiles):
                masks.append([sum(1<<(15-x) for x in range(16) if im.getpixel((16*tile+x,y))>=90) for y in range(16)])
            assert any(any(r) for r in masks),(text,size)
            return masks
    raise ValueError(('label does not fit',text,max_tiles))

def pack_locale(locale,rows,labels,font,index,dest,idx):
    units=sorted({unit for _,u,_,_ in rows for unit in u})
    # IDs are stable within a pack; deterministic lexical sort. Hindi
    # syllables get private IDs rather than invalid Unicode scalar fragments.
    ids={unit:(0xe000+i if locale=='hi' else ord(unit)) for i,unit in enumerate(units)}
    assert len(units)<0x1000
    glyphs={ids[u]:raster_text(u,font,index,1)[0] for u in units}
    labelids=[];next_id=0xf000
    for name,text in zip(base.UI,labels):
        strips=raster_text(text,font,index,MAX_LABEL.get(name,14))
        codes=[]
        for bitmap in strips:
            assert next_id not in glyphs
            glyphs[next_id]=bitmap;codes.append(next_id);next_id+=1
        labelids.append(codes)
    payload=bytearray()
    for _,unit,reading,gloss in rows:
        codes=[ids[u] for u in unit]
        payload+=struct.pack('<6HBx31s31s',*(codes+[0]*(6-len(codes))),len(codes),
                            reading.encode('ascii'),gloss.encode('ascii'))
    for cp,bitmap in sorted(glyphs.items()):
        payload+=struct.pack('<17H',cp,*bitmap)
    for codes in labelids:
        payload+=struct.pack('<15H',*(codes+[0]*(15-len(codes))))
    f=ImageFont.load_default()
    payload+=bytes(v for cp in range(32,127) for v in base.bitmap(f,chr(cp),8,10))
    h=2166136261
    for b in payload:h=((h^b)*16777619)&0xffffffff
    blob=struct.pack('<4sHHHHI',b'CJK1',len(rows),len(glyphs),len(labelids),0,h)+payload
    assert len(blob)<=65536 and len(blob)+len(rows)*24<=131072
    (dest/f'{idx}.bin').write_bytes(blob)
    print(locale,len(rows),'words',len(units),'native units',len(blob),'bytes')
    return {'language':locale,'records':len(rows),'units':len(units),'pack_bytes':len(blob),'tile_map':ids}

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cjk-font',default='/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
    p.add_argument('--hindi-font',default='/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf')
    p.add_argument('--latin-font',default='/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf')
    args=p.parse_args()
    assert features.check('raqm'),'Pillow with RAQM/HarfBuzz required for native Hindi shaping'
    assert hindi_tiles('क्षमा')==['क्ष','मा']
    assert hindi_tiles('किताब')==['कि','ता','ब']
    assert hindi_tiles('पुस्तक')==['पु','स्त','क']
    for bad in ('िक','क्','्क'):
        try:hindi_tiles(bad)
        except ValueError:pass
        else:raise AssertionError(bad)
    assert split_word('tr','ışık')==['ı','ş','ı','k']
    assert split_word('tr','iş')!=split_word('tr','ış')
    args.out.mkdir(parents=True,exist_ok=True)
    sets=base.read_words()
    base.UI['LOCALE_TITLE']=base.LABELS
    base.write_packs(sets,Path(args.cjk_font),args.out)
    rows=rows_for_new()
    assert all(len(UI_NEW[l])==len(base.UI) for l in NEW),[(l,len(UI_NEW[l]),len(base.UI)) for l in NEW]
    names={font_at(args.cjk_font,16,i).getname()[0]:i for i in range(5)}
    manifest=[]
    for i,l in enumerate(NEW,3):
        path=args.cjk_font if l=='ko' else args.hindi_font if l=='hi' else args.latin_font
        manifest.append(pack_locale(l,rows[l],UI_NEW[l],path,names['Noto Sans CJK KR'] if l=='ko' else 0,args.out,i))
    # Generate portable C records for the four new native test dictionaries.
    out=['#include "cjk_core.h"']
    for i,l in enumerate(NEW):
        mapping=manifest[i]['tile_map'];out.append(f'static const CjWord extra{i}[]={{')
        for _,units,r,g in rows[l]:
            codes=','.join(hex(mapping[u]) for u in units)
            out.append('{{'+codes+'},'+str(len(units))+','+json.dumps(r)+','+json.dumps(g)+'},')
        out.append('};')
    out.append('static const CjLexicon extraLexicons[4]={'+','.join('{extra%d,sizeof extra%d/sizeof *extra%d}'%(i,i,i) for i in range(4))+'};')
    (args.out/'extended_words.h').write_text('\n'.join(out)+'\n',encoding='ascii')
    (args.out/'EXTENDED-MANIFEST.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
if __name__=='__main__':main()
