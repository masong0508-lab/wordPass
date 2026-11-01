// Board logic: hex-column grid, chaining, words, gravity, fire, peeling.
#include "game.h"
#include <stdlib.h>
#include <string.h>

// Pass pools live in data/pools.bin (pools.c): 40 themes and about 18,000 words. Shelf n draws from
// theme (n - 1) % 40, the first eight being the original bookish shelves. Each pool runs from
// familiar to obscure, and the window of it a shelf may draw from widens as the shelves climb.
static const char *const TWIST_NAMES[4] = { "", "INK SPILL", "DAMP", "BURIED" };

const char *boardPassTheme(const Game *g) { return poolName(g->passTheme); }
const char *boardTwistName(int twist) { return TWIST_NAMES[(twist >= 0 && twist < 4) ? twist : 0]; }

// ---- board RNG: xorshift32, seeded per shelf (from the Daily date or libc rand()). Every
// board decision draws from it and nothing cosmetic does, so a Daily board, its refills and
// its fire are identical for everyone whatever the frame timing of sparks and shakes.
static u32 brng = 0x9E3779B9u;

u32 boardRand(void)
{
    u32 x = brng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    brng = x;
    return x;
}

void boardSeed(u32 seed) { brng = seed ? seed : 0x9E3779B9u; }

// Bookworm-style letter weights (Q stands for Qu); they sum to 102
static const u8 LETTER_WEIGHT[26] = {
    /*a*/9,/*b*/2,/*c*/3,/*d*/4,/*e*/12,/*f*/2,/*g*/3,/*h*/3,/*i*/9,/*j*/1,
    /*k*/1,/*l*/4,/*m*/3,/*n*/6,/*o*/8,/*p*/2,/*q*/1,/*r*/6,/*s*/5,/*t*/6,
    /*u*/4,/*v*/2,/*w*/2,/*x*/1,/*y*/2,/*z*/1
};

int letterValue(int l)
{
    static const u8 v[26] = {1,3,3,2,1,4,2,4,1,8,5,1,3,1,1,3,10,1,1,1,1,4,4,8,4,10};
    return v[l];
}

static bool isVowel(int l) { return l == 0 || l == 4 || l == 8 || l == 14 || l == 20; }

// ---- letter bag: 100 letters per cycle drawn without replacement, so a shelf's letter mix
// tracks the weights instead of drifting. J, Q, X and Z share two slots per cycle in
// rotation (rareRot picks the pair), so a bag never holds more than two rare letters.
#define BAG_N 100
static u8 bag[BAG_N];
static int bagPos = BAG_N;
static int rareRot;

static int rareIndex(int l) { return l == 9 ? 0 : l == 16 ? 1 : l == 23 ? 2 : l == 25 ? 3 : -1; }

static void refillBag(void)
{
    int n = 0;
    for (int l = 0; l < 26; l++) {
        int w = LETTER_WEIGHT[l], k = rareIndex(l);
        if (k >= 0) w = (k == (rareRot & 3) || k == ((rareRot + 1) & 3)) ? 1 : 0;
        for (int i = 0; i < w && n < BAG_N; i++) bag[n++] = (u8)l;
    }
    while (n < BAG_N) bag[n++] = 4;             // cannot happen with the weights above (102 - 4 + 2)
    rareRot = (rareRot + 2) & 3;
    for (int i = BAG_N - 1; i > 0; i--) {       // Fisher-Yates
        int j = (int)(boardRand() % (u32)(i + 1));
        u8 t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bagPos = 0;
}

static int randomLetter(void)
{
    if (bagPos >= BAG_N) refillBag();
    return bag[bagPos++];
}

void boardBagReset(void) { bagPos = BAG_N; }
int  boardRandomLetter(void) { return randomLetter(); }

int boardNeighbors(int c, int r, int outC[6], int outR[6])
{
    // odd columns are shifted down
    static const s8 evenD[6][2] = {{0,-1},{0,1},{-1,-1},{-1,0},{1,-1},{1,0}};
    static const s8 oddD[6][2]  = {{0,-1},{0,1},{-1,0},{-1,1},{1,0},{1,1}};
    const s8 (*d)[2] = (c & 1) ? oddD : evenD;
    int n = 0;
    for (int i = 0; i < 6; i++) {
        int nc = c + d[i][0], nr = r + d[i][1];
        if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS) continue;
        outC[n] = nc; outR[n] = nr; n++;
    }
    return n;
}

static bool adjacent(int c1, int r1, int c2, int r2)
{
    int nc[6], nr[6];
    int n = boardNeighbors(c1, r1, nc, nr);
    for (int i = 0; i < n; i++) if (nc[i] == c2 && nr[i] == r2) return true;
    return false;
}

