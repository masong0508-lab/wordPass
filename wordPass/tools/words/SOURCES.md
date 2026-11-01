# Word data

| File | What | Source / licence |
| --- | --- | --- |
| `../enable1.txt` | validation dictionary (172,823 words, 125,450 of them 3-10 letters with extras) | ENABLE word list, public domain |
| `extra.txt` | modern and British spellings ENABLE lacks, plus the mild-curse words | written for this project |
| `../common_words.txt` | the common tier (top 30k of the frequency list or Google 10k, plus a few extras) | FrequencyWords 2018 (hermitdave, CC BY-SA 4.0) and google-10000-english |
| `pools/<theme>.txt` | hand-written pass words per theme | written for this project |
| `pools/wn_<theme>.txt` | pass words derived from WordNet lexical categories | Princeton WordNet 3.1 (WordNet licence), familiar words only |
| `blocklist.txt` | words that never become a pass word | LDNOOBW (CC BY 4.0), google-profanity-words, zacanger/profane-words (MIT), plus this project's own additions |

Content ceiling: the strongest words the library will use are *holy*, *damn*, *crappola* and *moobs*.
Anything harsher is on the blocklist and is never picked as a pass word.

## Regenerating

```
python3 tools/gen_dict.py tools/enable1.txt,tools/words/extra.txt data/dict.bin tools/common_words.txt
python3 tools/gen_dict.py --verify data/dict.bin tools/enable1.txt,tools/words/extra.txt tools/common_words.txt
python3 tools/gen_pools.py                       # rebuild data/pools.bin from the text files
python3 tools/gen_pools.py --wordnet DIR --freq en_50k.txt --blocklist tools/words/blocklist.txt   # also re-derive the wn_ files
```

`gen_dict.py` takes several comma separated word lists, so a bigger list you are licensed to use
(for example a tournament Scrabble list) can be added without touching the code:
`tools/enable1.txt,tools/words/extra.txt,/path/to/more.txt`.
