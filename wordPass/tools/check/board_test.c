// Native rule tests: board.c, dict.c and music.c compiled with gcc against the stubs in
// tools/check/stub. Run with `make check` from the repo root (loads data/dict.bin).
// Each scenario prints PASS or FAIL; the exit status is the number of failures.
#include "game.h"
#include "dict_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(name, cond) do { if (cond) printf("PASS %s\n", name); else { printf("FAIL %s (%s) at %s:%d\n", name, #cond, __FILE__, __LINE__); fails++; } } while (0)

static Game g;

// a board of plain tiles, every one the given letter, no pass, calm fire
static void boardOf(char letter)
{
    memset(&g, 0, sizeof(g));
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) { g.cell[c][r].state = CELL_TILE; g.cell[c][r].letter = letter - 'a'; }
    strcpy(g.pass, "book"); g.passLen = 4;
    g.heatThreshold = 99; g.burnEvery = 99; g.shelf = 1; g.state = ST_PLAY;
}
static void set(int c, int r, int state, char letter)
{
    g.cell[c][r].state = state; g.cell[c][r].letter = letter ? letter - 'a' : 0;
    g.cell[c][r].yoff = 0; g.cell[c][r].yv = 0; g.cell[c][r].squash = 0; g.cell[c][r].flash = 0;
}
static int st(int c, int r) { return g.cell[c][r].state; }
// "bed" down column 6 so a turn's OUT OF WORDS check has something to find
static void plantWord(void) { set(6, 5, CELL_TILE, 'b'); set(6, 6, CELL_TILE, 'e'); set(6, 7, CELL_TILE, 'd'); }
static int countState(int state)
{
    int n = 0;
    for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (st(c, r) == state) n++;
    return n;
}

static bool loadDict(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s (run from the repo root)\n", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); return false; }
    fclose(f);
    dict_bin = buf;
    dictInit();
    return true;
}

