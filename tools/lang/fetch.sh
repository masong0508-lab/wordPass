#!/bin/bash
# Download the open sources of the ten languages into a scratch folder (they are not committed).
#
#   tools/lang/fetch.sh RAW_DIR
#
# Needs curl, unzip and tar with xz.  About 150 MB.  Layout written (what build_all.py expects):
#   hun/CODE.{dic,aff,license}   Hunspell dictionaries (wooorm/dictionaries; Finnish has none there, see voikko-fi)
#   freq/CODE.txt, CODE_full.txt OpenSubtitles frequency lists, top 50k and full (hermitdave/FrequencyWords, 2018)
#   ldnoobw/CODE                 bad-word lists (LDNOOBW)
#   wn/wordnet                   Princeton WordNet 3.0 (nltk_data)
#   omw/omw-1.4                  Open Multilingual Wordnet 1.4 (nltk_data)
#   wikt/wn-wikt-CODE.tab        the Wiktionary-derived wordnets of the Open Multilingual Wordnet project (omwn/omw-data)
#   freedict/*.tei               FreeDict bilingual dictionaries between a language and English
#   odenet/odenet-1.4            Open German WordNet 1.4 (release archive)
#   ili-map-pwn30.tab            Collaborative Interlingual Index to WordNet 3.0 offsets (globalwordnet/cili)
#   voikko-fi/                   the sources of the Finnish Voikko morphology (corevoikko/voikko-fi, GPL-2+): the
#                                vocabulary, the generators and the lexc grammar; tools/lang/voikko_fi.py compiles them
#   ud-fi/*.conllu               the Universal Dependencies Finnish treebanks (TDT, FTB, OOD, PUD: written Finnish with
#                                gold lemmas), whose word forms rank ahead of rare subtitle forms in the Finnish dictionary
set -e
RAW=${1:?usage: fetch.sh RAW_DIR}
mkdir -p "$RAW"/hun "$RAW"/freq "$RAW"/ldnoobw "$RAW"/wn "$RAW"/omw "$RAW"/odenet "$RAW"/wikt "$RAW"/freedict
cd "$RAW"
GH=https://raw.githubusercontent.com
for l in es fr de it pt nl sv pl da; do
  for e in dic aff license; do curl -fsS -o hun/$l.$e $GH/wooorm/dictionaries/main/dictionaries/$l/$( [ $e = license ] && echo license || echo index.$e ); done
done
for l in es fr de it pt nl sv pl da fi; do
  curl -fsS -o freq/$l.txt      $GH/hermitdave/FrequencyWords/master/content/2018/$l/${l}_50k.txt
  curl -fsS -o freq/${l}_full.txt $GH/hermitdave/FrequencyWords/master/content/2018/$l/${l}_full.txt
  curl -fsS -o ldnoobw/$l       $GH/LDNOOBW/List-of-Dirty-Naughty-Obscene-and-Otherwise-Bad-Words/master/$l
done
curl -fsS -o wn.zip  $GH/nltk/nltk_data/gh-pages/packages/corpora/wordnet.zip
curl -fsS -o omw.zip $GH/nltk/nltk_data/gh-pages/packages/corpora/omw-1.4.zip
unzip -q -o wn.zip -d wn
unzip -q -o omw.zip -d omw
curl -fsSL -o odenet.tar.xz https://github.com/hdaSprachtechnologie/odenet/releases/download/v1.4/odenet-1.4.tar.xz
tar -xJf odenet.tar.xz -C odenet
curl -fsS -o ili-map-pwn30.tab $GH/globalwordnet/cili/master/ili-map-pwn30.tab
# Wiktionary-derived wordnets (CC BY-SA) and FreeDict dictionaries (GPL-2+)
for l in spa fra deu ita por nld swe pol dan fin nob nno; do
  curl -fsS -o wikt/wn-wikt-$l.tab $GH/omwn/omw-data/main/wns/wikt/wn-wikt-$l.tab
done
for p in spa-eng fra-eng eng-fra por-eng eng-por nld-eng eng-nld swe-eng dan-eng eng-dan; do
  curl -fsS -o freedict/$p.tei $GH/freedict/fd-dictionaries/master/$p/$p.tei
done
# Voikko, the Finnish morphology: sources of voikko-fi (tools/lang/voikko_fi.py builds the transducer from them)
V=$GH/voikko/corevoikko/master/voikko-fi
mkdir -p voikko-fi/common voikko-fi/vvfst voikko-fi/vocabulary/autocorrect
for f in Makefile COPYING CONTRIBUTORS common/voikkoutils.py common/hfconv.py common/generate_lex_common.py \
         vocabulary/flags.txt vocabulary/joukahainen.xml vocabulary/autocorrect/autocorrect.dtd vocabulary/autocorrect/fi.xml \
         vvfst/generate_lex.py vvfst/generate_taivutuskaavat.py vvfst/root.lexc vvfst/taivutuskaavat.lexc.in vvfst/index.txt.in \
         vvfst/filter_lexc.py vvfst/autocorrect_to_lexc.py vvfst/autocorrect.foma.in vvfst/main.foma.in \
         vvfst/suomi.lexc vvfst/olla-ei.lexc vvfst/poikkeavat.lexc vvfst/lukusanat.lexc vvfst/asemosanat.lexc \
         vvfst/seikkasanat.lexc vvfst/suhdesanat.lexc vvfst/generate_sukija.py vvfst/poikkeavat-sukija.lexc; do
  curl -fsS -o voikko-fi/$f $V/$f
done
# Universal Dependencies Finnish treebanks (CC BY-SA 4.0, FTB CC BY 4.0)
mkdir -p ud-fi
U=$GH/UniversalDependencies
for f in TDT/fi_tdt-ud-train TDT/fi_tdt-ud-dev TDT/fi_tdt-ud-test FTB/fi_ftb-ud-train FTB/fi_ftb-ud-dev FTB/fi_ftb-ud-test OOD/fi_ood-ud-test PUD/fi_pud-ud-test; do
  curl -fsS -o ud-fi/$(basename $f).conllu $U/UD_Finnish-${f%%/*}/master/$(basename $f).conllu
done
echo "done: $RAW"
