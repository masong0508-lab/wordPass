// WPL2 uses the same sorted Unicode alphabet as its matching WPD4 dictionary.
// Runtime checks are intentionally strict: no poisoned tile indexes or
// inconsistent per-language alphabets can reach a future gameplay board.
//
// Layout: 20-byte header: "WPL2", version=1, flags=0, count n (2..64),
// rareCount (0..4), bagSize LE16=100, reserved LE16=0, totalSize LE32,
// FNV1a32 of payload. Payload: n×u32 LE codepoints, vowels u64 LE,
// rare[4] (255 for unused), weight[n], value[n].
// Total file length: 32+6*n bytes.
#include "tiles_v2.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}
static uint32_t checksum(const uint8_t *p, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
bool v4TilesOpen(V4Tiles *tiles, const uint8_t *data, size_t size,
                 const V4Dict *dict)
{
    if (!tiles) return false;
    memset(tiles, 0, sizeof(*tiles));
    if (!data || !dict || !dict->image || size < 32 ||
        memcmp(data, "WPL2", 4) || data[4] != 1 || data[5] ||
        data[6] < 2 || data[6] > WPD4_MAX_SYMBOLS ||
        data[6] != dict->symbols || data[7] > 4 ||
        data[8] != WPL2_BAG_SIZE || data[9] ||
        data[10] || data[11] ||
        size != 32u + 6u * data[6] || le32(data + 12) != size ||
        le32(data + 16) != checksum(data + 20, size - 20))
        return false;
    unsigned n = data[6], rareCount = data[7];
    const uint8_t *p = data + 20;
    for (unsigned i = 0; i < n; i++) {
        uint32_t cp = le32(p + 4u * i);
        if (cp != dict->alphabet[i]) return false;
        tiles->alphabet[i] = cp;
    }
    p += 4u * n;
    uint64_t vowels = le64(p);
    p += 8;
    if (n < 64 && (vowels >> n)) return false;
    tiles->vowels = vowels;
    unsigned sum = 0, vowelWeights = 0, rareWeights = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned id = p[i];
        if (i < rareCount) {
            if (id >= n) return false;
            for (unsigned j = 0; j < i; j++) if (id == p[j]) return false;
        } else if (id != 255) return false;
        tiles->rare[i] = (uint8_t)id;
    }
    p += 4;
    for (unsigned i = 0; i < n; i++) {
        unsigned w = p[i], value = p[n + i];
        if (w > 25 || (w && (value < 1 || value > 10)) ||
            (!w && value)) return false;
        tiles->weight[i] = (uint8_t)w;
        tiles->value[i] = (uint8_t)value;
        sum += w;
        if ((vowels >> i) & 1u) vowelWeights += w;
    }
    for (unsigned i = 0; i < rareCount; i++) {
        if (tiles->weight[tiles->rare[i]] != 1) return false;
        rareWeights += tiles->weight[tiles->rare[i]];
    }
    // A useful language bag must have vowel tiles, exactly 100 tiles and no
    // more than two rare tiles. Rarity is by symbol, not ASCII letter code.
    if (sum != WPL2_BAG_SIZE || vowelWeights < 10 || rareWeights > 2) return false;
    tiles->symbols = (uint8_t)n;
    tiles->rareCount = (uint8_t)rareCount;
    tiles->valid = true;
    return true;
}
bool v4TilesVowel(const V4Tiles *tiles, int id)
{
    return tiles && tiles->valid && id >= 0 && id < tiles->symbols &&
           ((tiles->vowels >> id) & 1u);
}
static uint32_t nextRand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}
bool v4TilesFill(const V4Tiles *tiles, uint8_t out[WPL2_BAG_SIZE], uint32_t seed)
{
    if (!tiles || !tiles->valid || !out ||
        tiles->symbols < 2 || tiles->symbols > WPD4_MAX_SYMBOLS) return false;
    // Validate the complete weighted bag BEFORE touching caller memory.
    // Checked files have exactly 100 tiles, but reject an invalidated or
    // damaged in-RAM table rather than writing past a 100-byte output.
    // This scan runs only when a bag is dealt/refilled, not per rendered frame.
    unsigned total = 0;
    for (unsigned i = 0; i < tiles->symbols; i++) {
        unsigned weight = tiles->weight[i];
        if (weight > WPL2_BAG_SIZE - total) return false;
        total += weight;
    }
    if (total != WPL2_BAG_SIZE) return false;
    unsigned pos = 0;
    for (unsigned i = 0; i < tiles->symbols; i++)
        for (unsigned j = 0; j < tiles->weight[i]; j++)
            out[pos++] = (uint8_t)i;
    if (pos != WPL2_BAG_SIZE) return false;
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    for (unsigned i = WPL2_BAG_SIZE - 1; i; i--) {
        unsigned j = nextRand(&rng) % (i + 1u);
        uint8_t temp = out[i];
        out[i] = out[j];
        out[j] = temp;
    }
    return true;
}
