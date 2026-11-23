# wordPass — release and language expansion roadmap

Updated November 21, 2026. This checklist is deliberately explicit: there
was no maintained list of remaining project work or open GitHub issues.
Checkmarks indicate implemented systems, not certified release quality.
There is **no defensible numeric completion percentage**.

## The current game

- [x] Classic and Daily Pass, gem chaining, fire, password deduction and hints
- [x] 40 themed password pools, score bonuses, library challenges and twists
- [x] Saving and load recovery, tutorial and eight-page guide, music and ledger
- [x] English plus ten playable Latin-encoded translations (11 total)
- [x] Native rules/data, ROM build and complete DeSmuME rules/layout/guide CI
- [x] Missing/damaged language pack fallback and language picker recovery
- [x] Bounded dictionary/hint optimization, single retail chain validation per
      frame, linked-ROM memory budgets and mutually exclusive retail/preview packs
- [x] Backup-preserving saves with failed-write/rename recovery regressions,
      plus default CI FAT-card save/reboot and damaged-language scenarios
- [x] Formatter-accurate localization checks and deterministic standard music
      previews across scalar/NumPy hosts
- [x] Free, bounded PEEK clue navigation using visible numbered evidence only,
      with no game-state mutation, new allocations or per-frame search work

The core game is playable; remaining work is predominantly editorial,
platform compatibility, balancing, broader tests and script expansion.

## Remaining release priorities

- [ ] Confirm licensing / redistributed third-party word lists and translations.
      See `NOTICE`, `LICENSES/` and `docs/THIRD-PARTY.md`.
- [ ] Native-speaker editorial pass across 385 messages, 40 themes, guide and
      dictionary per currently playable locale; the quality-report artifact is a
      review queue, not a replacement for proofreading.
- [ ] Native DS/DS Lite/DSi and flashcart playthroughs; measure 60-FPS,
      loading latency, memory headroom and FAT-card save roundtrips.
- [x] Release CI: run the full rules/layout/guide scenario suite for every
      PR, main build, tag and manual run, alongside FAT-card scenarios and
      the separate Cyrillic preview. ABI checks use the actual built ELF.
- [ ] Investigate and retest the Lockdown difficulty spike at shelves 14–15
      with before/after balance bots; preserve intended challenge.
- [ ] Continue dictionary and theme-content editorial fixes, especially
      Finnish inflections and false-positive/false-negative word acceptance.
- [x] CI creates a deterministic, SHA-256-verified ZIP containing the retail
      and separate experimental Cyrillic ROMs, a source-commit manifest,
      usage instructions and license notices. The v2 bundle also includes the
      full third-party/Cyrillic provenance inventory and checks SHA-256 for
      every notice; old v1 ZIPs remain readable by the verifier. This is
      integrity checking, **not** a licence clearance. Both stand-alone ROM
      artifacts remain available. Verify the bundle on real hardware.
- [x] Display a compact build revision and RETAIL / CYR DEMO flavor at the
      bottom of the title's top screen. The stamp uses a fixed compile-time
      string (Git short SHA, or `source` for source archives), no new assets,
      dynamic allocation or translation strings, and two native flavor tests.

## Multiscript expansion, with honest feature gates

The current gameplay data is restricted to 26 A–Z tile kinds. WPD3 encodes
dictionary letters in five bits, the pool files store Latin text, and the
static UI table has eleven fixed A–Z-only language columns. **A font alone
does not create a playable language.** Never advertise an incomplete pack
as available in the in-game language picker.

### Milestone 1 — UTF-8 and Cyrillic foundations

- [x] Strict UTF-8 decoding for interface strings, avoiding dynamic allocation.
- [x] Cache the Russian/Ukrainian preview's glyph pointers on successful
      dictionary load; no repeat codepoint switch for 56 tiles per frame.
      Test cache invalidation across languages and 120/120 emulator cadence.
- [x] Hand-drawn 5×7 Russian and Ukrainian uppercase glyph coverage with
      ASCII fast path, malformed-input fallback and native regressions.
- [x] Pixel centering uses displayed glyph counts rather than UTF-8 byte counts.
- [ ] Improve glyph size and shape after native-resolution readability tests.
- [ ] Upgrade the translation generator and guide layout checks to validate
      Unicode, combining characters, visible pixel widths and per-script fonts.

### Milestone 2 — Russian: first full Cyrillic language

