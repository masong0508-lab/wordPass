// wordPass balance bot: plays the real rules code (source/board.c, the dictionary, the pools, the letter bag)
// headlessly, many seeds, for a competent player model and a weak one, and prints the per-shelf statistics as
// markdown tables. Built by `make bot`, run by `make bot-run` (or ./build/bot --lang en). It is not part of
// `make check`, except for the small `--selftest` that `make check` runs.
//
//   build/bot --lang en [--n 300] [--runs 100] [--daily 400] [--shelves 24] [--md]   one language
//   build/bot --boards [--lang xx]                                                    fresh-board quality only
//   build/bot --selftest                                                              a few seconds, exit status = failures
//
// The bot never reads the back layer except where a player can see it: the numbers on holes, the diamonds it has
// found, and the pass word on the plaque.
#include "game.h"
#include "dict_bin.h"
#include "pools_bin.h"
#include "variants_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Game G;

// ---------------------------------------------------------------------------------------------- files, trie

static u8 *loadFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s (run from the repo root)\n", path); exit(2); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", path); exit(2); }
    fclose(f);
    if (size) *size = (size_t)n;
    return buf;
}

static const u8 RECLEN[21] = { 0, 0, 0, 4, 4, 4, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13 };
static u32 (*kid)[26];
static u8 *wflag;                 // 1 common word, 2 rare word (a node may be both ends of different tiers: kept as a mask)
static u32 nodes, nodeCap;

static u32 newNode(void)
{
    if (nodes == nodeCap) {
        nodeCap = nodeCap ? nodeCap * 2 : 1u << 20;
        kid = realloc(kid, (size_t)nodeCap * sizeof(*kid));
        wflag = realloc(wflag, nodeCap);
    }
    memset(kid[nodes], 0, sizeof(kid[0]));
    wflag[nodes] = 0;
    return nodes++;
}

// Decode a WPD3 image (the same packing dict.c reads) into a trie of every word, with its tier
static void buildTrie(const u8 *p)
{
    nodes = 0;
    newNode();                      // 0 is the root
    p += 4;
    u32 counts[21];
    for (int n = 3; n <= 20; n++) { counts[n] = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); p += 4; }
    for (int n = 3; n <= 20; n++) {
        const int rec = RECLEN[n];
        for (u32 i = 0; i < counts[n]; i++, p += rec) {
            u32 node = 0;
            for (int k = 0; k < n; k++) {
                int b = 5 * k, at = b >> 3;
                u32 v = ((u32)p[at] << 8) | (at + 1 < rec ? p[at + 1] : 0);
                int l = (int)((v >> (11 - (b & 7))) & 31);
                if (l > 25) l = 25;
                if (!kid[node][l]) { u32 nn = newNode(); kid[node][l] = nn; }
                node = kid[node][l];
            }
            wflag[node] |= (p[rec - 1] & 1) ? 2 : 1;
        }
    }
}

// ---------------------------------------------------------------------------------------------- rng (the bot's own)

static u64 rs = 88172645463325252ull;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (u32)(rs >> 16); }
static double rnd01(void) { return (rnd() & 0xFFFFFF) / 16777216.0; }
static void rseed(u64 s) { rs = s * 0x9E3779B97F4A7C15ull + 1442695040888963407ull; if (!rs) rs = 1; for (int i = 0; i < 4; i++) rnd(); }

// ---------------------------------------------------------------------------------------------- players

typedef struct {
    const char *name;
    int maxLen;               // longest word it looks for (letters)
    double seeShort;          // chance it notices a given common word of 3-4 letters
    double seeLong;           // ... of 5 or more
    double seeRare;           // ... a rare-tier word
    double seeFlame;          // ... a word through a flame (a targeted search)
    double lenWeight;         // utility per letter
    double heatWeight;        // utility per point of heat the word adds
    int    solver;            // 2: Minesweeper deduction, 1: naive (peels beside numbers and diamonds), 0: random
    double peelMin;           // peel when the best cell's chance of a pass letter is at least this ...
    double explore;           // ... or when nothing is better, with this probability per turn
    int    flameLook;         // reacts to a flame once it has this many turns left (0 = never looks)
    double flameAttention;    // chance per turn it checks the flames at all
    int    emergencyPeel;     // may peel a burning gem that no word reaches, as a last resort
} Player;

static const Player COMPETENT = { "competent", 9, 0.95, 0.70, 0.10, 0.92, 2.2, 1.6, 2, 0.30, 0.90, 3, 0.90, 1 };
static const Player WEAK      = { "weak",       5, 0.55, 0.20, 0.00, 0.40, 0.2, 0.0, 1, 0.50, 0.25, 2, 0.60, 0 };

// ---------------------------------------------------------------------------------------------- candidate words