bool boardIsChainable(const Game *g, int c, int r)
{
    int s = g->cell[c][r].state;
    return s == CELL_TILE || s == CELL_BURN || s == CELL_SECRET;
}

int boardBackNumber(const Game *g, int c, int r)
{
    int nc[6], nr[6], n = boardNeighbors(c, r, nc, nr), k = 0;
    for (int i = 0; i < n; i++) if (g->back[nc[i]][nr[i]]) k++;
    return k;
}

// Bury the pass: a random walk that starts at or below minRow and never climbs above it,
// so the whole pass sits in rows minRow..ROWS-1 (the fallback for a stuck walk keeps to
// the same rows).
static void placeSecrets(Game *g, int minRow)
{
    memset(g->back, 0, sizeof(g->back));
    int placed = 0, guard = 0;
    int c = boardRand() % COLS, r = minRow + boardRand() % (ROWS - minRow);
    while (placed < g->passLen && guard++ < 2000) {
        if (!g->back[c][r]) {
            g->back[c][r] = placed + 1;
            placed++;
        }
        int nc[6], nr[6], n = boardNeighbors(c, r, nc, nr);
        int pick = boardRand() % n;
        if (nr[pick] < minRow) continue;
        c = nc[pick]; r = nr[pick];
    }
    for (int i = placed; i < g->passLen; i++) {
        for (int t = 0; t < 500; t++) {
            int cc = boardRand() % COLS, rr = minRow + boardRand() % (ROWS - minRow);
            if (!g->back[cc][rr]) { g->back[cc][rr] = i + 1; break; }
        }
    }
}

// The Daily modifier: a hash of the date, so every player gets the same one. STANDARD changes
// nothing; EMBER lights the fire sooner and keeps it moving every turn, DEEP buries the pass
// in the bottom rows, LONG asks for a seven-letter pass.
int boardDailyMod(int seed)
{
    return (int)(((u32)((u32)seed * 2654435761u) >> 28) & 3);
}

const char *boardDailyModName(int mod)
{
    static const char *const names[DAILYMOD_COUNT] = { "STANDARD", "EMBER", "DEEP", "LONG" };
    return (mod >= 0 && mod < DAILYMOD_COUNT) ? names[mod] : "";
}

// Pick the shelf's pass from its theme pool. Classic: the target length climbs with the
// shelf (4, 5, 6, 7, 8), the pool's nearest length stands in when a pool lacks it (the
// longer neighbour on ties), and the last eight passes are avoided while that leaves a
// choice. Daily: theme and length come from a hash of the date, no RNG draws.
static int passTargetLen(int shelf)
{
    return shelf <= 2 ? 4 : shelf <= 5 ? 5 : shelf <= 8 ? 6 : shelf <= 12 ? 7 : 8;
}

// How many words from the front of a pool (the familiar end) a shelf may pick from.
static int passWindow(int shelf) { return 48 + 24 * shelf; }

static int nearestPoolLen(int theme, int want)
{
    int best = -1;
    for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN; len++) {
        if (!poolCount(theme, len)) continue;
        if (len == want) return want;
        int d = len > want ? len - want : want - len, bd = best > want ? best - want : want - best;
        if (best < 0 || d < bd || (d == bd && len > best)) best = len;
    }
    return best;
}

static bool recentHas(const Game *g, int theme, int len, int idx)
{
    u32 key = PASS_KEY(theme, len, idx);
    for (int i = 0; i < 8; i++) if (g->recentPass[i] == key) return true;
    return false;
}

static void pickPass(Game *g, int extraLen)
{
    const int themes = poolThemeCount();
    int theme, idx, len, n;
    if (g->mode == MODE_DAILY) {
        u32 hash = (u32)((u32)g->dailySeed * 2654435761u) >> 8;
        theme = hash % themes;
        len = nearestPoolLen(theme, g->dailyMod == DAILYMOD_LONG ? 7 : (hash & 8) ? 6 : 5);
        n = poolCount(theme, len);
        if (n > 200) n = 200;      // Daily stays on familiar words
        idx = hash % n;
    } else {
        theme = (g->shelf - 1) % themes;
        len = nearestPoolLen(theme, passTargetLen(g->shelf) + extraLen);
        n = poolCount(theme, len);
        if (n > passWindow(g->shelf)) n = passWindow(g->shelf);
        int fresh = 0;
        for (int i = 0; i < n; i++) if (!recentHas(g, theme, len, i)) fresh++;
        bool any = fresh > 0;    // every word of this length was used lately: length wins over variety
        int pick = boardRand() % (any ? fresh : n);
        idx = 0;
        for (int i = 0; i < n; i++) {
            if (any && recentHas(g, theme, len, i)) continue;
            if (pick-- == 0) { idx = i; break; }
        }
        g->recentPass[g->recentHead & 7] = PASS_KEY(theme, len, idx);
        g->recentHead = (g->recentHead + 1) & 7;
    }
    g->passTheme = theme;
    poolWord(theme, len, idx, g->pass);
    g->passLen = len;
}

