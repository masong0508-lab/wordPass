#!/usr/bin/env python3
"""Build data/pools.bin, the themed pass-word library.

    gen_pools.py                      build data/pools.bin from tools/words/pools/*.txt
    gen_pools.py --wordnet DIR --freq en_50k.txt [--blocklist FILE]
                                      (re)derive the wn_*.txt files from a WordNet 3.x
                                      dict directory, then build

Each theme is a name (7 letters at most, it is shown in the HUD) plus word files in
tools/words/pools: <theme>.txt is hand written, wn_<theme>.txt is derived from WordNet.
Every word belongs to exactly one theme.  Words of a pool are ordered from most to least
familiar and the game widens the window it draws from as the shelves climb, so early shelves
only ever see familiar words.  Every word must be a dictionary word (data/dict.bin), 4..9
letters, a-z, not on the blocklist.

Format WPP1 (little endian):
  4 bytes "WPP1"; u32 theme count N
  N x { char name[8] (NUL padded); u16 count[6] }      counts for lengths 4..9
  N x 6 groups of count*len ASCII bytes, no separators, theme-major then length
"""
import collections, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_dict

MIN_LEN, MAX_LEN, PER_LEN, MIN_PER_LEN, REQ_MAX = 4, 9, 1000, 8, 6
assert PER_LEN <= 1000           # PASS_KEY packs the word index into 10 bits
POOL_DIR = os.path.join(HERE, "words", "pools")
SPARSE_OK = {"LEXICON"}       # starts at 5 letters; the game falls back to the nearest length

# Theme order is shelf order (theme = (shelf - 1) % count): the first eight are the original
# bookish shelves, then the library opens out; neighbouring shelves never share a flavour.
# It is also the claim order for derived words: the first theme whose categories (and, if
# given, hierarchy roots) match a word's main sense takes it, so a rooted theme must come
# before the catch-all for the same category (GUILD before FOLK, HOUSE before WARES).
# (name, curated file stems, wordnet lexical categories, wordnet hierarchy roots or None)
THEMES = [
    ("BINDERY", ["bindery"], [], None),
    ("INK",     ["ink"], [], None),
    ("VERSE",   ["verse"], [], None),
    ("TALES",   ["tales"], [], None),
    ("ATLAS",   ["atlas"], ["noun.object", "noun.location"], None),
    ("ARCHIVE", ["archive"], [], None),
    ("LAMP",    ["lamp"], [], None),
    ("STUDY",   ["study"], [], None),
    ("BEASTS",  [], ["noun.animal"], None),
    ("MUSIC",   ["music"], [], None),
    ("HERBAL",  [], ["noun.plant"], None),
    ("VOYAGE",  ["voyage"], [], None),
    ("COOKERY", [], ["noun.food", "verb.consumption"], None),
    ("MYTH",    ["myth"], [], None),
    ("ANATOMY", [], ["noun.body", "verb.body"], None),
    ("PLAY",    ["play"], ["verb.competition"], None),
    ("ALCHEMY", [], ["noun.substance"], None),
    ("WEATHER", ["weather"], ["noun.phenomenon", "verb.weather"], None),
    ("GUILD",   [], ["noun.person"], ["worker", "professional", "skilled_worker", "leader"]),
    ("SMITHY",  [], ["noun.artifact"], ["tool", "implement", "weapon", "machine", "device"]),
    ("ATTIRE",  [], ["noun.artifact"], ["clothing", "garment", "jewelry", "footwear", "headdress"]),
    ("HOUSE",   [], ["noun.artifact"], ["furniture", "container", "room", "tableware", "bedclothes"]),
    ("MOODS",   [], ["noun.feeling", "verb.emotion"], None),
    ("CURSES",  ["curses"], [], None),
    ("WORDS",   [], ["noun.communication", "verb.communication"], None),
    ("DEEDS",   [], ["noun.act", "verb.social"], None),
    ("MOTION",  [], ["verb.motion"], None),
    ("TRAITS",  [], ["noun.attribute", "adj.all", "adj.pert", "adj.ppl"], None),
    ("CLANS",   [], ["noun.group"], None),
    ("MINDS",   [], ["noun.cognition", "verb.cognition", "verb.perception"], None),
    ("FOLK",    [], ["noun.person"], None),
    ("TIMES",   [], ["noun.time"], None),
    ("MAKING",  [], ["verb.creation", "verb.contact"], None),
    ("WARES",   [], ["noun.artifact"], None),
    ("SHAPES",  [], ["noun.shape", "noun.quantity", "noun.relation"], None),
    ("CHANGE",  [], ["verb.change"], None),
    ("TRADE",   [], ["verb.possession", "noun.possession"], None),
    ("STATES",  [], ["noun.state", "verb.stative"], None),
    ("EVENTS",  [], ["noun.event", "noun.process"], None),
    ("LEXICON", ["lexicon"], [], None),   # the most familiar plurals and -ing/-ed forms of words above
]
assert all(len(t[0]) <= 7 for t in THEMES) and len(THEMES) <= 48
BAD_GLOSS = re.compile(r"slang|vulgar|offensive|derogatory|disparaging|insulting|pejorative|obscene|\binformal\b|taboo|racial|"
                       r"sexual|genital|excret|feces|faeces|urine|anus|buttock|penis|vagina|breast|prostitut|sexually|"
                       r"kill|murder|corpse|dead body|rape|drug|narcotic|intoxicat|alcoholic|slave")