#define MAXC 40000
typedef struct {
    u8 n, letters, tier, flames, secrets;
    u8 c[MAX_CHAIN], r[MAX_CHAIN];
    short minTurns;           // turns left of the most urgent flame this word puts out (9999 = none)
} Cand;
static Cand cands[MAXC];
static int ncand, maxLetters;
static bool candFull;
static int  flameTurns[COLS][ROWS];     // turns until this flame (if one) reaches the floor; 9999 = never / no flame

static void dfs(int c, int r, u32 node, int depth, int letters, bool used[COLS][ROWS], Cand *cur)
{
    int l = G.cell[c][r].letter;
    node = kid[node][l];
    if (!node) return;
    letters++;
    if (l == 'q' - 'a' && langQuTile()) { node = kid[node]['u' - 'a'] ; if (!node) return; letters++; }
    cur->c[depth] = (u8)c; cur->r[depth] = (u8)r;
    depth++;
    int st = G.cell[c][r].state;
    int ft = st == CELL_BURN ? flameTurns[c][r] : 9999;
    short saveMin = cur->minTurns;
    if (ft < cur->minTurns) cur->minTurns = (short)ft;
    u8 sf = cur->flames, ss = cur->secrets;
    if (st == CELL_BURN) cur->flames++;
    if (st == CELL_SECRET) cur->secrets++;
    if (depth >= 3 && wflag[node] && ncand < MAXC) {
        Cand *o = &cands[ncand++];
        *o = *cur; o->n = (u8)depth; o->letters = (u8)letters;
        o->tier = (wflag[node] & 1) ? 1 : 2;
    } else if (depth >= 3 && wflag[node]) candFull = true;
    if (letters < maxLetters && depth < MAX_CHAIN) {
        used[c][r] = true;
        int nc[6], nr[6], k = boardNeighbors(c, r, nc, nr);
        for (int i = 0; i < k; i++)
            if (!used[nc[i]][nr[i]] && boardIsChainable(&G, nc[i], nr[i])) dfs(nc[i], nr[i], node, depth, letters, used, cur);
        used[c][r] = false;
    }
    cur->minTurns = saveMin; cur->flames = sf; cur->secrets = ss;
}

// every word on the board up to maxLetters letters (ncand of them, capped at MAXC)
static void findWords(int maxLen)
{
    ncand = 0; candFull = false; maxLetters = maxLen;
    bool used[COLS][ROWS] = {{false}};
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++)
            if (boardIsChainable(&G, c, r)) {
                Cand cur; memset(&cur, 0, sizeof cur); cur.minTurns = 9999;
                dfs(c, r, 0, 0, 0, used, &cur);
            }
}

// ---------------------------------------------------------------------------------------------- fire reading

// Turns until the flame at (c, r) ends the shelf if nobody puts it out. It steps onto the next gem below (skipping
// holes and empty cells), is stuck for good under a diamond or another flame, and ends the shelf one step after it
// reaches the floor (or falls out of the bottom).
static int flameClock(int c, int r)
{
    int steps = 0, pos = r;
    for (;;) {
        int rr = pos + 1;
        while (rr < ROWS && (G.cell[c][rr].state == CELL_EMPTY || G.cell[c][rr].state == CELL_HOLE)) rr++;
        if (rr >= ROWS) { steps++; break; }                  // nothing left to burn beneath: the next step is the end
        int s = G.cell[c][rr].state;
        if (s == CELL_SECRET || s == CELL_BURN) return 9999; // a firebreak, or another flame that will go first
        pos = rr; steps++;
        if (pos == ROWS - 1) { steps++; break; }
    }
    return (G.burnEvery - G.burnTimer) + (steps - 1) * G.burnEvery;
}

static int mostUrgentFlame(int *fc, int *fr)
{
    int best = 9999;
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            flameTurns[c][r] = 9999;
            if (G.cell[c][r].state != CELL_BURN) continue;
            int t = flameClock(c, r);
            flameTurns[c][r] = t;
            if (t < best) { best = t; if (fc) { *fc = c; *fr = r; } }
        }
    return best;
}

// ---------------------------------------------------------------------------------------------- the peel solver

typedef struct { double p[COLS][ROWS]; bool known[COLS][ROWS]; int unknownCells; } Odds;

static int knownSecretsNear(int c, int r)
{
    int nc[6], nr[6], n = boardNeighbors(c, r, nc, nr), k = 0;
    for (int i = 0; i < n; i++) if (G.cell[nc[i]][nr[i]].state == CELL_SECRET) k++;
    return k;
}

static int passLeft(void) { int k = 0; for (int i = 0; i < G.passLen; i++) if (!G.found[i]) k++; return k; }

// The chance that each peelable gem hides a pass letter, from what a player can see
static int regionMinRow(void)
{
    ShelfRules sr;
    if (G.mode == MODE_DAILY) boardDailyRules(G.dailyMod, &sr); else boardShelfRules(G.shelf, &sr);
    int m = sr.minRow;
    if (G.twist == TWIST_BURIED) m = TWIST_BURIED_ROW;
    return m;
}

