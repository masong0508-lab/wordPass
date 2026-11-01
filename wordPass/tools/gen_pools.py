#!/usr/bin/env python3
"""Build data/pools.bin, the themed pass-word library.

    gen_pools.py                      build data/pools.bin from tools/words/pools/*.txt
    gen_pools.py --wordnet DIR --freq en_50k.txt [--blocklist FILE]
                                      (re)derive the wn_*.txt files from a WordNet 3.x
                                      dict directory, then build

Each theme is a name (7 letters at most, it is shown in the HUD) plus word files in
tools/words/pools: <theme>.txt is hand written, wn_<theme>.txt is derived from WordNet
lexical categories, familiar words first.  Every word must be a dictionary word
(data/dict.bin), 4..9 letters, a-z, not on the blocklist.  Per length at most PER_LEN
words are kept, in file order, so the most familiar come first.

Format WPP1 (little endian):
  4 bytes "WPP1"; u32 theme count N
  N x { char name[8] (NUL padded); u16 count[6] }      counts for lengths 4..9
  N x 6 groups of count*len ASCII bytes, no separators, theme-major then length
"""
import collections, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_dict

MIN_LEN, MAX_LEN, PER_LEN, MIN_PER_LEN = 4, 9, 140, 8
REQ_MAX = 7        # lengths 4..7 must have MIN_PER_LEN words; 8 and 9 are bonus (nearest-length fallback covers them)
POOL_DIR = os.path.join(HERE, "words", "pools")

# Theme order is shelf order: consecutive shelves never share a flavour.
# (name, curated file stems, wordnet lexical categories, wordnet hierarchy roots or None)
# A root restricts the derived words to that branch of the noun hierarchy, which keeps a
# broad category such as noun.person to occupations or noun.artifact to a single kind of thing.
THEMES = [
    ("BINDERY", ["bindery"], [], None),
    ("BEASTS",  [], ["noun.animal"], None),
    ("INK",     ["ink"], [], None),
    ("HERBAL",  [], ["noun.plant"], None),
    ("VERSE",   ["verse"], [], None),
    ("COOKERY", [], ["noun.food"], None),
    ("TALES",   ["tales"], [], None),
    ("ANATOMY", [], ["noun.body"], None),
    ("ATLAS",   ["atlas"], ["noun.object", "noun.location"], None),
    ("ALCHEMY", [], ["noun.substance"], None),
    ("ARCHIVE", ["archive"], [], None),
    ("WEATHER", ["weather"], ["noun.phenomenon", "verb.weather"], None),
    ("LAMP",    ["lamp"], [], None),
    ("GUILD",   [], ["noun.person"], ["worker", "professional", "skilled_worker", "leader"]),
    ("STUDY",   ["study"], [], None),
    ("SMITHY",  [], ["noun.artifact"], ["tool", "implement", "weapon", "machine", "device"]),
    ("VOYAGE",  ["voyage"], [], None),
    ("ATTIRE",  [], ["noun.artifact"], ["clothing", "garment", "jewelry", "footwear", "headdress"]),
    ("MUSIC",   ["music"], [], None),
    ("HOUSE",   [], ["noun.artifact"], ["furniture", "container", "room", "tableware", "bedclothes"]),
    ("MYTH",    ["myth"], [], None),
    ("MOODS",   [], ["noun.feeling", "verb.emotion"], None),
    ("CURSES",  ["curses"], [], None),
    ("PLAY",    ["play"], [], None),
]
assert all(len(t[0]) <= 7 for t in THEMES)
BAD_GLOSS = re.compile(r"slang|vulgar|offensive|derogatory|disparaging|insulting|pejorative|obscene|\binformal\b|taboo|racial|"
                       r"sexual|genital|excret|feces|faeces|urine|anus|buttock|penis|vagina|breast|prostitut|sexually|"
                       r"kill|murder|corpse|dead body|rape|drug|narcotic|intoxicat|alcoholic|slave")

