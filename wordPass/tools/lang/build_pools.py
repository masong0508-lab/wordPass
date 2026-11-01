#!/usr/bin/env python3
"""Step 4 of the language pipeline: data/lang/CODE/pools.bin (format WPP1, as tools/gen_pools.py).

    build_pools.py CODE RAW_DIR WORK_DIR OUT.bin

The English library is translated THEME BY THEME through the Princeton WordNet 3.0 synsets:

  * wordnet for the target language: the Open Multilingual Wordnet 1.4 (Spanish MCR, French WOLF,
    Italian MultiWordNet, Portuguese OpenWN-PT, Dutch ODWN, Swedish SALDO, Polish plWordNet,
    Danish DanNet, Finnish FinnWordNet) or, for German (not in OMW 1.4), Open German WordNet
    (OdeNet 1.4), whose synsets reach WordNet 3.0 through the CILI index.
  * derived themes (those with WordNet lexical categories or hierarchy roots in gen_pools.py THEMES):
    every target lemma goes to the first theme its MAIN sense matches, exactly as the English
    derivation does.  The main sense is the synset of the lemma with the most SemCor tags
    (lowest offset on a tie); instance synsets (names of people and places) never count.
  * hand-written English themes (BINDERY, INK, ..., CURSES): each English word of
    tools/words/pools/<theme>.txt is looked up in WordNet (first two noun senses, first verb and
    adjective sense) and its synsets give the target lemmas.  If a theme is still short, the
    hyponyms (two levels) of those synsets are added.  CURSES additionally has a hand-written list
    per language in tools/words/lang/CODE/curses.txt (mild oaths, nonsense words, old-fashioned insults).
  * LEXICON is the most familiar words no theme claimed.
  * a word qualifies when it is in the language's dictionary (words.txt), 4..9 letters a-z, seen at
    least MIN_COUNT times in the frequency list, not on the blocklist, and its main sense has a clean
    gloss (BAD_GLOSS of gen_pools.py).  Words of a theme are ordered most familiar first (frequency).
  * blocklist: LDNOOBW's list for the language (CC BY 4.0), tools/words/lang/CODE/blocklist.txt
    (written for this project: stronger swear words, slurs, sexual and violent words), and any word
    that starts with a blocked word of 5+ letters.  The ceiling is the same mild level as English.
"""
import collections, os, re, struct, sys, xml.etree.ElementTree as ET
import common
from common import norm
import gen_pools, gen_dict
from themes import THEME_NAMES, ORDER

MIN_LEN, MAX_LEN, PER_LEN = 4, 9, 1000
MIN_COUNT = {"es": 40, "fr": 40, "de": 40, "it": 40, "pt": 40, "nl": 40, "sv": 40, "pl": 40, "da": 40, "fi": 25}
MIN_PER_LEN = 8
FILL_MIN = 4             # the fill passes below accept words seen at least this often
# The Danish and Swedish wordnets are small (about 5,000 synsets each), so each also reads the
# neighbouring Scandinavian ones; a borrowed lemma still has to be in the language's own dictionary.
# The Polish one has almost no verbs, so the verb themes there are hand-written (tools/words/lang/pl).
OMW = {"es": ["mcr/wn-data-spa.tab"], "fr": ["fra/wn-data-fra.tab"], "it": ["ita/wn-data-ita.tab"],
       "pt": ["por/wn-data-por.tab"], "nl": ["nld/wn-data-nld.tab"], "pl": ["pol/wn-data-pol.tab"],
       "sv": ["swe/wn-data-swe.tab", "dan/wn-data-dan.tab", "nor/wn-data-nob.tab", "nor/wn-data-nno.tab"],
       "da": ["dan/wn-data-dan.tab", "nor/wn-data-nob.tab", "nor/wn-data-nno.tab", "swe/wn-data-swe.tab"],
       "fi": ["fin/wn-data-fin.tab"]}
POS_KEY = {"n": "n", "v": "v", "a": "a", "s": "a", "r": "r"}