// INK SPILL: three holes in rows 2..6 over empty back cells, as if peeled before you came.
static void spillInk(Game *g)
{
    for (int k = 0, guard = 0; k < 3 && guard < 200; guard++) {
        int c = boardRand() % COLS, r = 2 + boardRand() % 5;
        if (g->back[c][r] || g->cell[c][r].state == CELL_HOLE) continue;
        g->cell[c][r].state = CELL_HOLE;
        g->cell[c][r].yoff = 0; g->cell[c][r].yv = 0;
        k++;
    }
}

void boardNewShelf(Game *g)
{
    boardSeed(g->mode == MODE_DAILY ? (u32)g->dailySeed : (u32)rand());
    if (g->mode == MODE_DAILY) g->dailyMod = boardDailyMod(g->dailySeed);   // before any boardRand() draw
    // twists: Classic shelves 4, 7, 10 ... get one of three, never the same one twice running
    g->twist = TWIST_NONE;
    if (g->mode == MODE_CLASSIC && g->shelf >= 4 && g->shelf % 3 == 1) {
        g->twist = 1 + boardRand() % 3;
        if (g->twist == g->lastTwist) g->twist = 1 + boardRand() % 3;
        g->lastTwist = g->twist;
    }
    pickPass(g, g->twist == TWIST_BURIED ? 1 : 0);
    memset(g->found, 0, sizeof(g->found));
    memset(g->scorched, 0, sizeof(g->scorched));
    // deeper shelves bury the pass lower in the board
    int minRow = (g->shelf <= 2) ? 1 : (g->shelf <= 5) ? 3 : (g->shelf <= 11) ? 4 : 5;
    if (g->mode == MODE_DAILY) minRow = g->dailyMod == DAILYMOD_DEEP ? 5 : 3;
    if (g->twist == TWIST_BURIED) minRow = 5;
    rareRot = boardRand() & 3;
    for (int attempt = 0; attempt < 10; attempt++) {
        boardBagReset();
        for (int c = 0; c < COLS; c++)
            for (int r = 0; r < ROWS; r++) {
                g->cell[c][r].letter = randomLetter();
                g->cell[c][r].state = CELL_TILE;
                g->cell[c][r].yoff = (-(ROWS - r) * TILE_H - 20 - (int)(boardRand() % 30) - c * 6) * 16;
                g->cell[c][r].yv = 0;
                g->cell[c][r].squash = 0;
                g->cell[c][r].flash = 0;
            }
        placeSecrets(g, minRow);
        if (g->twist != TWIST_INK) break;
        spillInk(g);
        if (boardHasWords(g)) break;      // the spill left a word: keep this board
    }
    g->chainLen = 0;
    g->turn = 0;
    g->heat = 0;
    g->overtime = false;
    g->fireN = 0;
    g->lockdown = g->shelf > 5;
    // Lockdown tiers: shelves 6-8, 9-11, 12-14, 15+ tighten the threshold and the pulse
    g->lockTier = g->lockdown ? (g->shelf - 6) / 3 : 0;
    if (g->lockTier > 3) g->lockTier = 3;
    g->heatThreshold = (g->shelf <= 2) ? 10 : (g->shelf <= 5) ? 7 : 5 - g->lockTier / 2;
    if (g->heatThreshold < 3) g->heatThreshold = 3;
    g->burnEvery = (g->shelf <= 3) ? 2 : 1;
    g->pulseEvery = 6 - g->lockTier;
    if (g->pulseEvery < 3) g->pulseEvery = 3;
    g->burnTimer = 0;
    g->fail = FAIL_NONE;
    g->wordsThisShelf = 0;
    g->shelfStartScore = g->score;
    g->lastWord[0] = 0;
    g->msg[0] = 0;
    g->bestWord[0] = 0;
    g->bestWordScore = 0;
    g->peels = 0;
    g->lastWordTier = 0;
    g->rareWords = 0;
    g->ledgerN = 0; g->ledgerHead = 0; g->uniqueWords = 0;
    g->lastWordDiamonds = 0; g->lastWordRepeat = 0; g->lastWordBurn = 0;
    g->speedBonus = 0; g->peelBonus = 0; g->passBonusKind = 0;
    if (g->mode == MODE_DAILY) {
        // one fixed shelf for everyone today: medium pressure, pass buried mid-board
        g->lockdown = false;
        g->lockTier = 0;
        g->pulseEvery = 6;
        g->heatThreshold = 7;
        g->burnEvery = 2;
        if (g->dailyMod == DAILYMOD_EMBER) { g->burnEvery = 1; g->heatThreshold = 8; }
    } else if (S.fire == FIRE_RELAXED) {
        g->burnEvery += 1;
        g->heatThreshold += 3;
    } else if (S.fire == FIRE_FAST) {
        g->burnEvery = 1;
        g->heatThreshold -= 2;
        if (g->heatThreshold < 4) g->heatThreshold = 4;
    }
    if (g->twist == TWIST_DAMP) {   // a damp shelf: the fire creeps slower but catches sooner
        g->burnEvery += 1;
        g->heatThreshold -= 2;
        if (g->heatThreshold < 3) g->heatThreshold = 3;
    }
}

