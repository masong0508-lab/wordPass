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
     letters) is added.  Finnish has no open Hunspell dictionary on a reachable host, but the
     sources of the Voikko morphology are: tools/lang/voikko_fi.py compiles them with foma, and
     every lower-case form of the subtitle list that the transducer accepts (any inflected
     form of the Joukahainen lexemes, compounds included) is a Finnish word.  A compound that
     Voikko only accepts as two glued words must occur 4 times, like the other compounding
     languages; the lexemes themselves are bare entries.  The word forms of the Universal
     Dependencies Finnish treebanks (written Finnish, names left out) are candidates too, and
     each occurrence there counts as 20 subtitle occurrences when the list is ranked for the
     cap, so written forms outrank the rarest subtitle ones.
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
MIN_COUNT = {"es": 2, "fr": 2, "de": 2, "it": 2, "pt": 2, "nl": 2, "sv": 2, "pl": 2, "da": 2, "fi": 2}
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

COMPOUND_MIN = 4        # a form Voikko accepts only as glued words must occur this often
UD_WEIGHT = 20          # a form seen once in the Finnish treebanks counts as this many subtitle occurrences when the list is ranked

def ud_forms(raw):
    """Counter of the lower-case word forms of the Universal Dependencies Finnish treebanks (written Finnish, names left out)."""
    out = collections.Counter()
    d = os.path.join(raw, "ud-fi")
    for name in sorted(os.listdir(d)) if os.path.isdir(d) else ():
        for line in open(os.path.join(d, name), encoding="utf-8"):
            p = line.split("\t")
            if len(p) > 3 and p[0].isdigit() and p[3] != "PROPN" and p[1].isalpha():
                out[p[1].lower()] += 1
    return out

def build(code, raw, work, cap=CAP):
    out = os.path.join(work, code)
    os.makedirs(out, exist_ok=True)
    freq = read_freq(os.path.join(raw, "freq", f"{code}_full.txt"))
    top = read_list(os.path.join(raw, "freq", f"{code}.txt"))
    vowels = {l[0]: l[3] for l in common.LANGS}[code]
    minc = MIN_COUNT[code]
    stems, forms, boost = set(), set(), {}
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
        import voikko_fi
        mor = voikko_fi.build(raw, work)
        stems = voikko_fi.lemmas(raw) | fin_lemmas(raw)           # Joukahainen lexemes and FinnWordNet lemmas, if Voikko accepts them
        boost = {w: UD_WEIGHT * n for w, n in ud_forms(raw).items()}
        analysis = voikko_fi.analyse(mor, {w for w, n in freq.items() if w.islower() and n >= minc} | set(boost) | stems)
        forms = set()
        for w, an in analysis.items():
            if all(re.match(r"\[L(ee|es|ep|em)\]", a) for a in an):      # a first name, surname, place or other name, only
                continue
            glued = all("[Bc]" in a for a in an)
            if w in stems or freq.get(w, 0) + boost.get(w, 0) >= (COMPOUND_MIN if glued else minc):
                forms.add(w)
        stems &= forms
    cand = {}                                   # normalised word -> attested count
    for f in forms:
        n = freq.get(f, 0) + (boost.get(f, 0) if code == "fi" else 0)
        if f in stems:
            ok = True
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
