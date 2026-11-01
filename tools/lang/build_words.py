#!/usr/bin/env python3
"""Step 1 of the language pipeline: the validation word list of one language.

    build_words.py CODE RAW_DIR WORK_DIR [--cap N]

Writes WORK_DIR/CODE/words.txt (normalised a-z, 3..20 letters, one per line, most frequent
first), common.txt (the common tier: top 30k of the frequency list) and stats.txt.

How a word gets in (all sources are open, see tools/words/SOURCES.md):
  1. Hunspell expansion (tools/lang/hunspell.py) of the language's wooorm/dictionaries
     dictionary: every inflected form of every lower-case entry (German: also nouns, see below).
  2. It must be ATTESTED: the exact form occurs in the OpenSubtitles frequency list
     (hermitdave FrequencyWords 2018, full list) at least MIN_COUNT times, or be a bare
     dictionary entry.  This is what keeps Italian (34 million clitic forms) and Portuguese
     inside a cartridge and what drops subtitle typos, because a typo is not in Hunspell.
  3. German, Dutch, Swedish, Danish: compounds are open-ended and Hunspell does not list
     them, so a frequent form that splits into two accepted words (with the usual linking
     letters) is added.  Finnish has no open Hunspell dictionary on a reachable host: its words
     are FinnWordNet lemmas plus frequent subtitle forms that pass a vowel-harmony and word-ending
     check (see FINNISH below), the weakest list of the ten.
  4. Normalise (diacritics stripped, sharp s -> ss, ...), keep 3..20 letters, at least one vowel,
     no letter three times in a row, and drop words that use a letter outside the language's
     alphabet (a letter with under 0.04 percent of the tiles, see letters.py).
  5. Cap: the words are ranked by attested frequency (bare entries without any count last)
     and the list is cut at the cap.
"""
import collections, math, os, re, sys
import common
from common import norm, read_freq, read_list, MIN_LEN, MAX_LEN
import hunspell

CAP = 220000
MIN_COUNT = {"es": 2, "fr": 2, "de": 2, "it": 2, "pt": 2, "nl": 2, "sv": 2, "pl": 2, "da": 2, "fi": 8}
COMPOUND = {"de", "nl", "sv", "da"}
LINKS = {"de": ("s", "es", "n", "en", "er", "e", "ns"), "nl": ("s", "e", "en"), "sv": ("s", "a", "e", "o", "u"),
         "da": ("s", "e")}

def german_nouns(raw):
    path = os.path.join(raw, "odenet", "odenet-1.4", "deWordNet.xml")
    nouns = set()
    for m in re.finditer(r'<Lemma writtenForm="([^"]+)" partOfSpeech="n"', open(path, encoding="utf-8").read()):
        nouns.add(m.group(1).lower())
    return nouns

def fin_lemmas(raw):
    out = set()
    for line in open(os.path.join(raw, "omw", "omw-1.4", "fin", "wn-data-fin.tab"), encoding="utf-8"):
        p = line.rstrip("\n").split("\t")
        if len(p) == 3 and p[1].endswith("lemma") and p[2].isalpha() and p[2].islower():
            out.add(p[2])
    return out

BACK, FRONT = set("aou"), set("äöy")
def finnish_ok(w, n):
    """Plausible Finnish form: vowel harmony (compounds and loans excepted when frequent), native ending."""
    if any(c in w for c in "bcfqwxzå") and n < 200:
        return False
    if (BACK & set(w)) and (FRONT & set(w)) and n < 100:
        return False
    if w[-1] not in "aeiouyäönstrl" :
        return False
    if re.search(r"[bcdfghjklmnpqrstvwxz]{4}", w):
        return False
    return True