static void computeOdds(Odds *o, int level)
{
    const int minRow = level >= 2 ? regionMinRow() : 2;
    const int left = passLeft();
    int free = 0;
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            o->p[c][r] = -1; o->known[c][r] = false;
            int s = G.cell[c][r].state;
            if ((s == CELL_TILE || s == CELL_BURN) && r >= minRow) { o->p[c][r] = -2; free++; }   // -2: still to estimate
        }
    // constraints: every hole shows how many of its neighbours hide a pass letter
    double cons[COLS][ROWS];
    for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) cons[c][r] = -1;
    int certain[COLS][ROWS];
    memset(certain, 0, sizeof certain);
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            if (G.cell[c][r].state != CELL_HOLE) continue;
            int n = boardBackNumber(&G, c, r);
            int nc[6], nr[6], k = boardNeighbors(c, r, nc, nr);
            int known = 0, un = 0, ucc[6], urr[6];
            for (int i = 0; i < k; i++) {
                int s = G.cell[nc[i]][nr[i]].state;
                if (s == CELL_SECRET) known++;
                else if (s == CELL_TILE || s == CELL_BURN) { ucc[un] = nc[i]; urr[un] = nr[i]; un++; }
            }
            int m = n - known;
            if (un == 0) continue;
            for (int i = 0; i < un; i++) {
                int uc = ucc[i], ur = urr[i];
                if (m <= 0) { certain[uc][ur] = -1; cons[uc][ur] = 0; }
                else if (m >= un) { if (certain[uc][ur] != -1) certain[uc][ur] = 1; if (cons[uc][ur] != 0) cons[uc][ur] = 1; }
                else if (cons[uc][ur] != 0 && cons[uc][ur] != 1) {
                    double q = (double)m / un;
                    if (q > cons[uc][ur]) cons[uc][ur] = q;
                }
            }
        }
    // prior for cells with no constraint: what is left over, concentrated beside the diamonds found (the pass is one
    // connected cluster, so its next letters touch the ones already found)
    int constrainedMass = 0;
    for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (o->p[c][r] == -2 && cons[c][r] >= 0) constrainedMass++;
    int unconstrained = free - constrainedMass;
    if (unconstrained < 1) unconstrained = 1;
    int adjFound = 0;
    for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (o->p[c][r] == -2 && cons[c][r] < 0 && knownSecretsNear(c, r)) adjFound++;
    double base = (double)left / unconstrained;
    if (base > 0.9) base = 0.9;
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            if (o->p[c][r] != -2) continue;
            double p;
            if (cons[c][r] >= 0) p = cons[c][r];
            else {
                p = base;
                if (level >= 2 && G.found[0] + G.passLen > 0 && left < G.passLen) {   // some diamond is known: the cluster grows from it
                    if (knownSecretsNear(c, r)) { p = 0.9 * (double)left / (adjFound > 0 ? adjFound : 1); if (p > 0.7) p = 0.7; if (p < base) p = base; }
                    else p = base * 0.6;
                }
            }
            if (level < 2) {                      // the naive player: any cell beside a number above zero or a diamond is "hot"
                p = base;
                int nc[6], nr[6], n = boardNeighbors(c, r, nc, nr);
                for (int i = 0; i < n; i++) {
                    int s = G.cell[nc[i]][nr[i]].state;
                    if (s == CELL_SECRET || (s == CELL_HOLE && boardBackNumber(&G, nc[i], nr[i]) > 0)) p = 0.5;
                }
                for (int i = 0; i < n; i++)
                    if (G.cell[nc[i]][nr[i]].state == CELL_HOLE && boardBackNumber(&G, nc[i], nr[i]) == 0) { p = 0.0; break; }
            }
            o->p[c][r] = p;
        }
    o->unknownCells = free;
}

// information value of peeling a cell with no constraints: prefer cells with many untouched neighbours (the number it
// shows says most about the cluster)
static int infoValue(int c, int r)
{
    int nc[6], nr[6], n = boardNeighbors(c, r, nc, nr), v = 0;
    for (int i = 0; i < n; i++) {
        int s = G.cell[nc[i]][nr[i]].state;
        if (s == CELL_TILE || s == CELL_BURN) v++;
    }
    return v;
}

