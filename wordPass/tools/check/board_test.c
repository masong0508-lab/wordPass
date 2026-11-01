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

static bool quietOk = true;
#define CHECK_QUIET(cond) do { if (!(cond)) quietOk = false; } while (0)

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

    // ---- the length multiplier and the heat preview behind the panel's HEAT line
    CHECK("boardLenMult: x1 to 4 letters, x2 for 5-6, x3 for 7-8, x4 beyond", boardLenMult(3) == 1 && boardLenMult(4) == 1 && boardLenMult(5) == 2 && boardLenMult(6) == 2 &&
          boardLenMult(7) == 3 && boardLenMult(8) == 3 && boardLenMult(9) == 4 && boardLenMult(20) == 4);
    {
        // boardTurnHeat is exactly what endTurn books: spell "bed" (heat 3) and a 7-letter word (heat 0)
        // on every turn 0..80 of a plain shelf and of a Lockdown shelf, and compare
        bool exact = true, pulse = false, over = false;
        for (int lock = 0; lock < 2; lock++)
            for (int turn = 0; turn <= 80; turn++)
                for (int word = 0; word < 2; word++) {
                    boardOf('e'); plantWord();
                    g.lockdown = lock; g.pulseEvery = 6 - lock; g.turn = turn; g.heat = 0; g.heatThreshold = 999;
                    int len = word ? 7 : 3;
                    for (int r = 0; r < len; r++) set(1, r, CELL_TILE, word ? "letters"[r] : "bed"[r]);
                    for (int r = 0; r < len; r++) boardTryAddToChain(&g, 1, r);
                    int want = boardTurnHeat(&g, boardHeatFor(len));
                    if (boardSubmit(&g) <= 0 || g.heat != want || g.turn != turn + 1) { exact = false; printf("   turn %d lock %d len %d: preview %d heat %d\n", turn, lock, len, want, g.heat); }
                    if (lock && want >= boardHeatFor(len) + 2 && turn < 16) pulse = true;
                    if (turn >= 24 && want > boardHeatFor(len)) over = true;
                    CHECK_QUIET(g.overtime == (turn + 1 > (lock ? 16 : 24)));
                }
        CHECK("boardTurnHeat matches the heat a word books on every turn, plain and Lockdown", exact && quietOk);
        CHECK("the preview includes the Lockdown pulse and the overtime heat", pulse && over);
        boardOf('e'); g.turn = 5; g.lockdown = 0; g.pulseEvery = 6;
        CHECK("a 7-letter word on a calm turn previews no heat; a 3-letter one previews 3",
              boardTurnHeat(&g, boardHeatFor(7)) == 0 && boardTurnHeat(&g, boardHeatFor(3)) == 3);
        g.turn = 23; CHECK("turn 24 is the last free one: a 7-letter word on it still previews no heat", boardTurnHeat(&g, 0) == 0);
        g.turn = 24; CHECK("turn 25 is overtime: a free word previews heat", boardTurnHeat(&g, 0) == 1);
    }

    // ---- the results tally
    {
        bool never = true, ends = true;
        int frames[3] = { 0, 0, 0 };
        static const int GAP[3] = { 3000, 30000, 3000000 };
        for (int k = 0; k < 3; k++) {
            int shown = 1000, target = 1000 + GAP[k], prev = shown;
            while (shown < target && frames[k] < 400) {
                shown = boardTallyNext(shown, target); frames[k]++;
                if (shown <= prev || shown > target) never = false;
                prev = shown;
            }
            if (shown != target) ends = false;
        }
        CHECK("the tally count always rises, never passes the target and arrives", never && ends);
        CHECK("a 3000-point shelf tallies in about half a second, a 30000-point one in under a second", frames[0] >= 20 && frames[0] <= 45 && frames[1] <= 60 && frames[2] <= 100);
        CHECK("the tally moves a sixth of the gap, at least 10 a frame, and stops at the target", boardTallyNext(100, 105) == 105 && boardTallyNext(100, 130) == 110 && boardTallyNext(100, 700) == 200);
        int o[3];
        boardTallySplit(0, 1000, 500, 400, o);   CHECK("tally 0: every row is 0", o[0] == 0 && o[1] == 0 && o[2] == 0);
        boardTallySplit(600, 1000, 500, 400, o); CHECK("a partial tally fills the shelf bonus first", o[0] == 600 && o[1] == 0 && o[2] == 0);
        boardTallySplit(1300, 1000, 500, 400, o); CHECK("then the speed bonus", o[0] == 1000 && o[1] == 300 && o[2] == 0);
        boardTallySplit(1800, 1000, 500, 400, o); CHECK("then the peel bonus", o[0] == 1000 && o[1] == 500 && o[2] == 300);
        boardTallySplit(99999, 1000, 500, 400, o); CHECK("a finished tally shows every bonus in full", o[0] == 1000 && o[1] == 500 && o[2] == 400);
        boardTallySplit(-5, 1000, 0, 0, o);       CHECK("a negative tally clamps to 0 and a zero bonus stays 0", o[0] == 0 && o[1] == 0 && o[2] == 0);
        boardTallySplit(1000, 1000, 0, 400, o);   CHECK("an empty speed bonus is skipped", o[0] == 1000 && o[1] == 0 && o[2] == 0);
    }

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

    // ---- the in-game guide (item 19): every page fits, every number comes from the rules
    {
        static char pg[GUIDE_PAGES][GUIDE_MAX_LINES][GUIDE_LINE_BUF];
        int cnt[GUIDE_PAGES];
        bool fits = true, tall = true, glyphs = true, titles = true;
        for (int p = 0; p < GUIDE_PAGES; p++) {
            cnt[p] = guideLines(p, pg[p]);
            int h = 0;
            if (cnt[p] < 5 || cnt[p] >= GUIDE_MAX_LINES) fits = false;           // 5 lines at least; one slot always spare, so an overflowing page is caught
            for (int i = 0; i < cnt[p]; i++) {
                int v = guideVisibleLen(pg[p][i]);
                h += v ? GUIDE_LINE_H : GUIDE_GAP_H;
                if (v > GUIDE_MAX_CHARS) { fits = false; printf("   page %d line %d is %d characters: %s\n", p + 1, i, v, pg[p][i]); }
                char plain[GUIDE_LINE_BUF]; guideStrip(plain, pg[p][i]);
                for (const char *c = plain; *c; c++) if (*c != ' ' && fontIndex(*c) == fontIndex('~')) { glyphs = false; printf("   page %d: no glyph for '%c' in %s\n", p + 1, *c, plain); }
            }
            if (h > GUIDE_MAX_H) { tall = false; printf("   page %d is %d px tall (max %d)\n", p + 1, h, GUIDE_MAX_H); }
            if (!guideTitle(p)[0] || strlen(guideTitle(p)) * 12 > 150) titles = false;     // a 2x title stays clear of BACK and the counter
        }
        CHECK("guide: eight pages, no visible line over 38 characters", fits && GUIDE_PAGES == 8);
        CHECK("guide: every page fits its 139 px of text", tall);
        CHECK("guide: every character has a glyph, every title fits at 2x", glyphs && titles);

        // the text of a page with the colour codes taken out, lines joined by '|'
        static char all[GUIDE_PAGES][1600];
        for (int p = 0; p < GUIDE_PAGES; p++) {
            all[p][0] = 0;
            for (int i = 0; i < cnt[p]; i++) { char plain[GUIDE_LINE_BUF]; guideStrip(plain, pg[p][i]); strcat(all[p], plain); strcat(all[p], "|"); }
        }
        // the multiplier ladder, heat table and pass ladder as the tables give them
        char want[160], num[16];
        want[0] = 0;
        for (int a = 3; a <= MAX_CHAIN;) {
            int b = a; while (b < MAX_CHAIN && boardLenMult(b + 1) == boardLenMult(a)) b++;
            if (a > 3) strcat(want, "  ");
            sprintf(num, "%d", a); strcat(want, num);
            if (b == MAX_CHAIN) strcat(want, "+"); else if (b > a) { sprintf(num, "-%d", b); strcat(want, num); }
            sprintf(num, " X%d", boardLenMult(a)); strcat(want, num);
            a = b + 1;
        }
        CHECK("guide SCORING prints the length multiplier ladder of boardLenMult", strstr(all[1], want) != NULL && strstr(want, "9+ X4") != NULL);
        CHECK("guide SCORING prints +200 at 7 letters, +30 a letter through a flame, two diamonds",
              strstr(all[1], "7+ LETTERS ADD A FLAT +200") && strstr(all[1], "+30 PER LETTER") && strstr(all[1], "(2 MAX)"));
        CHECK("guide FIRE prints the heat table of boardHeatFor", boardHeatFor(3) == 3 && boardHeatFor(4) == 2 && boardHeatFor(5) == 1 && boardHeatFor(6) == 1 && boardHeatFor(7) == 0 && boardHeatFor(MAX_CHAIN) == 0 &&
              strstr(all[4], "3 LETTERS +3, 4 +2, 5-6 +1, 7+ FREE") != NULL);
        want[0] = 0;
        for (int a = 1; a <= 20;) {
            int b = a; while (b < 20 && boardPassLenFor(b + 1) == boardPassLenFor(a)) b++;
            if (a > 1) strcat(want, "  ");
            sprintf(num, "%d", a); strcat(want, num);
            if (b == 20) strcat(want, "+"); else if (b > a) { sprintf(num, "-%d", b); strcat(want, num); }
            sprintf(num, ":%d", boardPassLenFor(a)); strcat(want, num);
            a = b + 1;
        }
        CHECK("guide THE PASS prints the pass-length ladder of boardPassLenFor", strstr(all[3], want) != NULL && strstr(want, "1-2:4  3-5:5  6-8:6  9-12:7  13+:8") != NULL);
        sprintf(want, "%s, %s, %s", poolName(0), poolName(8), poolName(17));
        sprintf(num, "%d IN ALL", poolThemeCount());
        CHECK("guide THE PASS names three themes and counts them from the pools", strstr(all[3], "BINDERY, BEASTS, WEATHER") && strstr(all[3], num) && !strcmp(want, "BINDERY, BEASTS, WEATHER"));
        CHECK("guide THE PASS prints the pass-letter reward and the shelf bonuses",
              strstr(all[3], "PAYS 100 X SHELF") && strstr(all[3], "+1000 X SHELF") && strstr(all[3], "+25 X SHELF") && strstr(all[3], "UNDER 40") &&
              strstr(all[3], "PERFECT, NO WASTED PEEK: +400 X SHELF") && strstr(all[3], "CLEAN, 2 SPARE PEEKS: +150 X SHELF") &&
              boardPassLetterReward(3) == 300 && boardShelfBonus(3) == 3000 && boardSpeedBonus(2, 30) == 500 && boardSpeedBonus(2, 50) == 0);
        int kind;
        CHECK("bonus helpers: perfect, clean and none", boardPeelBonus(2, 5, 5, &kind) == 800 && kind == 2 && boardPeelBonus(2, 7, 5, &kind) == 300 && kind == 1 && boardPeelBonus(2, 8, 5, &kind) == 0 && kind == 0);
        strcpy(want, "BOOKMARKS START A RUN");
        CHECK("guide SHELVES: bookmarks, twists, Lockdown tiers, overtime from the rules",
              strstr(all[5], "3 BOOKMARKS START A RUN") && strstr(all[5], "SHELF 4, 7, 10") && strstr(all[5], "INK SPILL: 3 HOLES") && strstr(all[5], "METER -2") && strstr(all[5], "PASS +1 LONGER") &&
              strstr(all[5], "LOCKDOWN FROM SHELF 6") && strstr(all[5], "EVERY 6 TURNS") && strstr(all[5], "SHELF 9, 12, 15") && strstr(all[5], "FROM 12") &&
              strstr(all[5], "AT TURN 25 (17 IN LOCKDOWN)") && boardTwistShelf(4) && boardTwistShelf(7) && !boardTwistShelf(5) && !boardTwistShelf(3));
        CHECK("guide FIRE: the three fail screens are named by the game", strstr(all[4], boardFailName(FAIL_FIRE)) && strstr(all[4], boardFailName(FAIL_SCORCH)) && strstr(all[4], boardFailName(FAIL_WORDS)) &&
              !strcmp(boardLockdownName(0), "LOCKDOWN") && !strcmp(boardLockdownName(3), "LOCKDOWN IV") && strstr(all[4], "SHELF 1-3") != NULL);
        CHECK("guide MODES names the four Daily modifiers", strstr(all[6], "STANDARD, EMBER, DEEP, LONG") != NULL);

        // the example words are worth what the game pays: I N K (+70) and Qu I Z (+210) on a real board
        boardOf('e'); S.library = LIB_FULL; S.spelling = SPELL_BOTH;
        set(0, 0, CELL_TILE, 'i'); set(0, 1, CELL_TILE, 'n'); set(0, 2, CELL_TILE, 'k');
        bool oki = boardTryAddToChain(&g, 0, 0) && boardTryAddToChain(&g, 0, 1) && boardTryAddToChain(&g, 0, 2);
        int ink = boardChainScore(&g);
        sprintf(num, "+%d", ink);
        CHECK("guide THE BOARD: INK is worth what boardChainScore says", oki && ink == 70 && strstr(all[0], "SPELL INK, CASH IN: +70") != NULL && strstr(all[0], num) != NULL);
        boardOf('e');
        set(0, 0, CELL_TILE, 'q'); set(0, 1, CELL_TILE, 'i'); set(0, 2, CELL_TILE, 'z');
        bool okq = boardTryAddToChain(&g, 0, 0) && boardTryAddToChain(&g, 0, 1) && boardTryAddToChain(&g, 0, 2);
        int quiz = boardChainScore(&g);
        sprintf(num, "= +%d", quiz);
        CHECK("guide SCORING: Qu+I+Z is worth what boardChainScore says", okq && quiz > 0 && strstr(all[1], num) != NULL && strstr(all[1], "10+1+10 = 21") != NULL);
        CHECK("guide THE BOARD: chains go up to MAX_CHAIN", strstr(all[0], "UP TO 20") != NULL && MAX_CHAIN == 20);
        // the guide's example and the game agree on every curve
        CHECK("boardWordPoints: flame, diamonds, repeat and the long-word flat", boardWordPoints(10, 7, false, 0, false) == 10 * 3 * 10 + 200 &&
              boardWordPoints(5, 4, true, 0, false) == 5 * 10 + 120 && boardWordPoints(5, 4, false, 2, false) == 100 && boardWordPoints(5, 4, false, 9, false) == 100 && boardWordPoints(5, 4, false, 0, true) == 25);
    }

    // ---- the rule accessors the guide prints are the ones boardNewShelf sets a shelf up with
    {
        bool rok = true;
        S.fire = FIRE_NORMAL;
        for (int shelf = 1; shelf <= 24; shelf++) {
            ShelfRules r; boardShelfRules(shelf, &r);
            bool plain = false;
            for (int attempt = 0; attempt < 60 && !plain; attempt++) {      // a shelf with no twist, to see the bare rules (twist shelves have their own rule)
                memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = shelf; boardSeed(0); srand(attempt + 1); boardNewShelf(&g);
                if (g.twist == TWIST_NONE) plain = true;
                else if (g.twist == TWIST_DAMP) {
                    int thr = r.threshold - TWIST_DAMP_METER; if (thr < 3) thr = 3;
                    if (g.heatThreshold != thr || g.burnEvery != r.burnEvery + TWIST_DAMP_SLOWER) { rok = false; printf("   DAMP on shelf %d: threshold %d every %d\n", shelf, g.heatThreshold, g.burnEvery); }
                }
            }
            if (!boardTwistShelf(shelf) && !plain) rok = false;
            if (plain && (g.heatThreshold != r.threshold || g.burnEvery != r.burnEvery || g.pulseEvery != r.pulseEvery || g.lockTier != r.tier || g.lockdown != r.lockdown)) { rok = false; printf("   shelf %d rules differ\n", shelf); }
        }
        CHECK("boardShelfRules matches boardNewShelf on shelves 1..24 (and DAMP is its documented offset)", rok);
        bool dok = true;
        for (int seed = 20260101; seed < 20260101 + 400; seed++) {
            int mod = boardDailyMod(seed);
            ShelfRules r; boardDailyRules(mod, &r);
            memset(&g, 0, sizeof(g)); g.mode = MODE_DAILY; g.dailySeed = seed; g.shelf = 1; boardNewShelf(&g);
            if (g.heatThreshold != r.threshold || g.burnEvery != r.burnEvery || g.pulseEvery != r.pulseEvery || g.lockdown) dok = false;
            int top = ROWS;
            for (int c = 0; c < COLS; c++) for (int rr = 0; rr < ROWS; rr++) if (g.back[c][rr] && rr < top) top = rr;
            if (top < r.minRow) dok = false;
            if (mod == DAILYMOD_LONG && g.passLen != DAILY_LONG_LEN) dok = false;
        }
        CHECK("boardDailyRules matches boardNewShelf for every Daily modifier (meter, fire, pulse, pass depth, LONG length)", dok);
        S.fire = FIRE_NORMAL;
        memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC;
        CHECK("heat for a peel and the overtime turns", boardPeelHeat(false) == 1 && boardPeelHeat(true) == 2 && boardOvertimeTurns(false) == 24 && boardOvertimeTurns(true) == 16);
    }

    // ---- the ghost drag of the guided shelf: a real, common word along touching, chainable cells
    {
        bool hok = true; int found = 0;
        S.library = LIB_FULL; S.spelling = SPELL_BOTH;
        for (int seed = 1; seed <= 30; seed++) {
            memset(&g, 0, sizeof(g)); g.mode = MODE_CLASSIC; g.shelf = 1; srand(seed); boardNewShelf(&g);
            for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) g.cell[c][r].yoff = 0;
            u8 hc[8], hr[8];
            int n = boardHintChain(&g, hc, hr, 5);
            if (!n) continue;
            found++;
            char hw[16]; int wl = 0; bool used[COLS][ROWS] = {{false}};
            if (n < 3 || n > 5) hok = false;
            for (int i = 0; i < n; i++) {
                if (used[hc[i]][hr[i]] || !boardIsChainable(&g, hc[i], hr[i])) hok = false;
                used[hc[i]][hr[i]] = true;
                if (i) { int nc[6], nr[6], k = boardNeighbors(hc[i - 1], hr[i - 1], nc, nr); bool adj = false; for (int j = 0; j < k; j++) if (nc[j] == hc[i] && nr[j] == hr[i]) adj = true; if (!adj) hok = false; }
                int l = g.cell[hc[i]][hr[i]].letter;
                hw[wl++] = 'a' + l; if (l == 'q' - 'a') hw[wl++] = 'u';
            }
            hw[wl] = 0;
            if (dictLookup(hw, wl) != 1 || !boardWordOk(hw, wl)) { hok = false; printf("   hint %s is not a common word\n", hw); }
        }
        CHECK("boardHintChain: a common word on touching cells on 30 first shelves", hok && found >= 25);
    }

    // ---- languages (item 21): the runtime half. langload.c reads data/lang/<code>/ the way the ROM reads nitro:/lang/<code>/
    {
        // English stays byte for byte what it was: a hash of 2000 bag draws and of 60 whole first boards, taken from the
        // build before the language work (same seeds, same dictionary)
        {
            boardSeed(12345); boardBagRotation(0); boardBagReset();
            u32 h = 2166136261u;
            for (int i = 0; i < 2000; i++) h = (h ^ (u32)boardRandomLetter()) * 16777619u;
            CHECK("English letter bag is byte-identical to before languages (2000 draws)", h == 3620680221u);
            h = 2166136261u;
            for (int sh = 1; sh <= 60; sh++) {
                memset(&g, 0, sizeof(g)); g.shelf = sh; g.mode = MODE_CLASSIC; srand(sh); boardNewShelf(&g);
                for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) { h = (h ^ g.cell[c][r].letter) * 16777619u; h = (h ^ g.back[c][r]) * 16777619u; }
                for (int i = 0; i < g.passLen; i++) h = (h ^ (u8)g.pass[i]) * 16777619u;
            }
            CHECK("English first boards and passes on shelves 1..60 are byte-identical to before", h == 2177031000u);
        }
        CHECK("English is the active language and its tables are the old ones", langCur() == LANG_EN && letterValue(16) == 10 && letterValue(0) == 1 && langWeight(4) == 12 && langQuTile() && langIsVowel(20) && !langIsVowel(24));
        CHECK("nothing is loadable before langInit (no filesystem): English stays", !langAvailable(LANG_ES) && !langLoad(LANG_ES) && langCur() == LANG_EN && boardWordOk("book", 4));
        langInit();
        bool allAvail = langAvailable(LANG_EN);
        for (int i = 1; i < LANG_COUNT; i++) allAvail = allAvail && langAvailable(i);
        CHECK("all ten languages are found on the filesystem", allAvail && !langAvailable(LANG_COUNT) && !langAvailable(-1));

        static const char *const KNOWN[LANG_COUNT] = { "",
            "casa perro mesa libro escuela ciudad mariposa", "maison chat table livre ecole ville papillon", "haus hund tisch buch schule stadt schmetterling",
            "casa cane tavolo libro scuola farfalla finestra", "casa gato mesa livro escola cidade borboleta", "huis hond tafel boek school vlinder raam",
            "hus hund bord bok skola arbeta fonster", "dom pies stol szkola miasto motyl okno", "hus hund bord bog skole sommerfugl vindue", "talo koira kissa kirja koulu perhonen ikkuna" };
        static const char *const UNKNOWN[LANG_COUNT] = { "", "xqzt casaz perroo", "xqzt maisonz chatz", "xqzt hausz hundo", "xqzt casaz canez", "xqzt casaz gatoo",
            "xqzt huisz hondo", "xqzt husz hundo", "xqzt domz piesz", "xqzt husz hundo", "xqzt talox koirax" };
        static const char *const NAME0[LANG_COUNT] = { "BINDERY", "LIBROS", "RELIURE", "BUCHER", "LIBRI", "LIVROS", "BOEKEN", "BOKBAND", "KSIAZKI", "BOGER", "KIRJAT" };
        char list[256];
        for (int li = 1; li < LANG_COUNT; li++) {
            char nm[96];
            snprintf(nm, sizeof(nm), "language %s", langCode(li));
            bool loaded = langLoad(li);
            CHECK(strcat(strcpy(list, nm), " loads and becomes the active language"), loaded && langCur() == li);
            if (!loaded) continue;
            // tables: weights sum to 100, values 1..10 on tiles, rare letters are tiles, a vowel tile exists, the Qu flag follows the data
            const LangTables *T = langTables();
            int sum = 0, tiles = 0, vowelTiles = 0; bool vals = true;
            for (int l = 0; l < 26; l++) {
                sum += T->weight[l];
                if (T->weight[l]) { tiles++; if (T->value[l] < 1 || T->value[l] > 10 || letterValue(l) != T->value[l]) vals = false; if (langIsVowel(l)) vowelTiles++; }
            }
            bool rareOk = true;
            for (int k = 0; k < 4; k++) if (langRare(k) != 255 && !T->weight[langRare(k)]) rareOk = false;
            CHECK(strcat(strcpy(list, nm), " letter tables: weights sum 100, values 1..10, a vowel tile, rare letters have tiles"), sum == 100 && vals && vowelTiles >= 3 && rareOk && tiles >= 20);
            CHECK(strcat(strcpy(list, nm), " Qu flag matches whether the language has a Q tile"), langQuTile() == (T->weight['q' - 'a'] > 0) && langQuTile() == (li <= LANG_PT));
            // words: known accepted, junk rejected, LIBRARY works through the tier bit
            bool known = true, junk = true;
            strcpy(list, KNOWN[li]);
            for (char *tk = strtok(list, " "); tk; tk = strtok(NULL, " ")) if (!boardWordOk(tk, (int)strlen(tk))) { known = false; printf("   %s: %s not accepted\n", langCode(li), tk); }
            strcpy(list, UNKNOWN[li]);
            for (char *tk = strtok(list, " "); tk; tk = strtok(NULL, " ")) if (boardWordOk(tk, (int)strlen(tk))) { junk = false; printf("   %s: %s accepted\n", langCode(li), tk); }
            CHECK(strcat(strcpy(list, nm), " accepts known words and rejects non-words"), known && junk);
            S.library = LIB_COMMON;
            int commonSeen = 0, rareSeen = 0, rareRejected = 0;
            for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN; len++)
                for (int i = 0; i < poolCount(8, len) && i < 400; i++) {
                    char pw[12]; poolWord(8, len, i, pw);
                    int t = boardWordTier(pw, len);
                    if (t == 1) commonSeen += boardWordOk(pw, len) ? 1 : 0;
                    if (t == 2) { rareSeen++; if (!boardWordOk(pw, len)) rareRejected++; }
                }
            S.library = LIB_FULL;
            CHECK(strcat(strcpy(list, nm), " LIBRARY COMMON keeps common words and rejects rare ones through the tier bit"), commonSeen > 0 && rareRejected == rareSeen);
            // pools: 40 themes, a name of 2..7 letters, every pool word in the dictionary and a-z, in the tile letters
            bool pools = poolThemeCount() == 40 && strcmp(poolName(0), NAME0[li]) == 0, az = true, inDict = true, tileOnly = true;
            for (int t = 0; t < poolThemeCount(); t++) {
                int nl = (int)strlen(poolName(t));
                if (nl < 2 || nl > 7) pools = false;
                for (int len = POOL_MIN_LEN; len <= POOL_MAX_LEN; len++)
                    for (int i = 0; i < poolCount(t, len); i++) {
                        char pw[12]; poolWord(t, len, i, pw);
                        for (int k = 0; k < len; k++) { if (pw[k] < 'a' || pw[k] > 'z') az = false; else if (!T->weight[pw[k] - 'a']) tileOnly = false; }
                        if (dictLookup(pw, len) == 0) inDict = false;
                    }
            }
            CHECK(strcat(strcpy(list, nm), " pools: 40 themes with names, every pass word a-z, a tile letter set, in the dictionary"), pools && az && inDict && tileOnly);
            CHECK(strcat(strcpy(list, nm), " the English theme names are still reachable for the guide"), strcmp(poolNameEn(0), "BINDERY") == 0 && strcmp(poolNameEn(39), "LEXICON") == 0);
            // the bag deals every tile letter of the language over a long run and nothing else; a cycle is 98..100 letters
            boardSeed(777); boardBagRotation(0); boardBagReset();
            int seen[26] = {0};
            for (int i = 0; i < 6000; i++) seen[boardRandomLetter()]++;
            bool every = true, only = true;
            for (int l = 0; l < 26; l++) { if (T->weight[l] && !seen[l]) every = false; if (!T->weight[l] && seen[l]) only = false; }
            bool vowelShare = true;
            { int v = 0; for (int l = 0; l < 26; l++) if (langIsVowel(l)) v += seen[l]; vowelShare = v > 6000 * 25 / 100 && v < 6000 * 60 / 100; }
            CHECK(strcat(strcpy(list, nm), " bag deals every tile letter and no other; the vowel share is sane"), every && only && vowelShare);
            // whole shelves: the pass is from the language's pool and in its dictionary, the tiles use its letters, the vowel floor holds
            bool shelvesOk = true;
            for (int sh = 1; sh <= 40; sh++) {
                memset(&g, 0, sizeof(g)); g.shelf = sh; g.mode = MODE_CLASSIC; srand(sh * 7); boardNewShelf(&g);
                bool inPool = false;
                for (int i = 0; i < poolCount(g.passTheme, g.passLen); i++) { char pw[12]; poolWord(g.passTheme, g.passLen, i, pw); if (!strcmp(pw, g.pass)) inPool = true; }
                if (!inPool || dictLookup(g.pass, g.passLen) == 0) shelvesOk = false;
                for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) if (!T->weight[g.cell[c][r].letter]) shelvesOk = false;
            }
            CHECK(strcat(strcpy(list, nm), " shelves 1..40: pass in the language's pool and dictionary, tiles from its alphabet"), shelvesOk);
            // SPELLING is English only: BRITISH changes nothing here
            S.spelling = SPELL_UK;
            bool spellIgnored = boardWordOk(KNOWN[li], (int)(strchr(KNOWN[li], ' ') - KNOWN[li])) && boardWordSpelling("color", 5) == 0;
            S.spelling = SPELL_BOTH;
            CHECK(strcat(strcpy(list, nm), " ignores the SPELLING setting"), spellIgnored);
            // a chain through the real rules: spell the first known word, it scores with the language's letter values
            {
                char sw[24]; size_t wl = strchr(KNOWN[li], ' ') - KNOWN[li]; memcpy(sw, KNOWN[li], wl); sw[wl] = 0;
                boardOf('e');
                for (int l = 0; l < 26; l++) if (T->weight[l]) { boardOf((char)('a' + l)); break; }
                int want = 0;
                for (size_t i = 0; i < wl; i++) { set(0, (int)i, CELL_TILE, sw[i]); want += letterValue(sw[i] - 'a'); }
                for (size_t i = 0; i < wl; i++) boardTryAddToChain(&g, 0, (int)i);
                char cw[WORD_BUF]; boardChainWord(&g, cw);
                int gain = boardSubmit(&g);
                CHECK(strcat(strcpy(list, nm), " a spelled word scores with the language's letter values"), strcmp(cw, sw) == 0 && gain == boardWordPoints(want, (int)wl, false, 0, false) && strcmp(g.ledger[0], sw) == 0);
            }
        }
        // back to English: the bag, the boards and the tables are exactly the old ones again; the buffers were freed
        CHECK("switching back to English restores it byte for byte", langLoad(LANG_EN) && langCur() == LANG_EN && letterValue(23) == 8 && boardWordOk("book", 4) && !boardWordOk("casaz", 5) && strcmp(poolName(0), "BINDERY") == 0);
        {
            boardSeed(12345); boardBagRotation(0); boardBagReset();
            u32 h = 2166136261u;
            for (int i = 0; i < 2000; i++) h = (h ^ (u32)boardRandomLetter()) * 16777619u;
            CHECK("after ten language loads the English bag is still byte-identical", h == 3620680221u);
        }
        // letters.bin validation: junk is refused and English stays active
        {
            LangTables t; u8 bad[LANG_LETTERS_BYTES]; memset(bad, 0, sizeof(bad));
            CHECK("a letters image without the magic is refused", !langParseLetters(bad, sizeof(bad), &t) && !langParseLetters(NULL, 0, &t));
            memcpy(bad, "WPL1", 4);
            CHECK("a letters image with no tiles or a short one is refused", !langParseLetters(bad, sizeof(bad), &t) && !langParseLetters(bad, 10, &t));
            CHECK("langApply with a bad letters buffer leaves English active", !langApply(LANG_ES, dict_bin, pools_bin, bad, sizeof(bad)) && langCur() == LANG_EN);
        }
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