static int chainIndexOf(const Game *g, int c, int r)
{
    for (int i = 0; i < g->chainLen; i++)
        if (g->chainC[i] == c && g->chainR[i] == r) return i;
    return -1;
}

// The one cell adjacent to both the chain's tail and (c, r) that a skipped step would have crossed:
// chainable and not already in the chain. Returns false unless there is exactly one.
static bool bridgeCell(const Game *g, int c, int r, int *bc, int *br)
{
    int tc = g->chainC[g->chainLen - 1], tr = g->chainR[g->chainLen - 1];
    int nc[6], nr[6], n = boardNeighbors(tc, tr, nc, nr), found = 0;
    for (int i = 0; i < n; i++) {
        if (!boardIsChainable(g, nc[i], nr[i]) || chainIndexOf(g, nc[i], nr[i]) >= 0) continue;
        if (!adjacent(nc[i], nr[i], c, r)) continue;
        *bc = nc[i]; *br = nr[i]; found++;
    }
    return found == 1;
}

// One touch sample on gem (c, r). `fresh` is a new stylus-down (a tap), false while dragging:
// a tap may cut the chain back, restart it or submit it; a drag never restarts mid-stroke.
enum ChainTouch boardChainTouch(Game *g, int c, int r, bool fresh)
{
    if (!boardIsChainable(g, c, r)) return CT_IGNORED;
    if (g->chainLen == 0) {
        g->chainC[0] = c; g->chainR[0] = r; g->chainLen = 1;
        return CT_ADDED;
    }
    int lc = g->chainC[g->chainLen - 1], lr = g->chainR[g->chainLen - 1];
    if (lc == c && lr == r) return (fresh && g->chainLen >= 3) ? CT_SUBMIT_TAP : CT_IGNORED;
    // backtrack onto the previous tile
    if (g->chainLen >= 2 && g->chainC[g->chainLen - 2] == c && g->chainR[g->chainLen - 2] == r) {
        g->chainLen--;
        return CT_BACKTRACKED;
    }
    int at = chainIndexOf(g, c, r);
    if (at >= 0) {                      // an earlier gem: a tap cuts the chain back to it
        if (!fresh) return CT_IGNORED;
        g->chainLen = at + 1;
        return CT_TRUNCATED;
    }
    if (adjacent(lc, lr, c, r)) {
        if (g->chainLen >= MAX_CHAIN) return CT_IGNORED;
        g->chainC[g->chainLen] = c; g->chainR[g->chainLen] = r; g->chainLen++;
        return CT_ADDED;
    }
    int bc, br;
    if (g->chainLen + 2 <= MAX_CHAIN && bridgeCell(g, c, r, &bc, &br)) {   // a fast stylus skipped one gem
        g->chainC[g->chainLen] = bc; g->chainR[g->chainLen] = br;
        g->chainC[g->chainLen + 1] = c; g->chainR[g->chainLen + 1] = r;
        g->chainLen += 2;
        return CT_BRIDGED;
    }
    if (fresh) {                        // a tap elsewhere starts a new chain
        g->chainC[0] = c; g->chainR[0] = r; g->chainLen = 1;
        return CT_RESTARTED;
    }
    return CT_NOT_ADJACENT;
}

// A drag sample: true if the gem was added, backtracked or bridged (the older rule-test entry point).
bool boardTryAddToChain(Game *g, int c, int r)
{
    enum ChainTouch t = boardChainTouch(g, c, r, false);
    return t == CT_ADDED || t == CT_BACKTRACKED || t == CT_BRIDGED;
}

