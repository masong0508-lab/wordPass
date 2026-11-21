// Standalone Cyrillic board integration prototype. Not yet the actual Game:
// normal wordPass must remain byte-for-byte compatible with existing packs.
// This model exercises the same 7x8 shifted-hex topology as boardNeighbors,
// new 33-symbol tile IDs, WPD4 lookup and a simplified refill/score cycle.
#include "script_board.h"
#include <string.h>

static bool inside(int c, int r)
{
    return c >= 0 && c < SCRIPT_COLS && r >= 0 && r < SCRIPT_ROWS;
}
static bool adjacent(int c1, int r1, int c2, int r2)
{
    if (c1 == c2) return r2 == r1 - 1 || r2 == r1 + 1;
    if (c2 != c1 - 1 && c2 != c1 + 1) return false;
    return (c1 & 1) ? (r2 == r1 || r2 == r1 + 1)
                    : (r2 == r1 || r2 == r1 - 1);
}
static bool sameAlphabet(const V4Dict *d, const V4Tiles *t)
{
    if (!d || !d->image || !t || !t->valid || d->symbols != t->symbols)
        return false;
    for (unsigned i = 0; i < d->symbols; i++)
        if (d->alphabet[i] != t->alphabet[i]) return false;
    return true;
}
static uint32_t nextSeed(uint32_t old)
{
    // Refill uses a separate per-board seed, never the cosmetic renderer RNG.
    return old * 1664525u + 1013904223u;
}
static bool take(ScriptBoard *b, const V4Tiles *tiles, uint8_t *out)
{
    if (b->bagPos >= WPL2_BAG_SIZE) {
        b->seed = nextSeed(b->seed);
        if (!v4TilesFill(tiles, b->bag, b->seed)) return false;
        b->bagPos = 0;
    }
    *out = b->bag[b->bagPos++];
    return true;
}
bool scriptBoardStart(ScriptBoard *b, const V4Tiles *tiles, uint32_t seed)
{
    if (!b || !tiles || !tiles->valid) return false;
    memset(b, 0, sizeof(*b));
    b->seed = seed ? seed : 0x9e3779b9u;
    if (!v4TilesFill(tiles, b->bag, b->seed)) return false;
    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = 0; r < SCRIPT_ROWS; r++) {
            b->tile[c][r] = b->bag[b->bagPos++];
            b->state[c][r] = SCRIPT_TILE;
        }
    return true;
}
bool scriptBoardAdd(ScriptBoard *b, int c, int r)
{
    if (!b || !inside(c, r) ||
        (b->state[c][r] != SCRIPT_TILE &&
         b->state[c][r] != SCRIPT_BURN &&
         b->state[c][r] != SCRIPT_DIAMOND))
        return false;
    if (b->chainLen == 0) {
        b->chainC[0] = (uint8_t)c;
        b->chainR[0] = (uint8_t)r;
        b->chainLen = 1;
        return true;
    }
    int last = b->chainLen - 1;
    if (b->chainC[last] == c && b->chainR[last] == r) return false;
    if (last > 0 && b->chainC[last - 1] == c && b->chainR[last - 1] == r) {
        b->chainLen--;
        return true;
    }
    for (int i = 0; i < last; i++)
        if (b->chainC[i] == c && b->chainR[i] == r) {
            b->chainLen = (uint8_t)(i + 1);
            return true;
        }
    if (b->chainLen >= SCRIPT_MAX_CHAIN ||
        !adjacent(b->chainC[last], b->chainR[last], c, r))
        return false;
    b->chainC[b->chainLen] = (uint8_t)c;
    b->chainR[b->chainLen] = (uint8_t)r;
    b->chainLen++;
    return true;
}
void scriptBoardClearChain(ScriptBoard *b)
{
    if (b) b->chainLen = 0;
}
static int preview(const ScriptBoard *b, const V4Dict *dict,
                   const V4Tiles *tiles, int *tier)
{
    if (tier) *tier = 0;
    if (!b || !sameAlphabet(dict, tiles) ||
        b->chainLen < WPD4_MIN_WORD || b->chainLen > WPD4_MAX_WORD)
        return 0;
    uint8_t ids[SCRIPT_MAX_CHAIN];
    int base = 0, fire = 0, diamonds = 0;
    for (unsigned i = 0; i < b->chainLen; i++) {
        int c = b->chainC[i], r = b->chainR[i];
        if (!inside(c, r)) return 0;
        unsigned sym = b->tile[c][r];
        int state = b->state[c][r];
        if (sym >= tiles->symbols || !tiles->weight[sym] ||
            (state != SCRIPT_TILE && state != SCRIPT_BURN &&
             state != SCRIPT_DIAMOND)) return 0;
        for (unsigned j = 0; j < i; j++)
            if (b->chainC[j] == c && b->chainR[j] == r) return 0;
        if (i && !adjacent(b->chainC[i-1], b->chainR[i-1], c, r)) return 0;
        ids[i] = (uint8_t)sym;
        base += tiles->value[sym];
        fire += state == SCRIPT_BURN;
        diamonds += state == SCRIPT_DIAMOND;
    }
    int t = v4DictLookupIds(dict, ids, b->chainLen);
    if (!t) return 0;
    if (tier) *tier = t;
    int n = b->chainLen;
    int multiplier = n <= 4 ? 1 : n <= 6 ? 2 : n <= 8 ? 3 : 4;
    int pts = base * multiplier * 10 + (n >= 7 ? 200 : 0);
    if (fire) pts += 30 * n;
    if (diamonds > 2) diamonds = 2;
    return pts * (2 + diamonds) / 2;
}
int scriptBoardPreview(const ScriptBoard *b, const V4Dict *dict,
                       const V4Tiles *tiles, int *tier)
{
    return preview(b, dict, tiles, tier);
}
int scriptBoardSubmit(ScriptBoard *b, const V4Dict *dict,
                      const V4Tiles *tiles, int *tier)
{
    int pts = preview(b, dict, tiles, tier);
    if (!pts) return 0; // rejected words leave the board and chain untouched
    bool used[SCRIPT_COLS][SCRIPT_ROWS] = {{false}};
    for (unsigned i = 0; i < b->chainLen; i++)
        used[b->chainC[i]][b->chainR[i]] = true;
    // First refill uses the 44 tiles left in the current 100-tile bag;
    // subsequent refills draw a new deterministic bag (no heap allocations).
    for (int c = 0; c < SCRIPT_COLS; c++) {
        int dst = SCRIPT_ROWS - 1;
        for (int r = SCRIPT_ROWS - 1; r >= 0; r--) {
            if (used[c][r]) continue;
            b->tile[c][dst] = b->tile[c][r];
            b->state[c][dst] = b->state[c][r];
            dst--;
        }
        for (; dst >= 0; dst--) {
            uint8_t sym;
            if (!take(b, tiles, &sym)) return 0;
            b->tile[c][dst] = sym;
            b->state[c][dst] = SCRIPT_TILE;
        }
    }
    b->chainLen = 0;
    return pts;
}
static unsigned encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xc0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 63u));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 63u));
        out[2] = (char)(0x80u | (cp & 63u));
        return 3;
    }
    out[0] = (char)(0xf0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 63u));
    out[2] = (char)(0x80u | ((cp >> 6) & 63u));
    out[3] = (char)(0x80u | (cp & 63u));
    return 4;
}
bool scriptBoardChainUtf8(const ScriptBoard *b, const V4Dict *dict,
                          char *out, size_t capacity)
{
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!b || !dict || !dict->image) return false;
    size_t n = 0;
    for (unsigned i = 0; i < b->chainLen; i++) {
        int c = b->chainC[i], r = b->chainR[i];
        if (!inside(c, r) || b->tile[c][r] >= dict->symbols) return false;
        char buf[4];
        unsigned count = encode(dict->alphabet[b->tile[c][r]], buf);
        if (n + count >= capacity) { out[0] = 0; return false; }
        memcpy(out + n, buf, count);
        n += count;
    }
    out[n] = 0;
    return true;
}
