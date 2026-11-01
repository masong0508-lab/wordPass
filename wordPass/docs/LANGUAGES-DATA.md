# Language data: ten languages, dictionaries, pass words and tile tables

This is the data half of "play the whole game in your own language". It is finished and tested:
`data/lang/<code>/{dict.bin, pools.bin, letters.bin}` for ten languages, a manifest, the scripts that
build them from open sources, and `tools/check/lang_test.c`. The game does not read any of it yet;
the NitroFS loader, the LANGUAGE option and the runtime letter tables are a later task that starts
from the formats and sizes in the last section of this page. English is untouched: `data/dict.bin`,
`data/pools.bin` and `data/variants.bin` are byte for byte what they were (the manifest pins their sha256
and `make check` verifies it).

## The ten languages

All ten of the preferred set were buildable; there are no substitutions. Two of them needed a
different route than the others (German's wordnet, Finnish's dictionary, see Limits).

| code | name in the menu | language | dictionary words | common tier | pass words | dict.bin | pools.bin | Qu tile | letters with no tile |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | :---: | --- |
| es | ESPANOL | Spanish | 188,926 | 23,250 | 13,690 | 1,257,867 | 93,249 | yes | K W |
| fr | FRANCAIS | French | 146,690 | 21,352 | 16,058 | 979,870 | 109,927 | yes | W |
| de | DEUTSCH | German | 167,158 | 21,859 | 14,330 | 1,278,177 | 99,976 | yes | none |
| it | ITALIANO | Italian | 213,299 | 23,320 | 15,846 | 1,494,184 | 111,164 | yes | J K W X Y |
| pt | PORTUGUES | Portuguese | 220,000 | 21,307 | 16,211 | 1,479,568 | 110,441 | yes | K W Y |
| nl | NEDERLANDS | Dutch | 220,000 | 22,787 | 16,290 | 1,638,962 | 111,426 | no | Q |
| sv | SVENSKA | Swedish | 220,000 | 23,072 | 9,926 | 1,571,118 | 64,298 | no | Q W Z |
| pl | POLSKI | Polish | 220,000 | 25,443 | 14,112 | 1,444,133 | 93,856 | no | Q V X |
| da | DANSK | Danish | 202,198 | 22,688 | 9,263 | 1,464,862 | 60,058 | no | Q |
| fi | SUOMI | Finnish | 220,000 | 28,821 | 16,261 | 1,502,316 | 111,314 | no | Q W X Z |

`data/lang` is 15,077,446 bytes in all (`data/lang/manifest.txt` has the exact numbers, one tab separated
row per language, read by the test). A letter with "no tile" is one the language does not use: it has
under 0.04 percent of the tiles, so words containing it are left out of the dictionary (a word you can
never spell is dead weight) and the bag never deals it.

## The pipeline

Everything is reproducible. Large downloads are not committed; small hand-written inputs are
(`tools/words/lang/<code>/`).

```
tools/lang/fetch.sh RAW                       # download the sources (about 130 MB) into a scratch folder
python3 tools/lang/build_all.py RAW WORK      # all ten languages; or add codes: ... RAW WORK es fr
python3 tools/lang/build_all.py --manifest    # only rewrite data/lang/manifest.txt (re-pins English too)
python3 tools/lang/report.py                  # the sparse-theme report below
make check                                    # includes tools/check/lang_test.c
```

A full rebuild from empty scratch folders reproduces the committed files exactly (checked by comparing
sha256 of all thirty files). Steps, per language:

1. **`tools/lang/hunspell.py`** expands the language's Hunspell dictionary (wooorm/dictionaries). It
   generalises `tools/unmunch.py`, which is unchanged so the English data stays identical: flag formats
   ASCII, UTF-8, `long` and `num`, continuation flags on affix rules (two levels), cross products,
   NEEDAFFIX, ONLYINCOMPOUND, FORBIDDENWORD, CIRCUMFIX. Compound rules are not expanded.
