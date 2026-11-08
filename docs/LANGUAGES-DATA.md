# Language data: ten languages, dictionaries, pass words and tile tables

This is the data half of "play the whole game in your own language". It is finished and tested:
`data/lang/<code>/{dict.bin, pools.bin, letters.bin}` for ten languages, a manifest, the scripts that
build them from open sources, and `tools/check/lang_test.c`. The game reads it (the NitroFS loader, the
LANGUAGE option and the runtime letter tables are done, see the last section). The first edition of the data
shipped with known gaps (thin pass-pool cells, small Swedish and Danish pools, a heuristic Finnish dictionary);
a second pass closed them, and "What the second pass changed" below says how and what is still limited.
English is untouched: `data/dict.bin`,
`data/pools.bin` and `data/variants.bin` are byte for byte what they were (the manifest pins their sha256
and `make check` verifies it).

## The ten languages

All ten of the preferred set were buildable; there are no substitutions. Two of them needed a
different route than the others (German's wordnet, Finnish's dictionary, see Limits).

| code | name in the menu | language | dictionary words | common tier | pass words | dict.bin | pools.bin | Qu tile | letters with no tile |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | :---: | --- |
| es | ESPANOL | Spanish | 188,926 | 23,250 | 13,731 | 1,257,867 | 93,369 | yes | K W |
| fr | FRANCAIS | French | 146,690 | 21,352 | 16,023 | 979,870 | 109,697 | yes | W |
| de | DEUTSCH | German | 167,158 | 21,859 | 14,293 | 1,278,177 | 99,715 | yes | none |
| it | ITALIANO | Italian | 213,299 | 23,320 | 15,839 | 1,494,184 | 111,132 | yes | J K W X Y |
| pt | PORTUGUES | Portuguese | 220,000 | 21,307 | 16,190 | 1,479,568 | 110,320 | yes | K W Y |
| nl | NEDERLANDS | Dutch | 220,000 | 22,787 | 16,233 | 1,638,962 | 111,119 | no | Q |
| sv | SVENSKA | Swedish | 220,000 | 23,072 | 14,016 | 1,571,118 | 91,687 | no | Q W Z |
| pl | POLSKI | Polish | 220,000 | 25,443 | 14,147 | 1,444,133 | 94,047 | no | Q V X |
| da | DANSK | Danish | 202,198 | 22,688 | 14,297 | 1,464,862 | 94,192 | no | Q |
| fi | SUOMI | Finnish | 220,000 | 25,819 | 15,489 | 1,610,723 | 107,594 | no | C Q W X Z |

`data/lang` is 15,243,016 bytes in all (`data/lang/manifest.txt` has the exact numbers, one tab separated
row per language, read by the test). A letter with "no tile" is one the language does not use: it has
under 0.04 percent of the tiles, so words containing it are left out of the dictionary (a word you can
never spell is dead weight) and the bag never deals it.

## The pipeline

Everything is reproducible. Large downloads are not committed; small hand-written inputs are
(`tools/words/lang/<code>/`).

```
tools/lang/fetch.sh RAW                       # download the sources (about 420 MB with the archives) into a scratch folder (Finnish also needs foma-bin: apt-get install foma-bin)
python3 tools/lang/build_all.py RAW WORK      # all ten languages; or add codes: ... RAW WORK es fr
python3 tools/lang/build_all.py --manifest    # only rewrite data/lang/manifest.txt (re-pins English too)
python3 tools/lang/report.py                  # the sparse-theme report below
make check                                    # includes tools/check/lang_test.c
```

A full rebuild from empty scratch folders reproduces the committed files exactly (checked by comparing
sha256 of all thirty files; repeated after the second pass with a fresh fetch.sh download). Steps, per language:

