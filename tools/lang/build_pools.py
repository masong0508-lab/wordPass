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
# How often a theme word must occur in the subtitle corpus: 40 for the big corpora (200 to 420 million tokens), scaled
# down with the corpus for the small ones (Swedish 112 million, Danish 89 million, German 156, Finnish 132).
MIN_COUNT = {"es": 40, "fr": 40, "de": 40, "it": 40, "pt": 40, "nl": 40, "sv": 18, "pl": 40, "da": 12, "fi": 25}
MIN_PER_LEN = 8
# The standard every theme but LEXICON meets in every language (tools/check/lang_test.c asserts it): 8 words at
# each of 4, 5 and 6 letters, 5 at 7 letters, 25 words in all.  LEXICON needs 100 words.
QUOTA = {4: 8, 5: 8, 6: 8, 7: 5}
HEADROOM = {4: 0, 5: 0, 6: 0, 7: 0}         # a fill pass stops at the quota: the passes are the weak sources, take no more than needed
MIN_TOTAL, LEXICON_TOTAL = 25, 100
FILL_MIN = 4             # the fill passes below accept words seen at least this often
HAND_MIN = 8             # a hand-written word must have been seen at least this often in the subtitles
# The Danish and Swedish wordnets are small (about 5,000 synsets each), so each also reads the
# neighbouring Scandinavian ones; a borrowed lemma still has to be in the language's own dictionary.
# The Polish one has almost no verbs, so the verb themes there are hand-written (tools/words/lang/pl).
OMW = {"es": ["mcr/wn-data-spa.tab"], "fr": ["fra/wn-data-fra.tab"], "it": ["ita/wn-data-ita.tab"],
       "pt": ["por/wn-data-por.tab"], "nl": ["nld/wn-data-nld.tab"], "pl": ["pol/wn-data-pol.tab"],
       "sv": ["swe/wn-data-swe.tab", "dan/wn-data-dan.tab", "nor/wn-data-nob.tab", "nor/wn-data-nno.tab"],
       "da": ["dan/wn-data-dan.tab", "nor/wn-data-nob.tab", "nor/wn-data-nno.tab", "swe/wn-data-swe.tab"],
       "fi": ["fin/wn-data-fin.tab"]}
# Wiktionary-derived wordnets of the Open Multilingual Wordnet project (omwn/omw-data, CC BY-SA): many more lemmas
# than the 1.4 wordnets but looser senses.  They are the main source of Swedish and Danish (whose 1.4 wordnets
# have about 6,000 lemmas) and the "any sense" source of the fill passes for the other languages.
WIKT = {"es": ["spa"], "fr": ["fra"], "it": ["ita"], "pt": ["por"], "nl": ["nld"], "pl": ["pol"], "de": ["deu"],
        "sv": ["swe", "dan", "nob", "nno"], "da": ["dan", "nob", "nno", "swe"], "fi": ["fin"]}
WIKT_MAIN = {"sv", "da"}
# Danish only: after everything else a theme also takes the regular inflected forms of the words it holds (plural and
# definite nouns, tenses of verbs, neuter and plural adjectives) that are in the dictionary and common enough.  The
# Danish wordnets are the smallest of the ten, and a form of a theme word ("hunde", "hunden") is still a theme word.
INFLECT = {"da"}
INFLECT_MIN = 500          # an inflected form must be this common (about 6 per million tokens in Danish)
DA_FORMS = {"n": ["e", "en", "et", "er", "ene", "erne"], "v": ["r", "er", "et", "ede", "ende", "te", "t"], "a": ["t", "e", "ere", "est"]}
POS_KEY = {"n": "n", "v": "v", "a": "a", "s": "a", "r": "r"}
# pointer symbols followed by the "related" fill pass (one hop): derivation, similar, also see, verb group,
# entailment, cause, parts and wholes, attribute, pertainym, participle, topic domain and its members
REL_SYMS = {"+", "&", "^", "$", "*", ">", "#m", "#s", "#p", "%m", "%s", "%p", "=", "\\", "<", ";c", "-c"}

