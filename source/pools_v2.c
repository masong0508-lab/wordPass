// WPP2 themed Cyrillic password format. No heap, no CJK atlas, no effect on
// original WPP1 pools. All pointers are into the caller-owned checked file.
//
// Header [0..19]: WPP2, version=1, flags=0 or 1 (Ukrainian apostrophes), theme count u8 (1..48),
// reserved=0, alphabet symbols u8 (2..64), minimum=4, maximum=9,
// reserved=0, byte size LE32, payload FNV1a32 LE32.
// Payload: alphabet symbols x u32 LE codepoint, theme records (24-byte
// NUL-padded UTF-8 name + six u16 LE counts for lengths 4..9),
// then words as raw WPD4 symbol-ID bytes grouped by theme and word length.
#include "pools_v2.h"
#include <string.h>

#define WPP2_HEADER 20u
#define WPP2_THEME_HEADER (WPP2_NAME_BYTES + 2u * WPP2_LENGTHS)
#define WPP2_MAX_BYTES (2u * 1024u * 1024u)

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((unsigned)p[1] << 8));
}
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
static bool validName(const uint8_t *name)
{
    size_t n = 0;
    while (n < WPP2_NAME_BYTES && name[n]) n++;
    if (!n || n == WPP2_NAME_BYTES) return false;
    for (size_t i = n + 1; i < WPP2_NAME_BYTES; i++)
        if (name[i]) return false; // no garbage after the terminating NUL
    for (size_t i = 0; i < n;) {
        unsigned cp = name[i], need = 1, min = 0;
        if (cp < 0x80) { if (cp < 0x20 || cp == 0x7f) return false; }
        else if (cp >= 0xc2 && cp <= 0xdf) { need = 2; cp &= 31; min = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { need = 3; cp &= 15; min = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { need = 4; cp &= 7; min = 0x10000; }
        else return false;
        if (n - i < need) return false;
        for (unsigned k = 1; k < need; k++) {
            uint8_t ch = name[i + k];
            if ((ch & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (ch & 63u);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += need;
    }
    return true;
}
bool v4PoolsOpen(V4Pools *pools, const uint8_t *data, size_t size,
                 const V4Dict *dict)
{
    if (!pools) return false;
    memset(pools, 0, sizeof(*pools));
    if (!dict || !dict->image || !data ||
        size < WPP2_HEADER || size > WPP2_MAX_BYTES ||
        memcmp(data, "WPP2", 4) || data[4] != 1 ||
        (data[5] & ~WPP2_UK_APOSTROPHE) ||
        data[6] == 0 || data[6] > WPP2_MAX_THEMES || data[7] ||
        data[8] != dict->symbols ||
        data[9] != WPP2_MIN_LEN || data[10] != WPP2_MAX_LEN ||
        data[11] || le32(data + 12) != size ||
        le32(data + 16) != fnv32(data + WPP2_HEADER, size - WPP2_HEADER))
        return false;

    unsigned themes = data[6], symbols = data[8];
    size_t headerBytes = WPP2_HEADER + 4u * symbols +
                         (size_t)themes * WPP2_THEME_HEADER;
    if (size < headerBytes) return false;
    const uint8_t *a = data + WPP2_HEADER;
    for (unsigned k = 0; k < symbols; k++)
        if (le32(a + 4u * k) != dict->alphabet[k]) return false;

    const uint8_t *headers = a + 4u * symbols;
    size_t offset = headerBytes;
    for (unsigned t = 0; t < themes; t++) {
        const uint8_t *h = headers + t * WPP2_THEME_HEADER;
        if (!validName(h)) return false;
        for (unsigned prev = 0; prev < t; prev++)
            if (!strcmp((const char *)(headers + prev * WPP2_THEME_HEADER),
                        (const char *)h)) return false;
        unsigned countTotal = 0;
        for (unsigned k = 0; k < WPP2_LENGTHS; k++)
            countTotal += le16(h + WPP2_NAME_BYTES + k * 2u);
        if (countTotal == 0) return false;  // no unplayable empty theme
    }
    bool hasApostrophe = false;
    for (unsigned t = 0; t < themes; t++) {
        const uint8_t *h = headers + t * WPP2_THEME_HEADER;
        // The earlier size cap (2 MiB) guarantees a lossless offset.
        pools->themeOffset[t] = (uint32_t)offset;
        for (unsigned k = 0; k < WPP2_LENGTHS; k++) {
            unsigned len = WPP2_MIN_LEN + k;
            unsigned count = le16(h + WPP2_NAME_BYTES + k * 2u);
            if (offset > size || count > (size - offset) / len)
                return false;
            const uint8_t *previous = NULL;
            for (unsigned i = 0; i < count; i++) {
                const uint8_t *word = data + offset + (size_t)i * len;
                for (unsigned j = 0; j < len; j++)
                    if (word[j] >= symbols) return false;
                if (previous && memcmp(previous, word, len) >= 0) return false;
                // The dictionary owns the unique spelling for this path.
                // Legacy pools refuse non-tile punctuation; flag 1 opts
                // in explicitly without changing any word record widths.
                uint32_t apostrophe;
                if (!v4DictLookupSpellingIds(dict, word, (int)len, &apostrophe) ||
                    (apostrophe && !(data[5] & WPP2_UK_APOSTROPHE))) return false;
                hasApostrophe |= apostrophe != 0;
                previous = word;
            }
            offset += (size_t)count * len;
        }
    }
    if (offset != size ||
        hasApostrophe != ((data[5] & WPP2_UK_APOSTROPHE) != 0)) return false;
    pools->image = data;
    pools->themes = (uint8_t)themes;
    pools->symbols = (uint8_t)symbols;
    return true;
}
bool v4PoolsMatches(const V4Pools *pools, const V4Dict *dictionary)
{
    if (!pools || !pools->image || !dictionary || !dictionary->image ||
        pools->symbols != dictionary->symbols) return false;
    const uint8_t *alphabet = pools->image + WPP2_HEADER;
    for (unsigned i = 0; i < pools->symbols; i++)
        if (le32(alphabet + 4u * i) != dictionary->alphabet[i]) return false;
    return true;
}
// Called only with a validated theme index and an immutable open image.
static const uint8_t *themeHeader(const V4Pools *pools, int theme)
{
    return pools->image + WPP2_HEADER + 4u * pools->symbols +
           (size_t)theme * WPP2_THEME_HEADER;
}
int v4PoolsCount(const V4Pools *pools, int theme, int len)
{
    if (!pools || !pools->image || theme < 0 || theme >= pools->themes ||
        len < WPP2_MIN_LEN || len > WPP2_MAX_LEN) return 0;
    return le16(themeHeader(pools, theme) + WPP2_NAME_BYTES +
                2u * (len - WPP2_MIN_LEN));
}
const char *v4PoolsName(const V4Pools *pools, int theme)
{
    if (!pools || !pools->image || theme < 0 || theme >= pools->themes)
        return "";
    return (const char *)themeHeader(pools, theme);
}
bool v4PoolsWord(const V4Pools *pools, int theme, int len, int index,
                 uint8_t out[WPP2_MAX_LEN])
{
    if (!out || index < 0 || index >= v4PoolsCount(pools, theme, len))
        return false;
    // At most five prior lengths. Word selection is not a frame operation.
    const uint8_t *counts = themeHeader(pools, theme) + WPP2_NAME_BYTES;
    size_t offset = pools->themeOffset[theme];
    for (int previous = WPP2_MIN_LEN; previous < len; previous++)
        offset += (size_t)previous *
                  le16(counts + 2u * (previous - WPP2_MIN_LEN));
    memcpy(out, pools->image + offset + (size_t)index * len, (size_t)len);
    return true;
}