1. **`tools/lang/hunspell.py`** expands the language's Hunspell dictionary (wooorm/dictionaries). It
   generalises `tools/unmunch.py`, which is unchanged so the English data stays identical: flag formats
   ASCII, UTF-8, `long` and `num`, continuation flags on affix rules (two levels), cross products,
   NEEDAFFIX, ONLYINCOMPOUND, FORBIDDENWORD, CIRCUMFIX. Compound rules are not expanded.
   **Finnish** has no Hunspell file on a reachable host, so `tools/lang/voikko_fi.py` replaces this step: it runs the
   generators of voikko-fi, compiles its lexc grammar with foma (package foma-bin) and `flookup` answers, for every
   lower-case subtitle form, whether the Voikko morphology accepts it (see "Finnish, every attempt" below).
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
   Danish and Finnish where it is a true vowel letter); the four rarest letters. A hand-tuned weight
   table (`TUNED_WEIGHTS`) overrides the apportioned weights for a language whose dictionary starves the
   boards of short words: today only German (E 24, S 14, T 14, N 9, R 9, I 6; its dictionary holds a third
   as many 3 to 5 letter words as English, see docs/BALANCE.md "German letter table"). `letters.py
   --retune CODE FILE` applies the table to an existing letters.bin without the downloads.
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
small (about 6,000 lemmas each), so each also reads the other Scandinavian ones (Danish, Norwegian
Bokmal and Nynorsk, Swedish); a borrowed lemma still has to be in the language's own dictionary.
Two more sources widen the senses (see "What the second pass changed"): the Wiktionary-derived wordnets
`wn-wikt-CODE.tab` of the same project, and the FreeDict dictionaries between the language and English
(each English word's WordNet senses give its translations a synset). They are the main source for Swedish and
Danish, and the "any sense" source of the fill passes for the others.

* Themes with WordNet categories or hierarchy roots in `tools/gen_pools.py` (BEASTS, HERBAL, GUILD,
  SMITHY, MOODS ...): every target lemma goes to the first theme its main sense matches, the rule the
  English derivation uses. The main sense is the synset with the most SemCor tags; instance synsets
  (people, places) never count, nor glosses that `BAD_GLOSS` rejects (slang, vulgar, drugs, killing ...).
* Hand-written English themes (BINDERY, INK, VERSE, TALES, ARCHIVE, LAMP, STUDY, MUSIC, MYTH, VOYAGE,
  PLAY, WEATHER, ATLAS, CURSES): each English word is looked up in WordNet (first two noun senses, first
  verb and adjective sense) and its synsets give the target lemmas.
* Hand-written target-language lists, committed in `tools/words/lang/<code>/`: `curses.txt` for every
  language (mild oaths, nonsense words, old-fashioned insults, the same ceiling as English, written for this
  project), a few verb or myth themes where the wordnet has nothing (Polish and Italian MOTION, MAKING,
  CHANGE; Swedish and Danish VERSE and MYTH; they come first in their theme), and `hand.txt` in every
  language, one line per theme ("THEME word word ..."), which tops up thin cells (see below).
* LEXICON is the most familiar words no theme claimed.
* A pass word must be in the language's dictionary, 4..9 letters a-z, seen often enough in the frequency
  list (40 times in the big subtitle corpora; the threshold follows the corpus size, 40 occurrences per 250
  million tokens: 18 for Swedish, 12 for Danish, 25 for Finnish), not blocked, and use only tile letters.
  Words of a theme are ordered most familiar first (frequency), at most 1,000 per length, every word in one
  theme only.
* Blocklist: LDNOOBW's single-word entries, plus `tools/words/lang/<code>/blocklist.txt` (stronger swear
  words, slurs, sexual and violent words; a word starting with one of 5 or more letters, or written with
  a trailing `*`, blocks every word that starts with it). The content ceiling is the English one: mild
  curses at most. The second pass added the stems the wider sources brought in (sexual, nazi, vulgar verbs and
  insults in every language) after scanning the pools for them.
* The 40 theme names are in `tools/lang/themes.py`: A-Z only, 2 to 7 letters, distinct within a language.

### The pool standard, and what the second pass changed