class WordNet:
    """Princeton WordNet 3.0: synsets keyed "00001740-n" (adjective satellites are "a")."""
    def __init__(self, d):
        self.lex, self.gloss, self.hyper, self.hypo, self.instance, self.pop = {}, {}, {}, {}, set(), {}
        self.rel = {}
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
                    elif sym in REL_SYMS:
                        self.rel.setdefault(key, []).append(tk)
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

def load_wikt(codes, raw):
    """{synset key: [raw lemma, ...]} from the Wiktionary-derived wordnets raw/wikt/wn-wikt-CODE.tab."""
    out = collections.defaultdict(list)
    for c in codes:
        path = os.path.join(raw, "wikt", f"wn-wikt-{c}.tab")
        if not os.path.exists(path):
            continue
        for l in open(path, encoding="utf-8"):
            if l.startswith("#"):
                continue
            p = l.rstrip("\n").split("\t")
            if len(p) == 3 and p[1].endswith("lemma"):
                out[p[0]].append(p[2])
    return out

def merge(a, b):
    out = collections.defaultdict(list)
    for src in (a, b):
        for k, v in src.items():
            for x in v:
                if x not in out[k]:
                    out[k].append(x)
    return out

FREEDICT = {"es": ["spa-eng"], "fr": ["fra-eng", "eng-fra"], "pt": ["por-eng", "eng-por"], "nl": ["nld-eng", "eng-nld"],
            "sv": ["swe-eng"], "da": ["dan-eng", "eng-dan"]}
FD_MAIN = {"sv", "da"}

def load_freedict(code, raw, wn):
    """{synset key: [raw lemma, ...]} from the FreeDict (GPL-2+) dictionaries between the language and English: for every
    pair (target word, single English word) the first two noun senses and the first verb and adjective sense of the
    English word give the target word a synset.  Looser than a wordnet, so it is only used where noted."""
    out = collections.defaultdict(list)
    entry = re.compile(r"<entry\b.*?</entry>", re.S)
    orth, quote = re.compile(r"<orth>([^<]+)</orth>"), re.compile(r"<quote>([^<]+)</quote>")
    for name in FREEDICT.get(code, ()):
        path = os.path.join(raw, "freedict", name + ".tei")
        if not os.path.exists(path):
            continue
        eng_first = name.startswith("eng-")
        text = open(path, encoding="utf-8").read()
        for m in entry.finditer(text):
            e = m.group(0)
            heads, trans = [h.strip() for h in orth.findall(e)], [q.strip() for q in quote.findall(e)]
            en, tg = (heads, trans) if eng_first else (trans, heads)
            en = [x[3:] if x.startswith("to ") else x for x in en]
            en = [x.lower() for x in en if x.isalpha() and x.isascii()]
            for t in tg:
                if not t.isalpha():
                    continue
                for x in en:
                    ix = wn.index.get(x, {})
                    ks = ix.get("n", [])[:2] + ix.get("v", [])[:1] + ix.get("a", [])[:1]
                    for k in ks:
                        if t not in out[k]:
                            out[k].append(t)
    return out

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

def english_pools():
    """{theme index: [English words]} of the shipped English library (data/pools.bin)."""
    d = open(os.path.join(common.ROOT, "data", "pools.bin"), "rb").read()
    n = struct.unpack_from("<I", d, 4)[0]
    out, off = {}, 8 + 20 * n
    counts = [struct.unpack_from("<6H", d, 16 + 20 * i) for i in range(n)]
    for i in range(n):
        out[i] = []
        for k in range(6):
            for j in range(counts[i][k]):
                out[i].append(d[off:off + MIN_LEN + k].decode())
                off += MIN_LEN + k
    return out