// The best peel: its cell and its chance. Never a cell that a flame would scorch unless the chance is zero.
static bool bestPeel(const Player *pl, int *bc, int *br, double *bp, bool *guess)
{
    Odds o;
    computeOdds(&o, pl->solver);
    double best = -1; int bestInfo = -1, cnt = 0;
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            double p = o.p[c][r];
            if (p < 0) continue;
            bool risky = G.cell[c][r].state == CELL_BURN || (r > 0 && G.cell[c][r - 1].state == CELL_BURN);
            if (risky && p > 0.0) continue;
            if (pl->solver >= 1 && p == 0.0) continue;       // a known-empty cell is no use
            int info = infoValue(c, r);
            if (p > best + 1e-9 || (p > best - 1e-9 && info > bestInfo)) { best = p; bestInfo = info; *bc = c; *br = r; cnt = 1; }
            else if (p > best - 1e-9 && info == bestInfo && rnd() % (u32)(++cnt) == 0) { *bc = c; *br = r; }
        }
    if (best < 0) return false;
    *bp = best;
    *guess = best < 0.2;
    return true;
}

// ---------------------------------------------------------------------------------------------- statistics

enum { FAILX_NONE = 0, FAILX_FIRE, FAILX_SCORCH, FAILX_WORDS, FAILX_TIMEOUT, FAILX_N };
typedef struct {
    long attempts, clears;
    long fails[FAILX_N];
    double turns, turnsCleared, peels, hits, words, wordLen, score, scoreCleared, guesses, lenHist[22];
    double flamesLit, ignitions, fireUnreach, wordsAtFail, chainable, heatSum;
    long fireFailUnreach;
    long outWordsHoles; double outWordsCells;
} Stat;

typedef struct { int outcome; int fail; } Outcome;

static int  gMaxTurns = 100;

static int wordHeatNow(int len) { return boardHeatFor(len); }

static bool wordInLedger(const char *w, char led[LEDGER_N + 8][MAX_WORD + 1], int n)
{
    for (int i = 0; i < n; i++) if (strcmp(led[i], w) == 0) return true;
    return false;
}

static void candWord(const Cand *k, char *out)
{
    int n = 0;
    for (int i = 0; i < k->n; i++) {
        int l = G.cell[k->c[i]][k->r[i]].letter;
        out[n++] = (char)('a' + l);
        if (l == 'q' - 'a' && langQuTile()) out[n++] = 'u';
    }
    out[n] = 0;
}

// ---------------------------------------------------------------------------------------------- one shelf

