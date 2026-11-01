#!/usr/bin/env python3
"""Build data/dict.bin from a plain word list (one word per line).

Format (little-endian):
  4 bytes  magic "WPD1"
  8 x u32  word counts for lengths 3..10
  records  7 bytes each, grouped by length, each group sorted.
           Letters are packed 5 bits each, big-endian, left aligned,
           so memcmp() order == alphabetical order within a group.
"""
import struct, sys

MIN_LEN, MAX_LEN, REC = 3, 10, 7

def pack(word):
    bits = 0
    for ch in word:
        bits = (bits << 5) | (ord(ch) - 97)
    bits <<= (REC * 8 - 5 * len(word))
    return bits.to_bytes(REC, "big")

def main(src, dst):
    groups = {n: set() for n in range(MIN_LEN, MAX_LEN + 1)}
    for line in open(src, encoding="utf-8", errors="ignore"):
        w = line.strip().lower()
        if w.isalpha() and w.isascii() and MIN_LEN <= len(w) <= MAX_LEN:
            # Qu tiles: a lone q followed by anything but u can never be spelled.
            if "q" in w and "qu" not in w:
                continue
            groups[len(w)].add(w)
    out = bytearray(b"WPD1")
    for n in range(MIN_LEN, MAX_LEN + 1):
        out += struct.pack("<I", len(groups[n]))
    total = 0
    for n in range(MIN_LEN, MAX_LEN + 1):
        for w in sorted(groups[n]):
            out += pack(w)
            total += 1
    open(dst, "wb").write(out)
    print(f"{total} words, {len(out)} bytes -> {dst}")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
