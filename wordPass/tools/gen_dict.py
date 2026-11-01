#!/usr/bin/env python3
"""Build data/dict.bin from a plain word list (one word per line).

    gen_dict.py WORDS OUT [COMMON]          build (COMMON marks the common tier)
    gen_dict.py --verify OUT WORDS [COMMON] re-read OUT and check every word

Format WPD2 (little-endian header):
  4 bytes  magic "WPD2"
  8 x u32  word counts for lengths 3..10
  records  grouped by length, each group sorted; record width per length
           REC_FOR[len] bytes (3-6: 4, 7: 5, 8: 6, 9-10: 7).  Letters are
           packed 5 bits each, big-endian, left aligned, so memcmp() order ==
           alphabetical order within a group.  Every width leaves >= 2 spare
           low bits; bit 0 of the last byte is the tier: 0 common, 1 rare.
"""
import random, struct, sys

MIN_LEN, MAX_LEN = 3, 10
REC_FOR = {3: 4, 4: 4, 5: 4, 6: 4, 7: 5, 8: 6, 9: 7, 10: 7}
MAGIC = b"WPD2"

for _n, _r in REC_FOR.items():
    assert _r * 8 - 5 * _n >= 2, f"length {_n} leaves no room for the tier bit"

def pack(word, tier=0):
    rec = REC_FOR[len(word)]
    bits = 0
    for ch in word:
        bits = (bits << 5) | (ord(ch) - 97)
    bits <<= (rec * 8 - 5 * len(word))
    return (bits | (tier & 1)).to_bytes(rec, "big")

def read_words(src):
    """Valid game words from a list: ascii letters, 3..10 long, no lone q."""
    words = set()
    for line in open(src, encoding="utf-8", errors="ignore"):
        w = line.strip().lower()
        if w.isalpha() and w.isascii() and MIN_LEN <= len(w) <= MAX_LEN:
            # Qu tiles: a lone q followed by anything but u can never be spelled.
            if "q" in w and "qu" not in w:
                continue
            words.add(w)
    return words

def build(src, dst, common_src=None):
    words = read_words(src)
    common = read_words(common_src) if common_src else set(words)
    groups = {n: sorted(w for w in words if len(w) == n) for n in range(MIN_LEN, MAX_LEN + 1)}
    out = bytearray(MAGIC)
    for n in range(MIN_LEN, MAX_LEN + 1):
        out += struct.pack("<I", len(groups[n]))
    total = rare = 0
    for n in range(MIN_LEN, MAX_LEN + 1):
        for w in groups[n]:
            tier = 0 if w in common else 1
            out += pack(w, tier)
            total += 1
            rare += tier
    open(dst, "wb").write(out)
    print(f"{total} words ({total - rare} common, {rare} rare), {len(out)} bytes -> {dst}")

def lookup(data, word):
    """0 = not found, 1 = common, 2 = rare.  Mirrors dictLookup() in source/dict.c."""
    n = len(word)
    if n < MIN_LEN or n > MAX_LEN or data[:4] != MAGIC:
        return 0
    counts = struct.unpack_from("<8I", data, 4)
    base = 4 + 8 * 4
    for m in range(MIN_LEN, n):
        base += counts[m - MIN_LEN] * REC_FOR[m]
    rec = REC_FOR[n]
    key = pack(word)
    lo, hi = 0, counts[n - MIN_LEN] - 1
    while lo <= hi:
        mid = (lo + hi) >> 1
        off = base + mid * rec
        r = bytes(data[off:off + rec])
        r_cmp = r[:-1] + bytes([r[-1] & 0xFE])
        if r_cmp == key:
            return 1 + (r[-1] & 1)
        if r_cmp < key:
            lo = mid + 1
        else:
            hi = mid - 1
    return 0

def verify(dst, src, common_src=None):
    data = open(dst, "rb").read()
    words = read_words(src)
    common = read_words(common_src) if common_src else set(words)
    bad = 0
    for w in words:
        want = 1 if w in common else 2
        got = lookup(data, w)
        if got != want:
            bad += 1
            if bad <= 10:
                print(f"MISMATCH {w}: want {want} got {got}")
    rng = random.Random(1234)
    for n in range(MIN_LEN, MAX_LEN + 1):
        k = 0
        while k < 20:
            w = "".join(rng.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(n))
            if w in words:
                continue
            k += 1
            if lookup(data, w) != 0:
                bad += 1
                print(f"FALSE POSITIVE {w}")
    if bad:
        print(f"VERIFY FAILED: {bad} problems")
        sys.exit(1)
    print(f"verify ok: {len(words)} words of lengths {MIN_LEN}..{MAX_LEN}, "
          f"{len(words & common)} common, 20 random non-words per length rejected")

if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--verify":
        verify(a[1], a[2], a[3] if len(a) > 3 else None)
    else:
        build(a[0], a[1], a[2] if len(a) > 2 else None)