def build(code, raw, work, out_path):
    here = common.TOOLS
    wn = WordNet(os.path.join(raw, "wn", "wordnet"))
    tgt_core = load_target(code, raw)
    wikt = load_wikt(WIKT[code], raw)
    fd = load_freedict(code, raw, wn)
    tgt_all = merge(merge(tgt_core, wikt), fd)                              # every source, for the fill passes
    tgt = tgt_all if code in WIKT_MAIN else tgt_core                        # the wordnet the themes are derived from
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
    # reverse indexes: candidate word -> synsets (instances never count).  rev feeds the derivation, rev_all the fill passes.
    def reverse(t):
        r = collections.defaultdict(set)
        for key, lemmas in t.items():
            if key in wn.instance or key not in wn.lex:
                continue
            for lem in lemmas:
                w = cand(lem, FILL_MIN)
                if w:
                    r[w].add(key)
        return r
    rev, rev_all = reverse(tgt), reverse(tgt_all)
    def main_sense(w, r=None):
        r = rev if r is None else r
        return max(r[w], key=lambda k: (wn.pop.get(k, 0), -int(k[:8])))
    bad = {w for w in rev_all if BAD(wn, main_sense(w, rev_all))}
    bad_any = {w for w in rev_all if any(BAD(wn, k) for k in rev_all[w])}
    # derived themes: first theme whose categories (and roots) match the main sense
    roots = {}
    for name, _, _, rts in gen_pools.THEMES:
        if rts:
            roots[name] = {wn.first_sense(x) for x in rts} - {None}
    derived = collections.defaultdict(list)
    for w in sorted(rev, key=lambda x: (-count[x], x)):
        if w in bad or count[w] < minc or (code in WIKT_MAIN and w in bad_any):
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
    curated, curated_keys, curated_eng = collections.defaultdict(list), {}, {}
    topup = collections.defaultdict(list)           # the accepted words of hand.txt, applied after the first claim
    own = os.path.join(here, "words", "lang", code)
    rejected = []
    # hand-written lists in the target language: tools/words/lang/CODE/THEME.txt is the theme's own list (it comes first,
    # before the wordnet words); hand.txt, one line per theme, "THEME word word ...", THEME being the English theme name
    # (a # starts a comment), tops up thin cells: its words are added after the derivation, and a word that already
    # belongs to another theme moves only if that theme keeps its quota at that length
    hand = collections.defaultdict(list)
    if os.path.exists(os.path.join(own, "hand.txt")):
        for line in open(os.path.join(own, "hand.txt"), encoding="utf-8"):
            line = line.split("#")[0].split()
            if line:
                assert line[0] in ORDER, line[0]
                hand[line[0]] += line[1:]
    def add_synsets(name, keys, seen):
        got = {}
        for k in keys:
            for lem in tgt.get(k, ()):
                w = cand(lem)
                if w and w not in bad and w not in seen and not (code in WIKT_MAIN and w in bad_any):
                    got[w] = None
        seen.update(got)
        curated[name] += sorted(got, key=lambda x: (-count[x], x))
    for name, stems, cats, _ in gen_pools.THEMES:
        if name == "LEXICON":
            continue
        for x in hand[name]:
            w = cand(x, HAND_MIN)
            if not w:
                if MIN_LEN <= len(x) <= MAX_LEN:
                    rejected.append((name, x))
            elif w not in topup[name]:
                topup[name].append(w)
        if not (stems or os.path.exists(os.path.join(own, f"{name.lower()}.txt"))):
            continue
        seen = set()
        keys = []
        curated_eng[name] = eng_words(stems)
        for e in curated_eng[name]:
            keys += synsets_of(e)
        # hand-written list in the target language, first
        mine = os.path.join(own, f"{name.lower()}.txt")
        written = open(mine, encoding="utf-8").read().split() if os.path.exists(mine) else []
        for x in written:
            w = cand(x, HAND_MIN)
            if not w:
                if MIN_LEN <= len(x) <= MAX_LEN:
                    rejected.append((name, x))
            elif w not in seen:
                seen.add(w); curated[name].append(w)
        add_synsets(name, keys, seen)
        curated_keys[name] = keys
    # claim: curated first, then derived, each word belongs to one theme
    claimed, taken = set(), collections.defaultdict(list)
    for source in (curated, derived):
        for name, _, _, _ in gen_pools.THEMES:
            for w in source.get(name, ()):
                if w not in claimed:
                    claimed.add(w)
                    taken[name].append(w)
    # hand.txt: top up thin cells.  A word the theme already holds stays; a free word is added; a word held by another
    # theme moves only when that theme keeps its quota at the length and its 25 words.
    owner = {w: name for name, ws in taken.items() for w in ws}
    moved = 0
    for name, _, _, _ in gen_pools.THEMES:
        for w in topup[name]:
            o = owner.get(w)
            if o == name:
                continue
            if o is not None:
                n = len(w)
                if sum(1 for x in taken[o] if len(x) == n) - 1 < QUOTA.get(n, 0) or len(taken[o]) - 1 < MIN_TOTAL:
                    rejected.append((name, w + " (held by " + o + ")"))
                    continue
                taken[o].remove(w)
                moved += 1
            claimed.add(w)
            taken[name].append(w)
            owner[w] = name
    # fill passes: a theme below the standard (QUOTA words at lengths 4..7, MIN_TOTAL in all) takes, for the short
    # length only, unclaimed words from, in this order: (1) hyponyms (3 levels) of its seed synsets, (2) their siblings,
    # (3) any sense of the English words of its hand-written list, (4) any sense with the theme's lexical category,
    # (5) the English library's own words of the theme, projected through every sense of every wordnet,
    # (6) one hop along every other WordNet relation, hypernyms included, (7) the commonest words whose main sense has
    # one of the theme's two commonest lexical categories.  The seed synsets are the English words' synsets plus the
    # main senses of the words the theme already holds.  What is still short is written by hand (tools/words/lang/CODE).
    eng_pool = english_pools()
    filled, fill_log = collections.Counter(), []
    def good(keys):
        return {k for k in keys if k not in wn.instance and not BAD(wn, k)}
    def seeds(name):
        keys = set(curated_keys.get(name, ()))
        keys |= {main_sense(w, rev_all) for w in taken[name] if w in rev_all}
        return good(keys)
    def lemmas(keys):
        return [w for k in keys for lem in tgt_all.get(k, ()) for w in [cand(lem, FILL_MIN)] if w]
    def hypos(keys):
        lvl, out = set(keys), set()
        for _ in range(3):
            lvl = good(h for k in lvl for h in wn.hypo.get(k, ()))
            out |= lvl
        return out
    def siblings(keys):
        return good(h for k in keys for p in wn.hyper.get(k, ()) for h in wn.hypo.get(p, ()))
    def related(keys):
        out = set()
        for k in keys:
            out |= set(wn.rel.get(k, ())) | set(wn.hyper.get(k, ()))
        return good(out)
    def allsenses(wordlist):
        return good(k for e in wordlist for t in wn.index.get(e, {}) for k in wn.index[e][t])
    def short(by):
        return [n for n in QUOTA if by[n] < QUOTA[n]]
    for ti, (name, stems, cats, rts) in enumerate(gen_pools.THEMES):
        if name == "LEXICON":
            continue
        by = collections.Counter(len(w) for w in taken[name])
        if not short(by) and len(taken[name]) >= MIN_TOTAL:
            continue
        sd = seeds(name)
        any_sense = {}
        if cats:
            for w, ks in rev_all.items():
                for k in ks:
                    if wn.lex[k] in cats and not BAD(wn, k) and (not rts or (k.endswith("-n") and wn.ancestors(k) & roots[name])):
                        any_sense[w] = k
        lexcount = collections.Counter(wn.lex[k] for k in sd)
        toplex = {c for c, _ in lexcount.most_common(2)}
        sources = [("hyponym", lambda: lemmas(hypos(sd))),
                   ("sibling", lambda: lemmas(siblings(sd))),
                   ("all-senses", lambda: lemmas(allsenses(curated_eng.get(name, ())))),
                   ("any-sense", lambda: list(any_sense)),
                   ("english", lambda: lemmas(allsenses(eng_pool[ti]))),
                   ("related", lambda: lemmas(related(sd))),
                   ("category", lambda: [w for w in rev_all if wn.lex[main_sense(w, rev_all)] in toplex])]
        for label, get in ([] if os.environ.get("WP_NOFILL") else sources):
            by = collections.Counter(len(w) for w in taken[name])
            lens = short(by)
            if not lens and len(taken[name]) >= MIN_TOTAL:
                break
            pool = sorted(set(get()), key=lambda x: (-count[x], x))
            def take(n, limit):
                for w in pool:
                    if by[n] >= limit:
                        break
                    if len(w) != n or w in claimed or w in bad or w in bad_any:
                        continue
                    claimed.add(w); taken[name].append(w); by[n] += 1; filled[(name, label)] += 1
                    fill_log.append(f"{name} {n} {label} {w}")
            for n in lens:
                take(n, QUOTA[n] + HEADROOM[n])
            # the total: a theme under MIN_TOTAL takes more words of the lengths 5..7 (then 4, 8, 9)
            for n in (5, 6, 7, 4, 8, 9):
                if len(taken[name]) >= MIN_TOTAL:
                    break
                take(n, by[n] + MIN_TOTAL - len(taken[name]))
    if code in INFLECT:
        for name, _, _, _ in gen_pools.THEMES:
            if name == "LEXICON":
                continue
            for w in list(taken[name]):
                if w not in rev_all:
                    continue
                pos = main_sense(w, rev_all)[-1]
                stems = {w, w[:-1]} if w.endswith("e") else {w}
                for st in stems:
                    for suf in DA_FORMS.get(pos, ()):
                        f = st + suf
                        if f == w or f in claimed or not (MIN_LEN <= len(f) <= MAX_LEN) or f in bad_any or f in bad:
                            continue
                        if f in vocab and count.get(f, 0) >= INFLECT_MIN and not blocked(f) and len([x for x in taken[name] if len(x) == len(f)]) < PER_LEN:
                            claimed.add(f); taken[name].append(f); filled[(name, "inflected")] += 1
                            fill_log.append(f"{name} {len(f)} inflected {f} <- {w}")
    # LEXICON: the most familiar unclaimed words
    lex = [w for w in sorted(vocab, key=lambda x: (-count.get(x, 0), x))
           if w not in claimed and MIN_LEN <= len(w) <= MAX_LEN and count.get(w, 0) >= minc
           and not blocked(w) and w not in bad_any and w not in bad]
    for w in lex:
        taken["LEXICON"].append(w)
    for (name, label), c in sorted(filled.items()):
        print(f"  fill {name:8s} {label:9s} +{c}")
    for name, w in rejected:
        print(f"  hand-written word rejected (not in the dictionary, too rare or blocked): {name} {w}")
    open(os.path.join(work, code, "fill_log.txt"), "w").write("\n".join(fill_log) + "\n")
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
        if name == "LEXICON":
            if sum(counts) < LEXICON_TOTAL:
                weak.append(f"{name}/{local} {counts}")
        elif any(counts[n - MIN_LEN] < q for n, q in QUOTA.items()) or sum(counts) < MIN_TOTAL:
            weak.append(f"{name}/{local} {counts}")
    with open(os.path.join(work, code, "pools_report.txt"), "w") as f:
        for name, local, counts in report:
            f.write(f"{name:8s} {local:8s} {' '.join(f'{c:4d}' for c in counts)} total {sum(counts)}\n")
        f.write("WEAK: " + "; ".join(weak) + "\n")
    print("  themes below the standard:", weak if weak else "none")

def BAD(wn, k):
    return bool(gen_pools.BAD_GLOSS.search(wn.gloss.get(k, "")))

if __name__ == "__main__":
    build(*sys.argv[1:5])
