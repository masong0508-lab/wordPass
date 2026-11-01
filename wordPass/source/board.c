// Board logic: hex-column grid, chaining, words, gravity, fire, peeling.
#include "game.h"
#include <stdlib.h>
#include <string.h>

static const char *PASSES[] = {
    "book", "page", "lamp", "spell", "shelf", "quill", "ink", "novel",
    "verse", "story", "fable", "index", "script", "margin", "chapter",
    "library", "binding", "folio", "glossary", "epilogue"
};
#define NPASSES (int)(sizeof(PASSES) / sizeof(PASSES[0]))

// Bookworm-style letter weights (Q stands for Qu)
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

static int randomLetter(void)
{
    int total = 0;
    for (int i = 0; i < 26; i++) total += LETTER_WEIGHT[i];
    int r = rand() % total;
    for (int i = 0; i < 26; i++) {
        r -= LETTER_WEIGHT[i];
        if (r < 0) return i;
    }
    return 4;
}

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

static void placeSecrets(Game *g)
{
    memset(g->back, 0, sizeof(g->back));
    // deeper shelves bury the pass lower in the board
    int minRow = (g->shelf <= 2) ? 1 : (g->shelf <= 5) ? 3 : 4;
    int placed = 0, guard = 0;
    int c = rand() % COLS, r = minRow + rand() % (ROWS - minRow);
    while (placed < g->passLen && guard++ < 2000) {
        if (!g->back[c][r]) {
            g->back[c][r] = placed + 1;
            placed++;
        }
        // random walk to a neighbour, biased to stay low
        int nc[6], nr[6], n = boardNeighbors(c, r, nc, nr);
        int pick = rand() % n;
        if (nr[pick] < minRow - 1 && (rand() & 3)) continue;
        c = nc[pick]; r = nr[pick];
    }
    // fallback for pathological walks
    for (int i = placed; i < g->passLen; i++) {
        for (int t = 0; t < 500; t++) {
            int cc = rand() % COLS, rr = rand() % ROWS;
            if (!g->back[cc][rr]) { g->back[cc][rr] = i + 1; break; }
        }
    }
}

void boardNewShelf(Game *g)
{
    int idx = (g->mode == MODE_DAILY) ? (g->dailySeed * 7 + 3) % NPASSES : (g->shelf - 1) % NPASSES;
    strcpy(g->pass, PASSES[idx]);
    g->passLen = strlen(g->pass);
    memset(g->found, 0, sizeof(g->found));
    memset(g->scorched, 0, sizeof(g->scorched));
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            g->cell[c][r].letter = randomLetter();
            g->cell[c][r].state = CELL_TILE;
            g->cell[c][r].yoff = (-(ROWS - r) * TILE_H - 20 - (rand() % 30) - c * 6) * 16;
            g->cell[c][r].yv = 0;
            g->cell[c][r].squash = 0;
            g->cell[c][r].flash = 0;
        }
    placeSecrets(g);
    g->chainLen = 0;
    g->turn = 0;
    g->heat = 0;
    g->lockdown = g->shelf > 5;
    g->heatThreshold = g->lockdown ? 4 : (g->shelf <= 2 ? 8 : 6);
    g->burnEvery = (g->shelf <= 3) ? 2 : 1;
    g->burnTimer = 0;
    g->fail = FAIL_NONE;
    g->wordsThisShelf = 0;
    g->shelfStartScore = g->score;
    g->lastWord[0] = 0;
    g->msg[0] = 0;
    g->bestWord[0] = 0;
    g->bestWordScore = 0;
    g->peels = 0;
    if (g->mode == MODE_DAILY) {
        // one fixed shelf for everyone today: medium pressure, pass buried mid-board
        g->lockdown = false;
        g->heatThreshold = 6;
        g->burnEvery = 2;
    }
}

