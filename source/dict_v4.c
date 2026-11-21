// WPD4 versioned, self-checking Unicode dictionary. WPD3 and all 11 existing
// language packs still use the original high-speed 5-bit A-Z lookup.
//
// Format (little endian unless noted):
//    0..3  "WPD4"
//    4     version = 1
//    5     flags = 0
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
static bool scalar(uint32_t cp)
{
    return cp >= 0x21 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
}
bool v4DictOpen(V4Dict *view, const uint8_t *data, size_t bytes)
{
    if (!view) return false;
    memset(view, 0, sizeof(*view));
    if (!data || bytes < HEADER || bytes > MAX_BYTES ||
        memcmp(data, "WPD4", 4) != 0 || data[4] != 1 || data[5] ||
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
        offset += (size_t)num * rec;
    }
    if (offset != bytes || !words) return false;
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
int v4DictLookupIds(const V4Dict *view, const uint8_t *ids, int symbols)
{
    if (!view || !view->image || !ids || symbols < WPD4_MIN_WORD ||
        symbols > WPD4_MAX_WORD || !view->count[symbols]) return 0;
    unsigned rec = width(symbols, view->bits);
    uint8_t key[16] = {0};  // maximum ceil((20*6+1)/8) = 16 bytes
    for (int i = 0; i < symbols; i++) {
        if (ids[i] >= view->symbols) return 0;
        for (int b = view->bits - 1; b >= 0; b--) {
            unsigned at = (unsigned)i * view->bits + (unsigned)(view->bits - 1 - b);
            key[at >> 3] |= (uint8_t)(((ids[i] >> b) & 1u) << (7u - (at & 7u)));
        }
    }
    int lo = 0, hi = (int)view->count[symbols] - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        const uint8_t *word = view->base[symbols] + (size_t)mid * rec;
        int cmp = memcmp(word, key, rec - 1u);
        if (!cmp) {
            uint8_t last = word[rec - 1u] & 0xfeu;
            cmp = (last > key[rec - 1u]) - (last < key[rec - 1u]);
        }
        if (!cmp) return 1 + (word[rec - 1u] & 1);
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
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
    while (*p) {
        uint32_t cp;
        if (n == WPD4_MAX_WORD || !nextCp(&p, &cp)) return 0;
        int id = v4DictSymbol(view, cp);
        if (id < 0) return 0;
        ids[n++] = (uint8_t)id;
    }
    return v4DictLookupIds(view, ids, n);
}
