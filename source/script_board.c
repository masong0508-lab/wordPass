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
// The clue counts the six shifted-hex neighbors of a fixed back-layer
// coordinate, regardless of where the falling front gem has moved. Found
// diamonds count once even after their back index has been cleared by peel.
// Do not call this for a hidden location: only scriptBoardVisibleClue decides
// whether an on-screen count is allowed to be shown.
static int nearbyPasses(const ScriptBoard *b, int c, int r)
{
    int count = 0;
    for (int dc = -1; dc <= 1; dc++)
        for (int dr = -1; dr <= 1; dr++) {
            int cc = c + dc, rr = r + dr;
            if (!inside(cc, rr) || !adjacent(c, r, cc, rr)) continue;
            if (b->back[cc][rr] || b->state[cc][rr] == SCRIPT_DIAMOND) count++;
        }
    return count;
}

int scriptBoardVisibleClue(const ScriptBoard *b, int c, int r)
{
    if (!b || !inside(c, r)) return -1;
    if (b->state[c][r] != SCRIPT_HOLE &&
        !(b->surveyed[c] & (1u << r))) return -1;
    return nearbyPasses(b, c, r);
}

int scriptBoardClueInference(const ScriptBoard *b, int c, int r,
                             int *remaining, int *covered)
{
    if (remaining) *remaining = 0;
    if (covered) *covered = 0;
    int total = scriptBoardVisibleClue(b, c, r);
    if (total < 0 || total > 6) return SCRIPT_CLUE_UNSEEN;
    int found = 0, unknown = 0;
    for (int dc = -1; dc <= 1; dc++)
        for (int dr = -1; dr <= 1; dr++) {
            int cc = c + dc, rr = r + dr;
            if (!inside(cc, rr) || !adjacent(c, r, cc, rr)) continue;
            int state = b->state[cc][rr];
            if (state == SCRIPT_DIAMOND) found++;
            else if (state != SCRIPT_HOLE) unknown++;
        }
    int left = total - found;
    if (remaining) *remaining = left;
    if (covered) *covered = unknown;
    if (left < 0 || left > unknown || unknown == 0) return SCRIPT_CLUE_UNKNOWN;
    if (left == 0) return SCRIPT_CLUE_SAFE;
    if (left == unknown) return SCRIPT_CLUE_PASS;
    return SCRIPT_CLUE_UNKNOWN;
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
// Compact plain tiles separately between permanent holes, diamonds and
// flames. SCRIPT_EMPTY is a *temporary* vacancy, unlike a peeled hole.
// Position-anchored hidden password indexes never slide with the tiles.
// Each vertical segment refills from the fixed 100-symbol bag in O(7*8).
static bool compact(ScriptBoard *b, const V4Tiles *tiles)
{
    for (int c = 0; c < SCRIPT_COLS; c++) {
        int dst = SCRIPT_ROWS - 1;
        for (int r = SCRIPT_ROWS - 1; r >= -1; r--) {
            int state = r >= 0 ? b->state[c][r] : -1;
            bool floor = r < 0 || state == SCRIPT_HOLE ||
                         state == SCRIPT_DIAMOND || state == SCRIPT_BURN;
            if (floor) {
                for (int fill = dst; fill > r; fill--) {
                    uint8_t sym;
                    if (!take(b, tiles, &sym)) return false;
                    b->tile[c][fill] = sym;
                    b->state[c][fill] = SCRIPT_TILE;
                }
                dst = r - 1;
            } else if (state == SCRIPT_TILE) {
                b->tile[c][dst] = b->tile[c][r];
                b->state[c][dst] = SCRIPT_TILE;
                dst--;
            } // SCRIPT_EMPTY is dropped, then replaced at the segment top
        }
    }
    return true;
}
int scriptBoardSubmit(ScriptBoard *b, const V4Dict *dict,
                      const V4Tiles *tiles, int *tier)
{
    int pts = preview(b, dict, tiles, tier);
    if (!pts) return 0;
    // A successful word earns one fixed location's numbered survey, even if
    // gravity replaces the gem at its tail. Invalid words publish nothing.
    const int tail = b->chainLen - 1;
    const int clueC = b->chainC[tail], clueR = b->chainR[tail];
    for (unsigned i = 0; i < b->chainLen; i++) {
        int c = b->chainC[i], r = b->chainR[i];
        if (b->state[c][r] == SCRIPT_TILE || b->state[c][r] == SCRIPT_BURN)
            b->state[c][r] = SCRIPT_EMPTY;
    }
    if (!compact(b, tiles)) return 0;
    b->surveyed[clueC] |= (uint8_t)(1u << clueR);
    b->clueC = (uint8_t)clueC;
    b->clueR = (uint8_t)clueR;
    b->chainLen = 0;
    return pts;
}
// Start a flame at the highest unprotected tile, favoring columns without
// existing fire. This keeps early turns spread across the board.
bool scriptBoardIgnite(ScriptBoard *b, uint32_t turnSeed)
{
    if (!b) return false;
    int start = (int)(nextSeed(turnSeed ? turnSeed : b->seed) % SCRIPT_COLS);
    for (int pass = 0; pass < 2; pass++)
        for (int step = 0; step < SCRIPT_COLS; step++) {
            int c = (start + step) % SCRIPT_COLS;
            bool burning = false;
            for (int r = 0; r < SCRIPT_ROWS; r++)
                if (b->state[c][r] == SCRIPT_BURN) burning = true;
            if (pass == 0 && burning) continue;
            for (int r = 0; r < SCRIPT_ROWS; r++)
                if (b->state[c][r] == SCRIPT_TILE) {
                    b->state[c][r] = SCRIPT_BURN;
                    b->chainLen = 0;
                    return true;
                }
        }
    return false;
}
// Each flame moves down by ONE playable tile per tick. Holes are chimneys,
// never destroyed or converted back into ordinary tiles; exposed password
// diamonds and other flames stop movement. A bottom flame, or one escaping
// past the bottom through empty cells, ends the shelf.
bool scriptBoardFireStep(ScriptBoard *b, const V4Tiles *tiles)
{
    if (!b || !tiles || !tiles->valid) return false;
    bool moved[SCRIPT_COLS][SCRIPT_ROWS] = {{false}};
    for (int c = 0; c < SCRIPT_COLS; c++)
        if (b->state[c][SCRIPT_ROWS - 1] == SCRIPT_BURN) return true;
    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = SCRIPT_ROWS - 2; r >= 0; r--) {
            if (b->state[c][r] != SCRIPT_BURN || moved[c][r]) continue;
            int rr = r + 1;
            while (rr < SCRIPT_ROWS &&
                   (b->state[c][rr] == SCRIPT_HOLE ||
                    b->state[c][rr] == SCRIPT_EMPTY)) rr++;
            if (rr >= SCRIPT_ROWS) return true;
            if (b->state[c][rr] != SCRIPT_TILE) continue;
            // Secret locations remain fixed to board coordinates, so a later
            // peel of a burning pass spot can be recognized as scorched.
            b->state[c][r] = SCRIPT_EMPTY;
            b->state[c][rr] = SCRIPT_BURN;
            moved[c][rr] = true;
        }
    return !compact(b, tiles);
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
        !v4PoolsMatches(pools, dict) || !pools->themes ||
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

// The tiny engineering word pools do not guarantee that the random 56-tile
// board contains a playable Cyrillic word. The original preview solved that
// by stamping ПРИВЕТ / ПРИВІТ into column zero on every shelf. Instead plant
// a real pool word selected by this seed and shelf theme, while keeping all
// hidden pass cells (bottom three rows) and the board's tile bag intact.
// The chosen 4..6-symbol path is guaranteed geometrically adjacent and safe
// for the six-symbol hint solver. All loops and buffers have fixed bounds.
// Shared bounded word planter. A newly generated shelf can request two
// distinct opening words on disjoint columns, without touching either secret
// locations or the persistent 100-letter bag. Rescue remains a SINGLE word.
// "reserved" is an optional 7-byte stack-only column mask. On success, a
// seeded word reserves each column it uses, so clearing/refilling that word
// cannot destroy the other seeded word in another column.
static bool plantThemeWord(ScriptBoard *b, const V4Dict *dict,
                           const V4Tiles *tiles, const V4Pools *pools,
                           uint32_t seed, uint8_t reserved[SCRIPT_COLS],
                           const uint8_t avoid[WPP2_MAX_LEN], int avoidLen,
                           uint8_t planted[WPP2_MAX_LEN], int *plantedLen,
                           uint8_t pathC[6], uint8_t pathR[6])
{
    if (!b || !b->passLen || b->passLen > WPP2_MAX_LEN ||
        !sameAlphabet(dict, tiles) || !pools || !pools->image ||
        !v4PoolsMatches(pools, dict) || !pools->themes ||
        b->passTheme >= pools->themes) return false;

    uint32_t choice = nextSeed(seed ^ 0x7f4a7c15u ^
                               ((uint32_t)b->passTheme * 0x9e3779b9u));
    const int preferredCol = (int)((choice >> 5) % SCRIPT_COLS);
    const int preferredReverse = (int)((choice >> 10) & 1u);

    // Prefer the active theme; use a nearby theme only when its draft pool
    // offers no short playable word. Never create a synthetic dictionary word.
    for (int step = 0; step < pools->themes; step++) {
        int theme = (b->passTheme + step) % pools->themes;
        for (int lengthStep = 0; lengthStep < 3; lengthStep++) {
            int length = 4 + (int)((choice + (uint32_t)lengthStep) % 3u);
            int count = v4PoolsCount(pools, theme, length);
            if (count <= 0) continue;
            int first = (int)((choice >> 16) % (unsigned)count);
            for (int attempt = 0; attempt < count; attempt++) {
                int pick = (first + attempt) % count;
                uint8_t ids[WPP2_MAX_LEN];
                if (!v4PoolsWord(pools, theme, length, pick, ids) ||
                    !v4DictLookupIds(dict, ids, length)) continue;
                // Never make the hidden pass itself the obvious opening
                // word when alternate choices exist in the pool.
                if (count > 1 && length == b->passLen &&
                    memcmp(ids, b->pass, (size_t)length) == 0) continue;
                // Even an extremely small pack must not seed the same word
                // twice. The second path is optional when none other exists.
                if (avoid && length == avoidLen &&
                    memcmp(ids, avoid, (size_t)length) == 0) continue;
                // Both paths are anchored at deterministic hex coordinates;
                // no mutation occurs until all candidate cells are legal.
                for (int columnTry = 0; columnTry < SCRIPT_COLS; columnTry++) {
                    int col = (preferredCol + columnTry) % SCRIPT_COLS;
                    int beside = col == 0 ? 1 :
                                 col == SCRIPT_COLS - 1 ? col - 1 :
                                 ((choice >> 13) & 1u) ? col - 1 : col + 1;
                    for (int dirTry = 0; dirTry < 2; dirTry++) {
                        int reverse = preferredReverse ^ dirTry;
                        bool eligible = true;
                        for (int i = 0; i < length; i++) {
                            int order = reverse ? length - 1 - i : i;
                            int c = order == 5 ? beside : col;
                            int r = order == 5 ? 4 : order;
                            if (ids[i] >= tiles->symbols ||
                                !tiles->weight[ids[i]] ||
                                b->state[c][r] != SCRIPT_TILE ||
                                b->back[c][r] || (reserved && reserved[c])) {
                                eligible = false;
                                break;
                            }
                        }
                        if (!eligible) continue;
                        for (int i = 0; i < length; i++) {
                            int order = reverse ? length - 1 - i : i;
                            int c = order == 5 ? beside : col;
                            int r = order == 5 ? 4 : order;
                            b->tile[c][r] = ids[i];
                            if (reserved) reserved[c] = 1;
                            if (pathC) pathC[i] = (uint8_t)c;
                            if (pathR) pathR[i] = (uint8_t)r;
                        }
                        if (planted) memcpy(planted, ids, (size_t)length);
                        if (plantedLen) *plantedLen = length;
                        return true;
                    }
                }
            }
        }
    }
    return false;  // fail closed; never invent words or overwrite secrets
}

bool scriptBoardPlantThemeWord(ScriptBoard *b, const V4Dict *dict,
                               const V4Tiles *tiles, const V4Pools *pools,
                               uint32_t seed)
{
    // Single-word rescue retains its original behavior and heat cost.
    return plantThemeWord(b, dict, tiles, pools, seed, NULL, NULL, 0,
                          NULL, NULL, NULL, NULL);
}

int scriptBoardPlantOpeningWords(ScriptBoard *b, const V4Dict *dict,
                                  const V4Tiles *tiles, const V4Pools *pools,
                                  uint32_t seed,
                                  uint8_t pathC[2][6], uint8_t pathR[2][6],
                                  uint8_t lengths[2])
{
    // This is called only at shelf creation, never per frame or on rescue.
    // No extra board fields, static arrays, or dynamic allocations are used.
    uint8_t reserved[SCRIPT_COLS] = {0};
    uint8_t firstWord[WPP2_MAX_LEN] = {0};
    int firstLen = 0;
    if (lengths) lengths[0] = lengths[1] = 0;
    if (!plantThemeWord(b, dict, tiles, pools, seed, reserved, NULL, 0,
                        firstWord, &firstLen,
                        pathC ? pathC[0] : NULL, pathR ? pathR[0] : NULL)) return 0;
    if (lengths) lengths[0] = (uint8_t)firstLen;
    // Preserve the first word's path by reserving whole columns. Its
    // submit/refill cannot disturb a second opening chain in another column.
    int secondLen = 0;
    if (plantThemeWord(b, dict, tiles, pools, seed ^ 0x9e3779b9u, reserved,
                       firstWord, firstLen, NULL, &secondLen,
                       pathC ? pathC[1] : NULL, pathR ? pathR[1] : NULL)) {
        if (lengths) lengths[1] = (uint8_t)secondLen;
        return 2;
    }
    return 1;  // sparse packs remain playable with at least one legal word
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
    // As in Classic, a burning tile (or a flame immediately above it)
    // scorches the hidden letter: a fatal guess rather than a free reveal.
    if (b->state[col][row] == SCRIPT_BURN ||
        (row > 0 && b->state[col][row - 1] == SCRIPT_BURN)) {
        b->state[col][row] = SCRIPT_HOLE;
        return index ? -2 : 0;
    }
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

// Bounded DFS: unlike a naive 56*(6^6) brute force, reject impossible
// Unicode prefixes via WPD4 binary searches. This is run ONLY when the
// user presses R, never from the frame renderer. A 12k-node limit protects
// the original DS's single-core 60 Hz budget on large imported word lists.
typedef struct {
    const ScriptBoard *b;
    const V4Dict *dict;
    const V4Tiles *tiles;
    uint8_t pathC[SCRIPT_HINT_MAX], pathR[SCRIPT_HINT_MAX];
    bool used[SCRIPT_COLS][SCRIPT_ROWS];
    int target, nodes;
} HintSearch;
// The parent call supplies exactly the WPD4 record interval sharing its
// prefix. Carry it into the recursion so each new letter searches only that
// narrowed span. The leaf interval itself proves a complete dictionary word.
// No per-frame work, heap, persistent cache or change to the 12k-node cap.
static bool hintWalk(HintSearch *h, int col, int row, int depth,
                     uint32_t lo, uint32_t hi)
{
    if (h->nodes >= 12000 || !inside(col, row) || h->used[col][row])
        return false;
    int state = h->b->state[col][row];
    int id = h->b->tile[col][row];
    if ((state != SCRIPT_TILE && state != SCRIPT_BURN &&
         state != SCRIPT_DIAMOND) ||
        id >= h->tiles->symbols || !h->tiles->weight[id])
        return false;
    h->nodes++;
    // A branch that cannot match even one dictionary word stops before
    // touching the traversal stack. Invalid IDs/ranges fail closed.
    if (!v4DictNarrowPrefixLengthId(h->dict, h->target, depth,
                                    (uint8_t)id, &lo, &hi)) return false;
    h->used[col][row] = true;
    h->pathC[depth] = (uint8_t)col;
    h->pathR[depth] = (uint8_t)row;
    int n = depth + 1;
    if (n == h->target) return true;
    int nc[6] = {col, col, col - 1, col - 1, col + 1, col + 1};
    int nr[6] = {row + 1, row - 1, row, row + ((col & 1) ? 1 : -1),
                 row, row + ((col & 1) ? 1 : -1)};
    for (int i = 0; i < 6; i++)
        if (hintWalk(h, nc[i], nr[i], n, lo, hi)) return true;
    h->used[col][row] = false;
    return false;
}
int scriptBoardHint(const ScriptBoard *b, const V4Dict *dict,
                    const V4Tiles *tiles, uint8_t outC[SCRIPT_HINT_MAX],
                    uint8_t outR[SCRIPT_HINT_MAX])
{
    if (!b || !sameAlphabet(dict, tiles) || !outC || !outR) return 0;
    for (int length = SCRIPT_HINT_MAX; length >= WPD4_MIN_WORD; length--) {
        if (!dict->count[length]) continue;
        HintSearch search;
        memset(&search, 0, sizeof search);
        search.b = b; search.dict = dict; search.tiles = tiles;
        search.target = length;
        for (int c = 0; c < SCRIPT_COLS; c++)
            for (int r = 0; r < SCRIPT_ROWS; r++)
                if (hintWalk(&search, c, r, 0, 0u, dict->count[length])) {
                    memcpy(outC, search.pathC, (size_t)length);
                    memcpy(outR, search.pathR, (size_t)length);
                    return length;
                }
    }
    return 0;
}
