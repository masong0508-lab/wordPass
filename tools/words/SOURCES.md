# Word data

| File | What | Source / licence |
| --- | --- | --- |
| `../enable1.txt` | validation dictionary core (172,823 words, 3-20 letters) | ENABLE word list, public domain |
| `more_open.txt` | about 10,800 additions: WordNet lemmas that are not names, and inflected forms of them confirmed by the dwyl list (`tools/derive_extra.py`) | Princeton WordNet 3.1; dwyl/english-words (Unlicense) |
| `us_only.txt`, `uk_only.txt` | words spelled one way only, for the SPELLING option (`tools/derive_variants.py`) | derived from the wooorm/dictionaries en and en-GB Hunspell dictionaries (SCOWL, permissive licences) plus spelling rules |
| `extra.txt` | modern and British spellings ENABLE lacks, plus the mild-curse words | written for this project |
| `../common_words.txt` | the common tier (top 30k of the frequency list or Google 10k, plus a few extras) | FrequencyWords 2018 (hermitdave, CC BY-SA 4.0) and google-10000-english |
| `pools/<theme>.txt` | hand-written pass words per theme (`lexicon.txt` is generated: the most familiar plurals and -ing/-ed forms of words already in the library) | written for this project |
| `pools/wn_<theme>.txt` | pass words derived from WordNet lexical categories | Princeton WordNet 3.1 (WordNet licence), words in the top 50k of the frequency list or tagged in SemCor, ordered familiar first |
| `blocklist.txt` | words that never become a pass word | LDNOOBW (CC BY 4.0), google-profanity-words, zacanger/profane-words (MIT), plus this project's own additions |

Library size: 40 themes, about 17,900 pass words in 122 KB (`data/pools.bin`).

Content ceiling: the strongest words the library will use are *holy*, *damn*, *crappola* and *moobs*.
Anything harsher is on the blocklist and is never picked as a pass word.

## Regenerating

```
make dict          # data/dict.bin from enable1.txt, extra.txt and more_open.txt, with the common tier
make variants      # data/variants.bin from us_only.txt and uk_only.txt
python3 tools/derive_extra.py --enable tools/enable1.txt --wordnet DIR --dwyl words_alpha.txt --freq en_50k.txt --google10k google-10000-english.txt
python3 tools/derive_variants.py US.words GB.words    # expanded Hunspell lists from tools/unmunch.py
python3 tools/gen_pools.py                       # rebuild data/pools.bin from the text files
python3 tools/gen_pools.py --wordnet DIR --freq en_50k.txt --blocklist tools/words/blocklist.txt   # also re-derive the wn_ files
```

`gen_dict.py` takes several comma separated word lists, so a bigger list you are licensed to use
(for example a tournament Scrabble list) can be added without touching the code:
`tools/enable1.txt,tools/words/extra.txt,/path/to/more.txt`.

## Ten languages (`data/lang`, see `docs/LANGUAGES-DATA.md`)

Nothing below is committed except the small hand-written inputs; `tools/lang/fetch.sh` downloads the rest.