// Play the shelf in G (already set up by boardNewShelf) to its end. Fills st; returns the outcome.
static int playShelf(const Player *pl, Stat *st, bool verbose)
{
    char led[LEDGER_N + 8][MAX_WORD + 1];
    int nled = 0;
    int words = 0, wordLen = 0, hits = 0, guesses = 0, ignitions = 0;
    int result = FAILX_TIMEOUT;
    bool lastFlameReach = true;
    int lastFlameDanger = 0;
    int prevHeat = G.heat;
    G.score = 0;
    for (int guard = 0; guard < gMaxTurns; guard++) {
        // ---- read the fire
        int fc = -1, fr = -1;
        int urgent = mostUrgentFlame(&fc, &fr);
        bool looks = rnd01() < pl->flameAttention;
        bool mustWord = pl->flameLook > 0 && looks && urgent <= pl->flameLook;
        bool wantWords = false;
        // ---- words
        bool nothingToPeel = boardPassComplete(&G);
        if (!nothingToPeel) { int tc, tr; double tp; bool tg; nothingToPeel = !bestPeel(pl, &tc, &tr, &tp, &tg); }
        findWords(pl->maxLen);
        int best = -1; double bestU = -1e9;
        bool anyThroughFlame = false;
        for (int i = 0; i < ncand; i++) {
            Cand *k = &cands[i];
            if (k->minTurns < 9999) anyThroughFlame = true;
            double see;
            if (k->minTurns < 9999 && pl->flameLook > 0) see = pl->seeFlame;
            else if (k->tier == 2) see = pl->seeRare;
            else see = k->letters <= 4 ? pl->seeShort : pl->seeLong;
            if (k->letters > pl->maxLen) continue;
            if (rnd01() > see) continue;
            char w[WORD_BUF]; candWord(k, w);
            double u = pl->lenWeight * k->letters - pl->heatWeight * wordHeatNow(k->letters) + rnd01() * 0.5;
            if (wordInLedger(w, led, nled)) u -= 6;
            if (k->secrets) u += 1.5 * k->secrets;
            if (pl->solver == 2) {            // a competent player also scores it
                u += 0.5 * k->letters * (k->letters >= 7 ? 1 : 0);
            }
            if (k->minTurns < 9999 && pl->flameLook > 0) {
                int t = k->minTurns;
                u += t <= 1 ? 400 : t <= 2 ? 200 : t <= 3 ? 90 : t <= 5 ? 25 : (nothingToPeel ? 60 : 8);
                u += 3 * k->flames;
            }
            if (pl->solver == 0 && pl->lenWeight < 0.5 && k->letters > 4) u -= 2;
            if (u > bestU) { bestU = u; best = i; }
        }
        if (urgent <= 3) { lastFlameReach = anyThroughFlame; lastFlameDanger = 1; } else lastFlameDanger = 0;
        // ---- peel?
        bool doPeel = false; int pc = 0, prr = 0; double pp = 0; bool gs = false;
        if (!boardPassComplete(&G)) {
            bool have = bestPeel(pl, &pc, &prr, &pp, &gs);
            if (have && !mustWord) {
                if (pp >= pl->peelMin) doPeel = true;
                else if (rnd01() < pl->explore) doPeel = true;
            } else if (have && mustWord && pl->emergencyPeel && urgent <= 1 && (best < 0 || cands[best].minTurns >= 9999)) {
                // last resort: peel the burning gem itself (scorches the back layer, so only if no pass letter can be there)
                Odds o; computeOdds(&o, 2);
                if (o.p[fc][fr] == 0.0 || (G.cell[fc][fr].state == CELL_BURN && o.p[fc][fr] < 0)) { pc = fc; prr = fr; pp = 0; doPeel = true; }
            }
        }
        if (pl->solver == 0) {     // never used by the shipped players; kept for the random model
            doPeel = !boardPassComplete(&G) && rnd01() < pl->explore;
            if (doPeel) { do { pc = (int)(rnd() % COLS); prr = (int)(rnd() % ROWS); } while (!(G.cell[pc][prr].state == CELL_TILE)); }
        }
        if (!doPeel && best < 0) {         // nothing noticed: look harder, then give up on words
            int fb = -1;
            for (int i = 0; i < ncand; i++) if (fb < 0 || cands[i].letters < cands[fb].letters) fb = i;
            if (fb >= 0 && (rnd01() < 0.5 || pl->solver == 2)) best = fb;
            else if (!boardPassComplete(&G)) {
                bool have = bestPeel(pl, &pc, &prr, &pp, &gs);
                if (have) doPeel = true; else wantWords = true;
            }
        }
        (void)wantWords;
        int prevFireN = G.fireN;
        (void)prevFireN;
        if (doPeel) {
            int sec = G.back[pc][prr];
            if (gs) guesses++;
            if (getenv("BOTTRACE") && G.turn > 90) printf("peel %d,%d p=%.2f urgent %d\n", pc, prr, pp, urgent);
            int res = boardPeel(&G, pc, prr);
            if (res == 2) { hits++; G.score += boardPassLetterReward(G.shelf); }
            if (res == 0) { /* not peelable: skip the turn */ continue; }
            (void)sec;
            if (boardPassComplete(&G)) { result = FAILX_NONE; break; }
            if (G.fail) { result = G.fail == FAIL_FIRE ? FAILX_FIRE : G.fail == FAIL_SCORCH ? FAILX_SCORCH : FAILX_WORDS; break; }
        } else if (best >= 0) {
            Cand *k = &cands[best];
            char w[WORD_BUF]; candWord(k, w);
            G.chainLen = k->n;
            for (int i = 0; i < k->n; i++) { G.chainC[i] = k->c[i]; G.chainR[i] = k->r[i]; }
            int gained = boardSubmit(&G);
            if (getenv("BOTTRACE") && G.turn > 90) printf("word %s len %d flames %d minT %d urgent %d nothingToPeel %d\n", w, k->letters, k->flames, k->minTurns, urgent, nothingToPeel);
            if (gained <= 0) { G.chainLen = 0; continue; }       // the trie and the rules disagree: skip
            if (nled < LEDGER_N + 8) { strcpy(led[nled++], w); }
            words++; wordLen += k->letters;
            if (k->letters < 22) st->lenHist[k->letters]++;
            if (G.fail) { result = G.fail == FAIL_FIRE ? FAILX_FIRE : G.fail == FAIL_SCORCH ? FAILX_SCORCH : FAILX_WORDS; break; }
        } else {
            result = FAILX_WORDS;        // no word and nothing to peel
            break;
        }
        if (G.heat < prevHeat) ignitions++;
        prevHeat = G.heat;
    }
    if (verbose && result != FAILX_NONE) {
        printf("--- shelf %d ended %d turn %d heat %d/%d pass %s found", G.shelf, result, G.turn, G.heat, G.heatThreshold, G.pass);
        for (int i = 0; i < G.passLen; i++) printf(" %d", G.found[i]);
        printf("\n");
        { mostUrgentFlame(NULL, NULL); findWords(9); int nf = 0; for (int i = 0; i < ncand; i++) if (cands[i].flames) nf++; printf("cands %d with flames %d full %d\n", ncand, nf, candFull); }
        for (int r = 0; r < ROWS; r++) {
            for (int c = 0; c < COLS; c++) {
                int stt = G.cell[c][r].state;
                char ch = stt == CELL_TILE ? 'a' + G.cell[c][r].letter : stt == CELL_BURN ? 'F' : stt == CELL_HOLE ? '0' + boardBackNumber(&G, c, r) : stt == CELL_SECRET ? '*' : '.';
                printf(" %c%c", ch, G.back[c][r] ? '+' : ' ');
            }
            printf("\n");
        }
    }
    st->attempts++;
    st->fails[result]++;
    st->turns += G.turn;
    st->peels += G.peels;
    st->hits += hits;
    st->words += words;
    st->wordLen += wordLen;
    st->guesses += guesses;
    st->ignitions += ignitions;
    if (result == FAILX_NONE) {
        st->clears++;
        st->turnsCleared += G.turn;
        int bonus = boardShelfBonus(G.shelf) + boardSpeedBonus(G.shelf, G.turn);
        int kind; bonus += boardPeelBonus(G.shelf, G.peels, G.passLen, &kind);
        G.score += bonus;
        st->scoreCleared += G.score;
    }
    st->score += G.score;
    if (result == FAILX_FIRE && !lastFlameReach) st->fireFailUnreach++;
    (void)lastFlameDanger;
    if (result == FAILX_WORDS) {
        int cells = 0, holes = 0;
        for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) {
            if (boardIsChainable(&G, c, r)) cells++;
            if (G.cell[c][r].state == CELL_HOLE) holes++;
        }
        st->outWordsCells += cells; st->outWordsHoles += holes;
    }
    return result;
}

