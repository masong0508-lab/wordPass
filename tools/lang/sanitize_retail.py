#!/usr/bin/env python3
"""Keep attested but unspellable Qu words out of staged WPD3 gameplay data.
Original source dictionaries remain untouched. Preserve every other record,
tier, order and password pool; refuse to filter an existing password.
"""
import argparse,struct
from pathlib import Path
WIDTHS=(4,4,4,4,5,6,7,7,8,8,9,9,10,11,11,12,13,13)

def unpack_word(record,length):
    key=int.from_bytes(record,'big')>>(len(record)*8-5*length)
    return ''.join(chr(97+((key>>(5*j))&31)) for j in range(length-1,-1,-1))

def unreachable(word):
    return 'q' in word.replace('qu','')

def sanitize(data):
    assert data[:4]==b'WPD3' and len(data)>=76
    counts=struct.unpack_from('<18I',data,4);out=bytearray(b'WPD3'+bytes(72));off=76;removed=[]
    for i,count in enumerate(counts):
        length=i+3;width=WIDTHS[i];keep=0
        for _ in range(count):
            record=data[off:off+width];off+=width
            assert len(record)==width
            word=unpack_word(record,length)
            if unreachable(word):removed.append(word)
            else:out.extend(record);keep+=1
        struct.pack_into('<I',out,4+4*i,keep)
    assert off==len(data)
    return bytes(out),removed

def passwords(data):
    assert data[:4]==b'WPP1'
    n=struct.unpack_from('<I',data,4)[0];off=8+20*n
    for i in range(n):
        for length,count in enumerate(struct.unpack_from('<6H',data,8+20*i+8),4):
            for _ in range(count):
                word=data[off:off+length].decode('ascii').lower();off+=length
                yield word
    assert off==len(data)

def selftest():
    def record(w,tier):
        k=0
        for ch in w:k=(k<<5)|(ord(ch)-97)
        return ((k<<(32-5*len(w)))|tier).to_bytes(4,'big')
    image=b'WPD3'+struct.pack('<18I',0,0,2,*([0]*15))+record('qqqua',1)+record('squad',0)
    result,removed=sanitize(image)
    assert removed==['qqqua'] and len(result)==80 and result[-4:]==record('squad',0)
    assert not unreachable('quiqua') and unreachable('soqquadro')
    assert sanitize(result)==(result,[])
    print('QU FILTER OK: every q requires following u; tiers and remaining bytes preserved')

def main():
    p=argparse.ArgumentParser();p.add_argument('root',nargs='?',type=Path);p.add_argument('--selftest',action='store_true');a=p.parse_args()
    if a.selftest:selftest();return
    if not a.root:p.error('staged language root required')
    for folder in sorted(a.root.iterdir()):
        if not folder.is_dir():continue
        letters=(folder/'letters.bin').read_bytes()
        assert letters[:4]==b'WPL1'
        if not letters[4]:continue
        file=folder/'dict.bin';original=file.read_bytes();filtered,removed=sanitize(original)
        if removed:
            assert not set(removed).intersection(passwords((folder/'pools.bin').read_bytes()))
            file.write_bytes(filtered);print(folder.name,'unreachable Qu words removed from staged pack:',', '.join(removed))
if __name__=='__main__':main()
