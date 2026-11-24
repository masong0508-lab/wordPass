// WPD4 versioned, self-checking Unicode dictionary. WPD3 and all 11 existing
// language packs still use the original high-speed 5-bit A-Z lookup.
//
// Format (little endian unless noted):
//    0..3  "WPD4"
//    4     version = 1
//    5     flags = 0 (legacy) or 1 (Ukrainian orthographic separators)
//    6     alphabet size, 2..64
//    7     bits/symbol (minimal 1..6)
//    8..9  min/max word size, 3 and 20 Unicode scalars
//   10..11 reserved zero
//   12..15 complete file byte count
//   16..19 FNV1a32 of bytes [20..end) (accidental-corruption check, NOT a signature)
//   20..   n Unicode scalar values as u32, strictly ascending
//          18 u32 counts for word lengths 3..20
//          sorted variable-length records: ceil((wordLen*bits + 1)/8) bytes,
//          symbols stored MSB-first; last byte bit 0 is tier (0 common/1 rare).
//          Every unused padding bit must be zero when built.
//          Flag 1 only: after ALL unchanged packed letter records, append
//          one little-endian 24-bit apostrophe mask per record, in exactly
//          the same length-group/record order. Bit i means U+02BC before
//          letter i. No extra tile, changed record stride, or prefix index.
//
// This is deliberately a parallel parser, not an implicit replacement for the
// current word-game tile pipeline. Future migration must be atomic across
// board glyphs, chain composition, pool words and UI before enabling a locale.
#include "dict_v4.h"
#include <string.h>

#define HEADER 20u
#define MAX_BYTES (8u * 1024u * 1024u)

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t fnv32(const uint8_t *p, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static unsigned width(int len, int bits)
{
    return ((unsigned)len * (unsigned)bits + 1u + 7u) / 8u;
}
static uint32_t mask24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16);
}
static bool apostropheVowel(uint32_t cp)
{
    return cp == 0x042f || cp == 0x042e || cp == 0x0404 || cp == 0x0407;
}
static unsigned recordSymbol(const uint8_t *record, unsigned stride,
                             unsigned bits, unsigned position);
static bool scalar(uint32_t cp)
{
    return cp >= 0x21 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}