- [ ] Legally redistributable Russian common-word and rare-word lexicon,
      native-speaker validation and appropriate source/license notices.
- [x] Implement a **parallel WPD4 dictionary proof** (2–64 Unicode scalars,
      compact variable-bit records, checksum, deterministic Russian/Ukrainian
      fixture builders and native corruption/lookup regressions).
- [x] Add an independent **WPL2** 33-symbol tile-table prototype with
      100-tile weighted bags, explicit Cyrillic vowels, rare-symbol limits,
      deterministic shuffling and matching WPD4 alphabet checks.
- [x] Add an isolated 7×8 hex-grid prototype using WPD4/WPL2 symbol IDs,
      stylus-style chain/backtracking, Cyrillic word scoring and deterministic
      submit/refill, with Russian and Ukrainian native gameplay tests.
- [x] Implement **WPP2** script-aware themed password pools, with
      exact WPD4 alphabet parity, Unicode theme names, validated dictionary
      words, deterministic sample themes and corrupt-file regression tests.
- [x] Extend the independent Cyrillic hex-board prototype with deterministic
      hidden-password placement, peel/reveal, permanent diamonds, pass
      completion and gravity barriers.
- [x] Build an opt-in **real DS Cyrillic engineering preview** with
      touch/stylus input, bitmap glyph rendering, WPD4/WPL2/WPP2 resource
      loading, Russian/Ukrainian switching and real-word scoring, with a
      separate ROM and dedicated emulator scenario in CI.
- [x] Introduce a **Cyrillic shelf-fire prototype** with word/peel heat,
      overtime, deterministic flame ignition and descent, diamond firebreaks,
      fatal scorched secrets, pass bonuses and next-shelf navigation, tested
      natively for both languages and in the optional DS preview.
- [x] Add a bounded, dictionary-validated Russian/Ukrainian word hint
      solver, tested natively and on the experimental DS.
- [x] Add experimental eight-word repeat penalties, 10%-per-word combos
      (capped at 50%) and peel resets, with native and DS regression coverage.
- [x] Show separate **РУССКИЙ — DEMO** and **УКРАЇНСЬКА — DEMO**
      selections in the opt-in preview ROM only; retail language options
      remain unchanged until translation and feature-parity gates pass.
- [x] Implement **unranked Daily Practice** with stable per-date and
      per-language puzzles, retry score resets and DS-clock validation;
      do not treat it as the retail scored Daily.
- [x] Replace the fixed Russian/Ukrainian greeting planted on every
      experimental shelf with a deterministic, dictionary-validated,
      theme-aware 4–6-symbol chain. The seed varies word/column, protects
      hidden password tiles, and supports the existing bounded hint solver;
      native checks cover all 40 themes in both languages.
- [x] On new RU/UK experimental shelves, plant up to two separate
      source-verified words in disjoint column groups. A scored word's
      refill cannot erase the other opening chain; preserve single-word
      rescue and fixed RAM/60-FPS budgets. Native tests cover all 40 themes.
- [x] Progress Russian/Ukrainian experimental password-length targets
      across 4–9 letters by shelf tier, using only words actually present
      in the active WPP2 theme and a deterministic nearest-length fallback.
      Daily retries are stable; native C tests cover bands and all available
      theme word lengths. Difficulty needs real-player balancing.
- [x] Replace hidden-password cheat marks in the Cyrillic renderer with
      legitimate numbered peels and accepted-word endpoint clues. The seven
      survey row masks preserve fixed back-layer clues through gravity;
      safe/forced-neighbor deduction uses only earned evidence, native rules
      tests, and an actual DS anti-spoiler screenshot comparison.
- [x] Add a bounded one-use recovery move for sparse Cyrillic shelves:
      L+R after a turn can plant one valid word around burns/holes, costs
      heat and time, breaks combos, never awards points, and avoids hidden
      passwords. Includes fail-atomic and real emulator input tests.
- [x] Add an opt-in, source-verified RU/UK theme expansion importer:
      preserve 160 original starters, accept editorial candidates only if
      present in the correctly licensed source dictionary, validate 40-theme
      identities and all counts, and retain a tamper-checked provenance JSON.
      Default ROMs remain unchanged and no new vocabulary is auto-created.
