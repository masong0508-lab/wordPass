#!/usr/bin/env python3
"""Print, per language, the themes with fewer than 8 words at a length 4, 5 or 6 (the sparse report).

    report.py [CODE ...]
"""
import os, struct, sys
import common

def load(code):
    d = open(os.path.join(common.ROOT, "data", "lang", code, "pools.bin"), "rb").read()
    n = struct.unpack_from("<I", d, 4)[0]
    out = []
    for i in range(n):
        out.append((d[8 + 20 * i:16 + 20 * i].rstrip(b"\0").decode(), struct.unpack_from("<6H", d, 16 + 20 * i)))
    return out

if __name__ == "__main__":
    import themes
    for code in sys.argv[1:] or common.CODES:
        short = [(themes.ORDER[i], name, c) for i, (name, c) in enumerate(load(code)) if i != 39 and min(c[:3]) < 8]
        at4 = [s for s in short if s[2][0] < 8 and min(s[2][1:3]) >= 8]
        deeper = [s for s in short if min(s[2][1:3]) < 8]
        total = sum(sum(c) for _, c in load(code))
        print(f"{code}: {total} pool words; short at 4 letters only: {len(at4)} themes; short at 5 or 6 letters: "
              + (", ".join(f"{e}/{n} {list(c[:3])}" for e, n, c in deeper) or "none"))