void boardClearChain(Game *g) { g->chainLen = 0; }

void boardChainWord(const Game *g, char *out)
{
    int n = 0;
    for (int i = 0; i < g->chainLen; i++) {
        int l = g->cell[g->chainC[i]][g->chainR[i]].letter;
        out[n++] = 'a' + l;
        if (l == 'q' - 'a') out[n++] = 'u';
    }
    out[n] = 0;
}

bool boardPassComplete(const Game *g)
{
    for (int i = 0; i < g->passLen; i++) if (!g->found[i]) return false;
    return true;
}

// Gravity with separators: holes, diamonds and burning gems are floors that split a
// column into segments. Gems compact within their segment and every segment refills
// from its top, so the gems under a hole come back too (they drop in from behind the
// separator: the renderer clips them to their segment). A flame hangs where it is, so
// clearing the gems beneath it only refills them: fire moves one row per fire step and
// never rides your own word down.
static bool gravityFloor(int s) { return s == CELL_HOLE || s == CELL_SECRET || s == CELL_BURN; }

static int applyGravity(Game *g)
{
    int spawned = 0;
    for (int c = 0; c < COLS; c++) {
        int w = ROWS - 1;
        for (int r = ROWS - 1; r >= 0; r--) {
            int s = g->cell[c][r].state;
            if (gravityFloor(s)) { w = r - 1; continue; }
            if (s == CELL_EMPTY) continue;
            if (w != r) {
                Cell moved = g->cell[c][r];
                moved.yoff -= (w - r) * TILE_H * 16;
                g->cell[c][w] = moved;
                g->cell[c][r].state = CELL_EMPTY;
            }
            w--;
        }
        // refill every segment: after compaction the empties sit at each segment's top
        int segStart = 0, colSpawned = 0, lowest = -1;
        bool vowel = false;
        for (int r = 0; r <= ROWS; r++) {
            if (r < ROWS && !gravityFloor(g->cell[c][r].state)) continue;
            int k = 0;
            for (int rr = r - 1; rr >= segStart; rr--) {
                if (g->cell[c][rr].state != CELL_EMPTY) continue;
                g->cell[c][rr].letter = randomLetter();
                g->cell[c][rr].state = CELL_TILE;
                g->cell[c][rr].yoff = (-(k + 1) * TILE_H - 12 - (rr == 0 ? 0 : 4)) * 16;
                g->cell[c][rr].yv = 0;
                g->cell[c][rr].squash = 0;
                g->cell[c][rr].flash = 0;
                if (isVowel(g->cell[c][rr].letter)) vowel = true;
                if (rr > lowest) lowest = rr;
                k++; spawned++; colSpawned++;
            }
            segStart = r + 1;
        }
        // vowel floor: a column refilled with three or more consonants and nothing else gets
        // a vowel in its lowest new gem, swapped with the next vowel in the bag so the
        // cycle's letter mix is unchanged (an empty bag tail leaves the column as it fell)
        if (colSpawned >= 3 && !vowel && lowest >= 0) {
            for (int i = bagPos; i < BAG_N; i++) {
                if (!isVowel(bag[i])) continue;
                u8 t = bag[i]; bag[i] = g->cell[c][lowest].letter; g->cell[c][lowest].letter = t;
                break;
            }
        }
    }
    return spawned;
}

// Ignite the topmost gem of a random column (holes and diamonds above it are skipped),
// trying every column from a random start. Returns false when no gem is left to light.
static bool igniteColumn(Game *g, int avoid)
{
    int start = boardRand() % COLS;
    for (int k = 0; k < COLS; k++) {
        int c = (start + k) % COLS;
        if (c == avoid) continue;
        for (int r = 0; r < ROWS; r++) {
            if (g->cell[c][r].state != CELL_TILE) continue;
            g->cell[c][r].state = CELL_BURN;
            g->cell[c][r].yoff = 0;
            g->cell[c][r].yv = 0;
            if (g->fireN < 2) { g->fireC[g->fireN] = c; g->fireR[g->fireN] = r; g->fireN++; }
            return true;
        }
    }
    return false;
}

// One new flame (two from Lockdown tier 2). The ignition cells are left in fireC/fireR
// for main.c's burst. Returns whether anything caught fire.
static bool spawnFire(Game *g)
{
    g->fireN = 0;
    if (!igniteColumn(g, -1)) return false;
    if (g->lockTier >= 2) igniteColumn(g, g->fireC[0]);
    return true;
}