2. **`tools/lang/build_words.py`** turns the expansion into the validation list. A form is kept when it
   is ATTESTED (it occurs at least twice in the OpenSubtitles frequency list) or is a bare dictionary
   entry. That is what makes Italian (34 million clitic forms) and Portuguese fit and it removes subtitle
   typos, which are not Hunspell words. German, Dutch, Swedish and Danish also get frequent compounds
   that split into two accepted words (4 or more occurrences, with the usual linking letters), because
   Hunspell lists only the compound pieces. Words are normalised, 3..20 letters, at least one vowel, no
   letter three times in a row, no letter outside the language's alphabet, and a `q` must be followed by
   `u` (the Qu tile). The list is cut at **220,000 words**, ranked by frequency; five languages hit the
   cap (the cut-off form still occurs 9 to 12 times in Polish and Finnish, so only the rare tail goes).
3. **`tools/gen_dict.py`** (the English builder, unchanged) writes `dict.bin` in format WPD3. The tier bit
   is 0 for the top 30,000 forms of the language's frequency list (hermitdave FrequencyWords), 1 for
   the rest, so the LIBRARY option can work per language.
4. **`tools/lang/letters.py`** writes `letters.bin` (format WPL1): tile weights from the dictionary's
   letter counts weighted by `1 + log10(1 + frequency)`, a Qu counted as one tile, apportioned to
   exactly 100, at least 1 for every letter in use; values 1..10 on a log scale from the commonest
   letter (1) to the rarest (10); the Qu flag; the vowel mask (A E I O U, plus Y in Swedish, Polish,
   Danish and Finnish where it is a true vowel letter); the four rarest letters.
5. **`tools/lang/build_pools.py`** translates the English library theme by theme through WordNet
   (details below) and writes `pools.bin` in format WPP1 with `tools/lang/themes.py` names.

### Normalisation

Diacritics are stripped (a-acute to a, n-tilde to n, c-cedilla to c, umlauts to the plain vowel, a-ring to
a, e-grave to e ...), sharp s to `ss`, the ae and oe ligatures to `ae` and `oe`, o-slash to `o`, l-stroke to
`l`, d-stroke to `d`. Two words that become the same collapse into one entry. Anything left outside
a..z drops the word. The options help and the guide will say so when the game integrates the data.

### Pass-word pools

Wordnets per language (all through Open Multilingual Wordnet 1.4 and Princeton WordNet 3.0 synset ids):
Spanish MCR, French WOLF, Italian MultiWordNet, Portuguese OpenWN-PT, Dutch ODWN, Swedish SALDO, Polish
plWordNet, Danish DanNet, Finnish FinnWordNet; German is not in OMW 1.4 so it uses Open German WordNet
(OdeNet 1.4), whose synsets reach WordNet 3.0 through the CILI index. Danish and Swedish wordnets are
small (about 5,000 synsets each), so each also reads the other Scandinavian ones (Danish, Norwegian
Bokmal and Nynorsk, Swedish); a borrowed lemma still has to be in the language's own dictionary.

* Themes with WordNet categories or hierarchy roots in `tools/gen_pools.py` (BEASTS, HERBAL, GUILD,
  SMITHY, MOODS ...): every target lemma goes to the first theme its main sense matches, the rule the
  English derivation uses. The main sense is the synset with the most SemCor tags; instance synsets
  (people, places) never count, nor glosses that `BAD_GLOSS` rejects (slang, vulgar, drugs, killing ...).
* Hand-written English themes (BINDERY, INK, VERSE, TALES, ARCHIVE, LAMP, STUDY, MUSIC, MYTH, VOYAGE,
  PLAY, WEATHER, ATLAS, CURSES): each English word is looked up in WordNet (first two noun senses, first
  verb and adjective sense) and its synsets give the target lemmas. A theme still short at some length gets,
  for that length only, hyponyms (three levels), siblings and then any-sense matches.
* Hand-written target-language lists, committed in `tools/words/lang/<code>/`: `curses.txt` for every
  language (mild oaths, nonsense words, old-fashioned insults, the same ceiling as English, written for this
  project), and a few verb or myth themes where the wordnet has nothing: Polish MOTION, MAKING, CHANGE
  (plWordNet has 28 verbs), Italian MOTION, MAKING, CHANGE (few verbs), Swedish and Danish VERSE and MYTH,
  Polish CHANGE (short words).
