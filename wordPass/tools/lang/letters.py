#!/usr/bin/env python3
"""Step 3 of the language pipeline: data/lang/CODE/letters.bin (format WPL1).

    letters.py CODE WORK_DIR OUT.bin

Tile weights come from the language's own dictionary: every word of words.txt counts its letters,
weighted by 1 + log10(1 + subtitle frequency) so familiar words shape the bag more than rare
ones.  A Q followed by U is one tile (the Qu tile), so that U is not counted.  The 26 weights are
apportioned to sum to exactly 100 (the bag holds 100 tiles per cycle); every letter that occurs
in the dictionary gets at least 1, letters that do not occur get 0 and never appear.
Values are 1..10 by rarity: log scale from the commonest letter (1) to the rarest (10).

Format WPL1, 68 bytes, little endian:
   0  "WPL1"
   4  u8   flags: bit 0 = Q is a Qu tile
   5  u8   reserved (0)
   6  u8[4] the four rarest letters (index 0..25, 255 = none), the rare-letter rotation
  10  u8[2] reserved (0)
  12  u32  vowel mask, bit i set when letter i (a=0) is a vowel: the vowel floor reads it
  16  u8[26] tile weight per letter
  42  u8[26] letter value per letter (1..10)
"""
import math, os, struct, sys
import common

def compute(code, work):
    words = open(os.path.join(work, code, "words.txt")).read().split()
    freq = {}
    for line in open(os.path.join(work, code, "freq.txt")):
        w, n = line.split()
        freq[w] = int(n)
    share = [0.0] * 26
    for w in words:
        wt = 1 + math.log10(1 + freq.get(w, 0))
        t = w.replace("qu", "q")
        for c in t:
            share[ord(c) - 97] += wt
    total = sum(share)
    share = [s / total for s in share]
    present = [i for i in range(26) if share[i] > 0]
    raw = [share[i] * 100 for i in range(26)]
    w = [max(1, int(raw[i])) if share[i] > 0 else 0 for i in range(26)]
    # largest remainder up or down until the weights sum to 100
    while sum(w) != 100:
        if sum(w) < 100:
            i = max(present, key=lambda k: raw[k] - w[k])
            w[i] += 1
        else:
            i = max((k for k in present if w[k] > 1), key=lambda k: w[k] - raw[k])
            w[i] -= 1
    smax = max(share)
    smin = min(share[i] for i in present)
    value = [0] * 26
    for i in present:
        value[i] = max(1, min(10, round(1 + 9 * math.log(smax / share[i]) / math.log(smax / smin))))
    rare = sorted(present, key=lambda k: (share[k], k))[:4]
    rare += [255] * (4 - len(rare))
    qu = 1 if w[16] > 0 else 0
    vowels = {l[0]: l[3] for l in common.LANGS}[code]
    mask = sum(1 << (ord(c) - 97) for c in vowels if w[ord(c) - 97] > 0)
    return qu, rare, mask, w, value

def build(code, work, out):
    qu, rare, mask, w, value = compute(code, work)
    data = b"WPL1" + struct.pack("<BB4BBB", qu, 0, *rare, 0, 0) + struct.pack("<I", mask) + bytes(w) + bytes(value)
    assert len(data) == 68, len(data)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(data)
    letters = "".join(f"{chr(97 + i)}{w[i]}/{value[i]} " for i in range(26) if w[i])
    print(f"{code}: qu={qu} rare={[chr(97 + r) for r in rare if r < 26]} vowels={bin(mask)} {letters}")

def parse(data):
    assert data[:4] == b"WPL1" and len(data) == 68
    return dict(qu=data[4] & 1, rare=list(data[6:10]), vowels=struct.unpack_from("<I", data, 12)[0],
                weight=list(data[16:42]), value=list(data[42:68]))

if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2], sys.argv[3])
