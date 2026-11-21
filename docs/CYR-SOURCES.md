# Source-licensed Russian/Ukrainian dictionary intake

wordPass now has an **offline, opt-in** tool for building larger WPD4
dictionaries from a licensed Hunspell .dic / .aff pair:

  python3 tools/lang/import_cyr_hunspell.py --code ru --dic ru_RU.dic --aff ru_RU.aff --notice README_ru_RU.txt --origin URL --out build/ru-intake

A corresponding --code uk invocation uses a Ukrainian .dic, .aff and
copyright/license notice. The tool DOES NOT download unknown data,
silently cut dictionaries in half or advertise a new playable language.
It validates Cyrillic Unicode, uses the project's Hunspell affix expansion
(including forbidden/need-affix handling), rejects theme passwords missing
from the source word list, and builds WPD4, WPL2 and 40 ordered WPP2
**starter** themes from the catalogs. By default a dictionary is capped at
1.5 MiB to avoid overrunning Nintendo DS RAM.

If a source dictionary is too large, provide a suitably licensed frequency
rank file with --rank and --max-words to prioritize everyday words. The
script refuses an over-budget dictionary rather than silently applying a
lexicographic cutoff. Full lexical filtering, human translation review,
offensive-word screening and accurate common/rare frequency tiers remain
an editorial responsibility; the output is marked EXPERIMENTAL.

## Candidate upstream sources

**Russian:** LibreOffice Russian spelling dictionary, upstream file paths
ru_RU/ru_RU.dic and ru_RU/ru_RU.aff at:
https://github.com/LibreOffice/dictionaries/tree/master/ru_RU

The upstream README credits Alexander I. Lebedev and describes source/binary
redistribution with copyright/disclaimer preservation and identification of
modifications. Read and retain the exact README and license terms.

**Ukrainian:** LibreOffice Ukrainian spelling dictionary based on dict_uk,
upstream file paths uk_UA/uk_UA.dic and uk_UA/uk_UA.aff at:
https://github.com/LibreOffice/dictionaries/tree/master/uk_UA

The README credits Andriy Rysin / dict_uk and states MPL 1.1. Redistribution
may carry source-availability and notice obligations; inspect the full
applicable terms and any other authorship notices before distributing a
derivative ROM. Do not assume the tool establishes legal compliance.

For reproducibility prefer upstream **commit-pinned** links rather than
master. The tool writes SOURCE-MANIFEST.json recording SHA-256 digests
of the inputs and license notice, and copies LICENSE-SOURCE.txt alongside
its output. Those files must accompany a distributed source-derived pack
along with any other required license materials.

## Native tests and safety

Run python3 tools/lang/import_cyr_hunspell.py --selftest build/cyr-source-intake
or make check. The synthetic selftest builds both language packs from
locally generated fixtures (no external downloads), validates all 40 seed
themes, Unicode word membership, provenance hashes and copied notices.

Real source-derived imports do not alter the current playable 11-language
ROM, do not automatically enter the Cyrillic engineering preview build and
do not replace native-speaker approval, 60 FPS profiling or DS RAM tests.
