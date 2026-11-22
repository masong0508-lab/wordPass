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

## Ukrainian apostrophe coverage — do not corrupt spelling

Ukrainian orthography requires an apostrophe in ordinary words, such as
`п’ять`, `м’ясо`, and `зв’язок` (official orthography, §7:
https://mon.gov.ua/static-objects/mon/sites/1/zagalna%20serednya/Pravopys.2019/ukrayinskii-pravopis-oficiine-vidannia-2026.pdf).
The source spell-checking data can use U+0027 (`'`), U+2019 (`’`),
or U+02BC (`ʼ`) for this internal separator.

The Ukrainian-only Hunspell intake now **recognizes and audits** these source
spellings. The `SOURCE-MANIFEST.json` reports
`ukrainian_apostrophe_forms_unrepresented`, gives up to twelve canonical
U+02BC examples, and records the exclusion policy. This is a deliberately
truthful count of the forms missing from the experimental 33-tile WPD4 game:
it does **not** delete apostrophes to make fake playable words, substitute
Russian hard signs, or pretend a 34th independent apostrophe letter/tile
already works. A future proper Ukrainian word engine must support the
separator without making the apostrophe a meaningless standalone gem.

The source audit runs only offline, never during play. It leaves the retail
11-language ROM and even the current miniature Ukrainian preview unchanged.
Its synthetic fixture checks straight, typographic and modifier-letter
apostrophe variants and confirms they are excluded from playable data but
visible in the attribution manifest.

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

## Expand Russian or Ukrainian themed password pools safely

The normal demo still has **four original passwords per theme**. An optional
licensed-source import can now accept extra **unreviewed** candidate passwords
for specific themes rather than remaining capped at four forever. This is
thematic editorial input, not automatic synonym generation or language
certification. Create a **separate JSON file per language**, identifying
categories by the original English theme key (never by translating the key):

```json
{
  "language": "uk",
  "review_status": "unreviewed-candidates",
  "themes": {
    "BINDERY": ["REPLACE_WITH_REAL_REVIEWED_UKRAINIAN_WORD"],
    "MUSIC": ["REPLACE_WITH_ANOTHER_REVIEWED_UKRAINIAN_WORD"]
  }
}
```

The uppercase English placeholders are illustrative, **not playable words**.
Replace them with actual NFC-uppercase Russian or Ukrainian spelling (4–9
alphabet letters) that is **already present in your licensed Hunspell
dictionary**, and choose the appropriate target language explicitly. An
apostrophe-dependent Ukrainian spelling is still not supported by the
33-tile game, and is **not** silently stripped or substituted.

Supply `--extra-themes /path/to/ru-extras.json` or
`--extra-themes /path/to/uk-extras.json` to the corresponding importer.
Only categories from the canonical 40-theme index are accepted; an expansion
may cover just one category at a time. The parser rejects unknown categories,
duplicate/reused starter passwords, wrong alphabets, mixed languages, words
missing from the sourced dictionary, malformed case/Unicode, and more than
64 extras in one theme. Every original starter remains mandatory even when
`--max-words` is used; a dictionary budget below the count of required
passwords is rejected rather than deleting themed content.

The output still has the regular `dict.bin`, `tiles.bin`, `pools.bin`,
`LICENSE-SOURCE.txt` and `SOURCE-MANIFEST.json`. For an expanded pack, it
also contains `THEME-EXTRAS.json` with the exact supplied bytes.
`SOURCE-MANIFEST.json` separately records the original 160 starters, the
additional count, the total candidate count, the expansion file SHA-256 and
its **unreviewed** status. The opt-in installer verifies that the counts
match the WPP2 binary, the thematic source file hash is unchanged, and
**both** language packs pass validation before publishing either. Older
160-word imports without an expansion remain supported.

The installer also preserves the previously installed source-backed preview
when a new publication fails: it writes complete RU/UK data to a sibling
staging directory, renames the old directory to a backup, and promotes
the new one in a second rename. A failed promotion rolls back to the
backup. A later run can restore an interrupted backup before continuing,
and rejects incomplete language/attribution data. The native selftest
injects a promotion failure and checks that both original packs survive.

This feature provides the **content pipeline**, not approved new vocabulary.
It ships **no newly invented candidate words** in the standard preview.
Native-speaker editing, word frequency, licensing and hardware testing are
still required before any promoted full Russian/Ukrainian release.

## Using attributable source packs in a DS preview

The installer `tools/lang/install_cyr_preview.py` provides an explicit
opt-in path from **both** imported Cyrillic packs to the separate Nintendo
DS engineering ROM:

```sh
python3 tools/lang/install_cyr_preview.py \
  --input-root ../wordpass-cyr-sourced \
  --output-root nitrofiles/cyrpreview
```

It verifies WPD4/WPL2/WPP2 internals, the source manifest, license notice,
dictionary size, expected Cyrillic Unicode alphabets and theme coverage.
Imported resource packs retain their notices and source manifest in NitroFS.
By default an imported language must have 1,000 or more dictionary words and
a non-synthetic origin, so a miniature technical fixture cannot accidentally
masquerade as a substantial sourced dictionary. It remains experimental.
See `docs/CYR-PREVIEW.md` for the `make WP_CYR_PREVIEW=1
CYR_EXTERNAL_DIR=...` build path.

Native validation: `python3 tools/lang/install_cyr_preview.py --selftest
build/cyr-pack-install`. The test checks both language installs, attribution
tampering and cross-language pack rejection. These offline checks do not
substitute for native speaker approval or hardware playtesting.