class WordNet:
    """Princeton WordNet 3.0: synsets keyed "00001740-n" (adjective satellites are "a")."""
    def __init__(self, d):
        self.lex, self.gloss, self.hyper, self.hypo, self.instance, self.pop = {}, {}, {}, {}, set(), {}
        names = {}
        for l in open(os.path.join(d, "lexnames")):
            p = l.split()
            if len(p) == 3:
                names[int(p[0])] = p[1]
        for pos in ("noun", "verb", "adj"):
            for l in open(os.path.join(d, f"data.{pos}"), encoding="utf-8"):
                if l.startswith(" "):
                    continue
                head, _, g = l.partition("|")
                p = head.split()
                key = f"{p[0]}-{POS_KEY[p[2]]}"
                self.lex[key] = names[int(p[1])]
                self.gloss[key] = g
                nw = int(p[3], 16)
                q = 4 + 2 * nw
                for k in range(int(p[q])):
                    sym, off, ptype = p[q + 1 + 4 * k], p[q + 2 + 4 * k], p[q + 3 + 4 * k]
                    tk = f"{off}-{POS_KEY[ptype]}"
                    if sym in ("@", "@i"):
                        self.hyper.setdefault(key, []).append(tk)
                        if sym == "@i":
                            self.instance.add(key)
                    elif sym == "~":
                        self.hypo.setdefault(key, []).append(tk)
        for l in open(os.path.join(d, "index.sense"), encoding="utf-8"):
            sk, off, _num, cnt = l.split()
            t = {"1": "n", "2": "v", "3": "a", "4": "r", "5": "a"}[sk.split("%")[1][0]]
            k = f"{off}-{t}"
            self.pop[k] = max(self.pop.get(k, 0), int(cnt))
        self.index = {}
        for pos in ("noun", "verb", "adj"):
            for l in open(os.path.join(d, f"index.{pos}"), encoding="utf-8"):
                if l.startswith(" "):
                    continue
                p = l.split()
                t = {"noun": "n", "verb": "v", "adj": "a"}[pos]
                self.index.setdefault(p[0], {})[t] = [f"{o}-{t}" for o in p[6 + int(p[3]):]]
        self._anc = {}
    def ancestors(self, k):
        if k not in self._anc:
            self._anc[k] = {k}
            for h in self.hyper.get(k, ()):
                self._anc[k] |= self.ancestors(h)
        return self._anc[k]
    def first_sense(self, lemma):
        for t in ("n", "v", "a"):
            if lemma in self.index and t in self.index[lemma]:
                return self.index[lemma][t][0]

def load_target(code, raw):
    """{synset key: [raw lemma, ...]} for the language."""
    out = collections.defaultdict(list)
    if code == "de":
        base = os.path.join(raw, "odenet", "odenet-1.4", "deWordNet.xml")
        ili = {}
        for l in open(os.path.join(raw, "ili-map-pwn30.tab")):
            a, b = l.split()
            ili[a] = b
        tree = ET.parse(base).getroot()
        syn = {}
        for s in tree.iter("Synset"):
            i = s.get("ili", "")
            if i in ili:
                syn[s.get("id")] = ili[i]
        for e in tree.iter("LexicalEntry"):
            lem = e.find("Lemma").get("writtenForm")
            for s in e.findall("Sense"):
                k = syn.get(s.get("synset"))
                if k:
                    out[k].append(lem)
        return out
    for f in OMW[code]:
        for l in open(os.path.join(raw, "omw", "omw-1.4", f), encoding="utf-8"):
            if l.startswith("#"):
                continue
            p = l.rstrip("\n").split("\t")
            if len(p) == 3 and p[1].endswith("lemma"):
                out[p[0]].append(p[2])
    return out

def read_blocklist(code, raw, here):
    """(exact words, prefixes).  LDNOOBW entries count when they are a single word (a phrase is not
    a word); this project's own list (tools/words/lang/CODE/blocklist.txt) blocks every word that starts
    with an entry of 5+ letters, and with any entry written with a trailing *."""
    exact, prefix = set(), set()
    p = os.path.join(raw, "ldnoobw", code)
    if os.path.exists(p):
        for l in open(p, encoding="utf-8"):
            if len(l.split()) == 1 and norm(l.strip()):
                exact.add(norm(l.strip()))
    p = os.path.join(here, "words", "lang", code, "blocklist.txt")
    for l in open(p, encoding="utf-8"):
        for tok in l.split():
            star = tok.endswith("*")
            w = norm(tok.rstrip("*"))
            if w:
                exact.add(w)
                if star or len(w) >= 5:
                    prefix.add(w)
    return exact, prefix