def derive_wordnet(wn, freq_path, block_path):
    """Write wn_<theme>.txt: lemmas whose first (most frequent) sense is in the theme's
    lexical category, in common use (the sense is tagged in SemCor or the word is in the
    top 12k of the frequency list; top 35k at all), clean, most familiar first."""
    rank = {}
    for i, line in enumerate(open(freq_path, encoding="utf-8")):
        rank.setdefault(line.split()[0], i)
    block = set()
    if block_path:
        block = {l.strip().lower() for l in open(block_path, encoding="utf-8") if l.strip()}
    names = {}
    for l in open(os.path.join(wn, "lexnames")):
        p = l.split()
        if len(p) == 3:
            names[int(p[0])] = p[1]
    case = collections.defaultdict(lambda: [0, 0])
    lex, gloss, hyper = {}, {}, collections.defaultdict(list)
    for pos in ("noun", "verb", "adj"):
        for l in open(os.path.join(wn, f"data.{pos}"), encoding="utf-8"):
            if l.startswith(" "):
                continue
            head, _, g = l.partition("|")
            p = head.split()
            lex[(pos, p[0])] = names[int(p[1])]
            gloss[(pos, p[0])] = g
            nw = int(p[3], 16)
            for i in range(nw):
                w = p[4 + 2 * i]
                case[re.sub(r"\(.*", "", w).lower()][0 if w[0].isupper() else 1] += 1
            if pos == "noun":
                q = 4 + 2 * nw
                for k in range(int(p[q])):
                    sym, off = p[q + 1 + 4 * k], p[q + 2 + 4 * k]
                    if sym in ("@", "@i"):
                        hyper[p[0]].append(off)
    tags = {}
    for l in open(os.path.join(wn, "index.sense"), encoding="utf-8"):
        key, off, num, cnt = l.split()
        tags[(key.split("%")[0], off)] = int(cnt)
    def first_noun(lemma):
        for l in open(os.path.join(wn, "index.noun"), encoding="utf-8"):
            if l.startswith(lemma + " "):
                p = l.split()
                return p[6 + int(p[3])]
    memo = {}
    def ancestors(off):
        if off not in memo:
            memo[off] = set()
            memo[off] = {off}.union(*[ancestors(h) for h in hyper[off]]) if hyper[off] else {off}
        return memo[off]
    adjs = set()
    for pos in ("adj", "adv"):
        for l in open(os.path.join(wn, f"index.{pos}"), encoding="utf-8"):
            if not l.startswith(" "):
                adjs.add(l.split()[0])
    by_cat = collections.defaultdict(set)
    for pos in ("noun", "verb"):
        for l in open(os.path.join(wn, f"index.{pos}"), encoding="utf-8"):
            if l.startswith(" "):
                continue
            p = l.split()
            lem, npt = p[0], int(p[3])
            if not (lem.isalpha() and MIN_LEN <= len(lem) <= MAX_LEN) or lem in block:
                continue
            if case[lem][0] >= case[lem][1] or lem in adjs:   # mostly a name, or also an adjective
                continue
            r = rank.get(lem)
            off = p[6 + npt]
            if r is None or r >= 35000 or not (tags.get((lem, off), 0) >= 1 or r < 12000):
                continue
            if BAD_GLOSS.search(gloss[(pos, off)]):
                continue
            by_cat[(lex[(pos, off)], off if pos == "noun" else None)].add((r, lem))
    for name, _, cats, roots in THEMES:
        if not cats:
            continue
        rootset = {first_noun(x) for x in roots} if roots else None
        words = set()
        for (cat, off), ws in by_cat.items():
            if cat in cats and (rootset is None or (off and ancestors(off) & rootset)):
                words |= ws
        words = sorted(words)
        with open(os.path.join(POOL_DIR, f"wn_{name.lower()}.txt"), "w") as f:
            f.write("\n".join(w for _, w in words) + "\n")
        print(f"wn_{name.lower()}.txt: {len(words)} words from {', '.join(cats)}")

def load_theme(name, stems, cats, dictdata, block):
    files = [f"{s}.txt" for s in stems] + ([f"wn_{name.lower()}.txt"] if cats else [])
    seen, by_len, dropped = set(), collections.defaultdict(list), []
    for fn in files:
        for w in open(os.path.join(POOL_DIR, fn)).read().split():
            w = w.lower()
            if w in seen:
                continue
            seen.add(w)
            if not (w.isalpha() and w.isascii() and MIN_LEN <= len(w) <= MAX_LEN):
                continue
            if w in block or "q" in w and "qu" not in w:
                dropped.append(w); continue
            if gen_dict.lookup(dictdata, w) == 0:
                dropped.append(w); continue
            by_len[len(w)].append(w)
    return {n: by_len[n][:PER_LEN] for n in range(MIN_LEN, MAX_LEN + 1)}, dropped

def build(block_path):
    dictdata = open(os.path.join(HERE, "..", "data", "dict.bin"), "rb").read()
    block = set()
    if block_path and os.path.exists(block_path):
        block = {l.strip().lower() for l in open(block_path, encoding="utf-8") if l.strip()}
    pools, ok = [], True
    for name, stems, cats, _ in THEMES:
        groups, dropped = load_theme(name, stems, cats, dictdata, block)
        pools.append((name, groups))
        row = " ".join(f"{len(groups[n]):3d}" for n in range(MIN_LEN, MAX_LEN + 1))
        print(f"{name:8s} {row}   total {sum(map(len, groups.values())):4d}   dropped {len(dropped)}")
        if dropped and stems and not cats:
            print("   not in dictionary / blocked:", " ".join(dropped[:30]))
        if min(len(groups[n]) for n in range(MIN_LEN, REQ_MAX + 1)) < MIN_PER_LEN:
            print(f"   !! {name}: fewer than {MIN_PER_LEN} words at some length 4..{REQ_MAX}"); ok = False
    if not ok:
        sys.exit(1)
    out = bytearray(b"WPP1") + struct.pack("<I", len(pools))
    for name, groups in pools:
        out += name.encode().ljust(8, b"\0")
        out += struct.pack("<6H", *[len(groups[n]) for n in range(MIN_LEN, MAX_LEN + 1)])
    for name, groups in pools:
        for n in range(MIN_LEN, MAX_LEN + 1):
            for w in groups[n]:
                out += w.encode()
    dst = os.path.join(HERE, "..", "data", "pools.bin")
    open(dst, "wb").write(out)
    total = sum(len(g[n]) for _, g in pools for n in g)
    print(f"{len(pools)} themes, {total} words, {len(out)} bytes -> data/pools.bin")

if __name__ == "__main__":
    a = sys.argv[1:]
    opt = {a[i]: a[i + 1] for i in range(0, len(a) - 1, 2) if a[i].startswith("--")}
    if "--wordnet" in opt:
        derive_wordnet(opt["--wordnet"], opt["--freq"], opt.get("--blocklist"))
    build(opt.get("--blocklist") or os.path.join(HERE, "words", "blocklist.txt"))
