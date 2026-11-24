#include "script_checkpoint.h"
#include <stdio.h>
#include <string.h>

// Bump if generation or initial shelf rules change such that an old
// seed would regenerate different gameplay. Cosmetic changes need not.
#define SCRIPT_CHECKPOINT_GENERATION 1u

static uint32_t addByte(uint32_t h, uint8_t b)
{
    return (h ^ b) * 16777619u;
}
static uint32_t addBytes(uint32_t h, const uint8_t *p, size_t n)
{
    while (n--) h = addByte(h, *p++);
    return h;
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
uint32_t scriptCheckpointTag(const V4Dict *dict, const V4Tiles *tiles,
                             const V4Pools *pools)
{
    if (!dict || !dict->image || !tiles || !tiles->valid ||
        !pools || !pools->image || dict->symbols > WPD4_MAX_SYMBOLS ||
        tiles->symbols != dict->symbols || pools->symbols != dict->symbols)
        return 0;
    uint8_t revision[4];
    put32(revision, SCRIPT_CHECKPOINT_GENERATION);
    uint32_t h = addBytes(2166136261u, revision, sizeof revision);
    // Both file headers already carry the verified byte size and payload
    // checksum. No second multi-megabyte hash or dictionary allocation.
    h = addBytes(h, dict->image, 20);
    h = addBytes(h, pools->image, 20);
    h = addByte(h, tiles->symbols);
    h = addByte(h, tiles->rareCount);
    h = addBytes(h, tiles->rare, sizeof tiles->rare);
    for (unsigned i = 0; i < 8; ++i)
        h = addByte(h, (uint8_t)(tiles->vowels >> (8u * i)));
    h = addBytes(h, tiles->weight, tiles->symbols);
    h = addBytes(h, tiles->value, tiles->symbols);
    return h;
}

static bool valid(const ScriptCheckpoint *c)
{
    return c && c->language <= 1u && c->shelf >= 1u &&
           c->shelf <= 999u && c->seed != 0 &&
           c->score <= SCRIPT_CHECKPOINT_MAX_SCORE &&
           (c->shelf != 1u || c->score == 0);
}
bool scriptCheckpointEncode(const ScriptCheckpoint *c,
                             uint8_t out[SCRIPT_CHECKPOINT_BYTES])
{
    if (!out || !valid(c)) return false;
    memcpy(out, "WPC1", 4);
    out[4] = (uint8_t)c->language;
    out[5] = out[6] = out[7] = 0;
    put32(out + 8, c->pack);
    put32(out + 12, c->serial);
    put32(out + 16, c->shelf);
    put32(out + 20, c->seed);
    put32(out + 24, c->score);
    put32(out + 28, addBytes(2166136261u, out, 28));
    return true;
}
bool scriptCheckpointDecode(const uint8_t *data, size_t length,
                             unsigned language, uint32_t pack,
                             ScriptCheckpoint *out)
{
    if (!data || !out || length != SCRIPT_CHECKPOINT_BYTES ||
        memcmp(data, "WPC1", 4) != 0 || language > 1 ||
        data[4] != language || data[5] || data[6] || data[7] ||
        get32(data + 8) != pack ||
        get32(data + 28) != addBytes(2166136261u, data, 28))
        return false;
    ScriptCheckpoint c = {
        .language = data[4], .pack = pack, .serial = get32(data + 12),
        .shelf = get32(data + 16), .seed = get32(data + 20),
        .score = get32(data + 24)
    };
    if (!valid(&c)) return false;
    *out = c;
    return true;
}

bool scriptCheckpointPath(const char *settingsPath, unsigned language,
                           uint32_t pack, unsigned slot,
                           char out[SCRIPT_CHECKPOINT_PATH])
{
    if (!settingsPath || !out || language > 1u || slot > 1u) return false;
    const char *slash = strrchr(settingsPath, '/');
    if (!slash) return false;
    size_t prefix = (size_t)(slash - settingsPath) + 1u;
    // Full 32-bit tag in an 8.3 filename: Rxxxxxxx.Cx0 / Uxxxxxxx.Cx1.
    // The last hex digit in the extension is the upper tag nibble.
    char leaf[] = "R0000000.C00";
    if (prefix > SCRIPT_CHECKPOINT_PATH - sizeof leaf) return false;
    const char hex[] = "0123456789ABCDEF";
    leaf[0] = language ? 'U' : 'R';
    for (unsigned i = 0; i < 7; ++i)
        leaf[1 + i] = hex[(pack >> (24u - 4u * i)) & 15u];
    leaf[10] = hex[pack >> 28];
    leaf[11] = (char)('0' + slot);
    memcpy(out, settingsPath, prefix);
    memcpy(out + prefix, leaf, sizeof leaf);
    return true;
}
static bool readSlot(const char *settingsPath, unsigned language,
                     uint32_t pack, unsigned slot, ScriptCheckpoint *out)
{
    char path[SCRIPT_CHECKPOINT_PATH];
    if (!scriptCheckpointPath(settingsPath, language, pack, slot, path))
        return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t data[SCRIPT_CHECKPOINT_BYTES + 1];
    size_t n = fread(data, 1, sizeof data, f);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok && scriptCheckpointDecode(data, n, language, pack, out);
}
static bool newer(uint32_t a, uint32_t b)
{
    uint32_t delta = a - b;
    return delta && delta < 0x80000000u;
}
static int readNewest(const char *settingsPath, unsigned language,
                      uint32_t pack, ScriptCheckpoint *out)
{
    ScriptCheckpoint a, b;
    bool haveA = readSlot(settingsPath, language, pack, 0, &a);
    bool haveB = readSlot(settingsPath, language, pack, 1, &b);
    if (!haveA && !haveB) return -1;
    if (haveB && (!haveA || newer(b.serial, a.serial))) {
        *out = b;
        return 1;
    }
    *out = a;
    return 0;
}
bool scriptCheckpointLoad(const char *settingsPath, unsigned language,
                           uint32_t pack, ScriptCheckpoint *out)
{
    return out && readNewest(settingsPath, language, pack, out) >= 0;
}
bool scriptCheckpointStore(const char *settingsPath, const ScriptCheckpoint *c)
{
    if (!valid(c)) return false;
    ScriptCheckpoint next = *c, active;
    int slot = readNewest(settingsPath, c->language, c->pack, &active);
    next.serial = slot < 0 ? 1u : active.serial + 1u;
    unsigned target = slot == 0 ? 1u : 0u;
    char path[SCRIPT_CHECKPOINT_PATH];
    uint8_t bytes[SCRIPT_CHECKPOINT_BYTES];
    if (!scriptCheckpointPath(settingsPath, c->language, c->pack, target, path) ||
        !scriptCheckpointEncode(&next, bytes))
        return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(bytes, 1, sizeof bytes, f) == sizeof bytes;
    if (fclose(f) != 0) ok = false;
    if (!ok) return false;  // the previously active slot remains untouched
    ScriptCheckpoint verify;
    return readSlot(settingsPath, c->language, c->pack, target, &verify) &&
           verify.serial == next.serial && verify.shelf == next.shelf &&
           verify.seed == next.seed && verify.score == next.score;
}
