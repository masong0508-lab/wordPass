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
        // Holes, revealed password diamonds and unconsumed fire split a
        // column into segments. Each segment settles within its own bounds:
        // no tiles can phase through a revealed secret or permanent hole.
        for (int r = SCRIPT_ROWS - 1; r >= -1; r--) {
            int state = r >= 0 ? b->state[c][r] : -1;
            bool consumed = r >= 0 && used[c][r] && state != SCRIPT_DIAMOND;
            bool floor = (r < 0 || state == SCRIPT_HOLE ||
                          state == SCRIPT_DIAMOND ||
                          (state == SCRIPT_BURN && !consumed));
            if (floor) {
                for (int fill = dst; fill > r; fill--) {
                    uint8_t sym;
                    if (!take(b, tiles, &sym)) return 0;
                    b->tile[c][fill] = sym;
                    b->state[c][fill] = SCRIPT_TILE;
                }
                dst = r - 1;
            } else if (!consumed) {
                b->tile[c][dst] = b->tile[c][r];
                b->state[c][dst] = b->state[c][r];
                dst--;
            }
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
static bool idsToUtf8(const uint8_t *ids, unsigned length,
                      const V4Dict *dict, char *out, size_t capacity)
{
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!dict || !dict->image || !ids) return false;
    size_t written = 0;
    for (unsigned i = 0; i < length; i++) {
        if (ids[i] >= dict->symbols) { out[0] = 0; return false; }
        char glyph[4];
        unsigned bytes = encode(dict->alphabet[ids[i]], glyph);
        if (written + bytes >= capacity) { out[0] = 0; return false; }
        memcpy(out + written, glyph, bytes);
        written += bytes;
    }
    out[written] = 0;
    return true;
}
bool scriptBoardChainUtf8(const ScriptBoard *b, const V4Dict *dict,
                          char *out, size_t capacity)
{
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!b || b->chainLen > SCRIPT_MAX_CHAIN) return false;
    uint8_t ids[SCRIPT_MAX_CHAIN];
    for (unsigned i = 0; i < b->chainLen; i++) {
        int c = b->chainC[i], r = b->chainR[i];
        if (!inside(c, r)) return false;
        ids[i] = b->tile[c][r];
    }
    return idsToUtf8(ids, b->chainLen, dict, out, capacity);
}
bool scriptBoardPassUtf8(const ScriptBoard *b, const V4Dict *dict,
                         char *out, size_t capacity)
{
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!b || !b->passLen || b->passLen > WPP2_MAX_LEN) return false;
    return idsToUtf8(b->pass, b->passLen, dict, out, capacity);
}


// Hidden password support for the independent script-ID board model.
// No code in the shipped GAME path uses this until the full Cyrillic
// dictionary/guide/renderer and port validation are complete.
bool scriptBoardStartPass(ScriptBoard *b, const V4Dict *dict,
                          const V4Tiles *tiles, const V4Pools *pools,
                          uint32_t seed, int requestedLength)
{
    if (!b || !sameAlphabet(dict, tiles) || !pools || !pools->image ||
        pools->symbols != dict->symbols || !pools->themes ||
        requestedLength < WPP2_MIN_LEN || requestedLength > WPP2_MAX_LEN)
        return false;
    unsigned theme = (seed >> 8) % pools->themes;
    int len = -1;
    for (int delta = 0; delta <= WPP2_MAX_LEN - WPP2_MIN_LEN; delta++) {
        int shorter = requestedLength - delta;
        int longer = requestedLength + delta;
        if (shorter >= WPP2_MIN_LEN &&
            v4PoolsCount(pools, theme, shorter) > 0) { len = shorter; break; }
        if (longer <= WPP2_MAX_LEN &&
            v4PoolsCount(pools, theme, longer) > 0) { len = longer; break; }
    }
    if (len < 0) return false;
    uint8_t word[WPP2_MAX_LEN] = {0};
    int count = v4PoolsCount(pools, theme, len);
    int pick = (int)((seed >> 16) % (unsigned)count);
    if (!v4PoolsWord(pools, theme, len, pick, word) ||
        !v4DictLookupIds(dict, word, len)) return false;

    // At most 9 passwords in the bottom three rows (21 candidate slots).
    // Their positions are independent of the tiles' 100-letter bag.
    if (!scriptBoardStart(b, tiles, seed)) return false;
    b->passLen = (uint8_t)len;
    b->passTheme = (uint8_t)theme;
    memcpy(b->pass, word, (size_t)len);
    uint8_t slots[SCRIPT_COLS * 3];
    for (unsigned i = 0; i < sizeof slots; i++) slots[i] = (uint8_t)i;
    uint32_t rng = (seed ? seed : 0x9e3779b9u) ^ 0x6e624eb7u;
    for (int i = (int)sizeof slots - 1; i > 0; i--) {
        rng = nextSeed(rng);
        unsigned j = rng % (unsigned)(i + 1);
        uint8_t tmp = slots[i]; slots[i] = slots[j]; slots[j] = tmp;
    }
    for (int i = 0; i < len; i++) {
        int c = slots[i] / 3, r = SCRIPT_ROWS - 3 + slots[i] % 3;
        b->back[c][r] = (uint8_t)(i + 1);
    }
    return true;
}
int scriptBoardPeel(ScriptBoard *b, int col, int row)
{
    if (!b || !b->passLen || !inside(col, row) ||
        (b->state[col][row] != SCRIPT_TILE &&
         b->state[col][row] != SCRIPT_BURN))
        return -1;
    uint8_t index = b->back[col][row];
    if (index && index > b->passLen) return -1;
    b->chainLen = 0;
    if (index) {
        // Discover this hidden pass symbol, then permanently expose its
        // actual Cyrillic glyph as a chainable, flame-stopping diamond.
        int i = index - 1;
        b->tile[col][row] = b->pass[i];
        b->state[col][row] = SCRIPT_DIAMOND;
        b->found[i] = 1;
        b->back[col][row] = 0;
        return index;
    }
    b->state[col][row] = SCRIPT_HOLE;
    return 0;
}
bool scriptBoardPassComplete(const ScriptBoard *b)
{
    if (!b || !b->passLen || b->passLen > WPP2_MAX_LEN) return false;
    for (unsigned i = 0; i < b->passLen; i++)
        if (!b->found[i]) return false;
    return true;
}
