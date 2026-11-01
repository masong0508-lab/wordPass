#!/usr/bin/env python3
"""Derive tools/words/more_open.txt (dictionary additions beyond ENABLE) and
tools/common_words.txt (the common tier) from open sources.

    derive_extra.py --enable tools/enable1.txt --wordnet DIR --dwyl words_alpha.txt \
                    --freq en_50k.txt --google10k google-10000-english.txt

more_open.txt = WordNet lemmas that are not names (single lower-case words, 3..20 letters)
plus inflected forms of WordNet lemmas that the dwyl list also confirms (WordNet's own
morphy rules, plus consonant doubling), minus anything already in ENABLE.
common_words.txt = ENABLE or additional words in the top 30,000 of the frequency list or in
the Google 10k list, 3..20 letters.
"""
import collections, os, re, sys

MIN_LEN, MAX_LEN = 3, 20
HERE = os.path.dirname(os.path.abspath(__file__))

def words(path):
    return {l.strip().lower() for l in open(path, encoding="utf-8", errors="ignore") if l.strip().isalpha()}

def ok(w):
    return w.isalpha() and w.isascii() and MIN_LEN <= len(w) <= MAX_LEN and re.search("[aeiouy]", w) and not ("q" in w and "qu" not in w) and not re.fullmatch("[ivxlcdm]+", w)

def main(opt):
    enable = {w for w in words(opt["--enable"]) if ok(w)}
    wn = opt["--wordnet"]
    case = collections.defaultdict(lambda: [0, 0])
    for pos in ("noun", "verb", "adj", "adv"):
        for l in open(os.path.join(wn, f"data.{pos}"), encoding="utf-8"):
            if l.startswith(" "):
                continue
            p = l.partition("|")[0].split()
            for i in range(int(p[3], 16)):
                w = p[4 + 2 * i]
                case[re.sub(r"\(.*", "", w).lower()][0 if w[0].isupper() else 1] += 1
    lemmas = {pos: set() for pos in ("noun", "verb", "adj", "adv")}
    for pos in lemmas:
        for l in open(os.path.join(wn, f"index.{pos}"), encoding="utf-8"):
            if l.startswith(" "):
                continue
            w = l.split()[0]
            if w.isalpha() and case[w][0] < case[w][1]:      # not mostly a name
                lemmas[pos].add(w)
    exc = {}
    for pos in lemmas:
        for l in open(os.path.join(wn, f"{pos}.exc"), encoding="utf-8"):
            p = l.split()
            if len(p) >= 2 and p[0].isalpha():
                exc[p[0]] = p[1:]
    RULES = {
        "noun": [("s", ""), ("ses", "s"), ("xes", "x"), ("zes", "z"), ("ches", "ch"), ("shes", "sh"), ("men", "man"), ("ies", "y")],
        "verb": [("s", ""), ("ies", "y"), ("es", "e"), ("es", ""), ("ed", "e"), ("ed", ""), ("ing", "e"), ("ing", "")],
        "adj":  [("er", ""), ("est", ""), ("er", "e"), ("est", "e")],
    }
    def inflected(w):
        if w in exc and any(b in lemmas[pos] for pos in lemmas for b in exc[w]):
            return True
        for pos, rules in RULES.items():
            for suf, rep in rules:
                if w.endswith(suf) and len(w) > len(suf) + 1:
                    stem = w[: len(w) - len(suf)] + rep
                    if stem in lemmas[pos]:
                        return True
                    if suf in ("ed", "ing", "er", "est") and len(stem) > 2 and stem[-1] == stem[-2] and stem[:-1] in lemmas[pos]:
                        return True      # running -> run
        return False
    dwyl = words(opt["--dwyl"])
    everything = set().union(*lemmas.values())
    extra = {w for w in everything if ok(w) and w not in enable}
    extra |= {w for w in dwyl if ok(w) and w not in enable and inflected(w) and case[w][0] <= case[w][1]}
    extra = {w for w in extra if not (case[w][0] > case[w][1])}
    path = os.path.join(HERE, "words", "more_open.txt")
    open(path, "w").write("\n".join(sorted(extra)) + "\n")
    by = collections.Counter(len(w) for w in extra)
    print(f"{len(extra)} additions ({', '.join(f'{n}:{by[n]}' for n in sorted(by))}) -> {path}")
    # common tier
    freq = []
    for l in open(opt["--freq"], encoding="utf-8"):
        w = l.split()[0].lower()
        if w.isalpha():
            freq.append(w)
    top = set(freq[:30000]) | words(opt["--google10k"])
    valid = enable | extra
    common = sorted(w for w in top if w in valid)
    open(os.path.join(HERE, "common_words.txt"), "w").write("\n".join(common) + "\n")
    print(f"{len(common)} common words -> tools/common_words.txt")

if __name__ == "__main__":
    a = sys.argv[1:]
    main({a[i]: a[i + 1] for i in range(0, len(a) - 1, 2)})
