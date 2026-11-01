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