// Each burning tile destroys the tile beneath and takes its place.
// Empty cells and holes are chimneys: fire drops straight through.
// Secrets are firebreaks. A flame already on the bottom row ends the game.
//
// Invariant: HOLE and SECRET cells are immutable except via boardPeel. Fire never
// assigns a whole Cell over one of them: a flame that is blocked (by a SECRET or another
// flame) or that falls out the bottom settles on the lowest EMPTY row between its own
// row and the blocker, and stays where it is when there is none. scorched[] therefore
// stays tied to holes that boardPeel made.
static void fireStep(Game *g)
{
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
            if (g->cell[c][r].state == CELL_BURN && r == ROWS - 1) { g->fail = FAIL_FIRE; return; }

    bool moved[COLS][ROWS] = {{false}};
    for (int c = 0; c < COLS; c++) {
        for (int r = ROWS - 1; r >= 0; r--) {
            if (g->cell[c][r].state != CELL_BURN || moved[c][r]) continue;
            int rr = r + 1;
            while (rr < ROWS && (g->cell[c][rr].state == CELL_EMPTY || g->cell[c][rr].state == CELL_HOLE)) rr++;
            if (rr < ROWS && g->cell[c][rr].state == CELL_TILE) {
                // TILE: burn it, move down onto it
                Cell f = g->cell[c][r];
                g->cell[c][r].state = CELL_EMPTY;
                f.letter = g->cell[c][rr].letter;
                g->cell[c][rr] = f;
                g->cell[c][rr].yoff = 0;
                moved[c][rr] = true;
                continue;
            }
            // blocked at rr (SECRET or BURN), or fell out the bottom (rr == ROWS): walk back up
            // to the nearest EMPTY row below r and settle there; holes are skipped, never overwritten
            int dest = -1;
            for (int k = rr - 1; k > r; k--)
                if (g->cell[c][k].state == CELL_EMPTY) { dest = k; break; }
            if (dest < 0) {
                if (rr >= ROWS) g->fail = FAIL_FIRE;   // nothing left to burn beneath it
                continue;                              // flame stays put
            }
            Cell f = g->cell[c][r];
            g->cell[c][r].state = CELL_EMPTY;
            g->cell[c][dest] = f;
            g->cell[c][dest].yoff = 0;
            moved[c][dest] = true;
        }
    }
}

// Test channel (debug.c poke 5): one fire advance and the gravity that follows it, with
// no turn, heat or word check, so rule tests can step the fire deterministically.
void boardDebugFireStep(Game *g)
{
    fireStep(g);
    applyGravity(g);
}

// Heat a word adds: short words feed the fire, seven letters and up are free.
int boardHeatFor(int len)
{
    return len <= 3 ? 3 : len == 4 ? 2 : len <= 6 ? 1 : 0;
}

// The heat the next turn adds in all: the move's own (`baseHeat`, boardHeatFor(len) for a word),
// the Lockdown pulse when this turn lands on one, and the overtime heat once the shelf is past
// its turn budget. endTurn books exactly this, and the panel previews it.
int boardTurnHeat(const Game *g, int baseHeat)
{
    int turn = g->turn + 1, heat = baseHeat;
    if (g->lockdown && g->pulseEvery > 0 && (turn % g->pulseEvery) == 0) heat += 2;   // Lockdown pulse
    int over = turn - (g->lockdown ? 16 : 24);                                        // shelf overtime
    if (over > 0) heat += 1 + over / 24;
    return heat;
}

// Every word and every peel is a turn. Order matters: the cells the turn vacated are
// refilled before the fire moves (no chimney through your own word), and a new flame is
// lit after the fire step, so it sits on its spawn row for a full burnEvery before it
// starts to creep.
static void endTurn(Game *g, int heatAdd)
{
    int add = boardTurnHeat(g, heatAdd);
    g->turn++;
    g->heat += add;
    g->overtime = g->turn > (g->lockdown ? 16 : 24);

    int spawned = applyGravity(g);
    g->burnTimer++;
    if (g->burnTimer >= g->burnEvery) {
        g->burnTimer = 0;
        fireStep(g);
        spawned += applyGravity(g);
    }
    if (g->fail == FAIL_NONE && g->heat >= g->heatThreshold) {
        if (spawnFire(g)) g->heat = 0;
        else g->heat = g->heatThreshold - 1;   // nothing left to light: stay on the brink
    }
    if (spawned >= 4) musicCascade(spawned);
    if (g->fail == FAIL_NONE && !boardHasWords(g)) g->fail = FAIL_WORDS;
}