| What | Source | Licence |
| --- | --- | --- |
| Hunspell dictionaries (es, fr, de, it, pt, nl, sv, pl, da) expanded by `tools/lang/hunspell.py` | wooorm/dictionaries on GitHub, which packages: Spanish (RLA-ES), French (Grammalecte), German (igerman98), Italian (Italian Writing Aids), Portuguese (Raimundo Moura), Dutch (OpenTaal), Swedish (Goran Andersson), Polish (Polish Native Lang Project), Danish (Stavekontrolden) | es GPL-3.0 or LGPL-3.0 or MPL-1.1; fr MPL-2.0; de GPL-2.0 or GPL-3.0; it GPL-3.0; pt LGPL-3.0 or MPL-2.0; nl BSD-3-Clause or CC BY 3.0; sv LGPL-3.0; pl GPL-3.0 or LGPL-3.0 or MPL-2.0; da GPL-2.0 or LGPL-2.1 or MPL-1.1. Only derived word lists are shipped. |
| Frequency lists (top 50k and full list), used for attestation, the common tier and ordering | FrequencyWords 2018 (hermitdave), from OpenSubtitles | CC BY-SA 4.0 |
| Finnish: word forms of the Universal Dependencies treebanks (TDT, FTB, OOD, PUD: written Finnish), which rank ahead of rare subtitle forms; only the derived word list is shipped | UniversalDependencies/UD_Finnish-* on GitHub (raw files) | CC BY-SA 3.0 or 4.0 (TDT, OOD, PUD), CC BY 4.0 (FTB) |
| Finnish word check: the Voikko morphology (the vocabulary `joukahainen.xml` with its Kotus inflection classes, the generators and the lexc grammar), compiled by `tools/lang/voikko_fi.py` with foma (Debian and Ubuntu package foma-bin); only the derived word list is shipped | corevoikko/voikko-fi on GitHub (raw files; the Voikko dictionary binary is not on an allowed host, its sources are) | GPL-2.0 or later |
| Wordnets for pass words (through Open Multilingual Wordnet 1.4 and WordNet 3.0 ids): Spanish | Multilingual Central Repository | CC BY 3.0 |
| French | WOLF, Wordnet Libre du Francais | CeCILL-C |
| Italian | MultiWordNet | CC BY 3.0 |
| Portuguese | OpenWN-PT | CC BY-SA 3.0 |
| Dutch | Open Dutch WordNet | CC BY-SA 4.0 |
| Swedish | SALDO wordnet | CC BY 3.0 |
| Polish | plWordNet | plWordNet licence (permits commercial use with the notice) |
| Danish (also used for Swedish) | DanNet | DanNet licence (BSD-style, permits redistribution) |
| Norwegian Bokmal and Nynorsk (used for Danish and Swedish) | Norwegian Wordnet (Language Bank, National Library) | NWN licence (WordNet-style, permits commercial use with the notice) |
| Finnish | FinnWordNet | CC BY 3.0 |
| Extra senses for every language (the main source for Swedish and Danish): the Wiktionary-derived wordnets `wn-wikt-CODE.tab` | omwn/omw-data on GitHub (Open Multilingual Wordnet) | CC BY-SA 3.0 (Wiktionary text) |
| English word to the language's word, for Swedish, Danish, Spanish, French, Portuguese and Dutch (each English word's WordNet senses) | FreeDict dictionaries swe-eng, dan-eng, eng-dan, spa-eng, fra-eng, eng-fra, por-eng, eng-por, nld-eng, eng-nld (freedict/fd-dictionaries on GitHub) | GPL-2.0 or later (Swedish and Danish: generated from Ergane) |
| German | Open German WordNet (OdeNet) 1.4, linked to WordNet 3.0 through the Collaborative Interlingual Index (globalwordnet/cili) | CC BY-SA 4.0 (OdeNet), CC BY 4.0 (CILI) |
| English side of the translation | Princeton WordNet 3.0 (nltk_data) | WordNet licence |
| `lang/<code>/blocklist.txt` (words that never become a pass word): LDNOOBW lists per language, plus project additions | List of Dirty, Naughty, Obscene and Otherwise Bad Words | CC BY 4.0 |
| `tools/words/lang/<code>/curses.txt`, `blocklist.txt`, `hand.txt` (words written to top up thin theme cells), and the verse, myth, motion, making and change lists | written for this project | MIT |

Finnish has no Hunspell dictionary on a host the build can reach (wooorm/dictionaries ships an empty stub for it),
so its words are the forms of the subtitle list that the Voikko transducer accepts (every inflected form of
the Joukahainen lexemes, compounds included), plus the lexemes themselves; see `docs/LANGUAGES-DATA.md` for every
attempt. UniMorph, which has inflection tables for several of the other languages, has no Finnish file on a
reachable host either.

Regenerating: `tools/lang/fetch.sh RAW`, then `python3 tools/lang/build_all.py RAW WORK` (all ten) or with
language codes; `python3 tools/lang/build_all.py --manifest` re-pins the English files; `make check`
runs `tools/check/lang_test.c` over the result.