bool v4DictOpen(V4Dict *view, const uint8_t *data, size_t bytes)
{
    if (!view) return false;
    memset(view, 0, sizeof(*view));
    if (!data || bytes < HEADER || bytes > MAX_BYTES ||
        memcmp(data, "WPD4", 4) != 0 || data[4] != 1 ||
        (data[5] & ~WPD4_UK_APOSTROPHE) ||
        data[6] < 2 || data[6] > WPD4_MAX_SYMBOLS ||
        data[7] < 1 || data[7] > 6 ||
        data[8] != WPD4_MIN_WORD || data[9] != WPD4_MAX_WORD ||
        data[10] || data[11] || le32(data + 12) != bytes)
        return false;
    unsigned n = data[6], bits = data[7], minimum = 1;
    while ((1u << minimum) < n) minimum++;
    if (bits != minimum) return false;
    const size_t head = HEADER + n * 4u + (WPD4_MAX_WORD - WPD4_MIN_WORD + 1) * 4u;
    if (bytes < head || le32(data + 16) != fnv32(data + HEADER, bytes - HEADER))
        return false;
    uint32_t prev = 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t cp = le32(data + HEADER + 4u * i);
        if (!scalar(cp) || cp <= prev) return false;
        view->alphabet[i] = prev = cp;
    }
    if (data[5] & WPD4_UK_APOSTROPHE) {
        // Only the exact 33-letter Ukrainian alphabet supports this flag.
        // Older RU and generic WPD4 views retain their original semantics.
        if (n != 33 || view->alphabet[0] != 0x0404 ||
            view->alphabet[1] != 0x0406 || view->alphabet[2] != 0x0407 ||
            view->alphabet[29] != 0x042c || view->alphabet[30] != 0x042e ||
            view->alphabet[31] != 0x042f || view->alphabet[32] != 0x0490)
            return false;
        for (unsigned i = 3; i < 29; i++)
            if (view->alphabet[i] != 0x0410u + i - 3u) return false;
    }
    size_t offset = head;
    uint64_t words = 0;
    const uint8_t *counts = data + HEADER + 4u * n;
    for (int k = WPD4_MIN_WORD; k <= WPD4_MAX_WORD; k++) {
        uint32_t num = le32(counts + 4u * (unsigned)(k - WPD4_MIN_WORD));
        unsigned rec = width(k, bits);
        if (!rec || num > (bytes - offset) / rec) return false;
        view->base[k] = data + offset;
        view->count[k] = num;
        words += num;

        // WPD4 is binary-searched by encoded symbol ID, so merely checking
        // offsets and the accidental-corruption checksum is not sufficient.
        // Reject malformed indexes, duplicate/out-of-order words and
        // nonzero padding BEFORE exposing the image to the game. A checksum
        // can be recomputed over malformed (or deliberately crafted) data.
        const unsigned padding = rec * 8u - (unsigned)k * bits - 1u;
        const unsigned padMask = (1u << padding) - 1u;
        const unsigned idMask = (1u << bits) - 1u;
        const uint8_t *previous = NULL;
        for (uint32_t i = 0; i < num; i++) {
            const uint8_t *word = data + offset + (size_t)i * rec;
            if (((unsigned)word[rec - 1u] >> 1u) & padMask) return false;
            for (int j = 0; j < k; j++) {
                unsigned bit = (unsigned)j * bits;
                unsigned at = bit >> 3, shift = bit & 7u;
                unsigned pair = (unsigned)word[at] << 8;
                if (at + 1u < rec) pair |= word[at + 1u];
                unsigned id = (pair >> (16u - bits - shift)) & idMask;
                if (id >= n) return false;
            }
            if (previous) {
                int cmp = memcmp(previous, word, rec - 1u);
                if (!cmp) {
                    unsigned a = previous[rec - 1u] & 0xfeu;
                    unsigned b = word[rec - 1u] & 0xfeu;
                    cmp = (a > b) - (a < b);
                }
                if (cmp >= 0) return false;
            }
            previous = word;
        }
        offset += (size_t)num * rec;
    }
    if (!words) return false;
    if (data[5] & WPD4_UK_APOSTROPHE) {
        bool haveSeparator = false;
        for (int k = WPD4_MIN_WORD; k <= WPD4_MAX_WORD; k++) {
            uint32_t count = view->count[k];
            if (count > (bytes - offset) / 3u) return false;
            view->apostrophe[k] = data + offset;
            unsigned stride = width(k, bits);
            uint32_t allowed = ((1u << k) - 1u) & ~1u;
            for (uint32_t i = 0; i < count; i++) {
                uint32_t mask = mask24(data + offset + (size_t)i * 3u);
                if (mask & ~allowed) return false; // no first/trailing separator
                if (!mask) continue;
                haveSeparator = true;
                const uint8_t *word = view->base[k] + (size_t)i * stride;
                for (unsigned j = 1; j < (unsigned)k; j++) {
                    if (!(mask & (1u << j))) continue;
                    unsigned id = recordSymbol(word, stride, bits, j);
                    if (!apostropheVowel(view->alphabet[id])) return false;
                }
            }
            offset += (size_t)count * 3u;
        }
        if (!haveSeparator) return false;
    }
    if (offset != bytes) return false;
    view->image = data;
    view->bits = (uint8_t)bits;
    view->symbols = (uint8_t)n;
    return true;
}
int v4DictSymbol(const V4Dict *view, uint32_t cp)
{
    if (!view || !view->image) return -1;
    int lo = 0, hi = (int)view->symbols - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t value = view->alphabet[mid];
        if (cp == value) return mid;
        if (cp < value) hi = mid - 1; else lo = mid + 1;
    }
    return -1;
}
// Each 1..6-bit symbol fits in a 16-bit window, even when it crosses a byte
// boundary. Pack once per symbol instead of once per bit. With at most
// 20 symbols the final window starts at byte 14, inside this 16-byte key.
static bool packIds(const V4Dict *view, const uint8_t *ids, int count,
                    uint8_t key[16])
{
    memset(key, 0, 16);
    unsigned bit = 0;
    for (int i = 0; i < count; i++, bit += view->bits) {
        if (ids[i] >= view->symbols) return false;
        unsigned at = bit >> 3;
        unsigned pair = (unsigned)ids[i] << (16u - view->bits - (bit & 7u));
        key[at] |= (uint8_t)(pair >> 8);
        key[at + 1u] |= (uint8_t)pair;
    }
    return true;
}
int v4DictLookupSpellingIds(const V4Dict *view, const uint8_t *ids,
                           int symbols, uint32_t *apostropheMask)
{
    if (apostropheMask) *apostropheMask = 0;
    if (!view || !view->image || !ids || symbols < WPD4_MIN_WORD ||
        symbols > WPD4_MAX_WORD || !view->count[symbols]) return 0;
    unsigned rec = width(symbols, view->bits);
    uint8_t key[16];  // maximum ceil((20*6+1)/8) = 16 bytes
    if (!packIds(view, ids, symbols, key)) return 0;
    int lo = 0, hi = (int)view->count[symbols] - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        const uint8_t *word = view->base[symbols] + (size_t)mid * rec;
        int cmp = memcmp(word, key, rec - 1u);
        if (!cmp) {
            uint8_t last = word[rec - 1u] & 0xfeu;
            cmp = (last > key[rec - 1u]) - (last < key[rec - 1u]);
        }
        if (!cmp) {
            if (apostropheMask && view->apostrophe[symbols])
                *apostropheMask = mask24(view->apostrophe[symbols] + (size_t)mid * 3u);
            return 1 + (word[rec - 1u] & 1);
        }
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
int v4DictLookupIds(const V4Dict *view, const uint8_t *ids, int symbols)
{
    return v4DictLookupSpellingIds(view, ids, symbols, NULL);
}
// Test a packed Unicode symbol-ID prefix, ignoring the record's rare/common
// tier bit and any trailing bits beyond this prefix. The WPD4 reader already
// checked strict lexicographical ordering when the image was opened, so
// binary searching is safe even with large externally sourced lexicons.
static int prefixCompare(const uint8_t *rec, const uint8_t *key,
                         unsigned prefixBits)
{
    unsigned whole = prefixBits >> 3;
    int cmp = memcmp(rec, key, whole);
    if (cmp || !(prefixBits & 7u)) return cmp;
    unsigned mask = 0xffu << (8u - (prefixBits & 7u));
    unsigned a = rec[whole] & mask, b = key[whole] & mask;
    return (a > b) - (a < b);
}
static bool prefixAtLength(const V4Dict *view, const uint8_t key[16],
                           unsigned prefixBits, int length)
{
    int lo = 0, hi = (int)view->count[length] - 1;
    unsigned stride = width(length, view->bits);
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        const uint8_t *word = view->base[length] + (size_t)mid * stride;
        int cmp = prefixCompare(word, key, prefixBits);
        if (!cmp) return true;
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}
// Read one prevalidated packed Unicode symbol directly from the WPD4 word
// record. No word buffer, key packing, or per-bit loop. A second byte is
// read only when it belongs to the fixed-width record.
static unsigned recordSymbol(const uint8_t *record, unsigned stride,
                             unsigned bits, unsigned position)
{
    unsigned bit = position * bits;
    unsigned at = bit >> 3;
    unsigned pair = (unsigned)record[at] << 8;
    if (at + 1u < stride) pair |= record[at + 1u];
    return (pair >> (16u - bits - (bit & 7u))) & ((1u << bits) - 1u);
}

bool v4DictNarrowPrefixLengthId(const V4Dict *view, int wordLength,
                                int position, uint8_t symbol,
                                uint32_t *first, uint32_t *past)
{
    if (!view || !view->image || !first || !past ||
        wordLength < WPD4_MIN_WORD || wordLength > WPD4_MAX_WORD ||
        position < 0 || position >= wordLength || symbol >= view->symbols)
        return false;
    uint32_t lo = *first, hi = *past;
    if (lo >= hi || hi > view->count[wordLength]) return false;
    const unsigned stride = width(wordLength, view->bits);
    const uint8_t *base = view->base[wordLength];
    // Within an interval that matches the preceding symbols, the next
    // symbol is monotonically ordered. A singleton prefix takes O(1), a
    // dense prefix takes O(log(interval)) rather than O(log(group)).
    unsigned smallest = recordSymbol(base + (size_t)lo * stride, stride,
                                     view->bits, (unsigned)position);
    unsigned largest = recordSymbol(base + (size_t)(hi - 1u) * stride, stride,
                                    view->bits, (unsigned)position);
    if (symbol < smallest || symbol > largest) return false;
    if (smallest == largest) return true;
    // Lower bound for symbol, using no memory writes or heap allocation.
    uint32_t left = lo, right = hi;
    while (left < right) {
        uint32_t mid = left + ((right - left) >> 1);
        unsigned value = recordSymbol(base + (size_t)mid * stride, stride,
                                      view->bits, (unsigned)position);
        if (value < symbol) left = mid + 1u;
        else right = mid;
    }
    if (left == hi ||
        recordSymbol(base + (size_t)left * stride, stride,
                     view->bits, (unsigned)position) != symbol) return false;
    uint32_t begin = left;
    left++; right = hi;
    while (left < right) {
        uint32_t mid = left + ((right - left) >> 1);
        unsigned value = recordSymbol(base + (size_t)mid * stride, stride,
                                      view->bits, (unsigned)position);
        if (value <= symbol) left = mid + 1u;
        else right = mid;
    }
    *first = begin;
    *past = left;
    return true;
}

bool v4DictHasPrefixLengthIds(const V4Dict *view, const uint8_t *ids,
                             int count, int wordLength)
{
    if (!view || !view->image || !ids || count <= 0 || count > wordLength ||
        wordLength < WPD4_MIN_WORD || wordLength > WPD4_MAX_WORD ||
        !view->count[wordLength]) return false;
    uint8_t key[16];
    if (!packIds(view, ids, count, key)) return false;
    return prefixAtLength(view, key, (unsigned)count * view->bits, wordLength);
}
bool v4DictHasPrefixIds(const V4Dict *view, const uint8_t *ids, int count)
{
    if (!view || !view->image || !ids || count <= 0 ||
        count > WPD4_MAX_WORD) return false;
    uint8_t key[16];
    if (!packIds(view, ids, count, key)) return false;
    unsigned prefixBits = (unsigned)count * view->bits;
    int firstLength = count > WPD4_MIN_WORD ? count : WPD4_MIN_WORD;
    for (int length = firstLength; length <= WPD4_MAX_WORD; length++)
        if (prefixAtLength(view, key, prefixBits, length)) return true;
    return false;
}

// This restricted uppercasing is intentional: only simple Latin/Cyrillic
// case pairs are supported in this version, not arbitrary Unicode casefold.
static uint32_t upper(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0x0430 && cp <= 0x044f) return cp - 32;
    switch (cp) {
        case 0x0451: return 0x0401; // ё
        case 0x0454: return 0x0404; // є
        case 0x0456: return 0x0406; // і
        case 0x0457: return 0x0407; // ї
        case 0x0491: return 0x0490; // ґ
        default: return cp;
    }
}
// Return the next validated scalar; advance only after a complete sequence.
// A malformed byte sequence rejects the *whole word* (no lossy fallback).
static bool nextCp(const unsigned char **text, uint32_t *out)
{
    const unsigned char *p = *text;
    uint32_t cp = p[0];
    unsigned need = 1, min = 0;
    if (cp < 0x80) { if (!cp) return false; }
    else if (cp >= 0xc2 && cp <= 0xdf) { need = 2; min = 0x80; cp &= 31; }
    else if (cp >= 0xe0 && cp <= 0xef) { need = 3; min = 0x800; cp &= 15; }
    else if (cp >= 0xf0 && cp <= 0xf4) { need = 4; min = 0x10000; cp &= 7; }
    else return false;
    for (unsigned i = 1; i < need; i++) {
        if (!p[i] || (p[i] & 0xc0) != 0x80) return false;
        cp = (cp << 6) | (p[i] & 63u);
    }
    if (cp < min || !scalar(cp)) return false;
    *text = p + need;
    *out = upper(cp);
    return true;
}
int v4DictLookup(const V4Dict *view, const char *utf8)
{
    if (!view || !view->image || !utf8) return 0;
    const unsigned char *p = (const unsigned char *)utf8;
    uint8_t ids[WPD4_MAX_WORD];
    int n = 0;
    uint32_t supplied = 0;
    while (*p) {
        uint32_t cp;
        if (!nextCp(&p, &cp)) return 0;
        if (view->apostrophe[WPD4_MIN_WORD] &&
            (cp == 0x0027 || cp == 0x2019 || cp == 0x02bc)) {
            // Canonical equivalent spellings are accepted. Leading,
            // trailing, repeated and misplaced punctuation is not.
            if (!n || n >= WPD4_MAX_WORD || !*p ||
                (supplied & (1u << n))) return 0;
            supplied |= 1u << n;
            continue;
        }
        if (n == WPD4_MAX_WORD) return 0;
        int id = v4DictSymbol(view, cp);
        if (id < 0) return 0;
        ids[n++] = (uint8_t)id;
    }
    uint32_t stored;
    int tier = v4DictLookupSpellingIds(view, ids, n, &stored);
    return supplied == stored ? tier : 0;
}