// A word counts if the dictionary knows it, the LIBRARY setting allows its tier and the
// SPELLING setting allows its spelling (a word spelled the same everywhere always passes).
static int wordAccepted(const char *w, int len)
{
    int t = dictLookup(w, len);
    if (!t) return 0;
    if (S.library == LIB_COMMON && t == 2) return 0;
    if (S.spelling != SPELL_BOTH) {
        int v = dictSpelling(w, len);
        if (v && v != S.spelling) return 0;
    }
    return t;
}

// Which dialect rejected a dictionary word: 1 = American-only spelling under BRITISH, 2 = the
// other way round, 0 = the spelling is not the reason.
int boardWordSpelling(const char *w, int len)
{
    if (S.spelling == SPELL_BOTH || !dictLookup(w, len)) return 0;
    int v = dictSpelling(w, len);
    return (v && v != S.spelling) ? v : 0;
}

bool boardWordOk(const char *w, int len) { return wordAccepted(w, len) != 0; }
int  boardWordTier(const char *w, int len) { return dictLookup(w, len); }

// Case-insensitive compare of a chain word against a ledger entry (entries are stored
// lowercase; the compare tolerates either case so a poked or uppercase word still matches).
static bool sameWordCI(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

int ledgerIndex(const Game *g, int age)
{
    return (g->ledgerHead - 1 - age + 2 * LEDGER_N) % LEDGER_N;
}

static bool ledgerHas(const Game *g, const char *w)
{
    for (int i = 0; i < g->ledgerN; i++) if (sameWordCI(g->ledger[i], w)) return true;
    return false;
}

// The length multiplier of a word: x1 up to four letters, x2 for five and six, x3 for seven and
// eight, x4 beyond.
int boardLenMult(int len)
{
    return (len <= 4) ? 1 : (len <= 6) ? 2 : (len <= 8) ? 3 : 4;
}

// The score a chain is worth, with the parts the callouts and the ledger need. Returns 0
// when the chain is not an accepted word. Curve: letter values x length multiplier x 10,
// +200 for seven letters and up, +30 per letter for a word through a flame, +50% per
// diamond in the chain (two at most), and half for a word already in this shelf's ledger.
static int chainScoreDetail(const Game *g, char *w, int *lenOut, int *tierOut, int *diamondsOut, int *burnOut, int *repeatOut)
{
    *lenOut = 0; *tierOut = 0; *diamondsOut = 0; *burnOut = 0; *repeatOut = 0;
    if (g->chainLen < 3) return 0;
    boardChainWord(g, w);
    int len = strlen(w);
    int tier = wordAccepted(w, len);
    if (!tier) return 0;
    int base = 0, diamonds = 0, usedBurn = 0;
    for (int i = 0; i < g->chainLen; i++) {
        const Cell *cl = &g->cell[g->chainC[i]][g->chainR[i]];
        base += letterValue(cl->letter);
        if (cl->state == CELL_BURN) usedBurn = 1;
        if (cl->state == CELL_SECRET) diamonds++;
    }
    if (diamonds > 2) diamonds = 2;
    int mult = boardLenMult(len);
    int gained = base * mult * 10 + (len >= 7 ? 200 : 0) + 30 * len * usedBurn;
    gained += gained * diamonds / 2;
    int repeat = ledgerHas(g, w) ? 1 : 0;
    if (repeat) gained /= 2;
    *lenOut = len; *tierOut = tier; *diamondsOut = diamonds; *burnOut = usedBurn; *repeatOut = repeat;
    return gained;
}

int boardChainScore(const Game *g)
{
    char w[WORD_BUF]; int len, tier, diamonds, burn, repeat;
    return chainScoreDetail(g, w, &len, &tier, &diamonds, &burn, &repeat);
}

int boardSubmit(Game *g)
{
    char w[WORD_BUF]; int len, tier, diamonds, burn, repeat;
    int gained = chainScoreDetail(g, w, &len, &tier, &diamonds, &burn, &repeat);
    if (!tier) return 0;

    for (int i = 0; i < g->chainLen; i++) {
        Cell *cl = &g->cell[g->chainC[i]][g->chainR[i]];
        if (cl->state == CELL_TILE || cl->state == CELL_BURN) {
            cl->state = CELL_EMPTY;
            cl->flash = 8;
        }
    }
    strncpy(g->lastWord, w, MAX_WORD); g->lastWord[MAX_WORD] = 0;
    g->lastWordLen = len;
    g->lastWordScore = gained;
    g->lastWordTier = tier;
    g->lastWordDiamonds = diamonds;
    g->lastWordBurn = burn;
    g->lastWordRepeat = repeat;
    if (tier == 2) g->rareWords++;
    if (gained > g->bestWordScore) { g->bestWordScore = gained; strcpy(g->bestWord, g->lastWord); }
    // ledger ring: newest at ledgerHead - 1
    memcpy(g->ledger[g->ledgerHead], w, len); g->ledger[g->ledgerHead][len] = 0;   // len <= MAX_WORD: the word was accepted
    g->ledgerPts[g->ledgerHead] = gained;
    g->ledgerHead = (g->ledgerHead + 1) % LEDGER_N;
    if (g->ledgerN < LEDGER_N) g->ledgerN++;
    if (!repeat) g->uniqueWords++;
    g->score += gained;
    g->wordsThisShelf++;
    if (!g->practice) {            // lifetime stats: a Daily replay leaves no trace
        S.wordsSpelled++;
        if (tier == 2) S.rareWords++;
        if (len > S.longestLen) { S.longestLen = len; strcpy(S.longestWord, w); }
        if (gained > S.bestWordPts) { S.bestWordPts = gained; strcpy(S.bestWordTxt, w); }
    }
    g->chainLen = 0;
    endTurn(g, boardHeatFor(len));
    return gained;
}

int boardPeel(Game *g, int c, int r)
{
    Cell *cl = &g->cell[c][r];
    if (cl->state != CELL_TILE && cl->state != CELL_BURN) return 0;
    bool scorch = (cl->state == CELL_BURN) || (r > 0 && g->cell[c][r - 1].state == CELL_BURN);
    g->peels++;
    if (!g->practice) S.peels++;
    // drop a chained tile from the chain
    g->chainLen = 0;
    int secret = g->back[c][r];
    int peelHeat = g->lockdown ? 2 : 1;      // a peel is a turn and warms the shelf
    if (scorch) {
        g->scorched[c][r] = 1;
        cl->state = CELL_HOLE;
        cl->flash = 10;
        if (secret) { g->fail = FAIL_SCORCH; return -1; }
        endTurn(g, peelHeat);
        return 3;
    }
    if (secret) {
        cl->state = CELL_SECRET;
        cl->letter = g->pass[secret - 1] - 'a';
        cl->flash = 14;
        g->found[secret - 1] = 1;
        if (!g->practice) S.peelsHit++;
        endTurn(g, peelHeat);
        return 2;
    }
    cl->state = CELL_HOLE;
    cl->flash = 6;
    endTurn(g, peelHeat);
    return 1;
}

// Gravity-driven fall with a squash on landing. yoff and yv are in 1/16 px.
void boardAnimate(Game *g)
{
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            Cell *cl = &g->cell[c][r];
            if (cl->yoff < 0) {
                cl->yv += 10;                       // gravity
                if (cl->yv > 160) cl->yv = 160;     // terminal velocity, 10 px/frame
                cl->yoff += cl->yv;
                if (cl->yoff >= 0) {
                    cl->yoff = 0;
                    cl->squash = (cl->yv > 60) ? SQUASH_MAX : (cl->yv > 30 ? 3 : 0);
                    cl->yv = 0;
                }
            } else if (cl->squash) {
                cl->squash--;
            }
            if (cl->flash) cl->flash--;
        }
}