bool boardTryAddToChain(Game *g, int c, int r)
{
    if (!boardIsChainable(g, c, r)) return false;
    if (g->chainLen == 0) {
        g->chainC[0] = c; g->chainR[0] = r; g->chainLen = 1;
        return true;
    }
    int lc = g->chainC[g->chainLen - 1], lr = g->chainR[g->chainLen - 1];
    if (lc == c && lr == r) return false;
    // backtrack onto the previous tile
    if (g->chainLen >= 2 && g->chainC[g->chainLen - 2] == c && g->chainR[g->chainLen - 2] == r) {
        g->chainLen--;
        return true;
    }
    for (int i = 0; i < g->chainLen; i++)
        if (g->chainC[i] == c && g->chainR[i] == r) return false;
    if (adjacent(lc, lr, c, r)) {
        if (g->chainLen >= MAX_CHAIN) return false;
        g->chainC[g->chainLen] = c; g->chainR[g->chainLen] = r; g->chainLen++;
        return true;
    }
    // not adjacent: start a new chain here
    g->chainC[0] = c; g->chainR[0] = r; g->chainLen = 1;
    return true;
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

// Gravity with separators: holes and secrets are floors. Only the top
// segment of each column (above the highest separator) ever refills.
static int applyGravity(Game *g)
{
    int spawned = 0;
    for (int c = 0; c < COLS; c++) {
        int w = ROWS - 1;
        for (int r = ROWS - 1; r >= 0; r--) {
            int s = g->cell[c][r].state;
            if (s == CELL_HOLE || s == CELL_SECRET) { w = r - 1; continue; }
            if (s == CELL_EMPTY) continue;
            if (w != r) {
                Cell moved = g->cell[c][r];
                moved.yoff -= (w - r) * TILE_H * 16;
                g->cell[c][w] = moved;
                g->cell[c][r].state = CELL_EMPTY;
            }
            w--;
        }
        // refill the top segment
        int topSep = -1;
        for (int r = 0; r < ROWS; r++) {
            int s = g->cell[c][r].state;
            if (s == CELL_HOLE || s == CELL_SECRET) { topSep = r; break; }
        }
        int end = (topSep < 0) ? ROWS : topSep;
        int k = 0;
        for (int r = end - 1; r >= 0; r--) {
            if (g->cell[c][r].state == CELL_EMPTY) {
                g->cell[c][r].letter = randomLetter();
                g->cell[c][r].state = CELL_TILE;
                g->cell[c][r].yoff = (-(k + 1) * TILE_H - 12 - (r == 0 ? 0 : 4)) * 16;
                g->cell[c][r].yv = 0;
                g->cell[c][r].squash = 0;
                g->cell[c][r].flash = 0;
                k++; spawned++;
            }
        }
    }
    return spawned;
}

static void spawnFire(Game *g)
{
    for (int t = 0; t < 20; t++) {
        int c = rand() % COLS;
        if (g->cell[c][0].state == CELL_TILE) { g->cell[c][0].state = CELL_BURN; return; }
    }
}

// Each burning tile destroys the tile beneath and takes its place.
// Empty cells and holes are chimneys: fire drops straight through.
// Secrets are firebreaks. A flame already on the bottom row ends the game.
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
            if (rr >= ROWS) {
                // fell out the bottom: park on the lowest cell, burning
                Cell f = g->cell[c][r];
                g->cell[c][r].state = CELL_EMPTY;
                g->cell[c][ROWS - 1] = f;
                g->cell[c][ROWS - 1].yoff = 0;
                moved[c][ROWS - 1] = true;
                continue;
            }
            int s = g->cell[c][rr].state;
            if (s == CELL_SECRET || s == CELL_BURN) {
                if (rr - 1 != r) {
                    Cell f = g->cell[c][r];
                    g->cell[c][r].state = CELL_EMPTY;
                    g->cell[c][rr - 1] = f;
                    moved[c][rr - 1] = true;
                }
                continue;
            }
            // TILE: burn it, move down onto it
            Cell f = g->cell[c][r];
            g->cell[c][r].state = CELL_EMPTY;
            f.letter = g->cell[c][rr].letter;
            g->cell[c][rr] = f;
            g->cell[c][rr].yoff = 0;
            moved[c][rr] = true;
        }
    }
}

