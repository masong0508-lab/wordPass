#!/usr/bin/env python3
"""The Finnish word check: the Voikko morphology (voikko-fi, GPL-2+), compiled here from its open sources.

    voikko_fi.py RAW_DIR WORK_DIR          build the transducer and print a few checks

wooorm/dictionaries has no Finnish Hunspell file and the Voikko dictionary is not on the allowed hosts as a
binary, but its sources are: voikko-fi/vocabulary/joukahainen.xml (about 39,000 lexemes with their Kotus
inflection classes), the generators (vvfst/generate_lex.py ...) and the lexc grammar of the Finnish inflection
and compounding, all on raw.githubusercontent.com (tools/lang/fetch.sh downloads them to RAW_DIR/voikko-fi).
Building follows voikko-fi's own Makefile up to the finite-state transducer: its Python generators write the
lexc files, foma compiles them (the same main.foma script the Makefile runs, with `save stack` added) and
flookup answers "is this word form Finnish, and what is it" for a whole list at once.

Needs `make`, `foma` and `flookup` (Debian and Ubuntu package foma-bin).  The transducer accepts every inflected
form of every lexeme (cases, plural, possessive suffixes, clitics, verb forms, participles) and compounds of
two or more of them, so it replaces the Hunspell expansion the other nine languages use.  Colloquial
spellings (mä, sä, mut), names and foreign words are not accepted, which is what a word game wants.
"""
import os, re, shutil, subprocess, sys

def build(raw, work):
    """Path of the compiled transducer WORK/fi/voikko/mor.foma (built once)."""
    src = os.path.join(raw, "voikko-fi")
    dst = os.path.join(work, "fi", "voikko")
    out = os.path.join(dst, "vvfst", "mor.foma")
    if os.path.exists(out):
        return out
    for tool in ("make", "foma", "flookup"):
        if not shutil.which(tool):
            sys.exit(f"voikko_fi.py: {tool} not found (apt-get install make foma-bin)")
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    subprocess.run(["make", "vvfst/all.lexc", "PYTHON=" + sys.executable], cwd=dst, check=True, stdout=subprocess.DEVNULL)
    script = open(os.path.join(dst, "vvfst", "main.foma.in")).read().replace("VVFST_BUILDDIR", "vvfst")
    open(os.path.join(dst, "vvfst", "main.foma"), "w").write(script + "\nsave stack vvfst/mor.foma\n")
    subprocess.run(["foma", "-f", "vvfst/main.foma"], cwd=dst, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert os.path.exists(out)
    return out

def analyse(mor, forms, chunk=200000):
    """{form: [analysis, ...]} for the forms the transducer accepts (a form it rejects is absent).  An analysis looks
    like [Ln][Ica][Xp]talo[X]talo[Sine][Nm]issa; [Bc] marks a compound boundary."""
    forms = list(forms)
    res = {}
    for i in range(0, len(forms), chunk):
        part = forms[i:i + chunk]
        r = subprocess.run(["flookup", "-i", "-b", mor], input="\n".join(part) + "\n", capture_output=True, text=True, check=True)
        for line in r.stdout.split("\n"):
            if "\t" in line:
                a, b = line.split("\t", 1)
                if b != "+?":
                    res.setdefault(a, []).append(b)
    return res

def lemmas(raw):
    """Lower-case base forms of the Joukahainen lexemes that are common words, not names or abbreviations."""
    text = open(os.path.join(raw, "voikko-fi", "vocabulary", "joukahainen.xml"), encoding="utf-8").read()
    out = set()
    for m in re.finditer(r"<word\b.*?</word>", text, re.S):
        e = m.group(0)
        cls = re.findall(r"<wclass>([a-z_]+)</wclass>", e)
        if not cls or any(c.startswith("pnoun") or c in ("abbreviation", "prefix") for c in cls):
            continue
        for f in re.findall(r"<form>([^<]+)</form>", e):
            if f.isalpha() and f.islower():
                out.add(f)
    return out

if __name__ == "__main__":
    mor = build(sys.argv[1], sys.argv[2])
    test = "talo talossa taloissa kirjastossa kävelemme mä sä charlie talonomistajalle xyzzy".split()
    for w, a in analyse(mor, test).items():
        print(w, a[0])
    print(len(lemmas(sys.argv[1])), "lemmas")