bool boardFireAbove(const Game *g, int c, int r)
{
    for (int rr = 0; rr < r; rr++) if (g->cell[c][rr].state == CELL_BURN) return true;
    return false;
}

int boardLowestFireRow(const Game *g)
{
    int low = -1;
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
            if (g->cell[c][r].state == CELL_BURN && r > low) low = r;
    return low;
}

// Is any 3- to 5-letter word still spellable? (longer-only boards are rare)
static bool dfsWords(const Game *g, int c, int r, char *w, int n, bool used[COLS][ROWS])
{
    int l = g->cell[c][r].letter;
    w[n++] = 'a' + l;
    if (l == 'q' - 'a') w[n++] = 'u';
    if (n >= 3 && wordAccepted(w, n)) return true;
    if (n >= 5) return false;
    used[c][r] = true;
    int nc[6], nr[6], k = boardNeighbors(c, r, nc, nr);
    for (int i = 0; i < k; i++) {
        if (used[nc[i]][nr[i]] || !boardIsChainable(g, nc[i], nr[i])) continue;
        if (dfsWords(g, nc[i], nr[i], w, n, used)) { used[c][r] = false; return true; }
    }
    used[c][r] = false;
    return false;
}

bool boardHasWords(const Game *g)
{
    char w[12];
    bool used[COLS][ROWS] = {{false}};
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
            if (boardIsChainable(g, c, r) && dfsWords(g, c, r, w, 0, used)) return true;
    return false;
}