// Set up shelf n of a Classic run (the same call the game makes) on a fresh seed
static void setupClassic(int shelf, u32 seed)
{
    memset(&G, 0, sizeof G);
    G.mode = MODE_CLASSIC; G.shelf = shelf; G.lives = BOOKMARKS_MAX; G.state = ST_PLAY;
    G.tutorStep = TUTOR_DONE;
    srand(seed);
    // a twist never repeats the previous one: pick the previous at random so the first twist is not always the same
    G.lastTwist = 0;
    boardNewShelf(&G);
}

static void setupDaily(int ymd)
{
    memset(&G, 0, sizeof G);
    G.mode = MODE_DAILY; G.shelf = 1; G.state = ST_PLAY; G.dailySeed = ymd; G.practice = true;
    G.tutorStep = TUTOR_DONE;
    boardNewShelf(&G);
}

// ---------------------------------------------------------------------------------------------- reports

static const char *LANGS[] = { "en", "es", "fr", "de", "it", "pt", "nl", "sv", "pl", "da", "fi" };

static int langIndex(const char *code)
{
    for (int i = 0; i < LANG_COUNT; i++) if (!strcmp(LANGS[i], code)) return i;
    return -1;
}

static void useLanguage(int idx)
{
    if (idx == 0) {
        langLoad(LANG_EN);
        size_t n; u8 *d = loadFile("data/dict.bin", &n);
        buildTrie(d); free(d);
        return;
    }
    if (!langLoad(idx)) { fprintf(stderr, "cannot load language %s\n", LANGS[idx]); exit(2); }
    char path[64]; snprintf(path, sizeof path, "data/lang/%s/dict.bin", LANGS[idx]);
    size_t n; u8 *d = loadFile(path, &n);
    buildTrie(d); free(d);
}

static void rowHeader(void)
{
    printf("| shelf | clear %% | turns | turns (clears) | peels | peel hit %% | guess %% | words | avg word len | ignitions | score (clears) | FIRE %% | SCORCH %% | WORDS %% | stall %% |\n");
    printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
}

static void rowPrint(int shelf, const Stat *s)
{
    double n = s->attempts ? (double)s->attempts : 1;
    printf("| %d | %.1f | %.1f | %.1f | %.1f | %.0f | %.0f | %.1f | %.2f | %.1f | %.0f | %.1f | %.1f | %.1f | %.1f |\n",
        shelf, 100.0 * s->clears / n, s->turns / n, s->clears ? s->turnsCleared / s->clears : 0.0, s->peels / n,
        s->peels > 0 ? 100.0 * s->hits / s->peels : 0.0, s->peels > 0 ? 100.0 * s->guesses / s->peels : 0.0,
        s->words / n, s->words > 0 ? s->wordLen / s->words : 0.0, s->ignitions / n,
        s->clears ? s->scoreCleared / s->clears : 0.0,
        100.0 * s->fails[FAILX_FIRE] / n, 100.0 * s->fails[FAILX_SCORCH] / n, 100.0 * s->fails[FAILX_WORDS] / n, 100.0 * s->fails[FAILX_TIMEOUT] / n);
}

typedef struct { Stat shelf[40]; } Table;

static void playTable(const Player *pl, Table *t, int shelves, int n, u32 seedBase)
{
    for (int sh = 1; sh <= shelves; sh++)
        for (int i = 0; i < n; i++) {
            u32 seed = seedBase + (u32)sh * 100003u + (u32)i * 7919u;
            rseed(seed ^ 0xABCDEF);
            setupClassic(sh, seed);
            playShelf(pl, &t->shelf[sh], getenv("BOTDUMP") != NULL);
        }
}

