# Repetitive-board Search Latency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the reproduced post-submit dictionary-search stall without changing gameplay results.

**Architecture:** Share a stack-local prefix-answer cache across the short and extended traversals of each public search call. Keep input grammar tests separate from dead-board rescue timing, with an explicit actual-ROM latency gate.

**Tech Stack:** C/libnds on ARM9; native GCC tests; Python/py-desmume and matching ELF DWARF for emulator tests.

**Spec:** `docs/superpowers/specs/2026-11-21-search-latency-design.md`

## Global Constraints

- `MAX_CHAIN` remains 20 gems.
- `MAX_WORD` remains 20 letters.
- `WORD_SEARCH_NODES` remains 4096.
- No heap allocation for search or its cache.
- No game-rule, control, language-data, or save-format change.
- Compare the final compiled stack and both ROM budgets again.

## Review Focus

- Same letters with different requested dictionary-length ranges must not share an incompatible answer.
- A new public search after dictionary, language, library, or spelling changes must start with an empty cache.
- Qu expansion, plain-Q diamonds, and the 20-letter boundary must retain their existing accepted paths.
- Cache hits must preserve node-budget exhaustion, first-hint ordering, board bytes, and RNG.
- Tail taps must retain their score and stroke behavior while EEE/OAE dead-board rescue completes within 12 scans.

---

### Task 1: Cache repeated queries and pin responsive submission

**Files:**
- Modify: `source/board.c`
- Modify: `tools/emu/test_rules.py`
- Modify: `tools/emu/test_cohesion.py`
- Modify: `TESTING.md`, `docs/PERFORMANCE.md`

**Interfaces:**
- Preserve `bool boardHasWords(const Game *g)` and `int boardHintChain(const Game *g, u8 *outC, u8 *outR, int max)`.
- Add private `PrefixCache` and `bool searchHasPrefix(PrefixCache *cache, const char *word, int length, int minLength, int maxLength)`.
- Pass the cache through private short/hint DFS functions and `WordSearch`.

- [x] **Step 1: Reproduce the old-ROM stall.** The unchanged CI prefix at date 20261122 fails the left tail tap; actual G scores at scan 2 while the displayed/debug state waits until scan 27. An EEE refill independently reproduces it.
- [x] **Step 2: Add the real-ROM latency regression.** Force legal EEE and OAE refill letters, submit BED through its tail, and require visible BED score, empty chain, one normal word turn, and a consumed rescue by scan 12, in both hands. Run against `f2b473e`; expect failure at the latency assertion.
- [x] **Step 3: Implement the per-call cache.** One slot per prefix length stores exact bytes, clamped length bounds, and an empty/false/true state. Initialize once per public search, share across its traversal stages, and preserve all existing branch order and node decrements.
- [x] **Step 4: Isolate the chain grammar fixture.** Plant CAT in column 0, rows 4 through 6. Preserve all existing tail-tap, score, empty-chain, and FPS assertions.
- [x] **Step 5: Verify native equivalence.** Run `make check`, board ASan/UBSan, and `tools/check/benchmark_search.py --baseline f2b473eb383a4bc3bc8a956a83a4669abd80a24a --samples 100`. Require exact result/path parity and unchanged board/RNG state, including existing filters, Qu, maximum-depth, and exhausted-budget cases.
- [x] **Step 6: Document and commit.** Record the reproduced cause, cache lifetime/memory scope, and new latency coverage; commit the repair and tests together.
- [ ] **Step 7: Verify the rebuilt ROM and merge.** Require the new latency cases, complete cohesion suite, both memory budgets, compiled stack review, and the full rules/layout CI gate. Compare the merged tree with the tested tree.

### Task 2: Keep word cues coherent with PEEK

**Files:** `source/main.c`, `tools/emu/test_cohesion.py`, `TESTING.md`.

**Interfaces:** No shared or public interfaces change. `updateCue` uses existing `peekHeld`, `ghostDone`, `hintReq` and idle state.

- [x] Add a real-ROM regression for latched PEEK before an automatic hint, interrupting/resuming it with a shoulder, hiding the tail cue in PEEK, and X/L/R interruption of paid hints without an additional charge. Observe the pre-fix failure.
- [x] Gate word cues while PEEK is active, reset the automatic hint idle counter, rearm an interrupted automatic demonstration, and cancel paid requests on deliberate PEEK entry.
- [ ] Run the complete focused suite on the rebuilt ROM, repeat native/Python/guide checks, and obtain the same reviewer's verification of this finding. Include the fix in the final full CI gate before merge.

### Task 3: Match submit affordances and the tutorial prompt to PEEK

**Files:** `source/gfx.c`, `source/main.c`, `tools/emu/test_cohesion.py`, `TESTING.md`.

**Interfaces:** No shared interfaces. Use existing `peekHeld` and the renderer's `preview` predicate; reuse localized `TU_PEEK` and `TU_DRAG` without changing the tutorial stage.

- [x] Verify the pre-fix gold submit ring stays visible during PEEK. Add pixel checks for the ring and button before, during and after PEEK, plus real tail submission after exit; assert the first-word sticky prompt follows the active input mode.
- [x] Hide the independent gold tail ring in PEEK, use the existing active-preview predicate for the submit button, and refresh the sticky first-word prompt on PEEK changes while preserving timed gameplay messages.
- [ ] Verify the final compiled ROM, obtain the existing reviewer's focused follow-up and complete all final gates before merge.
