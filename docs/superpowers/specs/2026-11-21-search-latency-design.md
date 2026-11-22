# Repetitive-board search latency

## Intent and evidence

Restore responsive word submission while preserving the existing game rules. This is a repair within the authorized testing and optimization pass.

Candidate `f2b473e` accepted the failing BED tail tap at emulated cycle 2, then blocked the main loop in repeated dictionary-prefix searches. The debug/render state did not update until cycle 27. Replaying the original CI sequence with its date reproduced the failure: an OAE refill left an almost entirely E board. A separate EEE refill produced the same stall. One EEE trace made 2,756 prefix queries with only 19 unique argument tuples.

## Approaches

- **Cache dictionary-prefix answers within each search (selected).** Preserve traversal and node accounting while avoiding repeated scans of the same packed dictionary ranges. A small stack allocation needs no heap or persistent invalidation. This is the implementation choice for the existing optimization request.
- **Spread search across game frames.** This would need a resumable search and rules for input, board changes, and pending rescue decisions. That additional state is unnecessary for the observed duplicated work.

## Design

Add a private `PrefixCache` in `source/board.c`, scoped to one call of `boardHasWords` or `boardHintChain`. Use one entry for each spelled prefix length, with its exact prefix bytes, normalized minimum/maximum dictionary lengths, and cached Boolean answer. Both short and extended traversals use that cache. A mismatch calls `dictHasPrefix` and replaces the entry. A hit skips only that dictionary query; traversal and node counts are unchanged.

The cache is not shared across calls, languages, libraries, or dictionary replacements. It must not alter the board, random stream, first returned hint, acceptance filters, or score/heat/turn rules.

Keep the chain-input fixture independent of random refills by retaining a spare CAT. Separately exercise both the observed OAE refill and the adversarial EEE refill in a real-ROM latency regression, in both hand layouts. Require submission and rescue to become visible within 12 emulated scans after pen-down. Preserve the original tail-tap, score, empty-chain, and frame assertions.

## Constraints

- `MAX_CHAIN` remains 20 gems.
- `MAX_WORD` remains 20 letters.
- `WORD_SEARCH_NODES` remains 4096.
- No heap allocation for search or its cache.
- No game-rule, control, language-data, or save-format change.
- Compare the final compiled stack and both ROM budgets again.

## Completion

Existing native/sanitizer tests and exact search/path/RNG parity pass. The old ROM fails the new latency bound, the rebuilt ROM passes it, and the complete CI rules/layout suite passes before merge.

## Review follow-up: word cues must match the input mode

Final branch review found that an automatic tutorial word ghost could start during latched PEEK, and an existing paid or automatic word cue could remain visible after entering PEEK. Following that gesture can peel instead of chaining. This is a concrete conflict with the cohesion goal.

Keep all word cues out of PEEK. Stop a paid hint when the player deliberately enters PEEK, without spending another hint. Pause the automatic first-word demonstration while inspecting clues; if it was interrupted, let it return after the normal idle delay once PEEK ends. Preserve the selected chain and all resource costs. The actual-ROM regression covers latched PEEK before the automatic ghost, interruption by a shoulder, return to a usable word, tail-cue suppression, and X/L/R interruption of paid hints in both hands.