static void endTurn(Game *g, int wordLen)
{
    g->turn++;
    if (wordLen <= 3) g->heat += 2;
    else if (wordLen == 4) g->heat += 1;
    if (g->lockdown && (g->turn % 6) == 0) g->heat += 2;
    if (g->heat >= g->heatThreshold) { g->heat = 0; spawnFire(g); }

    g->burnTimer++;
    if (g->burnTimer >= g->burnEvery) {
        g->burnTimer = 0;
        fireStep(g);
    }
    int spawned = applyGravity(g);
    if (spawned >= 4) musicCascade(spawned);
    if (g->fail == FAIL_NONE && !boardHasWords(g)) g->fail = FAIL_WORDS;
}

int boardSubmit(Game *g)
{
    if (g->chainLen < 3) return 0;
    char w[24];
    boardChainWord(g, w);
    int len = strlen(w);
    if (!dictLookup(w, len)) return 0;

    int base = 0;
    bool usedBurn = false;
    for (int i = 0; i < g->chainLen; i++) {
        Cell *cl = &g->cell[g->chainC[i]][g->chainR[i]];
        base += letterValue(cl->letter);
        if (cl->state == CELL_BURN) usedBurn = true;
    }
    int mult = (len <= 4) ? 1 : (len <= 6) ? 2 : (len <= 8) ? 3 : 4;
    int gained = base * mult * 10 + (usedBurn ? 100 : 0) + (len >= 7 ? 200 : 0);

    for (int i = 0; i < g->chainLen; i++) {
        Cell *cl = &g->cell[g->chainC[i]][g->chainR[i]];
        if (cl->state == CELL_TILE || cl->state == CELL_BURN) {
            cl->state = CELL_EMPTY;
            cl->flash = 8;
        }
    }
    strncpy(g->lastWord, w, 15); g->lastWord[15] = 0;
    g->lastWordLen = len;
    g->lastWordScore = gained;
    if (gained > g->bestWordScore) { g->bestWordScore = gained; strcpy(g->bestWord, g->lastWord); }
    g->score += gained;
    g->wordsThisShelf++;
    g->chainLen = 0;
    endTurn(g, len);
    return gained;
}

int boardPeel(Game *g, int c, int r)
{
    Cell *cl = &g->cell[c][r];
    if (cl->state != CELL_TILE && cl->state != CELL_BURN) return 0;
    bool scorch = (cl->state == CELL_BURN) || (r > 0 && g->cell[c][r - 1].state == CELL_BURN);
    g->peels++;
    // drop a chained tile from the chain
    g->chainLen = 0;
    int secret = g->back[c][r];
    if (scorch) {
        g->scorched[c][r] = 1;
        cl->state = CELL_HOLE;
        cl->flash = 10;
        if (secret) { g->fail = FAIL_SCORCH; return -1; }
        applyGravity(g);
        return 3;
    }
    if (secret) {
        cl->state = CELL_SECRET;
        cl->letter = g->pass[secret - 1] - 'a';
        cl->flash = 14;
        g->found[secret - 1] = 1;
        applyGravity(g);
        return 2;
    }
    cl->state = CELL_HOLE;
    cl->flash = 6;
    applyGravity(g);
    if (!boardHasWords(g)) g->fail = FAIL_WORDS;
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

// Is any 3- or 4-letter word still spellable? (longer-only boards are rare)
static bool dfsWords(const Game *g, int c, int r, char *w, int n, bool used[COLS][ROWS])
{
    int l = g->cell[c][r].letter;
    w[n++] = 'a' + l;
    if (l == 'q' - 'a') w[n++] = 'u';
    if (n >= 3 && dictLookup(w, n)) return true;
    if (n >= 4) return false;
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
