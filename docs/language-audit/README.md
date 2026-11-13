# Language audit

Every pass-word list (English and the ten other languages) and every interface translation was read
through word by word, then read again by an independent second reader who decided each point
finally. This folder records what the audit removed from the pass pools.

## Method

1. **First read.** Each language's 40 themes were read at every length, with the part of speech and a
   note per doubtful word, and the translation of every interface string was read against the English
   one (slots, meaning, idiom, consistent terms, the pixel budget).
2. **Second read.** A second reader, working on the first reader's findings and the raw lists, accepted,
   changed or rejected each finding and returned final decisions in five groups: words flagged for the
   content ceiling (each with a category), junk (names, fragments, foreign words, misspellings), words
   that are clearly off-theme, theme names that do not carry the English theme's meaning, and
   interface-string corrections (rejected proposals were not applied).
3. **Applied.** The owner's rule below was applied to those decisions; the pools were rebuilt with the
   normal pipeline (`tools/gen_pools.py` for English, `tools/lang/build_all.py` with the downloaded
   sources for the other ten; the dictionaries were not rebuilt and are byte-identical).

## The owner's rule on content (strict)

Every word that was flagged for the content ceiling **stays**, except outright slurs (ethnic, national,
religious, sexual-orientation and gender slurs, for example taffy, queer, redneck, zigeuner, moro,
gitano, tzigane, esquimau, cigano, moffen). The categories profanity, sexual, anatomy, bodily, insult,
drugs-alcohol, violence-crime, hate, ableist, reads-as and other were all kept.

Removed from the pools: the slurs; every junk word (not a word a native speaker accepts as a pass
word); every clearly off-theme word (removed from its theme and not moved). Removed words are still valid
dictionary words: you can play them, they are just never a pass.

## Files

* `removed-<code>.txt` (en es fr de it pt nl sv pl da fi): one line per word that left a pool, tab
  separated: word, theme as it was (the old theme name), reason (`slur`, `junk`, `off-theme`, or
  `rebuild-shift` for a word that dropped only because a refill shifted a cap). The header lists the
  words that entered the pools in the same rebuild.
* The source of truth for the removals is the blocklist of each language
  (`tools/words/blocklist.txt` for English, `tools/words/lang/<code>/blocklist.txt`), in three commented
  groups; `=word` blocks that exact word only.

The counts per language are in `docs/LANGUAGES-DATA.md` (section "The content audit").