int main(int argc, char **argv)
{
    if (!loadDict(argc > 1 ? argv[1] : "data/dict.bin")) return 1;
    srand(1);

    // ---- dictionary tiers
    CHECK("dictLookup zax is rare", dictLookup("zax", 3) == 2);
    CHECK("dictLookup book is common", dictLookup("book", 4) == 1);
    CHECK("dictLookup rejects a non-word", dictLookup("xxx", 3) == 0);
    S.library = LIB_COMMON;
    CHECK("LIBRARY COMMON rejects a rare word", !boardWordOk("zax", 3) && boardWordOk("book", 4));
    S.library = LIB_FULL;

    // ---- fire over a hole over a secret: nothing moves, the hole stays a hole
    boardOf('e');
    set(0, 2, CELL_BURN, 'a'); set(0, 3, CELL_HOLE, 0); set(0, 4, CELL_SECRET, 'b');
    boardDebugFireStep(&g);
    CHECK("flame over hole over secret stays put", st(0, 2) == CELL_BURN && st(0, 3) == CELL_HOLE && st(0, 4) == CELL_SECRET && g.fail == FAIL_NONE);

    // ---- fire falling out the bottom over holes: Library Lost, holes untouched
    boardOf('e');
    set(0, 5, CELL_BURN, 'a'); set(0, 6, CELL_HOLE, 0); set(0, 7, CELL_HOLE, 0);
    g.scorched[0][7] = 1;
    boardDebugFireStep(&g);
    CHECK("flame out the bottom over holes fails without converting a hole",
          g.fail == FAIL_FIRE && st(0, 6) == CELL_HOLE && st(0, 7) == CELL_HOLE && st(0, 5) == CELL_BURN && g.scorched[0][7] == 1);

    // ---- fire burns the tile below a hole chimney
    boardOf('e');
    set(0, 2, CELL_BURN, 'a'); set(0, 3, CELL_HOLE, 0); set(0, 4, CELL_TILE, 'b');
    boardDebugFireStep(&g);
    CHECK("flame burns the tile below a hole chimney",
          st(0, 4) == CELL_BURN && g.cell[0][4].letter == 'b' - 'a' && st(0, 3) == CELL_HOLE && st(0, 2) == CELL_TILE && g.fail == FAIL_NONE);

    // ---- blocked flame settles on the lowest empty row above the blocker, never on a hole
    boardOf('e');
    set(0, 1, CELL_BURN, 'a'); set(0, 2, CELL_EMPTY, 0); set(0, 3, CELL_HOLE, 0); set(0, 4, CELL_SECRET, 'b');
    boardDebugFireStep(&g);
    CHECK("blocked flame drops onto the empty row, hole kept", st(0, 2) == CELL_BURN && st(0, 3) == CELL_HOLE && st(0, 4) == CELL_SECRET && st(0, 1) == CELL_TILE);

    // ---- gravity with a hole: the hole stays, the segment above refills
    boardOf('e');
    set(0, 4, CELL_HOLE, 0); set(0, 1, CELL_EMPTY, 0); set(0, 2, CELL_TILE, 'c'); set(0, 3, CELL_TILE, 'd');
    boardDebugFireStep(&g);   // no flames: just the gravity pass
    CHECK("gravity keeps the hole and refills above it",
          st(0, 4) == CELL_HOLE && g.cell[0][3].letter == 'd' - 'a' && g.cell[0][2].letter == 'c' - 'a' &&
          g.cell[0][1].letter == 'e' - 'a' && st(0, 1) == CELL_TILE && st(0, 0) == CELL_TILE && st(0, 5) == CELL_TILE);

    // ---- Qu + I + Z spells quiz
    boardOf('e');
    set(0, 0, CELL_TILE, 'q'); set(0, 1, CELL_TILE, 'i'); set(0, 2, CELL_TILE, 'z');
    bool ok = boardTryAddToChain(&g, 0, 0) && boardTryAddToChain(&g, 0, 1) && boardTryAddToChain(&g, 0, 2);
    char w[24]; boardChainWord(&g, w);
    int gained = boardSubmit(&g);
    CHECK("Qu+I+Z chain scores quiz", ok && strcmp(w, "quiz") == 0 && gained > 0 && strcmp(g.lastWord, "quiz") == 0 && g.lastWordLen == 4);

    // ---- chain backtracking
    boardOf('e');
    boardTryAddToChain(&g, 0, 0); boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2);
    bool three = g.chainLen == 3;
    boardTryAddToChain(&g, 0, 1);               // back onto the previous tile
    bool two = g.chainLen == 2 && g.chainC[1] == 0 && g.chainR[1] == 1;
    boardTryAddToChain(&g, 0, 0);
    bool one = g.chainLen == 1;
    boardTryAddToChain(&g, 5, 5);               // not adjacent: a fresh chain
    CHECK("chain backtracking", three && two && one && g.chainLen == 1 && g.chainC[0] == 5 && g.chainR[0] == 5);
    boardTryAddToChain(&g, 5, 6); boardTryAddToChain(&g, 5, 7);
    bool rejected = !boardTryAddToChain(&g, 5, 5);   // two steps back: not a backtrack, a revisit
    CHECK("chain rejects a revisit of an earlier tile", rejected && g.chainLen == 3);

    // ---- peeling (every peel is a turn: the board must still hold a word afterwards)
    boardOf('e'); plantWord();
    set(0, 1, CELL_BURN, 'a');
    int res = boardPeel(&g, 0, 2);
    CHECK("peel under a flame scorches a plain cell", res == 3 && st(0, 2) == CELL_HOLE && g.scorched[0][2] == 1 && g.fail == FAIL_NONE);
    CHECK("a scorching peel is a turn", g.turn == 1 && g.heat == 1);
    boardOf('e');
    set(0, 1, CELL_BURN, 'a'); g.back[0][2] = 1;
    res = boardPeel(&g, 0, 2);
    CHECK("peel under a flame destroys a pass letter", res == -1 && g.fail == FAIL_SCORCH && st(0, 2) == CELL_HOLE);
    boardOf('e'); plantWord();
    g.back[3][3] = 2;
    res = boardPeel(&g, 3, 3);
    CHECK("peel reveals a pass letter as a diamond", res == 2 && st(3, 3) == CELL_SECRET && g.cell[3][3].letter == 'o' - 'a' && g.found[1] == 1);
    CHECK("a revealing peel is a turn", g.turn == 1 && g.heat == 1 && g.fail == FAIL_NONE);
    boardOf('e'); plantWord();
    res = boardPeel(&g, 2, 2);
    CHECK("a plain peel is a turn (+1 heat, +2 in Lockdown)", res == 1 && g.turn == 1 && g.heat == 1 && g.fail == FAIL_NONE);
    g.lockdown = true; g.pulseEvery = 6;
    boardPeel(&g, 2, 5);
    CHECK("Lockdown peel adds 2 heat", g.turn == 2 && g.heat == 3);
    boardOf('x');
    res = boardPeel(&g, 2, 2);
    CHECK("peel on an all-x board is OUT OF WORDS", res == 1 && g.fail == FAIL_WORDS && st(2, 2) == CELL_HOLE);
    boardOf('e');
    CHECK("boardHasWords finds nothing on an all-e board", !boardHasWords(&g));
    set(1, 1, CELL_TILE, 'b'); set(1, 2, CELL_TILE, 'e'); set(1, 3, CELL_TILE, 'd');
    CHECK("boardHasWords finds bed", boardHasWords(&g));

    // ---- scoring: boardChainScore previews exactly what boardSubmit awards
    boardOf('e'); plantWord();
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    int preview = boardChainScore(&g);
    gained = boardSubmit(&g);
    int bedBase = letterValue('b' - 'a') + letterValue('e' - 'a') + letterValue('d' - 'a');
    CHECK("plain word: preview == gain == base*10", preview == gained && gained == bedBase * 10 && g.ledgerN == 1 && g.uniqueWords == 1 &&
          strcmp(g.ledger[0], "bed") == 0 && g.ledgerPts[0] == gained && !g.lastWordRepeat && !g.lastWordBurn && g.lastWordDiamonds == 0);
    CHECK("boardChainScore is 0 with no chain", boardChainScore(&g) == 0);
    boardOf('e'); plantWord();
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_BURN, 'e'); set(0, 3, CELL_TILE, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    preview = boardChainScore(&g); gained = boardSubmit(&g);
    CHECK("word through a flame: +30 per letter", preview == gained && gained == bedBase * 10 + 30 * 3 && g.lastWordBurn == 1);
    boardOf('e'); plantWord();
    set(0, 1, CELL_SECRET, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_SECRET, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    preview = boardChainScore(&g); gained = boardSubmit(&g);
    CHECK("word through two diamonds scores double", preview == gained && gained == bedBase * 10 * 2 && g.lastWordDiamonds == 2 && st(0, 1) == CELL_SECRET);
    boardOf('e'); plantWord();
    set(0, 1, CELL_SECRET, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    preview = boardChainScore(&g); gained = boardSubmit(&g);
    CHECK("word through one diamond scores x1.5", preview == gained && gained == bedBase * 15 && g.lastWordDiamonds == 1);
    boardOf('e'); plantWord();
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    int first = boardSubmit(&g);
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    preview = boardChainScore(&g); gained = boardSubmit(&g);
    CHECK("repeated word scores half", preview == gained && gained == first / 2 && g.lastWordRepeat == 1 && g.ledgerN == 2 && g.uniqueWords == 1 && g.wordsThisShelf == 2);
    boardOf('e'); plantWord();
    strcpy(g.ledger[0], "BED"); g.ledgerN = 1; g.ledgerHead = 1;   // an uppercase ledger entry still counts as the same word
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    preview = boardChainScore(&g); gained = boardSubmit(&g);
    CHECK("repeat check ignores case", preview == gained && gained == bedBase * 5 && g.lastWordRepeat == 1 && strcmp(g.ledger[1], "bed") == 0);
    CHECK("ledgerIndex: newest first", ledgerIndex(&g, 0) == 1 && ledgerIndex(&g, 1) == 0);
    boardOf('e'); plantWord();
    for (int r = 0; r < 7; r++) set(1, r, CELL_TILE, "letters"[r]);
    for (int r = 0; r < 7; r++) boardTryAddToChain(&g, 1, r);
    preview = boardChainScore(&g); gained = boardSubmit(&g);
    int lettersBase = 0; for (int i = 0; i < 7; i++) lettersBase += letterValue("letters"[i] - 'a');
    CHECK("seven letters: x3 and +200", preview == gained && gained == lettersBase * 30 + 200);
    for (int i = 0; i < LEDGER_N + 2; i++) { strcpy(g.ledger[g.ledgerHead], "x"); g.ledgerHead = (g.ledgerHead + 1) % LEDGER_N; if (g.ledgerN < LEDGER_N) g.ledgerN++; }
    g.shelf = 1; g.mode = MODE_CLASSIC; boardNewShelf(&g);
    CHECK("boardNewShelf empties the ledger", g.ledgerN == 0 && g.ledgerHead == 0 && g.uniqueWords == 0 && g.passBonusKind == 0 && g.speedBonus == 0 && g.peelBonus == 0);

    // ---- heat table
    CHECK("boardHeatFor: 3, 2, 1, 1, 0, 0", boardHeatFor(3) == 3 && boardHeatFor(4) == 2 && boardHeatFor(5) == 1 && boardHeatFor(6) == 1 && boardHeatFor(7) == 0 && boardHeatFor(10) == 0);

    // ---- per-segment refill: the gems under a hole come back
    boardOf('e');
    set(0, 4, CELL_HOLE, 0); set(0, 5, CELL_EMPTY, 0); set(0, 6, CELL_EMPTY, 0);
    boardDebugFireStep(&g);   // no flames: just the gravity pass
    CHECK("refill below a hole", st(0, 4) == CELL_HOLE && st(0, 5) == CELL_TILE && st(0, 6) == CELL_TILE && st(0, 7) == CELL_TILE &&
          g.cell[0][6].yoff < 0 && g.cell[0][5].yoff < g.cell[0][6].yoff && g.cell[0][7].letter == 'e' - 'a');
    boardOf('e');
    set(0, 2, CELL_SECRET, 'b'); set(0, 3, CELL_EMPTY, 0); set(0, 4, CELL_TILE, 'c'); set(0, 5, CELL_EMPTY, 0);
    boardDebugFireStep(&g);
    CHECK("segment under a diamond compacts then refills from its top",
          st(0, 2) == CELL_SECRET && st(0, 3) == CELL_TILE && g.cell[0][3].yoff < 0 && g.cell[0][5].letter == 'c' - 'a' && g.cell[0][4].yoff < 0 && countState(CELL_EMPTY) == 0);

    // ---- a word is a turn: heat by length, the cleared cells refill before the fire moves
    boardOf('e'); plantWord();
    set(0, 1, CELL_BURN, 'a'); set(0, 2, CELL_TILE, 'b'); set(0, 3, CELL_TILE, 'e'); set(0, 4, CELL_TILE, 'd');
    g.burnEvery = 99; g.burnTimer = 0;
    boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3); boardTryAddToChain(&g, 0, 4);
    gained = boardSubmit(&g);
    CHECK("a flame hangs over the word cleared beneath it (gems refill under it)",
          gained > 0 && st(0, 1) == CELL_BURN && st(0, 2) == CELL_TILE && st(0, 3) == CELL_TILE && st(0, 4) == CELL_TILE && g.cell[0][2].yoff < 0 && countState(CELL_BURN) == 1);
    boardOf('e'); plantWord();
    set(0, 1, CELL_BURN, 'a'); set(0, 2, CELL_TILE, 'b'); set(0, 3, CELL_TILE, 'e'); set(0, 4, CELL_TILE, 'd');
    g.burnEvery = 1; g.burnTimer = 0;
    boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3); boardTryAddToChain(&g, 0, 4);
    gained = boardSubmit(&g);
    CHECK("three-letter word: +3 heat, one turn", gained > 0 && g.turn == 1 && g.heat == 3 && g.fail == FAIL_NONE);
    CHECK("no chimney through your own word: the flame burns the refilled gem one row down",
          st(0, 1) == CELL_TILE && st(0, 2) == CELL_BURN && countState(CELL_BURN) == 1 && countState(CELL_EMPTY) == 0);

    // ---- spawn skips holes and ignites the topmost tile
    boardOf('e'); plantWord();
    for (int c = 0; c < COLS; c++) set(c, 0, CELL_HOLE, 0);
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    g.heatThreshold = 3; g.heat = 1;
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    gained = boardSubmit(&g);
    bool rowOneFlame = false;
    for (int c = 0; c < COLS; c++) if (st(c, 1) == CELL_BURN) rowOneFlame = true;
    CHECK("spawn skips the holes in row 0 and lights row 1",
          gained > 0 && g.heat == 0 && countState(CELL_BURN) == 1 && rowOneFlame && countState(CELL_HOLE) == COLS &&
          g.fireN == 1 && g.fireR[0] == 1 && st(g.fireC[0], 1) == CELL_BURN);

    // ---- Lockdown tier 2 lights two columns
    boardOf('e'); plantWord();
    set(0, 1, CELL_TILE, 'b'); set(0, 2, CELL_TILE, 'e'); set(0, 3, CELL_TILE, 'd');
    g.heatThreshold = 3; g.heat = 2; g.lockdown = true; g.lockTier = 2; g.pulseEvery = 4;
    boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2); boardTryAddToChain(&g, 0, 3);
    boardSubmit(&g);
    CHECK("Lockdown tier 2 ignites two different columns", countState(CELL_BURN) == 2 && g.fireN == 2 && g.fireC[0] != g.fireC[1] && g.fireR[0] == 0 && g.fireR[1] == 0);

    // ---- heat clamps to threshold-1 when nothing can burn (all diamonds)
    boardOf('e');
    for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) set(c, r, CELL_SECRET, 'e');
    set(1, 1, CELL_SECRET, 'b'); set(1, 2, CELL_SECRET, 'e'); set(1, 3, CELL_SECRET, 'd');
    g.heatThreshold = 3; g.heat = 3;
    boardTryAddToChain(&g, 1, 1); boardTryAddToChain(&g, 1, 2); boardTryAddToChain(&g, 1, 3);
    gained = boardSubmit(&g);
    CHECK("no tile to light: heat clamps to threshold - 1", gained > 0 && g.heat == 2 && countState(CELL_BURN) == 0 && g.fireN == 0 && g.fail == FAIL_NONE);

    // ---- overtime and the Lockdown pulse
    boardOf('e'); plantWord();
    g.turn = 30;
    boardPeel(&g, 2, 2);
    CHECK("overtime past turn 24 adds heat", g.turn == 31 && g.heat == 2 && g.overtime);
    boardOf('e'); plantWord();
    g.lockdown = true; g.pulseEvery = 3; g.turn = 2;
    boardPeel(&g, 2, 2);
    CHECK("Lockdown pulse every pulseEvery turns", g.turn == 3 && g.heat == 4 && !g.overtime);

    // ---- dfsWords depth 5: a five-letter-only board still has words
    boardOf('x');
    set(2, 1, CELL_TILE, 'b'); set(2, 2, CELL_TILE, 'r'); set(2, 3, CELL_TILE, 'i'); set(2, 4, CELL_TILE, 'n'); set(2, 5, CELL_TILE, 'g');
    CHECK("boardHasWords finds a five-letter word", boardHasWords(&g));

    // ---- shelf thresholds
    S.fire = FIRE_NORMAL;
    memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC;
    g.shelf = 1; boardNewShelf(&g);
    bool s1 = g.heatThreshold == 10 && g.burnEvery == 2 && !g.lockdown && g.lockTier == 0;
    g.shelf = 4; boardNewShelf(&g);
    bool s4 = g.heatThreshold == 7 && g.burnEvery == 1 && !g.lockdown;
    g.shelf = 6; boardNewShelf(&g);
    bool s6 = g.heatThreshold == 5 && g.lockdown && g.lockTier == 0 && g.pulseEvery == 6;
    g.shelf = 12; boardNewShelf(&g);
    bool s12 = g.heatThreshold == 4 && g.lockTier == 2 && g.pulseEvery == 4;
    g.shelf = 20; boardNewShelf(&g);
    bool s20 = g.heatThreshold == 4 && g.lockTier == 3 && g.pulseEvery == 3;
    CHECK("shelf thresholds 1/4/6/12/20", s1 && s4 && s6 && s12 && s20);
    g.mode = MODE_DAILY; g.dailySeed = 20260101; g.shelf = 1; boardNewShelf(&g);
    CHECK("Daily: threshold 7, fire every other turn, not Lockdown", g.heatThreshold == 7 && g.burnEvery == 2 && !g.lockdown);
    g.mode = MODE_CLASSIC; S.fire = FIRE_FAST; g.shelf = 1; boardNewShelf(&g);
    bool fast = g.heatThreshold == 8 && g.burnEvery == 1;
    S.fire = FIRE_RELAXED; g.shelf = 6; boardNewShelf(&g);
    bool relaxed = g.heatThreshold == 8 && g.burnEvery == 2;
    S.fire = FIRE_NORMAL;
    CHECK("FAST and RELAXED adjust the thresholds", fast && relaxed);

    // ---- note table
    u16 hz[128];
    musicBuildNotes(hz);
    CHECK("note table C4 = 262 Hz", hz[60] == 262);
    CHECK("note table A4 = 440 Hz", hz[69] == 440);
    CHECK("note table octaves double", hz[81] == 880 && hz[57] == 220);

    printf("%s: %d failure(s)\n", fails ? "CHECK FAILED" : "CHECK OK", fails);
    return fails;
}
