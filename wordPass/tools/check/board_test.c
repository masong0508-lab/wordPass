// Native rule tests: board.c, dict.c and music.c compiled with gcc against the stubs in
// tools/check/stub. Run with `make check` from the repo root (loads data/dict.bin).
// Each scenario prints PASS or FAIL; the exit status is the number of failures.
#include "game.h"
#include "dict_bin.h"
#include "pools_bin.h"
#include "variants_bin.h"
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

static u8 *loadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s (run from the repo root)\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); return NULL; }
    fclose(f);
    return buf;
}

static bool loadDict(const char *path, const char *poolPath, const char *variantPath)
{
    u8 *d = loadFile(path), *p = loadFile(poolPath), *v = loadFile(variantPath);
    if (!d || !p || !v) return false;
    dict_bin = d; variants_bin = v; dictInit();
    pools_bin = p; poolsInit();
    return true;
}

int main(int argc, char **argv)
{
    if (!loadDict(argc > 1 ? argv[1] : "data/dict.bin", argc > 2 ? argv[2] : "data/pools.bin", argc > 3 ? argv[3] : "data/variants.bin")) return 1;
    srand(1);

    // ---- dictionary tiers
    CHECK("dictLookup zax is rare", dictLookup("zax", 3) == 2);
    CHECK("dictLookup book is common", dictLookup("book", 4) == 1);
    CHECK("dictLookup rejects a non-word", dictLookup("xxx", 3) == 0);
    S.library = LIB_COMMON;
    CHECK("LIBRARY COMMON rejects a rare word", !boardWordOk("zax", 3) && boardWordOk("book", 4));
    S.library = LIB_FULL;

    // ---- long words: the dictionary holds words up to 20 letters
    {
        static const char *const LONG[] = { "lophophores", "livingnesses", "marlinespikes", "microcephalics", "majoritarianism", "magnetostrictive", "kindheartednesses", "largeheartednesses", "interavailabilities", "lymphogranulomatoses" };
        bool all = true, shortFails = true;
        for (int n = 11; n <= 20; n++) {
            const char *lw = LONG[n - 11];
            if ((int)strlen(lw) != n || dictLookup(lw, n) == 0) { all = false; printf("   %d-letter %s missing\n", n, lw); }
        }
        char junk[21]; memset(junk, 'z', 20); junk[20] = 0;
        for (int n = 3; n <= 20; n++) if (dictLookup(junk, n) != 0) shortFails = false;
        CHECK("dictLookup finds a word of every length 11..20", all);
        CHECK("dictLookup rejects zzz... at every length 3..20 and anything over 20", shortFails && dictLookup("zzzzzzzzzzzzzzzzzzzzz", 21) == 0);
        CHECK("international and counterrevolutionary are words", dictLookup("international", 13) != 0 && dictLookup("counterrevolutionary", 20) != 0);
    }

    // ---- a 20-gem chain: a snake up and down three columns spells counterrevolutionary
    {
        boardOf('e');
        static const char *const W20 = "counterrevolutionary";
        int path[20][2], n = 0;
        for (int r = 0; r < 8; r++) { path[n][0] = 0; path[n][1] = r; n++; }
        for (int r = 7; r >= 0; r--) { path[n][0] = 1; path[n][1] = r; n++; }
        for (int r = 0; r < 4; r++) { path[n][0] = 2; path[n][1] = r; n++; }
        bool ok20 = true;
        for (int i = 0; i < 20; i++) { set(path[i][0], path[i][1], CELL_TILE, W20[i]); }
        for (int i = 0; i < 20; i++) ok20 = boardTryAddToChain(&g, path[i][0], path[i][1]) && ok20;
        bool capped = g.chainLen == 20 && !boardTryAddToChain(&g, 2, 4) && g.chainLen == 20;
        char cw[WORD_BUF]; boardChainWord(&g, cw);
        bool spelled = strcmp(cw, W20) == 0 && boardChainScore(&g) > 0;
        int pts = boardSubmit(&g);
        CHECK("a 20-gem chain spells and scores a 20-letter word", ok20 && capped && spelled && pts > 0 && g.lastWordLen == 20 &&
              strcmp(g.lastWord, W20) == 0 && strcmp(g.ledger[0], W20) == 0 && g.chainLen == 0);
    }

    // ---- spelling: BOTH accepts either, AMERICAN or BRITISH only its own; same-everywhere words always pass
    {
        S.spelling = SPELL_BOTH;
        bool both = boardWordOk("color", 5) && boardWordOk("colour", 6) && boardWordOk("tour", 4) && boardWordOk("organize", 8) && boardWordOk("organise", 8);
        S.spelling = SPELL_US;
        bool us = boardWordOk("color", 5) && !boardWordOk("colour", 6) && boardWordOk("tour", 4) && boardWordOk("organise", 8) &&
                  boardWordOk("center", 6) && !boardWordOk("centre", 6) && boardWordOk("traveled", 8) && !boardWordOk("travelled", 9);
        S.spelling = SPELL_UK;
        bool uk = !boardWordOk("color", 5) && boardWordOk("colour", 6) && boardWordOk("tour", 4) && boardWordOk("organize", 8) &&
                  !boardWordOk("center", 6) && boardWordOk("centre", 6) && boardWordOk("grey", 4) && !boardWordOk("gray", 4);
        S.spelling = SPELL_US;
        bool why = boardWordSpelling("colour", 6) == 2 && boardWordSpelling("color", 5) == 0 && boardWordSpelling("tour", 4) == 0 && boardWordSpelling("zzzz", 4) == 0;
        S.spelling = SPELL_UK;
        why = why && boardWordSpelling("color", 5) == 1 && boardWordSpelling("colour", 6) == 0;
        S.spelling = SPELL_BOTH;
        CHECK("SPELLING BOTH accepts color and colour and the -ize/-ise forms", both);
        CHECK("SPELLING AMERICAN rejects British-only spellings and keeps shared words", us);
        CHECK("SPELLING BRITISH rejects American-only spellings and keeps shared words", uk);
        CHECK("boardWordSpelling names the dialect that rejected a word", why);
        bool pools = true;
        for (int t = 0; t < poolThemeCount() && pools; t++)
            for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN && pools; len++)
                for (int i = 0; i < poolCount(t, len); i++) { char pw[12]; poolWord(t, len, i, pw); if (dictSpelling(pw, len)) { pools = false; printf("   %s is spelled one way only\n", pw); break; } }
        CHECK("no pass word has a US-only or UK-only spelling", pools);
    }

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
    char w[WORD_BUF]; boardChainWord(&g, w);
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
    boardChainTouch(&g, 5, 5, true);            // a tap elsewhere: a fresh chain
    CHECK("chain backtracking", three && two && one && g.chainLen == 1 && g.chainC[0] == 5 && g.chainR[0] == 5);
    boardTryAddToChain(&g, 5, 6); boardTryAddToChain(&g, 5, 7);
    bool rejected = !boardTryAddToChain(&g, 5, 5);   // two steps back: not a backtrack, a revisit
    CHECK("chain rejects a revisit of an earlier tile", rejected && g.chainLen == 3);

    // ---- chain input grammar (item 10): truncate, bridge, restart only on a tap, submit tap
    boardOf('e');
    {
        // column 0 is a vertical run, so (0,r) neighbours (0,r+-1) only within the column
        bool t1 = boardChainTouch(&g, 0, 0, true) == CT_ADDED;
        bool t2 = boardChainTouch(&g, 0, 1, false) == CT_ADDED;
        bool t3 = boardChainTouch(&g, 0, 2, false) == CT_ADDED;
        bool t4 = boardChainTouch(&g, 0, 3, false) == CT_ADDED;
        CHECK("drag over earlier gems does nothing", t1 && t2 && t3 && t4 && boardChainTouch(&g, 0, 0, false) == CT_IGNORED && g.chainLen == 4);
        CHECK("tail tap on a chain of three or more is a submit tap", boardChainTouch(&g, 0, 3, true) == CT_SUBMIT_TAP && g.chainLen == 4);
        CHECK("tail drag is ignored", boardChainTouch(&g, 0, 3, false) == CT_IGNORED);
        CHECK("tap on an earlier gem cuts the chain back", boardChainTouch(&g, 0, 1, true) == CT_TRUNCATED && g.chainLen == 2 && g.chainC[1] == 0 && g.chainR[1] == 1);
        CHECK("tap on the previous gem backtracks one", boardChainTouch(&g, 0, 0, true) == CT_BACKTRACKED && g.chainLen == 1);
        CHECK("tail tap under three gems is ignored", boardChainTouch(&g, 0, 0, true) == CT_IGNORED);
    }
    boardOf('e');
    {
        boardChainTouch(&g, 0, 0, true);
        // (0,0) and (1,1) share exactly the neighbour (0,1)? column 1 is odd: (1,0),(1,1) touch (0,0)... (0,0) even
        // neighbours (0,1),(1,-1)x,(1,0); (1,1) is odd with neighbours (1,0),(1,2),(0,1),(0,2),(2,1),(2,2): shared (1,0) and (0,1) = two
        CHECK("two possible bridges: a drag is not adjacent", boardChainTouch(&g, 1, 1, false) == CT_NOT_ADJACENT && g.chainLen == 1);
        // (0,0) and (0,2): the only common neighbour is (0,1)
        CHECK("one possible bridge: bridged", boardChainTouch(&g, 0, 2, false) == CT_BRIDGED && g.chainLen == 3 && g.chainR[1] == 1 && g.chainR[2] == 2);
        CHECK("a far gem on a drag keeps the chain", boardChainTouch(&g, 5, 5, false) == CT_NOT_ADJACENT && g.chainLen == 3);
        CHECK("a far gem on a tap restarts", boardChainTouch(&g, 5, 5, true) == CT_RESTARTED && g.chainLen == 1 && g.chainC[0] == 5);
        set(0, 1, CELL_HOLE, 'a'); boardClearChain(&g); boardChainTouch(&g, 0, 0, true);
        CHECK("a bridge cell that is a hole is not available", boardChainTouch(&g, 0, 2, false) == CT_NOT_ADJACENT);
    }
    boardOf('e');
    {   // a full chain cannot take a bridge; the tail tap submits a valid chain
        boardChainTouch(&g, 0, 0, true);
        for (int r = 1; r < 8; r++) boardChainTouch(&g, 0, r, false);
        for (int r = 7; r >= 0; r--) boardChainTouch(&g, 1, r, false);
        for (int r = 0; r < 3; r++) boardChainTouch(&g, 2, r, false);   // 8 + 8 + 3 = 19
        CHECK("19 gems chained", g.chainLen == 19);
        CHECK("no room for a two-gem bridge", boardChainTouch(&g, 2, 4, false) == CT_NOT_ADJACENT && g.chainLen == 19);
    }

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
    CHECK("ledger entries hold a 10-gem chain through two Qu gems (12 chars)", sizeof(g.ledger[0]) >= 13);

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
    g.shelf = 4; boardNewShelf(&g);   // shelf 4 is a twist shelf: DAMP slows the fire and lowers the meter
    bool s4 = (g.twist == TWIST_DAMP ? (g.heatThreshold == 5 && g.burnEvery == 2) : (g.heatThreshold == 7 && g.burnEvery == 1)) && !g.lockdown;
    g.shelf = 6; boardNewShelf(&g);
    bool s6 = g.heatThreshold == 5 && g.lockdown && g.lockTier == 0 && g.pulseEvery == 6;
    g.shelf = 12; boardNewShelf(&g);
    bool s12 = g.heatThreshold == 4 && g.lockTier == 2 && g.pulseEvery == 4;
    g.shelf = 20; boardNewShelf(&g);
    bool s20 = g.heatThreshold == 4 && g.lockTier == 3 && g.pulseEvery == 3;
    CHECK("shelf thresholds 1/4/6/12/20", s1 && s4 && s6 && s12 && s20);
    int stdDay = 20260101;
    while (boardDailyMod(stdDay) != DAILYMOD_STANDARD) stdDay++;       // a STANDARD date: the base Daily rules
    g.mode = MODE_DAILY; g.dailySeed = stdDay; g.shelf = 1; boardNewShelf(&g);
    CHECK("Daily: threshold 7, fire every other turn, not Lockdown", g.heatThreshold == 7 && g.burnEvery == 2 && !g.lockdown);
    g.mode = MODE_CLASSIC; S.fire = FIRE_FAST; g.shelf = 1; boardNewShelf(&g);
    bool fast = g.heatThreshold == 8 && g.burnEvery == 1;
    S.fire = FIRE_RELAXED; g.shelf = 6; boardNewShelf(&g);
    bool relaxed = g.heatThreshold == 8 && g.burnEvery == 2;
    S.fire = FIRE_NORMAL;
    CHECK("FAST and RELAXED adjust the thresholds", fast && relaxed);

    // ---- letter bag (item 08): 100 letters a cycle, two of J/Q/X/Z, the rest by weight
    {
        static const u8 W[26] = {9,2,3,4,12,2,3,3,9,1,1,4,3,6,8,2,1,6,5,6,4,2,2,1,2,1};
        int count[26] = {0}, rare1 = 0, rare2 = 0, bad = 0;
        boardSeed(7); boardBagReset();
        int seq1[100], seq2[100];
        for (int i = 0; i < 100; i++) { seq1[i] = boardRandomLetter(); count[seq1[i]]++; }
        for (int l = 0; l < 26; l++) {
            bool rare = l == 9 || l == 16 || l == 23 || l == 25;
            if (rare) rare1 += count[l];
            else if (count[l] != W[l]) bad++;
        }
        int count2[26] = {0};
        for (int i = 0; i < 100; i++) { seq2[i] = boardRandomLetter(); count2[seq2[i]]++; }
        for (int l = 0; l < 26; l++) if (l == 9 || l == 16 || l == 23 || l == 25) rare2 += count2[l];
        bool rotated = true;   // the second cycle holds the other pair
        for (int l = 0; l < 26; l++) if ((l == 9 || l == 16 || l == 23 || l == 25) && count[l] && count2[l]) rotated = false;
        CHECK("bag: 100 letters per cycle by weight with exactly two of J/Q/X/Z, the pair rotating", rare1 == 2 && rare2 == 2 && bad == 0 && rotated);
        boardSeed(7); boardBagReset();
        bool same = true;
        for (int i = 0; i < 100; i++) if (boardRandomLetter() != seq1[i]) same = false;
        for (int i = 0; i < 100; i++) if (boardRandomLetter() != seq2[i]) same = false;
        CHECK("boardSeed(k) twice gives identical letter sequences", same);
        // boardRand itself is a deterministic xorshift
        boardSeed(12345); u32 a = boardRand(), b = boardRand();
        boardSeed(12345);
        CHECK("boardRand repeats after boardSeed", boardRand() == a && boardRand() == b && a != b);
    }

    // ---- vowel floor: a four-tile refill of consonants gets a vowel in its lowest new gem
    {
        int seed = 0; bool found = false;
        for (seed = 1; seed < 5000 && !found; seed++) {
            boardSeed(seed); boardBagReset();
            bool allCons = true;
            for (int i = 0; i < 4; i++) { int l = boardRandomLetter(); if (l == 0 || l == 4 || l == 8 || l == 14 || l == 20) allCons = false; }
            if (allCons) found = true;
        }
        seed--;
        CHECK("found a seed whose first four draws are consonants", found);
        boardOf('e');
        for (int r = 0; r < 4; r++) set(0, r, CELL_EMPTY, 0);
        boardSeed(seed); boardBagReset();
        boardDebugFireStep(&g);   // no flames: just the gravity refill of column 0
        int l3 = g.cell[0][3].letter;
        bool lowVowel = l3 == 0 || l3 == 4 || l3 == 8 || l3 == 14 || l3 == 20;
        int vowelsAbove = 0;
        for (int r = 0; r < 3; r++) { int l = g.cell[0][r].letter; if (l == 0 || l == 4 || l == 8 || l == 14 || l == 20) vowelsAbove++; }
        CHECK("vowel floor: the lowest of four consonant refills becomes a vowel", st(0, 3) == CELL_TILE && lowVowel && vowelsAbove == 0);
        // the swap keeps the cycle's composition: the remaining 96 draws plus the 4 on the board hold 42 vowels
        int vowels = lowVowel ? 1 : 0;
        for (int i = 0; i < 96; i++) { int l = boardRandomLetter(); if (l == 0 || l == 4 || l == 8 || l == 14 || l == 20) vowels++; }
        CHECK("vowel floor swap preserves the bag's composition (42 vowels a cycle)", vowels == 42);
    }

    // ---- the pass library: shape, validity, no repeats, content ceiling
    {
        int themes = poolThemeCount();
        CHECK("pass library has 40 themes", themes == 40);
        bool names = true, cover = true, valid = true, dups = true, clean = true, total3k = true, early = true;
        int total = 0;
        static const char *const NEVER[] = { "ass", "anal", "anus", "tits", "boob", "sex", "dyke", "fag", "nude", "naked",
                                             "shit", "piss", "cunt", "cock", "dick", "bitch", "whore", "slut", "rape", "porn" };
        for (int t = 0; t < themes; t++) {
            int nl = strlen(poolName(t));
            if (nl < 3 || nl > 7) names = false;
            for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN; len++) {
                int n = poolCount(t, len);
                if (len <= 6 && n < 8 && strcmp(poolName(t), "LEXICON")) { cover = false; printf("   %s has %d words of length %d\n", poolName(t), n, len); }
                total += n;
                for (int i = 0; i < n; i++) {
                    char pw[12], pv[12];
                    poolWord(t, len, i, pw);
                    if ((int)strlen(pw) != len || dictLookup(pw, len) == 0) { valid = false; printf("   %s: %s not a dictionary word\n", poolName(t), pw); }
                    for (int j = i + 1; j < n; j++) { poolWord(t, len, j, pv); if (!strcmp(pw, pv)) dups = false; }
                    for (unsigned k = 0; k < sizeof(NEVER) / sizeof(NEVER[0]); k++) if (!strcmp(pw, NEVER[k])) clean = false;
                }
            }
        }
        total3k = total >= 17500;
        CHECK("theme names are 3 to 7 letters (they sit in a 32 character intro line)", names);
        CHECK("every theme but LEXICON has at least 8 words at each length 4..6", cover);
        CHECK("every pass word is a dictionary word of its stored length", valid);
        CHECK("no theme repeats a word at one length", dups);
        CHECK("the library holds at least 17500 pass words", total3k);
        CHECK("no strong profanity is in the library (ceiling: holy damn crappola moobs)", clean);
        bool mild = false;
        for (int t = 0; t < themes; t++) if (!strcmp(poolName(t), "CURSES")) {
            char pw[12]; int hit = 0;
            static const char *const FOUR[] = { "holy", "damn", "crappola", "moobs" };
            for (int f = 0; f < 4; f++) for (int i = 0; i < poolCount(t, (int)strlen(FOUR[f])); i++) { poolWord(t, (int)strlen(FOUR[f]), i, pw); if (!strcmp(pw, FOUR[f])) hit++; }
            mild = hit == 4;
        }
        CHECK("the CURSES theme carries holy, damn, crappola and moobs", mild);
        bool pickAll = true;
        for (int sh = 1; sh <= 80; sh++) {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = sh; boardNewShelf(&g);
            char pw[12];
            bool inPool = false;
            for (int i = 0; i < poolCount(g.passTheme, g.passLen); i++) { poolWord(g.passTheme, g.passLen, i, pw); if (!strcmp(pw, g.pass)) inPool = true; }
            if (!inPool || (int)strlen(g.pass) != g.passLen || g.passTheme != (sh - 1) % themes) pickAll = false;
        }
        CHECK("shelves 1..80 cycle through all 40 themes and always pick a pool word", pickAll);
        bool lately = true;   // twenty passes from one theme and length: the last eight are never repeated
        {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = 1;
            char hist[40][12];
            for (int k = 0; k < 40; k++) {
                boardNewShelf(&g); strcpy(hist[k], g.pass);
                for (int b = k - 8 < 0 ? 0 : k - 8; b < k; b++) if (!strcmp(hist[b], g.pass)) lately = false;
            }
        }
        CHECK("a pass is never one of the previous eight in the same theme", lately);
        // familiarity window: shelf s only reaches the first 48 + 24 s words of a pool
        for (int k = 0; k < 60 && early; k++) {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = 28 + 0 * k; boardNewShelf(&g);   // TRAITS, 6 letters
            int idx = -1; char pw[12];
            for (int i = 0; i < poolCount(g.passTheme, g.passLen); i++) { poolWord(g.passTheme, g.passLen, i, pw); if (!strcmp(pw, g.pass)) idx = i; }
            if (idx < 0 || idx >= 48 + 24 * 28) early = false;
        }
        CHECK("a shelf-28 pass comes from the first 720 words of its pool", early);
    }

    // ---- pass pools: shelf 2 draws a 4-letter INK word whatever the recent ring holds
    {
        bool ok4 = true, theme1 = true;
        for (int variant = 0; variant < 6; variant++) {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = 2;
            for (int i = 0; i < 8; i++) {
                if (variant == 1) g.recentPass[i] = PASS_KEY(1, 4, i);            // the first eight 4-letter INK words
                if (variant == 2) g.recentPass[i] = PASS_KEY(1, 4, i & 3);        // four of them, twice over
                if (variant == 3) g.recentPass[i] = PASS_KEY(1, 4, 3);            // one word eight times
                if (variant == 4) g.recentPass[i] = PASS_KEY(i & 7, 4, 0);        // one word of every early pool
                if (variant == 5) g.recentPass[i] = (u32)(i * 7919 + 5);          // garbage
            }
            for (int k = 0; k < 20; k++) {
                boardNewShelf(&g);
                if (g.passLen != 4) ok4 = false;
                if (g.passTheme != 1) theme1 = false;
            }
        }
        CHECK("shelf 2 pass has 4 letters and theme INK for every recentPass state", ok4 && theme1);
        memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = 1; boardNewShelf(&g);
        CHECK("shelf 1 pass: 4 letters from BINDERY", g.passLen == 4 && g.passTheme == 0 && strcmp(boardPassTheme(&g), "BINDERY") == 0 && g.twist == TWIST_NONE);
        bool distinct = false; char firstPass[12]; strcpy(firstPass, g.pass);
        for (int k = 0; k < 2; k++) { boardNewShelf(&g); if (strcmp(g.pass, firstPass)) distinct = true; }
        CHECK("three shelf-1 picks in a row rotate through the pool", distinct);
        bool lens = true;
        int want[] = {0, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8};
        for (int sh = 1; sh <= 14; sh++) {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = sh; boardNewShelf(&g);
            int extra = g.twist == TWIST_BURIED ? 1 : 0;
            // the nearest length present in the pool stands in if a theme lacks the target
            int target = want[sh] + extra, got = g.passLen;
            bool near = got == target || (got == target - 1) || (got == target + 1) || (got == target + 2);
            if (!near || g.passTheme != (sh - 1) % poolThemeCount()) { lens = false; printf("   shelf %d: %s (%d, want %d)\n", sh, g.pass, got, target); }
        }
        CHECK("pass length climbs with the shelf (4,4,5,5,5,6,6,6,7,7,7,7,8,8) within the pool's nearest length", lens);
    }

    // ---- Daily: the same date gives the same board, pass and refills; no twist
    {
        Game a, b;
        memset(&a, 0, sizeof(a)); a.mode = MODE_DAILY; a.dailySeed = 20260314; a.shelf = 1; boardNewShelf(&a);
        memset(&b, 0, sizeof(b)); b.mode = MODE_DAILY; b.dailySeed = 20260314; b.shelf = 1; boardNewShelf(&b);
        bool same = strcmp(a.pass, b.pass) == 0 && memcmp(a.back, b.back, sizeof(a.back)) == 0;
        for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (a.cell[c][r].letter != b.cell[c][r].letter) same = false;
        CHECK("Daily: identical board, back layer and pass for the same date", same && a.twist == TWIST_NONE && (a.passLen == 5 || a.passLen == 6 || a.passLen == 7));
        memset(&b, 0, sizeof(b)); b.mode = MODE_DAILY; b.dailySeed = 20260315; b.shelf = 1; boardNewShelf(&b);
        bool differ = false;
        for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (a.cell[c][r].letter != b.cell[c][r].letter) differ = true;
        CHECK("Daily: the next day is a different board", differ);
    }

    // ---- twists: shelves 4, 7, 10 ... (Classic only); INK SPILL holes, BURIED depth, DAMP fire
    {
        int seen[4] = {0}, bad = 0, inkBad = 0, buriedBad = 0, dampBad = 0;
        for (int k = 0; k < 60; k++) {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = 4 + 3 * (k % 4); boardNewShelf(&g);
            if (g.twist < 1 || g.twist > 3) { bad++; continue; }
            seen[g.twist]++;
            if (g.twist == TWIST_INK) {
                int holes = 0, holeBack = 0, holeRow = 0;
                for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (st(c, r) == CELL_HOLE) {
                    holes++; if (g.back[c][r]) holeBack++; if (r < 2 || r > 6) holeRow++; if (g.scorched[c][r]) holeBack++;
                }
                if (holes != 3 || holeBack || holeRow || !boardHasWords(&g)) inkBad++;
            } else if (countState(CELL_HOLE)) bad++;
            if (g.twist == TWIST_BURIED) {
                int minR = ROWS;
                for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (g.back[c][r] && r < minR) minR = r;
                int target = g.shelf <= 5 ? 5 : g.shelf <= 8 ? 6 : g.shelf <= 12 ? 7 : 8;
                if (minR < 5 || g.passLen < target) buriedBad++;
            }
            if (g.twist == TWIST_DAMP) {
                Game plain; memset(&plain, 0, sizeof(plain)); plain.mode = MODE_CLASSIC; plain.shelf = g.shelf + 1; boardNewShelf(&plain);
                int baseEvery = g.shelf <= 3 ? 2 : 1;
                int baseThr = g.shelf <= 2 ? 10 : g.shelf <= 5 ? 7 : 5 - g.lockTier / 2; if (baseThr < 3) baseThr = 3;
                int wantThr = baseThr - 2; if (wantThr < 3) wantThr = 3;
                if (g.burnEvery != baseEvery + 1 || g.heatThreshold != wantThr) dampBad++;
            }
        }
        CHECK("twist shelves roll one of the three twists", bad == 0 && seen[1] && seen[2] && seen[3]);
        CHECK("INK SPILL: exactly three unscorched holes in rows 2..6 over empty back cells, words left", inkBad == 0);
        CHECK("BURIED: every pass letter in rows 5..7 and the pass one letter longer", buriedBad == 0);
        CHECK("DAMP: fire every extra turn, meter two shorter (min 3)", dampBad == 0);
        memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC;
        bool none = true;
        for (int sh = 1; sh <= 12; sh++) { g.shelf = sh; boardNewShelf(&g); if ((sh < 4 || sh % 3 != 1) && g.twist) none = false; }
        CHECK("no twist on shelves 1-3 or off the 3n+1 beat", none);
        int repeats = 0; g.lastTwist = 0;
        for (int k = 0; k < 40; k++) { g.shelf = 7; int prev = g.lastTwist; boardNewShelf(&g); if (g.twist == prev) repeats++; }
        CHECK("a repeated twist is rerolled once (fewer than a third repeat)", repeats < 14);
        g.mode = MODE_DAILY; g.dailySeed = 20260101; g.shelf = 4; boardNewShelf(&g);
        CHECK("Daily never twists", g.twist == TWIST_NONE && countState(CELL_HOLE) == 0);
        // the pass never sits above minRow: shelf 12 buries it in rows 5..7
        g.mode = MODE_CLASSIC; bool deep = true;
        for (int k = 0; k < 20; k++) {
            g.shelf = 12; boardNewShelf(&g);
            for (int c = 0; c < COLS; c++) for (int r = 0; r < 5; r++) if (g.back[c][r]) deep = false;
        }
        CHECK("shelf 12: every pass letter in rows 5..7", deep);
    }

    // ---- Daily modifiers: a hash of the date, applied before any random draw
    {
        int seen[DAILYMOD_COUNT] = {0}, bad = 0, stdBad = 0, emberBad = 0, deepBad = 0, longBad = 0, seedBad = 0, sameBad = 0, nameBad = 0;
        for (int d = 0; d < 400; d++) {
            int day = 20260101 + d / 28 * 100 + d % 28;       // 28 days a month: always a real date
            if (day % 10000 / 100 > 12) continue;
            Game a, b;
            memset(&a, 0, sizeof(a)); a.mode = MODE_DAILY; a.dailySeed = day; a.shelf = 1; boardNewShelf(&a);
            memset(&b, 0, sizeof(b)); b.mode = MODE_DAILY; b.dailySeed = day; b.shelf = 1; boardNewShelf(&b);
            int m = boardDailyMod(day);
            if (m != (int)(((u32)((u32)day * 2654435761u) >> 28) & 3)) seedBad++;
            if (a.dailyMod != m) bad++;
            seen[m]++;
            bool same = strcmp(a.pass, b.pass) == 0 && memcmp(a.back, b.back, sizeof(a.back)) == 0 && a.dailyMod == b.dailyMod;
            for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (a.cell[c][r].letter != b.cell[c][r].letter) same = false;
            if (!same) sameBad++;
            if (a.twist != TWIST_NONE || a.lockdown) bad++;
            int minRow = ROWS;
            for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (a.back[c][r] && r < minRow) minRow = r;
            if (m == DAILYMOD_STANDARD && (a.heatThreshold != 7 || a.burnEvery != 2 || minRow < 3)) stdBad++;
            if (m == DAILYMOD_EMBER && (a.heatThreshold != 8 || a.burnEvery != 1)) emberBad++;
            if (m == DAILYMOD_DEEP && (minRow < 5 || a.heatThreshold != 7 || a.burnEvery != 2)) deepBad++;
            if (m == DAILYMOD_LONG && (poolCount(a.passTheme, 7) ? a.passLen != 7 : (a.passLen < 6 || a.passLen > 8))) longBad++;
            if (m != DAILYMOD_LONG && a.passLen == 7 && poolCount(a.passTheme, 5) && poolCount(a.passTheme, 6)) longBad++;   // 5 or 6 unless the pool lacks both
            if (!boardDailyModName(m)[0]) nameBad++;
        }
        CHECK("Daily modifier: every date has one, all four occur, the board stores it", bad == 0 && seedBad == 0 && seen[0] && seen[1] && seen[2] && seen[3]);
        CHECK("Daily modifier names", !strcmp(boardDailyModName(0), "STANDARD") && !strcmp(boardDailyModName(1), "EMBER") &&
              !strcmp(boardDailyModName(2), "DEEP") && !strcmp(boardDailyModName(3), "LONG") && nameBad == 0);
        CHECK("Daily STANDARD: meter 7, fire every other turn, pass from row 3", stdBad == 0);
        CHECK("Daily EMBER: meter 8 and the fire moves every turn", emberBad == 0);
        CHECK("Daily DEEP: every pass letter in rows 5..7", deepBad == 0);
        CHECK("Daily LONG: a seven-letter pass (the nearest length when the pool lacks one)", longBad == 0);
        CHECK("Daily with a modifier is still one board for everyone (same date, same board)", sameBad == 0);
    }

    // ---- lifetime stats: words, longest, best word, rare words, peels; a Daily replay leaves no trace
    {
        memset(&S, 0, sizeof(S)); S.music = 1;
        boardOf('e'); g.shelf = 1;
        set(0, 0, CELL_TILE, 'z'); set(0, 1, CELL_TILE, 'a'); set(0, 2, CELL_TILE, 'x');      // "zax": rare
        boardTryAddToChain(&g, 0, 0); boardTryAddToChain(&g, 0, 1); boardTryAddToChain(&g, 0, 2);
        int zaxPts = boardSubmit(&g);
        CHECK("stats: a word counts, a rare word counts as rare, longest and best word are kept",
              zaxPts > 0 && S.wordsSpelled == 1 && S.rareWords == 1 && S.longestLen == 3 && !strcmp(S.longestWord, "zax") &&
              S.bestWordPts == zaxPts && !strcmp(S.bestWordTxt, "zax"));
        boardOf('e'); g.shelf = 1;
        static const char *const W = "book";
        for (int i = 0; i < 4; i++) set(1, i, CELL_TILE, W[i]);
        for (int i = 0; i < 4; i++) boardTryAddToChain(&g, 1, i);
        int bookPts = boardSubmit(&g);
        CHECK("stats: a longer word takes LONGEST, a cheaper one leaves BEST WORD",
              bookPts > 0 && S.wordsSpelled == 2 && S.longestLen == 4 && !strcmp(S.longestWord, "book") &&
              (bookPts > zaxPts ? !strcmp(S.bestWordTxt, "book") : !strcmp(S.bestWordTxt, "zax")));
        boardOf('e'); g.shelf = 1; g.practice = true;
        for (int i = 0; i < 4; i++) set(1, i, CELL_TILE, "dogs"[i]);
        for (int i = 0; i < 4; i++) boardTryAddToChain(&g, 1, i);
        int before = S.wordsSpelled;
        CHECK("stats: a practice word scores but is not counted", boardSubmit(&g) > 0 && S.wordsSpelled == before && !strcmp(S.longestWord, "book"));
        boardOf('e'); g.shelf = 1; g.passLen = 4; strcpy(g.pass, "book");
        memset(g.back, 0, sizeof(g.back)); g.back[2][2] = 1;
        set(2, 2, CELL_TILE, 'q'); set(4, 4, CELL_TILE, 'q');
        int p0 = S.peels, h0 = S.peelsHit;
        int r1 = boardPeel(&g, 4, 4), r2 = boardPeel(&g, 2, 2);
        CHECK("stats: a peel counts, a hit counts as a hit", r1 == 1 && r2 == 2 && S.peels == p0 + 2 && S.peelsHit == h0 + 1);
        g.practice = true;
        set(5, 5, CELL_TILE, 'q'); boardPeel(&g, 5, 5);
        CHECK("stats: a practice peel is not counted", S.peels == p0 + 2 && S.peelsHit == h0 + 1);
        memset(&S, 0, sizeof(S)); S.music = 1;
    }

    // ---- note table
    u16 hz[128];
    musicBuildNotes(hz);
    CHECK("note table C4 = 262 Hz", hz[60] == 262);
    CHECK("note table A4 = 440 Hz", hz[69] == 440);
    CHECK("note table octaves double", hz[81] == 880 && hz[57] == 220);

    printf("%s: %d failure(s)\n", fails ? "CHECK FAILED" : "CHECK OK", fails);
    return fails;
}
