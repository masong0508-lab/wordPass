"""Shared helpers for the per-language data builders (tools/lang/*.py)."""
import collections, math, os, re, struct, sys, unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)                 # the wordPass project folder
sys.path.insert(0, TOOLS)
sys.path.insert(0, HERE)

# code, native name (upper case A-Z, shown in the options row), English name, letters that count as vowels
LANGS = [
    ("es", "ESPANOL",   "Spanish",    "aeiou"),
    ("fr", "FRANCAIS",  "French",     "aeiou"),
    ("de", "DEUTSCH",   "German",     "aeiou"),
    ("it", "ITALIANO",  "Italian",    "aeiou"),
    ("pt", "PORTUGUES", "Portuguese", "aeiou"),
    ("nl", "NEDERLANDS", "Dutch",     "aeiou"),
    ("sv", "SVENSKA",   "Swedish",    "aeiouy"),
    ("pl", "POLSKI",    "Polish",     "aeiouy"),
    ("da", "DANSK",     "Danish",     "aeiouy"),
    ("fi", "SUOMI",     "Finnish",    "aeiouy"),
]
CODES = [l[0] for l in LANGS]

MIN_LEN, MAX_LEN = 3, 20
EXTRA = {"ß": "ss", "æ": "ae", "œ": "oe", "ø": "o", "ł": "l", "đ": "d",
         "ð": "d", "þ": "th", "ı": "i"}

def norm(word):
    """Lower case, strip diacritics (a-acute -> a, n-tilde -> n, c-cedilla -> c, umlauts, ring, ...),
    sharp s -> ss, ae/oe ligatures -> ae/oe, o-slash -> o, l-stroke -> l.  None if anything but a-z is left."""
    w = word.lower()
    w = "".join(EXTRA.get(c, c) for c in w)
    w = "".join(c for c in unicodedata.normalize("NFKD", w) if not unicodedata.combining(c))
    return w if w.isascii() and w.isalpha() else None

def read_freq(path):
    """hermitdave FrequencyWords: 'word count' per line.  Returns {raw word: count} (alphabetic only)."""
    out = {}
    for line in open(path, encoding="utf-8", errors="ignore"):
        p = line.split()
        if len(p) == 2 and p[0].isalpha() and p[1].isdigit():
            out[p[0]] = out.get(p[0], 0) + int(p[1])
    return out

def read_list(path):
    """First 50k list in rank order, as an ordered list of words."""
    return [l.split()[0] for l in open(path, encoding="utf-8", errors="ignore") if l.split()]