// A whole run with three bookmarks: how far does it get
static void playRuns(const Player *pl, int n, u32 seedBase, int *hist, double *avgShelf, double *avgScore, int cap)
{
    long total = 0; double score = 0;
    for (int i = 0; i < n; i++) {
        int lives = BOOKMARKS_MAX, shelf = 1, runScore = 0, lastTwist = 0, cleared = 0;
        u32 s = seedBase + (u32)i * 15485863u;
        while (lives > 0 && shelf <= cap) {
            s += 104729u;
            memset(&G, 0, sizeof G);
            G.mode = MODE_CLASSIC; G.shelf = shelf; G.lives = lives; G.state = ST_PLAY; G.lastTwist = lastTwist; G.tutorStep = TUTOR_DONE;
            srand(s);
            boardNewShelf(&G);
            lastTwist = G.lastTwist;
            rseed(s ^ 0x1234567);
            Stat dummy; memset(&dummy, 0, sizeof dummy);
            int res = playShelf(pl, &dummy, false);
            if (res == FAILX_NONE) { runScore += (int)dummy.score; cleared = shelf; if (G.lockdown && lives < BOOKMARKS_MAX) lives++; shelf++; }
            else lives--;
        }
        total += cleared; score += runScore;
        hist[cleared < 39 ? cleared : 39]++;
    }
    *avgShelf = (double)total / n; *avgScore = score / n;
}

static void boardQuality(int lang, int n)
{
    int vowelMin = 99; double vowelSum = 0, wordSum = 0; int fewWords = 0, noWords = 0, lowVowel = 0, qs = 0;
    for (int i = 0; i < n; i++) {
        setupClassic(1 + i % 24, 5000 + (u32)i * 31u);
        int v = 0;
        for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) {
            if (langIsVowel(G.cell[c][r].letter)) v++;
            if (G.cell[c][r].letter == 'q' - 'a') qs++;
        }
        findWords(5);
        int w = ncand;
        vowelSum += v; if (v < vowelMin) vowelMin = v; if (v < 14) lowVowel++;
        wordSum += w; if (w < 25) fewWords++; if (w == 0) noWords++;
    }
    printf("| %s | %.1f | %d | %.1f%% | %.0f | %.1f%% | %.2f%% |\n", LANGS[lang], vowelSum / n, vowelMin, 100.0 * lowVowel / n, wordSum / n, 100.0 * fewWords / n, 100.0 * noWords / n);
    (void)qs;
}

static int selftest(void)
{
    int fails = 0;
    int idx = 0;
    useLanguage(idx);
    Table *t = calloc(1, sizeof *t);
    playTable(&COMPETENT, t, 2, 12, 1);
    if (t->shelf[1].attempts != 12) { printf("FAIL bot played %ld shelves\n", t->shelf[1].attempts); fails++; }
    if (t->shelf[1].clears < 6) { printf("FAIL the competent bot cleared %ld of 12 on shelf 1\n", t->shelf[1].clears); fails++; }
    // determinism: the same seed gives the same board and the same result
    Stat a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    rseed(77); setupClassic(5, 99); int ra = playShelf(&COMPETENT, &a, false); int sa = G.score;
    rseed(77); setupClassic(5, 99); int rb = playShelf(&COMPETENT, &b, false); int sb = G.score;
    if (ra != rb || sa != sb || a.turns != b.turns) { printf("FAIL the bot is not deterministic\n"); fails++; }
    free(t);
    printf(fails ? "BOT FAIL: %d\n" : "BOT OK: 0 failure(s)\n", fails);
    return fails;
}