def derive_wordnet(wn, freq_path, block_path, dictdata):
    """Write wn_<theme>.txt.  A lemma qualifies when it is a dictionary word of 4..9 letters,
    not mostly a name, not on the blocklist, has a clean gloss, and is familiar: in the top
    30k of the frequency list (tier 0), the top 50k (tier 1) or tagged in SemCor (tier 2).
    Words go to the first theme matching their main sense (noun first, then verb, adjective),
    most familiar first."""
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
    first = {}
    for l in open(os.path.join(wn, "index.noun"), encoding="utf-8"):
        if not l.startswith(" "):
            p = l.split()
            first[p[0]] = p[6 + int(p[3])]
    memo = {}
    def ancestors(off):
        if off not in memo:
            memo[off] = {off}
            memo[off] = {off}.union(*[ancestors(h) for h in hyper[off]]) if hyper[off] else {off}
        return memo[off]
    roots = {t[0]: {first[x] for x in t[3] if x in first} for t in THEMES if t[3]}
    pools = collections.defaultdict(list)
    seen = set()
    for pos in ("noun", "verb", "adj"):
        for l in open(os.path.join(wn, f"index.{pos}"), encoding="utf-8"):
            if l.startswith(" "):
                continue
            p = l.split()
            lem, npt = p[0], int(p[3])
            if lem in seen or not (lem.isalpha() and MIN_LEN <= len(lem) <= MAX_LEN) or lem in block:
                continue
            off = p[6 + npt]
            if case[lem][0] >= case[lem][1] or BAD_GLOSS.search(gloss[(pos, off)]):
                continue
            if gen_dict.lookup(dictdata, lem) == 0 or ("q" in lem and "qu" not in lem):
                continue
            r, tg = rank.get(lem), tags.get((lem, off), 0)
            if r is not None and r < 30000: tier, key = 0, r
            elif r is not None and r < 50000: tier, key = 1, r
            elif tg >= 1: tier, key = 2, -tg
            else: continue
            cat = lex[(pos, off)]
            for name, _, cats, rts in THEMES:
                if cat in cats and (not rts or (pos == "noun" and ancestors(off) & roots[name])):
                    seen.add(lem)
                    pools[name].append((tier, key, lem))
                    break
    for name, _, cats, _ in THEMES:
        if not cats:
            continue
        words = [w for _, _, w in sorted(pools[name])]
        with open(os.path.join(POOL_DIR, f"wn_{name.lower()}.txt"), "w") as f:
            f.write("\n".join(words) + "\n")
        print(f"wn_{name.lower()}.txt: {len(words)} words from {', '.join(cats)}")

def read_words_file(fn):
    path = os.path.join(POOL_DIR, fn)
    return [w.lower() for w in open(path).read().split()] if os.path.exists(path) else []

def build(block_path):
    dictdata = open(os.path.join(HERE, "..", "data", "dict.bin"), "rb").read()
    block = set()
    if block_path and os.path.exists(block_path):
        block = {l.strip().lower() for l in open(block_path, encoding="utf-8") if l.strip()}
    def ok(w):
        return (w.isalpha() and w.isascii() and MIN_LEN <= len(w) <= MAX_LEN and w not in block
                and not ("q" in w and "qu" not in w) and gen_dict.lookup(dictdata, w) != 0)
    curated = {n: [w for s in stems for w in read_words_file(f"{s}.txt")] for n, stems, _, _ in THEMES}
    derived = {n: read_words_file(f"wn_{n.lower()}.txt") if cats else [] for n, _, cats, _ in THEMES}
    claimed, taken, dropped = set(), {}, collections.defaultdict(list)
    for source in (curated, derived):      # hand-written words claim first, then derived ones
        for name, _, _, _ in THEMES:
            for w in source[name]:
                if w in claimed:
                    continue
                if not ok(w):
                    if w.isalpha(): dropped[name].append(w)
                    continue
                claimed.add(w)
                taken.setdefault(name, []).append(w)
    pools, good = [], True
    for name, stems, cats, _ in THEMES:
        by_len = collections.defaultdict(list)
        for w in taken.get(name, []):
            by_len[len(w)].append(w)
        groups = {n: by_len[n][:PER_LEN] for n in range(MIN_LEN, MAX_LEN + 1)}
        pools.append((name, groups))
        row = " ".join(f"{len(groups[n]):4d}" for n in range(MIN_LEN, MAX_LEN + 1))
        print(f"{name:8s} {row}   total {sum(map(len, groups.values())):5d}   dropped {len(dropped[name])}")
        if dropped[name] and stems and not cats:
            print("   not in dictionary / blocked:", " ".join(dropped[name][:30]))
        if name not in SPARSE_OK and min(len(groups[n]) for n in range(MIN_LEN, REQ_MAX + 1)) < MIN_PER_LEN:
            print(f"   !! {name}: fewer than {MIN_PER_LEN} words at some length 4..{REQ_MAX}"); good = False
    if not good:
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
    block = opt.get("--blocklist") or os.path.join(HERE, "words", "blocklist.txt")
    if "--wordnet" in opt:
        derive_wordnet(opt["--wordnet"], opt["--freq"], block,
                       open(os.path.join(HERE, "..", "data", "dict.bin"), "rb").read())
    build(block)
