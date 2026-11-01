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

    // ---- peeling
    boardOf('e');
    set(0, 1, CELL_BURN, 'a');
    int res = boardPeel(&g, 0, 2);
    CHECK("peel under a flame scorches a plain cell", res == 3 && st(0, 2) == CELL_HOLE && g.scorched[0][2] == 1 && g.fail == FAIL_NONE);
    boardOf('e');
    set(0, 1, CELL_BURN, 'a'); g.back[0][2] = 1;
    res = boardPeel(&g, 0, 2);
    CHECK("peel under a flame destroys a pass letter", res == -1 && g.fail == FAIL_SCORCH && st(0, 2) == CELL_HOLE);
    boardOf('e');
    g.back[3][3] = 2;
    res = boardPeel(&g, 3, 3);
    CHECK("peel reveals a pass letter as a diamond", res == 2 && st(3, 3) == CELL_SECRET && g.cell[3][3].letter == 'o' - 'a' && g.found[1] == 1);
    boardOf('x');
    res = boardPeel(&g, 2, 2);
    CHECK("peel on an all-x board is OUT OF WORDS", res == 1 && g.fail == FAIL_WORDS && st(2, 2) == CELL_HOLE);
    boardOf('e');
    CHECK("boardHasWords finds nothing on an all-e board", !boardHasWords(&g));
    set(1, 1, CELL_TILE, 'b'); set(1, 2, CELL_TILE, 'e'); set(1, 3, CELL_TILE, 'd');
    CHECK("boardHasWords finds bed", boardHasWords(&g));

    // ---- note table
    u16 hz[128];
    musicBuildNotes(hz);
    CHECK("note table C4 = 262 Hz", hz[60] == 262);
    CHECK("note table A4 = 440 Hz", hz[69] == 440);
    CHECK("note table octaves double", hz[81] == 880 && hz[57] == 220);

    printf("%s: %d failure(s)\n", fails ? "CHECK FAILED" : "CHECK OK", fails);
    return fails;
}
