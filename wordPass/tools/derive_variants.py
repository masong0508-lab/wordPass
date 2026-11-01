#!/usr/bin/env python3
"""Derive tools/words/us_only.txt and tools/words/uk_only.txt: dictionary words that are
spelled one way only (color / colour).  A word in neither list is accepted in both settings.

    derive_variants.py US.words GB.words

US.words and GB.words are the expanded Hunspell en_US and en_GB word lists (unmunch.py).
Sources of a US-only or UK-only word, always checked against the game dictionary:
  1. in one Hunspell dictionary and not the other;
  2. a spelling rule (our/or, re/er, ll/l, ogue/og, ence/ense, ae/e, oe/e) turns it into a
     different dictionary word that the other dialect's Hunspell dictionary does not
     already contain.  Both spellings must be dictionary words, so for/four never pairs.
The -ize/-ise family is left out: both spellings are accepted everywhere (Oxford spelling).
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def rd(p): return {l.strip() for l in open(p, encoding="utf-8") if l.strip()}

# (regex on the US spelling, replacement) -> UK spelling; the lookahead keeps it to word ends and suffixes
SUF = r"(?=$|s$|ed$|ing$|ful$|less$|able$|ite$|ites$|y$|ier$|iest$|ly$|ist$|ists$|ism$|ful$|ier$|ies$)"
RULES = [
    (re.compile(r"([^aeiou])or" + SUF), r"\1our"),            # color -> colour
    (re.compile(r"([^aeiou])er(?=$|s$|ed$|ing$)"), r"\1re"),   # center -> centre
    (re.compile(r"([aeiou])l(?=ed$|ing$|er$|ers$|or$|ors$|ist$|ists$)"), r"\1ll"),   # traveled -> travelled
    (re.compile(r"og(?=$|s$)"), r"ogue"),                      # catalog -> catalogue
    (re.compile(r"ense(?=$|s$)"), r"ence"),                    # defense -> defence
    (re.compile(r"^(an|f|h|p|l|o)e"), None),                    # handled below
]
PREFIX_UK = [("anemi", "anaemi"), ("anesthe", "anaesthe"), ("fetus", "foetus"), ("fetal", "foetal"),
             ("esophag", "oesophag"), ("estrogen", "oestrogen"), ("pediatr", "paediatr"), ("hemo", "haemo"),
             ("hemor", "haemor"), ("leukemi", "leukaemi"), ("gynecolog", "gynaecolog"), ("orthopedi", "orthopaedi"),
             ("diarrhea", "diarrhoea"), ("edema", "oedema"), ("archeolog", "archaeolog"), ("pedophil", "paedophil"),
             ("sulf", "sulph"), ("gray", "grey"), ("plow", "plough"), ("mold", "mould"), ("molt", "moult"),
             ("smolder", "smoulder"), ("skeptic", "sceptic"), ("aluminum", "aluminium"), ("curb", "kerb"),
             ("maneuver", "manoeuvre"), ("jewelry", "jewellery"), ("mustache", "moustache"), ("pajama", "pyjama"),
             ("cozy", "cosy"), ("donut", "doughnut"), ("program", "programme"), ("tire", "tyre")]

def uk_of(w):
    out = set()
    for rx, rep in RULES[:-1]:
        for m in rx.finditer(w):
            out.add(w[:m.start()] + rx.sub(rep, w[m.start():], count=1))
            break
    for us, uk in PREFIX_UK:
        if w.startswith(us):
            out.add(uk + w[len(us):])
        elif us in w and len(us) >= 5:
            out.add(w.replace(us, uk, 1))
    return {x for x in out if x != w}

ISE = re.compile(r"(is|iz)(e|es|ed|ing|er|ers|ation|ations|able)$")

def main(us_path, gb_path):
    us, gb = rd(us_path), rd(gb_path)
    dd = set()
    for f in (os.path.join(HERE, "enable1.txt"), os.path.join(HERE, "words", "extra.txt"), os.path.join(HERE, "words", "more_open.txt")):
        dd |= {w for w in rd(f) if w.isalpha() and 3 <= len(w) <= 20}
    pairs = set()
    for w in us - gb:
        if w in dd: pairs.add(("us", w))
    for w in gb - us:
        if w in dd: pairs.add(("uk", w))
    rule_pairs = 0
    for w in dd:
        for uk in uk_of(w):
            if uk in dd and uk not in us and w not in gb:
                if (("us", w) not in pairs): rule_pairs += 1
                pairs.add(("us", w)); pairs.add(("uk", uk))
    def izefam(w):
        m = ISE.search(w)
        if not m: return False
        other = w[:m.start()] + ("iz" if m.group(1) == "is" else "is") + w[m.start() + 2:]
        return other in dd or other in us or other in gb
    usO = sorted({w for k, w in pairs if k == "us" and not izefam(w)})
    ukO = sorted({w for k, w in pairs if k == "uk" and not izefam(w)})
    both = set(usO) & set(ukO)      # a word that is both (rules clashed): neutral
    usO = [w for w in usO if w not in both]; ukO = [w for w in ukO if w not in both]
    open(os.path.join(HERE, "words", "us_only.txt"), "w").write("\n".join(usO) + "\n")
    open(os.path.join(HERE, "words", "uk_only.txt"), "w").write("\n".join(ukO) + "\n")
    print(f"{len(usO)} US-only, {len(ukO)} UK-only ({rule_pairs} added by rules)")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