* LEXICON is the most familiar words no theme claimed.
* A pass word must be in the language's dictionary, 4..9 letters a-z, seen at least 40 times in the
  frequency list (25 in Finnish; the fill passes accept 4), not blocked, and use only tile letters. Words
  of a theme are ordered most familiar first (frequency), at most 1,000 per length, every word in one
  theme only.
* Blocklist: LDNOOBW's single-word entries, plus `tools/words/lang/<code>/blocklist.txt` (stronger swear
  words, slurs, sexual and violent words; a word starting with one of 5 or more letters, or written with
  a trailing `*`, blocks every word that starts with it). The content ceiling is the English one: mild
  curses at most.
* The 40 theme names are in `tools/lang/themes.py`: A-Z only, 2 to 7 letters, distinct within a language.

### Sparse themes

English requires 8 words at every length 4..6 for every theme except LEXICON. The other languages cannot
fully: a 4-letter Spanish word is rare, and verbs are thin in some wordnets. The game already falls back to
the nearest length when a pool has none at the wanted length. `tools/lang/report.py` prints the picture;
`make check` demands at least 25 words per theme, 3 words of 5 letters and 5 of 6 letters in every theme but
LEXICON.

| language | themes short only at 4 letters | themes short at 5 or 6 letters (count at 4, 5, 6) |
| --- | ---: | --- |
| es | 16 | GUILD [7, 7, 16], ATTIRE [6, 5, 13] |
| fr | 8 | none |
| de | 7 | MOTION [0, 6, 30] |
| it | 5 | GUILD [7, 6, 18], MAKING [3, 6, 29], CHANGE [1, 5, 11] |
| pt | 7 | none |
| nl | 2 | GUILD [9, 5, 39], MOTION [1, 4, 22] |
| sv | 6 | ALCHEMY [24, 7, 17], GUILD [5, 5, 10], ATTIRE [3, 16, 7] |
| pl | 3 | MAKING [5, 10, 7] |
| da | 7 | VERSE [7, 11, 6], GUILD [5, 3, 8], TIMES [5, 9, 6], SHAPES [11, 8, 6], EVENTS [3, 7, 9] |
| fi | 10 | none |

The weakest overall are Swedish and Danish (about 9,300 to 9,900 pass words against 14,000 to 16,000 for
the others, because their wordnets are small), and their LEXICON theme carries more of the load. Themes
there repeat sooner. The pools are noisier than the English hand-written ones, in the same way as the
English WordNet-derived themes: a theme is a flavour, not a definition.

### Limits, stated plainly

* **Finnish has no open Hunspell dictionary on a reachable host** (wooorm/dictionaries ships an empty
  stub, the Voikko dictionary is not on the allowed hosts). Its dictionary is FinnWordNet lemmas plus
  OpenSubtitles forms that occur at least 8 times and pass a vowel-harmony and word-ending test
  (`finnish_ok` in `build_words.py`). It is the weakest list of the ten: it accepts some subtitle
  slang and loanwords and misses rare inflections.
* **Attestation** means a rare but valid inflected form that never appears in the subtitle corpus is not in
  the dictionary. The dictionaries are "every word a player is likely to type", not complete.
* **Compounds** (German, Dutch, Swedish, Danish) are accepted when frequent and made of two accepted
  words; a few frequent run-together phrases get in (for example a German "lass uns" glued together).
* **German nouns**: igerman98 lists capitalised stems; a capitalised stem is kept when one of its forms is
  a lemma in Open German WordNet, which drops names.
* **Licences**: most Hunspell dictionaries are copyleft (GPL, LGPL, MPL; Italian is GPL-3.0 only). The
  shipped files hold only word lists derived from them, not the dictionaries or affix rules; the
  licences are listed in `tools/words/SOURCES.md` so a distributor can judge. Dutch (BSD-3-Clause or
  CC BY 3.0) and French (MPL-2.0) are the most permissive.