English has 8 words at every length 4..6 in every theme but LEXICON. The first edition of the other languages
could not match it: a 4-letter Spanish word is rare and verbs are thin in some wordnets, so it asked only for 25
words a theme and a few at 5 and 6 letters, and it let Swedish and Danish stay small. The standard is now the
same for all ten languages (`tools/lang/report.py` prints it, `tools/check/lang_test.c` asserts it, no
language is exempt): **every theme but LEXICON has at least 25 words, at least 8 at each of 4, 5 and 6 letters
and at least 5 at 7 letters; LEXICON has at least 100 words; every language has at least 13,000 pass words.**

| language | pass words before | after | themes below the standard before | after | thin cells before | after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| es | 13,690 | 13,731 | 18 | 0 | 20 | 0 |
| fr | 16,058 | 16,023 | 8 | 0 | 8 | 0 |
| de | 14,330 | 14,293 | 8 | 0 | 9 | 0 |
| it | 15,846 | 15,839 | 8 | 0 | 11 | 0 |
| pt | 16,211 | 16,190 | 7 | 0 | 7 | 0 |
| nl | 16,290 | 16,233 | 4 | 0 | 5 | 0 |
| sv | 9,926 | 14,016 | 12 | 0 | 15 | 0 |
| pl | 14,112 | 14,147 | 5 | 0 | 6 | 0 |
| da | 9,263 | 14,297 | 15 | 0 | 21 | 0 |
| fi | 16,261 | 15,489 | 10 | 0 | 10 | 0 |

("Thin cell" is a theme and a length below its quota; before the second pass 95 themes had one, 346 words
were missing in all. Finnish has fewer pass words because its dictionary no longer holds names and slang.)

The methods, in the order `tools/lang/build_pools.py` tries them, each for the short length only and only as far
as the quota:

1. the wordnet derivation itself, with more senses: the Wiktionary-derived wordnets and, for Swedish and Danish,
   the FreeDict dictionaries are merged into it, words are screened against the gloss of every sense (not only
   the main one), and the familiarity threshold follows the corpus size (Swedish 18 occurrences, Danish 12);
2. hyponyms (three levels) and siblings of the theme's synsets, and the synsets of the words it already holds;
3. any sense of the English words of the theme's hand-written list, and any sense with the theme's lexical category;
4. the English library's own words of the theme (`data/pools.bin`) projected through every sense of every wordnet
   ("cross-language projection through English synsets with any sense");
5. one hop along every other WordNet relation (derivation, similar, also see, verb group, entailment, cause, parts
   and wholes, attribute, pertainym, topic domain) and the hypernyms;
6. the commonest words whose main sense has one of the theme's two commonest lexical categories (frequency-ranked
   words of the right category);
7. words written in the language: `tools/words/lang/<code>/hand.txt`, 58 to 271 words a language (1,462 written for
   this project), validated against that language's dictionary, seen at least 8 times in the
   subtitle corpus, not blocked and under the mild-content ceiling; a word that another theme already holds moves
   only when that theme keeps its quota at that length.

The ladder (1 to 6) closed no cell on its own that was fit to ship: what it filled in the thin cells was mostly
noise (INK got *tres*, *seis* and *alfa*), so the committed `hand.txt` lists cover every cell the first
derivation left short and the shipped data uses none of the ladder's fills (it is still there as the safety net;
`WP_NOFILL=1` shows what the derivation alone leaves short). What did the work: for Swedish the Wiktionary
wordnets, the FreeDict dictionary and the scaled threshold (9,926 to 14,016); for Danish the same plus the regular
inflected forms (plural, definite, tenses and neuter forms, at least 500 subtitle occurrences each) of the words a
theme holds (9,263 to 14,297, 1,478 of them inflected forms).

Quality, honestly: the derived themes are "a flavour, not a definition", as in English (the Swedish MIND shelf now holds
*vide* and *fanatism*, the Danish ATLAS shelf *lyng* and *gejser*); the Wiktionary and FreeDict senses
are looser than a wordnet's, so the Swedish and Danish themes are noisier than the others; the hand-written
words are the work of one author and have not been read by native speakers.

### Limits, stated plainly