int main(int argc, char **argv)
{
    int lang = 0, n = 200, runs = 60, dailyN = 240, shelves = 24;
    bool boardsOnly = false, self = false, weakToo = true, md = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--lang") && i + 1 < argc) { lang = langIndex(argv[++i]); if (lang < 0) { fprintf(stderr, "unknown language\n"); return 2; } }
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--daily") && i + 1 < argc) dailyN = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shelves") && i + 1 < argc) shelves = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--boards")) boardsOnly = true;
        else if (!strcmp(argv[i], "--selftest")) self = true;
        else if (!strcmp(argv[i], "--competent-only")) weakToo = false;
        else if (!strcmp(argv[i], "--md")) md = true;
    }
    (void)md;
    // the shared data
    size_t sz;
    dict_bin = loadFile("data/dict.bin", &sz);
    pools_bin = loadFile("data/pools.bin", &sz);
    variants_bin = loadFile("data/variants.bin", &sz);
    dictInit(); poolsInit(); langInit();
    S.fire = FIRE_NORMAL; S.library = LIB_FULL; S.spelling = SPELL_BOTH;
    if (self) return selftest() ? 1 : 0;
    useLanguage(lang);
    if (boardsOnly) { boardQuality(lang, 400); return 0; }
    if (shelves > 39) shelves = 39;

    Table *comp = calloc(1, sizeof *comp), *weak = calloc(1, sizeof *weak);
    printf("### %s (%s)\n\n", langName(lang), LANGS[lang]);
    playTable(&COMPETENT, comp, shelves, n, 1000);
    printf("**Competent bot, %d fresh attempts per shelf**\n\n", n);
    rowHeader();
    for (int sh = 1; sh <= shelves; sh++) rowPrint(sh, &comp->shelf[sh]);
    printf("\n");
    if (weakToo) {
        playTable(&WEAK, weak, shelves, n, 2000);
        printf("**Weak bot, %d fresh attempts per shelf**\n\n", n);
        rowHeader();
        for (int sh = 1; sh <= shelves; sh++) rowPrint(sh, &weak->shelf[sh]);
        printf("\n");
    }
    // runs
    {
        int hc[40] = {0}, hw[40] = {0}; double sc, scc, sw, scw;
        playRuns(&COMPETENT, runs, 5000, hc, &sc, &scc, 40);
        printf("**Whole runs (three bookmarks, %d runs)**: competent bot clears %.1f shelves on average (score %.0f)", runs, sc, scc);
        if (weakToo) { playRuns(&WEAK, runs, 6000, hw, &sw, &scw, 40); printf(", weak bot %.1f shelves (score %.0f)", sw, scw); }
        printf(".\n\n");
        printf("Highest shelf cleared, competent: ");
        for (int s = 0; s < 40; s++) if (hc[s]) printf("%d:%d ", s, hc[s]);
        printf("\n\n");
    }
    // daily modifiers
    {
        static const char *MOD[4] = { "STANDARD", "EMBER", "DEEP", "LONG" };
        Stat dc[4], dw[4];
        memset(dc, 0, sizeof dc); memset(dw, 0, sizeof dw);
        int found = 0;
        for (int d = 0; d < dailyN * 4 && found < dailyN * 4; d++) {
            int ymd = 20260101 + (d / 28) * 100 + (d % 28);      // calendar-ish: never read as a date, only hashed
            int mod = boardDailyMod(ymd);
            if (dc[mod].attempts >= dailyN) continue;
            rseed((u64)ymd * 31 + 7);
            setupDaily(ymd);
            playShelf(&COMPETENT, &dc[mod], false);
            if (weakToo) { rseed((u64)ymd * 17 + 3); setupDaily(ymd); playShelf(&WEAK, &dw[mod], false); }
            found++;
        }
        printf("**Daily Pass by modifier** (%d dates each)\n\n| modifier | competent clear %% | turns | FIRE %% | SCORCH %% | WORDS %% | weak clear %% |\n| --- | ---: | ---: | ---: | ---: | ---: | ---: |\n", dailyN);
        for (int m = 0; m < 4; m++) {
            double a = dc[m].attempts ? dc[m].attempts : 1, b = dw[m].attempts ? dw[m].attempts : 1;
            printf("| %s | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |\n", MOD[m], 100.0 * dc[m].clears / a, dc[m].turns / a,
                100.0 * dc[m].fails[FAILX_FIRE] / a, 100.0 * dc[m].fails[FAILX_SCORCH] / a, 100.0 * dc[m].fails[FAILX_WORDS] / a, 100.0 * dw[m].clears / b);
        }
        printf("\n");
    }
    // extra diagnostics for the competent bot: fire fails nobody could prevent, OUT OF WORDS boards
    {
        long fire = 0, unreach = 0, wordsF = 0; double oc = 0, oh = 0, gu = 0, pe = 0;
        for (int sh = 1; sh <= shelves; sh++) {
            fire += comp->shelf[sh].fails[FAILX_FIRE]; unreach += comp->shelf[sh].fireFailUnreach;
            gu += comp->shelf[sh].guesses; pe += comp->shelf[sh].peels;
            wordsF += comp->shelf[sh].fails[FAILX_WORDS]; oc += comp->shelf[sh].outWordsCells; oh += comp->shelf[sh].outWordsHoles;
        }
        printf("**Diagnostics (competent, all shelves)**: FIRE fails %ld, of which %ld had no word through any flame near the floor on the last look; OUT OF WORDS fails %ld (average %.1f chainable gems and %.1f holes on the board at the end); guess peels (chance under 20%%) %.1f%% of all peels.\n\n",
            fire, unreach, wordsF, wordsF ? oc / wordsF : 0.0, wordsF ? oh / wordsF : 0.0,
            pe > 0 ? 100.0 * gu / pe : 0.0);
    }
    return 0;
}