* **Qu**: five languages (es, fr, de, it, pt) use the Qu tile and drop words with a `q` not followed by
  `u`; the other five have no Q tile at all.
* The theme names for hand-written themes are the closest word of at most seven letters, not always the
  exact translation (CLANS is "groups", SMITHY is "forge" or "smith", and so on).

## Formats and sizes, for the integration

All integers are little endian. Paths in the ROM filesystem (NitroFS, `nitro:/`): `lang/<code>/dict.bin`,
`lang/<code>/pools.bin`, `lang/<code>/letters.bin` for `es fr de it pt nl sv pl da fi`.

**dict.bin**, format WPD3, byte for byte the English format (`source/dict.c` reads it as is): magic `WPD3`;
18 u32 word counts for lengths 3..20; then the records grouped by length, each group sorted. Record width
by length: 3-6 letters 4 bytes, 7: 5, 8: 6, 9-10: 7, 11-12: 8, 13-14: 9, 15: 10, 16-17: 11, 18: 12, 19-20: 13.
Letters are 5 bits each, big endian, left aligned, a=0; bit 0 of the last byte is the tier (0 common,
1 rare). Sizes: 979,870 bytes (French) to 1,638,962 bytes (Dutch). A loader allocates the file size from
the heap (the largest is 1.6 MB; main RAM is 4 MB), reads it once, and points the table at it.

**pools.bin**, format WPP1, byte for byte the English format (`source/pools.c` reads it): magic `WPP1`, u32
theme count (always 40), then per theme `char name[8]` (NUL padded, 2 to 7 letters A-Z) and `u16
count[6]` for lengths 4..9, then the words of each theme and length back to back with no separators.
Sizes: 60,058 to 111,426 bytes. Theme order is the English shelf order (BINDERY first, LEXICON last).

**letters.bin**, format WPL1, 68 bytes:

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | magic `WPL1` |
| 4 | u8 | flags: bit 0 = Q is a Qu tile (es fr de it pt) |
| 5 | u8 | reserved, 0 |
| 6 | u8[4] | the four rarest letters (0..25, 255 = none): the rare-letter rotation of the bag |
| 10 | u8[2] | reserved, 0 |
| 12 | u32 | vowel mask, bit i = letter i (a = 0) is a vowel: the vowel floor reads it |
| 16 | u8[26] | tile weight per letter, sums to exactly 100 (the bag size); 0 = no tile |
| 42 | u8[26] | letter value per letter, 1..10 (0 for a letter with no tile) |

English tables for the same layout (what `board.c` has compiled in): weights `9 2 3 4 12 2 3 3 9 1 1 4 3
6 8 2 1 6 5 6 4 2 2 1 2 1` (sum 102, Q = Qu), values `1 3 3 2 1 4 2 4 1 8 5 1 3 1 1 3 10 1 1 1 1 4 4 8 4
10`, vowels A E I O U, rare J Q X Z.

**manifest.txt**: `#` comment lines, then one row per language `code, native name, English name, words,
themes, pool words, qu, dict bytes, pools bytes, letters bytes` (tab separated, the `en` row first with the
built-in sizes), a `#sha256 en <dict> <pools> <variants>` line that pins the English files, and a total.
The native names (ESPANOL, FRANCAIS, DEUTSCH, ITALIANO, PORTUGUES, NEDERLANDS, SVENSKA, POLSKI, DANSK,
SUOMI) are the labels for the options row; at most 10 letters, A-Z only.

What the integration still has to do (not done here): NitroFS in the Makefile (`NITRO := nitrofiles`) with
`data/lang` copied in; `nitroFSInit` at startup with English as the fallback; a LANGUAGE setting with
validation; loading into heap buffers with the previous language's freed; the runtime letter tables (weights,
values, vowel mask, Qu flag, rare rotation) behind `letterValue`, `LETTER_WEIGHT`, `isVowel`, the Qu rule, the
repeat ledger and the results card; and the debug mirror field with a poke command.
