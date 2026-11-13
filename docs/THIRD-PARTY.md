# Third-party inputs, licences and notices

This file states facts from `tools/words/SOURCES.md`, `docs/LANGUAGES-DATA.md` and `docs/MUSIC.md`. It makes no
legal conclusion. Whether a compiled word list is a derivative work of the lists it was built from is a question
for the owner (or a lawyer); this page lists what went in, what ships, and which notices each licence asks for.

## What the repository is

| Part | Licence as stated by the project |
| --- | --- |
| `source/`, the scripts in `tools/`, `Makefile`, `build.sh`, text of `docs/` | MIT (`LICENSE`) |
| `data/` and `data/lang/` (derived word lists), `tools/common_words.txt`, `tools/words/` lists | **derived from third-party sources; each source's licence applies** (see below). Not MIT-only. |
| `wordPass.nds` | embeds `data/*.bin` (English) and carries `data/lang/*` in its filesystem, so the same applies to the ROM |
| `docs/music/*.wav`, `*.mid`, `source/songs.c` | renderings and tables derived from the author's own bore-gba tracker songs: **provenance and licence not stated** (see Decisions) |
| `icon.bmp`, the 5x7 font in `source/font.c`, other graphics | made for the project (no third-party input recorded) |

## Inputs and what ships

"Ships" means a derived file committed in this repository (and so in the ROM where the ROM embeds or contains it).
Raw downloads of the sources are not committed (`tools/lang/fetch.sh` fetches them).

### English

| Input | Licence | Ships as |
| --- | --- | --- |
| ENABLE word list | public domain | `tools/enable1.txt` (committed in full), `data/dict.bin` |
| Princeton WordNet 3.1 lemmas | WordNet licence (`LICENSES/WordNet-3.0.txt`; the notice must appear on all copies, including derived data) | `tools/words/more_open.txt` (about 10,800 words), `tools/words/pools/wn_*.txt`, hence `data/dict.bin`, `data/pools.bin` |
| dwyl/english-words | Unlicense | filter for `more_open.txt` |
| wooorm/dictionaries `en`, `en-GB` Hunspell (SCOWL and others) | permissive (SCOWL notice below) | `tools/words/us_only.txt`, `uk_only.txt`, `data/variants.bin` |
| FrequencyWords 2018 (hermitdave, from OpenSubtitles) | CC BY-SA 4.0 | `tools/common_words.txt` (top 30k filtered to ENABLE words), the common-tier bit of `data/dict.bin`, the ordering of `data/pools.bin` |
| google-10000-english (first20hours) | stated in SOURCES.md only as a source; the upstream licence file should be checked by the owner | a few common-tier additions in `tools/common_words.txt` |
| LDNOOBW | CC BY 4.0 | `tools/words/blocklist.txt` and `tools/words/lang/<code>/blocklist.txt` |
| google-profanity-words | licence not recorded in SOURCES.md | entries in `tools/words/blocklist.txt` |
| zacanger/profane-words | MIT | entries in `tools/words/blocklist.txt` |
| written for this project | MIT | `tools/words/extra.txt`, `pools/<theme>.txt`, `tools/words/lang/<code>/{curses,hand}.txt` |

### Ten languages (`data/lang/<code>/{dict,pools,letters}.bin`)

| Input | Licence | Used for |
| --- | --- | --- |
| Hunspell dictionaries via wooorm/dictionaries: es (RLA-ES), fr (Grammalecte), de (igerman98), it (Italian Writing Aids), pt (Raimundo Moura), nl (OpenTaal), sv (Goran Andersson), pl (Polish Native Lang Project), da (Stavekontrolden) | es GPL-3.0 or LGPL-3.0 or MPL-1.1; fr MPL-2.0; de GPL-2.0 or GPL-3.0; it GPL-3.0; pt LGPL-3.0 or MPL-2.0; nl BSD-3-Clause or CC BY 3.0; sv LGPL-3.0; pl GPL-3.0 or LGPL-3.0 or MPL-2.0; da GPL-2.0 or LGPL-2.1 or MPL-1.1 | expanded word lists; only derived lists ship |
| FrequencyWords 2018 | CC BY-SA 4.0 | attestation filter, common tier and ordering in every language's `dict.bin` and `pools.bin` |
| Voikko (corevoikko/voikko-fi: Joukahainen vocabulary, Kotus classes, generators, lexc grammar) | GPL-2.0 or later | the whole Finnish `dict.bin` (the forms the Voikko transducer accepts) |
| Universal Dependencies Finnish treebanks TDT, OOD, PUD / FTB | CC BY-SA 3.0 or 4.0 / CC BY 4.0 | ranking of Finnish forms |
| Wordnets through Open Multilingual Wordnet 1.4: MCR (es), MultiWordNet (it), SALDO (sv), FinnWordNet (fi) | CC BY 3.0 | pass-word pools |
| WOLF (fr) | CeCILL-C | pass-word pools |
| OpenWN-PT (pt) | CC BY-SA 3.0 | pass-word pools |
| Open Dutch WordNet (nl), Open German WordNet / OdeNet 1.4 (de) | CC BY-SA 4.0 | pass-word pools |
| CILI (globalwordnet/cili) | CC BY 4.0 | WordNet id mapping for German |
| plWordNet (pl), DanNet (da), Norwegian Wordnet (nb, nn; used for da and sv) | their own licences (permit commercial use / redistribution with the notice; the texts are not copied here) | pass-word pools |
| Wiktionary-derived wordnets `wn-wikt-CODE.tab` (omwn/omw-data) | CC BY-SA 3.0 | extra senses, mainly sv and da |
| FreeDict (swe-eng, dan-eng, eng-dan, spa-eng, fra-eng, eng-fra, por-eng, eng-por, nld-eng, eng-nld) | GPL-2.0 or later (sv, da generated from Ergane) | English to language translation for the sv, da, es, fr, pt, nl pools |
| Princeton WordNet 3.0 (nltk_data) | WordNet licence | English side of the translation |
| LDNOOBW per-language lists | CC BY 4.0 | `tools/words/lang/<code>/blocklist.txt` |

