# Russian and Ukrainian: 40-theme editorial starter catalogs

Two candidate JSON catalogs are stored at data/cyrlex/ru_themes.json and
data/cyrlex/uk_themes.json. Each contains **40 translated concept headings and
four distinct starter words per theme**, aligned to the original English
shelf order. That makes **160 possible password words per language**, but
these are NOT complete 40-theme playable packs.

The catalogs require native-speaker editorial verification, usable frequency
tiers, source licensing, inflection review, clean-word filters, and many more
password candidates for each theme. Do not advertise these as full locales.

Theme names use NFC Cyrillic and fit within 23 UTF-8 bytes plus NUL in WPP2.
Words are 4..9 Unicode symbols, alphabet-valid, and not repeated across themes.
Each theme retains its English category key so concept alignment can be
audited independently of localized titles.

The command python3 tools/lang/check_cyr_seeds.py is wired into make check.
It validates Unicode, 40-theme shelf order, byte budgets, word lengths,
alphabet coverage, and unique words. It cannot prove good translation.

Before release: expand theme pools, build complete licensed WPD4 dictionaries,
validate WPL2 tile frequencies, translate 385 UI messages and the in-game
guide, then test the full DS gameplay and review with native speakers.
These JSON catalogs are not compiled into the normal 11-language ROM.
