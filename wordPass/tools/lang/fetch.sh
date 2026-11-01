#!/bin/bash
# Download the open sources of the ten languages into a scratch folder (they are not committed).
#
#   tools/lang/fetch.sh RAW_DIR
#
# Needs curl, unzip and tar with xz.  About 130 MB.  Layout written (what build_all.py expects):
#   hun/CODE.{dic,aff,license}   Hunspell dictionaries (wooorm/dictionaries; the Finnish one does not exist there)
#   freq/CODE.txt, CODE_full.txt OpenSubtitles frequency lists, top 50k and full (hermitdave/FrequencyWords, 2018)
#   ldnoobw/CODE                 bad-word lists (LDNOOBW)
#   wn/wordnet                   Princeton WordNet 3.0 (nltk_data)
#   omw/omw-1.4                  Open Multilingual Wordnet 1.4 (nltk_data)
#   odenet/odenet-1.4            Open German WordNet 1.4 (release archive)
#   ili-map-pwn30.tab            Collaborative Interlingual Index to WordNet 3.0 offsets (globalwordnet/cili)
set -e
RAW=${1:?usage: fetch.sh RAW_DIR}
mkdir -p "$RAW"/hun "$RAW"/freq "$RAW"/ldnoobw "$RAW"/wn "$RAW"/omw "$RAW"/odenet
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
echo "done: $RAW"