* **Finnish** has no Hunspell dictionary on a reachable host (wooorm/dictionaries ships a 14 byte "404" stub for
  it). Its dictionary is built from the Voikko morphology instead (see "Finnish, every attempt" below). It is
  standard Finnish only: slang spellings (*ma*, *sa*, *mut*, *oon*) and names are not in it. It holds the
  220,000 forms (the cap, set by the DS's memory) that rank highest by subtitle frequency, with the forms of
  the Universal Dependencies treebanks counted 20 times: that covers 87.6 percent of the treebanks' tokens
  (78.4 percent for the first, heuristic list; the treebanks are part of the ranking, so 87.6 is an upper
  bound), while Voikko itself accepts 97.7 percent. So the remaining limit is the cap: a very inflected language
  has far more forms than 220,000, and a rare but correct form (an uncommon case ending of an uncommon noun) can
  be missing. Voikko accepts any two glued words as a compound, so a glued form needs 4 subtitle or treebank
  occurrences.
* **Attestation** means a rare but valid inflected form that never appears in the subtitle corpus is not in
  the dictionary. The dictionaries are "every word a player is likely to type", not complete.
* **Compounds** (German, Dutch, Swedish, Danish) are accepted when frequent and made of two accepted
  words; a few frequent run-together phrases get in (for example a German "lass uns" glued together).
* **German nouns**: igerman98 lists capitalised stems; a capitalised stem is kept when one of its forms is
  a lemma in Open German WordNet, which drops names.
* **Licences**: most Hunspell dictionaries are copyleft (GPL, LGPL, MPL; Italian is GPL-3.0 only). The
  shipped files hold only word lists derived from them, not the dictionaries or affix rules; the
  licences are listed in `tools/words/SOURCES.md` so a distributor can judge. Dutch (BSD-3-Clause or
  CC BY 3.0) and French (MPL-2.0) are the most permissive. The second pass added the Voikko vocabulary and
  grammar (GPL-2.0 or later, Finnish), the FreeDict dictionaries (GPL-2.0 or later), the Wiktionary-derived
  wordnets (CC BY-SA) and the Universal Dependencies Finnish treebanks (CC BY-SA, CC BY): again only derived
  word lists ship, and the GPL-2.0+ sources mean the Finnish dictionary and the Swedish and Danish pass pools
  are derived from copyleft data, which a distributor should weigh.
* **Qu**: five languages (es, fr, de, it, pt) use the Qu tile and drop words with a `q` not followed by
  `u`; the other five have no Q tile at all.
* The theme names for hand-written themes are the closest word of at most seven letters, not always the
  exact translation (CLANS is "groups", SMITHY is "forge" or "smith", and so on).

### Finnish, every attempt

What was tried to get an open Finnish word list or Hunspell and Voikko data, and what answered (the build runs
behind a proxy that allows a short list of hosts):

| attempt | result |
| --- | --- |
| wooorm/dictionaries `fi` (raw.githubusercontent.com) | a 14 byte "404: Not Found" stub: no Finnish there |
| LibreOffice/dictionaries `fi_FI`, titoBouzout/Dictionaries `Finnish.dic` and `.aff` | 404 |
| puimula.org (the Voikko dictionary distribution), www.puimula.org snapshots | blocked (no answer) |
| Debian mirrors (deb.debian.org, ftp.debian.org, ftp.fi.debian.org, mirrors.kernel.org, funet.fi, snapshot.debian.org) for the voikko-fi package | blocked |
| Ubuntu archive (archive.ubuntu.com, reachable): the Packages indexes of noble and jammy, main, universe, multiverse | no voikko-fi, hunspell-fi or other Finnish dictionary package; the foma and hfst tools are there |
| Kotus word list (kaino.kotus.fi, kotus.fi), fi.wiktionary.org, dumps.wikimedia.org, kaikki.org, Wikidata, opus.nlpl.eu, Hugging Face, Zenodo, the fastText CDN | blocked |
| github.com pages, api.github.com, the zip and tar archives of repositories | 403 (only `raw.githubusercontent.com` files are served) |
| PyPI, npm, crates.io, conda | `voikko` and `libvoikko` bindings only, no Finnish data; no Finnish dictionary packages |
| UniMorph `fin` (unimorph/fin) | 404 (Swedish, Danish, Polish, German and Italian tables exist there, but those languages are covered) |
| **corevoikko/voikko-fi on raw.githubusercontent.com**: `vocabulary/joukahainen.xml` (about 39,000 lexemes with Kotus inflection classes), `vvfst/generate_lex.py`, `generate_taivutuskaavat.py`, the lexc grammar (`root.lexc`, `suomi.lexc`, `taivutuskaavat.lexc.in`, ...), `main.foma.in`, the Makefile | **reachable, GPL-2.0 or later: used** |
| **Universal Dependencies Finnish treebanks** (TDT, FTB, OOD, PUD) on raw.githubusercontent.com | **reachable, CC BY-SA 3.0 or 4.0 (FTB CC BY 4.0): used to rank the cap** |
| **Ubuntu `foma-bin`** (the finite-state compiler and `flookup`) from archive.ubuntu.com | **reachable: used to build and run the transducer** |

So the data was rebuilt, not guessed: `tools/lang/voikko_fi.py` copies `RAW/voikko-fi`, runs the voikko-fi
generators (`make vvfst/all.lexc`), compiles the grammar with `foma -f` (the same `main.foma` as Voikko's own
Makefile, plus `save stack`; about 8 seconds) and checks all subtitle forms with `flookup`. That is a proper
inflected-forms check, not an expansion: it accepts every inflected form of every lexeme (cases, plural,
possessive suffixes, clitics, verb tenses and moods, participles) and compounds, which a Hunspell expansion of the
same size would not produce (it would be billions of forms); the forms that matter are the ones in use, which
the subtitle and treebank frequencies select. The lexemes themselves are bare entries.

## Formats and sizes, for the integration

All integers are little endian. Paths in the ROM filesystem (NitroFS, `nitro:/`): `lang/<code>/dict.bin`,
`lang/<code>/pools.bin`, `lang/<code>/letters.bin` for `es fr de it pt nl sv pl da fi`.

**dict.bin**, format WPD3, byte for byte the English format (`source/dict.c` reads it as is): magic `WPD3`;
18 u32 word counts for lengths 3..20; then the records grouped by length, each group sorted. Record width
by length: 3-6 letters 4 bytes, 7: 5, 8: 6, 9-10: 7, 11-12: 8, 13-14: 9, 15: 10, 16-17: 11, 18: 12, 19-20: 13.
Letters are 5 bits each, big endian, left aligned, a=0; bit 0 of the last byte is the tier (0 common,
1 rare). Sizes: 979,870 bytes (French) to 1,638,962 bytes (Dutch); Finnish 1,610,723. A loader allocates the file size from
the heap (the largest is 1.6 MB; main RAM is 4 MB), reads it once, and points the table at it.

**pools.bin**, format WPP1, byte for byte the English format (`source/pools.c` reads it): magic `WPP1`, u32
theme count (always 40), then per theme `char name[8]` (NUL padded, 2 to 7 letters A-Z) and `u16
count[6]` for lengths 4..9, then the words of each theme and length back to back with no separators.
Sizes: 91,687 to 111,119 bytes. Theme order is the English shelf order (BINDERY first, LEXICON last).

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

The integration is done (item 21, runtime): the Makefile copies `data/lang` into `nitrofiles/lang` for the ROM
filesystem and links `-lfilesystem`; `source/langload.c` reads a language into heap buffers (the previous one freed
first) behind a LOADING screen, with a fast raw card path where NitroFS in card mode is slow (see TESTING.md);
`source/lang.c` holds the active language's tile weights, letter values, vowel mask, Qu flag and rare letters behind
`letterValue`, the bag, the vowel floor and the Qu rule; `Settings.language` is saved with validation; the LANGUAGE
options row and the title tag switch it; the debug mirror carries `language`, `loadMs` and `langMask` and a poke
loads a language for the tests. The menus and the in-game guide are translated too (`tools/lang/ui_strings.tsv`); the guide's examples are words of each language's own dictionary (the EX_* rows) with numbers computed from its letter table.
