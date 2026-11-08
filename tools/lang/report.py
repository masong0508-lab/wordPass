#!/usr/bin/env python3
"""Print, per language, the pass-pool size and every theme below the standard.

    report.py [CODE ...]

The standard (also asserted by tools/check/lang_test.c, and the same as English's): every theme but LEXICON has at
least 25 words, 8 words at each of 4, 5 and 6 letters and 5 at 7 letters; LEXICON has at least 100 words; every
language has at least 13,000 pass words.  A healthy build prints "meets the standard" for all ten.
"""
import os, struct, sys
import common

QUOTA = {4: 8, 5: 8, 6: 8, 7: 5}
MIN_TOTAL, LEXICON_TOTAL, MIN_POOL = 25, 100, 13000

def load(code):
    d = open(os.path.join(common.ROOT, "data", "lang", code, "pools.bin"), "rb").read()
    n = struct.unpack_from("<I", d, 4)[0]
    out = []
    for i in range(n):
        out.append((d[8 + 20 * i:16 + 20 * i].rstrip(b"\0").decode(), struct.unpack_from("<6H", d, 16 + 20 * i)))
    return out

def short(counts, last):
    """What a theme lacks: [(length, have, want)] and the total."""
    miss = []
    if last:
        return [("all", sum(counts), LEXICON_TOTAL)] if sum(counts) < LEXICON_TOTAL else []
    for n, q in QUOTA.items():
        if counts[n - 4] < q:
            miss.append((n, counts[n - 4], q))
    if sum(counts) < MIN_TOTAL:
        miss.append(("all", sum(counts), MIN_TOTAL))
    return miss

if __name__ == "__main__":
    import themes
    bad = 0
    for code in sys.argv[1:] or common.CODES:
        pools = load(code)
        total = sum(sum(c) for _, c in pools)
        weak = [(themes.ORDER[i], name, short(c, i == 39)) for i, (name, c) in enumerate(pools)]
        weak = [w for w in weak if w[2]]
        if total < MIN_POOL:
            weak.append(("POOL", "-", [("all", total, MIN_POOL)]))
        bad += len(weak)
        print(f"{code}: {total} pass words; " + ("meets the standard" if not weak else
              "; ".join(f"{e}/{n} " + " ".join(f"{L}:{h}/{w}" for L, h, w in m) for e, n, m in weak)))
    sys.exit(1 if bad else 0)