- [x] Prepare genuine source-backed, independently screened Russian
      (187) and Ukrainian (173) themed candidate words across all 40 themes,
      using pinned upstream Hunspell headwords and source attribution.
      Add a 100k-word-per-language opt-in DS build/test workflow with
      verified original sources, memory-budget checks and emulator playthrough.
      These candidates still require native-speaker review and production gates.
- [ ] Expand every draft theme beyond four starter passwords, edit native
      vocabulary and balance word lengths/weighted tiles. The seeded helper
      and limited rescue offer bounded help when a safe path exists; they do
      not replace an abundant dictionary.
- [ ] Bring Cyrillic modes to full retail Classic/Daily parity: lockdown,
      twists, complete hints economy, scored Daily/streaks, full statistics,
      saves, remaining fail conditions and measured difficulty balance.
- [ ] Integrate complete Cyrillic packs into the **normal DS Classic and
      Daily systems** including fire, hints, stats, pass bonuses, progression,
      saves and native speaker reviewed content. Keep 11 locales compatible.
- [ ] Define Qu-free vowel floors, tile distribution and word lengths for
      Cyrillic; balance with competent and weak bots.
- [ ] Translate 385 UI messages, game guide and 40 theme names into Russian;
      generate sample words actually valid in the Russian dictionary.
- [ ] Test full real stylus gameplay, fast language switching, memory,
      both-hand UI layouts and saves; only then offer Russian in the picker.

The completed checkboxes above refer to the **separate experimental
ROM**, not the normal 11-language release. The preview still uses small
four-password-per-theme seed pools; native-speaker dictionaries, all 385 UI
strings, guide text, memory/FPS measurements and save compatibility are open
release blockers. See `docs/CYR-PREVIEW.md` for how to try both languages
and test unranked Daily Practice.

- [x] Add a reproducible 40-theme source-backed Russian/Ukrainian word
      review queue (Markdown + JSON), validating 100k-word WPD4 packs,
      external candidate provenance and packed pool membership. Keep
      native-speaker editorial approval as an explicit release blocker.

- [x] Give fresh 7×8 Russian/Ukrainian preview boards a deterministic
      Qu-free vowel floor: at least one per opening column (two where the
      existing 100-tile bag permits 14), preserving every weighted letter,
      original RNG seed and the 60-FPS/no-heap runtime budget. Native tests
      exercise 10-vowel and 14-vowel sparse packs and both languages.

### Milestone 3 — Ukrainian

- [x] Audit source-only Ukrainian spellings with U+0027, U+2019 and U+02BC
      apostrophes; count excluded forms in the licensed source manifest rather
      than silently converting them to incorrect Cyrillic-only words.
- [ ] Implement a true non-tile Ukrainian apostrophe separator throughout
      dictionary indexing, on-screen words, theme pools and hint scoring.
- [ ] Native Ukrainian dictionary and distinct frequency/balance model.
- [ ] Explicit Ґ, Є, І, Ї, Й and apostrophe handling, not Russian reuse.
- [ ] Complete translated UI, themes and guide with full parity tests.

### Milestone 4 — Japanese and Korean

- [ ] Decide a play-tested unit: Japanese kana/mora versus syllables,
      Korean Hangul blocks versus jamo; design chain/word-length scoring.
- [ ] Readable 12×12 or 16×16 glyph atlas, with layout for variable-width
      text and bounded glyph loading under DS RAM and 60-FPS budgets.
- [ ] Legally usable morphology-aware dictionaries and themed word pools.
- [ ] Native-speaker review, hardware legibility, balance bot and emulator.

### Milestone 5 — Chinese and other scripts

- [ ] Choose Simplified and Traditional Chinese tile/word segmentation
      (characters, pinyin or zhuyin) through gameplay prototypes.
- [ ] Paged CJK atlas instead of preloading thousands of large glyphs.
- [ ] Separately investigate Thai, Hindi, Arabic and Hebrew; several require
      shaping, clustering and/or right-to-left layout before inclusion.

## Performance and compatibility guardrails

- Keep the Nintendo DS's 60-FPS rendering budget and strict RAM limits.
- Avoid per-frame heap allocations, giant font arrays or extra framebuffers.
- Maintain compatibility with the existing 11 playable languages and saves.
- Include file-format migration tests and fail closed on unsupported packs.
- Keep in-game language selection limited to actually validated dictionaries,
  tile tables, pass pools, UI translations and guide examples.