def build(code, raw, work, out_path):
    here = common.TOOLS
    wn = WordNet(os.path.join(raw, "wn", "wordnet"))
    tgt = load_target(code, raw)
    words = open(os.path.join(work, code, "words.txt")).read().split()
    vocab = set(words)
    count = {}
    for l in open(os.path.join(work, code, "freq.txt")):
        w, n = l.split()
        count[w] = int(n)
    block, block_stems = read_blocklist(code, raw, here)
    minc = MIN_COUNT[code]
    def blocked(w):
        return w in block or any(w.startswith(b) for b in block_stems)
    german = code == "de"
    def cand(raw_lemma, mc=None):
        if not raw_lemma.isalpha() or (not german and not raw_lemma.islower()):
            return None
        w = norm(raw_lemma)
        if not w or not (MIN_LEN <= len(w) <= MAX_LEN) or w not in vocab or count.get(w, 0) < (mc or minc) or blocked(w):
            return None
        return w
    # reverse index: candidate word -> synsets (instances never count)
    rev = collections.defaultdict(set)
    for key, lemmas in tgt.items():
        if key in wn.instance or key not in wn.lex:
            continue
        for lem in lemmas:
            w = cand(lem, FILL_MIN)
            if w:
                rev[w].add(key)
    def main_sense(w):
        return max(rev[w], key=lambda k: (wn.pop.get(k, 0), -int(k[:8])))
    bad = {w for w in rev if BAD(wn, main_sense(w))}
    bad_any = {w for w in rev if any(BAD(wn, k) for k in rev[w])}
    # derived themes: first theme whose categories (and roots) match the main sense
    roots = {}
    for name, _, _, rts in gen_pools.THEMES:
        if rts:
            roots[name] = {wn.first_sense(x) for x in rts} - {None}
    derived = collections.defaultdict(list)
    for w in sorted(rev, key=lambda x: (-count[x], x)):
        if w in bad or count[w] < minc:
            continue
        k = main_sense(w)
        cat = wn.lex[k]
        for name, _, cats, rts in gen_pools.THEMES:
            if cat in cats and (not rts or (k.endswith("-n") and wn.ancestors(k) & roots[name])):
                derived[name].append(w)
                break
    # curated themes: the English hand-written words through WordNet
    pooldir = gen_pools.POOL_DIR
    def eng_words(stems):
        out = []
        for s in stems:
            p = os.path.join(pooldir, f"{s}.txt")
            if os.path.exists(p):
                out += [x.lower() for x in open(p).read().split()]
        return out
    def synsets_of(e):
        ks = []
        ix = wn.index.get(e, {})
        ks += ix.get("n", [])[:2] + ix.get("v", [])[:1] + ix.get("a", [])[:1]
        return [k for k in ks if k not in wn.instance and not BAD(wn, k)]
    curated, short_report, curated_keys, curated_eng = collections.defaultdict(list), [], {}, {}
    own = os.path.join(here, "words", "lang", code)
    def add_synsets(name, keys, seen):
        got = {}
        for k in keys:
            for lem in tgt.get(k, ()):
                w = cand(lem)
                if w and w not in bad and w not in seen:
                    got[w] = None
        seen.update(got)
        curated[name] += sorted(got, key=lambda x: (-count[x], x))
    for name, stems, cats, _ in gen_pools.THEMES:
        if name == "LEXICON" or not (stems or os.path.exists(os.path.join(own, f"{name.lower()}.txt"))):
            continue
        seen = set()
        keys = []
        curated_eng[name] = eng_words(stems)
        for e in curated_eng[name]:
            keys += synsets_of(e)
        # hand-written list in the target language, first
        mine = os.path.join(own, f"{name.lower()}.txt")
        if os.path.exists(mine):
            hand = [cand(x) for x in open(mine, encoding="utf-8").read().split()]
            hand = [w for w in hand if w]
            for w in hand:
                if w not in seen:
                    seen.add(w); curated[name].append(w)
        add_synsets(name, keys, seen)
        def short():
            by = collections.Counter(len(w) for w in curated[name])
            return min(by[n] for n in range(MIN_LEN, 7)) < MIN_PER_LEN
        curated_keys[name] = keys
    # claim: curated first, then derived, each word belongs to one theme
    claimed, taken = set(), collections.defaultdict(list)
    for source in (curated, derived):
        for name, _, _, _ in gen_pools.THEMES:
            for w in source.get(name, ()):
                if w not in claimed:
                    claimed.add(w)
                    taken[name].append(w)
    # fill passes: a theme with fewer than MIN_PER_LEN words at a length 4..6 takes, for that length
    # only, unclaimed words from (1) hyponyms of its keys (3 levels) and siblings, (2) lemmas with the
    # theme's lexical category in ANY sense, not only the main one.  Each pass stops at the quota.
    def related(name, cats, rts):
        keys = set(curated_keys.get(name, ()))
        lvl = set(keys)
        hypos = set()
        for _ in range(3):
            lvl = {h for k in lvl for h in wn.hypo.get(k, ()) if h not in wn.instance and not BAD(wn, h)}
            hypos |= lvl
        sib = {h for k in keys for p in wn.hyper.get(k, ()) for h in wn.hypo.get(p, ())
               if h not in wn.instance and not BAD(wn, h)}
        allsense = {k for e in curated_eng.get(name, ()) for t in wn.index.get(e, {}) for k in wn.index[e][t]
                    if k not in wn.instance and not BAD(wn, k)}
        return [hypos, sib, allsense]
    filled = collections.Counter()
    for name, stems, cats, rts in gen_pools.THEMES:
        if name == "LEXICON":
            continue
        passes = []
        if stems:
            hy, sib, alls = related(name, cats, rts)
            passes += [("hyponym", hy), ("sibling", sib), ("all-senses", alls)]
        if cats:
            any_sense = {}
            for w, ks in rev.items():
                for k in ks:
                    if wn.lex[k] in cats and not BAD(wn, k) and (not rts or (k.endswith("-n") and wn.ancestors(k) & roots[name])):
                        any_sense[w] = k
            passes.append(("any-sense", any_sense))
        for label, src in passes:
            bylen = collections.Counter(len(w) for w in taken[name])
            for n in range(MIN_LEN, 7):
                if bylen[n] >= MIN_PER_LEN:
                    continue
                if isinstance(src, dict):
                    pool = [w for w in src if len(w) == n]
                else:
                    pool = [w for k in src for lem in tgt.get(k, ()) for w in [cand(lem, FILL_MIN)] if w and len(w) == n]
                for w in sorted(set(pool), key=lambda x: (-count[x], x)):
                    if bylen[n] >= MIN_PER_LEN + 4:
                        break
                    if w in claimed or w in bad:
                        continue
                    claimed.add(w); taken[name].append(w); bylen[n] += 1; filled[(name, label)] += 1
    # LEXICON: the most familiar unclaimed words
    lex = [w for w in sorted(vocab, key=lambda x: (-count.get(x, 0), x))
           if w not in claimed and MIN_LEN <= len(w) <= MAX_LEN and count.get(w, 0) >= minc
           and not blocked(w) and w not in bad_any and w not in bad]
    for w in lex:
        taken["LEXICON"].append(w)
    for (name, label), c in sorted(filled.items()):
        print(f"  fill {name:8s} {label:9s} +{c}")
    pools, report = [], []
    names = THEME_NAMES[code]
    assert [t[0] for t in gen_pools.THEMES] == ORDER
    dictdata = open(os.path.join(work, code, "dict.bin"), "rb").read()
    for (name, _, _, _), local in zip(gen_pools.THEMES, names):
        by_len = collections.defaultdict(list)
        for w in taken[name]:
            by_len[len(w)].append(w)
        groups = {n: by_len[n][:PER_LEN] for n in range(MIN_LEN, MAX_LEN + 1)}
        for n in groups:
            for w in groups[n]:
                assert gen_dict.lookup(dictdata, w) != 0, w
        pools.append((local, groups))
        counts = [len(groups[n]) for n in range(MIN_LEN, MAX_LEN + 1)]
        report.append((name, local, counts))
    out = bytearray(b"WPP1") + struct.pack("<I", len(pools))
    for local, groups in pools:
        out += local.encode().ljust(8, b"\0")
        out += struct.pack("<6H", *[len(groups[n]) for n in range(MIN_LEN, MAX_LEN + 1)])
    for local, groups in pools:
        for n in range(MIN_LEN, MAX_LEN + 1):
            for w in groups[n]:
                out += w.encode()
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    open(out_path, "wb").write(out)
    total = sum(sum(c) for _, _, c in report)
    print(f"{code}: {len(pools)} themes, {total} words, {len(out)} bytes -> {out_path}")
    weak = []
    for name, local, counts in report:
        lo = min(counts[:3])
        if name != "LEXICON" and lo < MIN_PER_LEN:
            weak.append(f"{name}/{local} {counts}")
    with open(os.path.join(work, code, "pools_report.txt"), "w") as f:
        for name, local, counts in report:
            f.write(f"{name:8s} {local:8s} {' '.join(f'{c:4d}' for c in counts)} total {sum(counts)}\n")
        f.write("WEAK: " + "; ".join(weak) + "\n")
    print("  weak themes (<8 words at a length 4..6):", weak if weak else "none")

def BAD(wn, k):
    return bool(gen_pools.BAD_GLOSS.search(wn.gloss.get(k, "")))

if __name__ == "__main__":
    build(*sys.argv[1:5])