def build(code, raw, work, cap=CAP):
    out = os.path.join(work, code)
    os.makedirs(out, exist_ok=True)
    freq = read_freq(os.path.join(raw, "freq", f"{code}_full.txt"))
    top = read_list(os.path.join(raw, "freq", f"{code}.txt"))
    vowels = {l[0]: l[3] for l in common.LANGS}[code]
    minc = MIN_COUNT[code]
    stems, forms = set(), set()
    if code != "fi":
        dic, aff = os.path.join(raw, "hun", f"{code}.dic"), os.path.join(raw, "hun", f"{code}.aff")
        keep = None
        if code == "de":
            nouns = german_nouns(raw)
            keep = lambda s, forms: any(f.lower() in nouns for f in forms)
        for line in open(dic, encoding="utf-8", errors="ignore").read().split("\n")[1:]:
            if line and line[0] not in "#\t /":
                s = line.split()[0].partition("/")[0]
                if s.isalpha() and (s.islower() or (keep is not None and keep(s, {s}))):
                    stems.add(s.lower())
        forms = {f.lower() for f in hunspell.expand(dic, aff, keep) if f.isalpha()}
    else:
        stems = fin_lemmas(raw)
        forms = {w for w in freq if w.islower()}
    cand = {}                                   # normalised word -> attested count
    for f in forms:
        n = freq.get(f, 0)
        if f in stems and code != "fi":
            ok = True
        elif code == "fi":
            ok = (f in stems and n >= 1) or (n >= minc and finnish_ok(f, n))
        else:
            ok = n >= minc
        if not ok:
            continue
        w = norm(f)
        if w and MIN_LEN <= len(w) <= MAX_LEN:
            cand[w] = cand.get(w, 0) + n
    if code == "fi":
        for s in stems:
            w = norm(s)
            if w and MIN_LEN <= len(w) <= MAX_LEN and w not in cand:
                cand[w] = 0
    # compounds (de nl sv da): a frequent unlisted form that is two accepted words
    added = 0
    if code in COMPOUND:
        base = set(cand)
        links = LINKS[code]
        def split_ok(w):
            for i in range(3, len(w) - 2):
                left, right = w[:i], w[i:]
                if right not in base or len(right) < 4:
                    continue
                if left in base or any(left.endswith(l) and len(left) - len(l) >= 3 and left[:-len(l)] in base for l in links):
                    return True
            return False
        for f, n in freq.items():
            if n < 4 or not f.islower():
                continue
            w = norm(f)
            if w and w not in cand and 7 <= len(w) <= MAX_LEN and split_ok(w):
                cand[w] = n
                added += 1
    # letters used by the language: drop words with letters that are nearly unused
    tiles = collections.Counter()
    for w, n in cand.items():
        wt = 1 + math.log10(1 + n)
        for c in w.replace("qu", "q"):
            tiles[c] += wt
    total = sum(tiles.values())
    alphabet = {c for c in "abcdefghijklmnopqrstuvwxyz" if tiles[c] / total >= 0.0004}
    final = {}
    for w, n in cand.items():
        if not set(w) <= alphabet:
            continue
        if not any(c in vowels for c in w) or re.search(r"(.)\1\1", w):
            continue
        if "q" in w and "qu" not in w:
            continue
        final[w] = n
    ranked = sorted(final, key=lambda w: (-final[w], w))
    kept = ranked[:cap]
    cutoff = final[kept[-1]] if kept else 0
    with open(os.path.join(out, "words.txt"), "w") as f:
        f.write("\n".join(kept) + "\n")
    wordset = set(kept)
    seen, common_words = set(), []
    for t in top[:30000]:
        w = norm(t)
        if w and w in wordset and w not in seen:
            seen.add(w); common_words.append(w)
    with open(os.path.join(out, "common.txt"), "w") as f:
        f.write("\n".join(common_words) + "\n")
    # frequency by normalised word (all freq words, not only kept), for the letter and pool steps
    nf = collections.Counter()
    for w, n in freq.items():
        x = norm(w)
        if x:
            nf[x] += n
    with open(os.path.join(out, "freq.txt"), "w") as f:
        for w, n in nf.most_common(300000):
            f.write(f"{w} {n}\n")
    with open(os.path.join(out, "stats.txt"), "w") as f:
        f.write(f"hunspell forms {len(forms)}; candidates {len(cand)} (compounds added {added}); "
                f"after letter filter {len(final)}; kept {len(kept)} (cap {cap}, cutoff count {cutoff}); "
                f"common {len(common_words)}; alphabet {''.join(sorted(alphabet))}\n")
    print(code, open(os.path.join(out, "stats.txt")).read().strip())

if __name__ == "__main__":
    a = sys.argv[1:]
    cap = int(a[a.index("--cap") + 1]) if "--cap" in a else CAP
    pos = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or a[i - 1] != "--cap")]
    build(pos[0], pos[1], pos[2], cap)
