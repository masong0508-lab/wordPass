// WPD4: versioned Unicode-alphabet dictionary *prototype* (not yet selected
// by the 26-tile board). A view keeps pointers into an owned, validated image.
// No heap allocations, no change to WPD3 dictionaries or saved games.
#ifndef WORDPASS_DICT_V4_H
#define WORDPASS_DICT_V4_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WPD4_MAX_SYMBOLS 64
#define WPD4_MAX_WORD 20
#define WPD4_MIN_WORD 3
#define WPD4_UK_APOSTROPHE 1u
typedef struct {
    uint32_t alphabet[WPD4_MAX_SYMBOLS]; // sorted Unicode scalar values
    const uint8_t *base[WPD4_MAX_WORD + 1];
    uint32_t count[WPD4_MAX_WORD + 1];
    uint8_t symbols, bits;              // 2..64 symbols, minimal 1..6 bit width
    const uint8_t *image;               // valid only while owner keeps bytes alive
    // Optional Ukrainian orthography trailer, one 24-bit mask per word
    // in each sorted letter-length group. NULL in legacy packs.
    const uint8_t *apostrophe[WPD4_MAX_WORD + 1];
} V4Dict;
bool v4DictOpen(V4Dict *view, const uint8_t *data, size_t bytes);
int v4DictLookupIds(const V4Dict *view, const uint8_t *ids, int symbols);
// A tile path has no apostrophe tile. Return the stored, unambiguous
// spelling alongside the tier; bit i inserts U+02BC BEFORE letter i.
// Exact UTF-8 lookup separately requires that punctuation be present.
int v4DictLookupSpellingIds(const V4Dict *view, const uint8_t *ids,
                           int symbols, uint32_t *apostropheMask);
// Read-only, allocation-free prefix binary search across valid word lengths.
// Used only on an explicit hint request, never during per-frame rendering.
bool v4DictHasPrefixIds(const V4Dict *view, const uint8_t *ids, int symbols);
// Restrict a hint branch to its exact target length, ignoring words it cannot
// reach. This searches one table rather than all 18 possible word lengths.
bool v4DictHasPrefixLengthIds(const V4Dict *view, const uint8_t *ids,
                             int symbols, int wordLength);
// Restrict one validated, sorted WPD4 word-length group to the next symbol.
// [*first, *past) must already be the exact interval matching all earlier
// symbols (initially [0, count[wordLength])). On success it becomes the
// narrower matching interval; on failure it remains unchanged. This lets
// a hex-board DFS reuse its parent's prefix interval rather than repack and
// binary-search the entire dictionary for every additional letter.
// Pure, allocation-free, bounded; safe to use only on explicit hint input.
bool v4DictNarrowPrefixLengthId(const V4Dict *view, int wordLength,
                                int position, uint8_t symbol,
                                uint32_t *first, uint32_t *past);
int v4DictLookup(const V4Dict *view, const char *utf8); // NFC Cyrillic UTF-8
int v4DictSymbol(const V4Dict *view, uint32_t codepoint); // index or -1
#endif