Where the lists were changed: every shipped file is a filtered, merged, normalised (A-Z, accents stripped) and
packed derivative, never the original file. CC BY and CC BY-SA ask for that to be stated; it is stated here and in
`data/NOTICE` and `data/lang/NOTICE`.

## Notices and attributions

* **Princeton WordNet**: "WordNet 3.0 Copyright 2006 by Princeton University. All rights reserved." The full licence is in
  `LICENSES/WordNet-3.0.txt`; it has to appear with all copies of the derived data and the name of Princeton must not
  be used in publicity.
* **FrequencyWords** (https://github.com/hermitdave/FrequencyWords), hermitdave, from the OpenSubtitles corpus, CC BY-SA 4.0
  (https://creativecommons.org/licenses/by-sa/4.0/). Changed: filtered and truncated (top 30k and 50k), reordered.
* **LDNOOBW** (List of Dirty, Naughty, Obscene and Otherwise Bad Words), CC BY 4.0
  (https://creativecommons.org/licenses/by/4.0/). Changed: merged with other lists, normalised to A-Z, extended.
* **zacanger/profane-words**, MIT: the upstream copyright line and permission notice apply to the entries taken;
  fetch the upstream LICENSE file when the blocklist is next regenerated and copy it here.
* **SCOWL and the wooorm/dictionaries en and en-GB packages**: the SCOWL copyright and permission notice of the
  version used apply to `us_only.txt`, `uk_only.txt` and `data/variants.bin`; upstream text to be copied here from
  the package (not available offline when this file was written).
* **Voikko and FreeDict (GPL-2.0 or later)**: the licence text is in `LICENSES/GPL-2.0.txt`. The GPL-2.0-or-later
  notice for the derived lists: "Derived from voikko-fi (corevoikko, GPL-2.0 or later) and the FreeDict dictionaries
  (GPL-2.0 or later); this derived list is provided WITHOUT ANY WARRANTY under the terms of the GNU General Public
  License version 2 or, at your option, any later version." Sources: https://github.com/voikko/corevoikko and
  https://github.com/freedict/fd-dictionaries. The scripts that built the lists are in `tools/lang/`.
* **CC BY-SA wordnets and OdeNet / Open Dutch WordNet / OpenWN-PT / Wiktionary-derived wordnets**: credited in
  `data/lang/NOTICE`; links and versions are in `tools/words/SOURCES.md`.
* **Hunspell dictionaries** (GPL, LGPL, MPL, BSD): the authors are credited in `data/lang/NOTICE`; the dictionaries
  themselves are not shipped, only word lists derived from them.

## Decisions only the owner can make

1. **Licence of `data/`.** The repository is MIT as a whole, but the data files derive from GPL-2.0-or-later (Finnish
   dictionary; Swedish, Danish, Spanish, French, Portuguese, Dutch pools via FreeDict), GPL/LGPL/MPL Hunspell
   dictionaries, and CC BY-SA 3.0/4.0 sources (FrequencyWords, UD Finnish, OpenWN-PT, Open Dutch WordNet, OdeNet,
   Wiktionary wordnets). Options: (a) keep as is and treat word lists as facts (legal judgement needed, and
   `docs/LANGUAGES-DATA.md` already says the Finnish dictionary and Swedish and Danish pools are "derived from
   copyleft data, which a distributor should weigh"); (b) relicense `data/` and the ROM's data under one share-alike or copyleft licence that all inputs allow (CC BY-SA 4.0 or GPL-2.0-or-later; whether either can absorb every input, for example GPL-2.0-only and CC BY-SA 3.0 material, needs checking), keeping source and tools MIT; (c) drop or replace the copyleft inputs
   (Voikko, FreeDict, FrequencyWords, wiki-derived wordnets) and rebuild. This file only records the facts; the
   notices above are the minimum each licence asks for and do not settle this question.
2. **Distributing `wordPass.nds`.** CI artifacts and releases embed the English data and carry the other ten
   languages; whatever is decided in 1 applies to the ROM too.
3. **Provenance of the music.** The ten songs are distilled from the author's other game, bore-gba, whose own
   tracker modules (`.xm`) are listed in `docs/MUSIC.md`. Their licence and the origin of those modules (own work,
   samples or modules by other composers, a tracker-music archive) are not stated anywhere in this repository.
   `tools/music/analysis/*.json`, `source/songs.c` and `docs/music/*` are derived from them. The owner must confirm
   that the source songs may be reused and under which licence.
4. **Missing upstream texts**: the SCOWL notice, the zacanger/profane-words MIT notice, the google-profanity-words
   and google-10000-english licences and the licences of plWordNet, DanNet and the Norwegian wordnet are named but
   not copied here, because they were not available offline when this file was written.
5. **Generated audio.** `docs/music/` tracks about 13 MB of generated WAV and MIDI; see `docs/MUSIC.md` and the
   Makefile `music-render` target (a drift check compares the committed files with a fresh render).
