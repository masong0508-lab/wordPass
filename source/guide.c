// The pages of HOW TO PLAY. Pure text: every number a page prints comes from the same tables and functions the
// rules run on (letterValue, boardLenMult, boardHeatFor, boardWordPoints, boardShelfRules, boardPassLenFor, the
// bonus macros in game.h), so the guide cannot drift from the game. tools/check asserts that no visible line is
// longer than GUIDE_MAX_CHARS and that every character has a glyph.
//
// Colour codes inside a line (game.h GM_*): accent for key terms, gold for numbers, fire for warnings.
#include "game.h"
#include <stdarg.h>
#include <string.h>

typedef char Page[GUIDE_MAX_LINES][GUIDE_LINE_BUF];

static const char *const TITLES[GUIDE_PAGES] = {
    "THE BOARD", "SCORING", "PEEKING", "THE PASS", "FIRE", "SHELVES", "MODES", "CONTROLS"
};

const char *guideTitle(int page) { return TITLES[(page >= 0 && page < GUIDE_PAGES) ? page : 0]; }

int guideVisibleLen(const char *line)
{
    int n = 0;
    for (; *line; line++) if ((unsigned char)*line > 6) n++;
    return n;
}

void guideStrip(char *dst, const char *line)
{
    for (; *line; line++) if ((unsigned char)*line > 6) *dst++ = *line;
    *dst = 0;
}

// ---- a tiny formatter: %d and %s, nothing else (no libc printf needed, the same code runs natively and on the DS)
static void put(char *dst, const char *s)
{
    size_t n = strlen(dst);
    while (*s && n < GUIDE_LINE_BUF - 1) dst[n++] = *s++;
    dst[n] = 0;
}

static void putInt(char *dst, int v)
{
    char tmp[12]; int n = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    char out[14]; int k = 0;
    if (v < 0) out[k++] = '-';
    while (n) out[k++] = tmp[--n];
    out[k] = 0;
    put(dst, out);
}

static void bld(char *dst, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    dst[0] = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { char c[2] = { *fmt, 0 }; put(dst, c); continue; }
        fmt++;
        if (*fmt == 'd') putInt(dst, va_arg(ap, int));
        else if (*fmt == 's') put(dst, va_arg(ap, const char *));
        else { char c[2] = { *fmt, 0 }; put(dst, c); }
    }
    va_end(ap);
}

// ---- generated pieces

// The runs of f(n) over n = lo..hi as "3-4 X1  5-6 X2  9+ X4": the last run reaches hi and reads "9+". `zero` replaces a value of 0.
static void runList(char *out, int lo, int hi, int (*f)(int), const char *pre, const char *zero, const char *sep, const char *unit, const char *join)
{
    out[0] = 0;
    for (int a = lo; a <= hi;) {
        int b = a;
        while (b < hi && f(b + 1) == f(a)) b++;
        if (a > lo) put(out, sep);
        putInt(out, a);
        if (b == hi) put(out, "+");
        else if (b > a) { put(out, "-"); putInt(out, b); }
        if (a == lo && unit) put(out, unit);
        put(out, join);
        put(out, GM_GOLD);
        if (f(a) == 0 && zero) put(out, zero); else { put(out, pre); putInt(out, f(a)); }
        put(out, GM_BASE);
        a = b + 1;
    }
}

static int heatOf(int len) { return boardHeatFor(len); }
static int burnEveryOf(int shelf) { ShelfRules r; boardShelfRules(shelf, &r); return r.burnEvery; }
static int pulseOf(int shelf) { ShelfRules r; boardShelfRules(shelf, &r); return r.pulseEvery; }

// the first shelf from which pred(shelf) holds, scanning a Classic run's first 40
static int firstShelf(bool (*pred)(int))
{
    for (int s = 1; s <= 40; s++) if (pred(s)) return s;
    return 0;
}
static bool isLockdown(int s) { ShelfRules r; boardShelfRules(s, &r); return r.lockdown; }
static bool isTwinFire(int s) { ShelfRules r; boardShelfRules(s, &r); return r.tier >= LOCKDOWN_TWIN_FIRE_TIER; }
static bool isTier1(int s) { ShelfRules r; boardShelfRules(s, &r); return r.tier >= 1; }

// letter values, grouped by value ("1 AEILNORSTU  2 DG ...") and packed into lines of at most GUIDE_MAX_CHARS
static int valueLines(char lines[][GUIDE_LINE_BUF], int max)
{
    int n = 0, last = 0;
    lines[0][0] = 0;
    for (int v = 1; v <= 10; v++) {
        char grp[28]; int k = 0;
        for (int l = 0; l < 26; l++) if (letterValueEn(l) == v) grp[k++] = (char)('A' + l);
        grp[k] = 0;
        if (!k) continue;
        char tok[48]; tok[0] = 0;
        put(tok, GM_GOLD); putInt(tok, v); put(tok, GM_BASE " "); put(tok, grp);
        int w = guideVisibleLen(lines[n]), tw = guideVisibleLen(tok);
        if (w && w + 2 + tw > GUIDE_MAX_CHARS) { if (n + 1 >= max) break; n++; lines[n][0] = 0; w = 0; }
        if (w) put(lines[n], "  ");
        put(lines[n], tok);
        last = n;
    }
    return last + 1;
}

// ---- the pages. Each one fills `p` and returns its line count. Fit: GUIDE_MAX_CHARS visible characters a line and
// at most GUIDE_MAX_H pixels of lines (10 a line, 4 a gap), which tools/check asserts for every page.
#define L(...) do { if (n < GUIDE_MAX_LINES) { bld(p[n], __VA_ARGS__); n++; } } while (0)
#define GAP()  do { if (n < GUIDE_MAX_LINES) { p[n][0] = 0; n++; } } while (0)

static int pageBoard(Page p)
{
    int n = 0;
    int ink = letterValueEn('i' - 'a') + letterValueEn('n' - 'a') + letterValueEn('k' - 'a');   // the example word: I, N, K
    L(GM_ACCENT "DRAG" GM_BASE " THE STYLUS ACROSS TOUCHING GEMS.");
    L("EACH ONE JOINS THE CHAIN, UP TO " GM_GOLD "%d" GM_BASE ".", MAX_CHAIN);
    L(GM_ACCENT "THREE" GM_BASE " OR MORE LETTERS SPELL A WORD.");
    GAP();
    L("A GOOD WORD PUTS A " GM_GOLD "GOLD RING" GM_BASE " ON ITS");
    L("LAST GEM: " GM_ACCENT "TAP IT" GM_BASE " TO CASH IN, OR PRESS");
    L(GM_ACCENT "SUBMIT" GM_BASE " OR " GM_ACCENT "A" GM_BASE ".");
    GAP();
    L("DRAG BACK TO THE PREVIOUS GEM TO UNDO.");
    L("TAP AN EARLIER GEM TO CUT BACK TO IT.");
    GAP();
    L("SPELL " GM_GOLD "INK" GM_BASE ", CASH IN: " GM_GOLD "+%d" GM_BASE, boardWordPoints(ink, 3, false, 0, false));
    L(GM_ACCENT "TRY IT:" GM_BASE " CHAIN A WORD ON SHELF 1.");
    return n;
}

static int pageScoring(Page p)
{
    int n = 0;
    char vl[3][GUIDE_LINE_BUF], runs[GUIDE_LINE_BUF];
    int nv = valueLines(vl, 3);
    L(GM_ACCENT "GEM VALUES" GM_BASE " ADD UP:");
    for (int i = 0; i < nv; i++) L("%s", vl[i]);
    GAP();
    runList(runs, 3, MAX_CHAIN, boardLenMult, "X", NULL, "  ", NULL, " ");
    L("THE SUM X " GM_ACCENT "LENGTH" GM_BASE " X " GM_GOLD "%d" GM_BASE ", BY LETTERS:", SCORE_UNIT);
    L("%s", runs);
    L(GM_GOLD "%d+" GM_BASE " LETTERS ADD A FLAT " GM_GOLD "+%d" GM_BASE ".", LONG_WORD_LEN, LONG_WORD_BONUS);
    L("A " GM_FIRE "FLAME" GM_BASE " IN THE WORD: " GM_GOLD "+%d" GM_BASE " PER LETTER.", BURN_BONUS_PER_LETTER);
    L("EACH " GM_ACCENT "DIAMOND" GM_BASE " ADDS HALF AGAIN (" GM_GOLD "%d" GM_BASE " MAX).", DIAMOND_CAP);
    L("THE SAME WORD TWICE A SHELF: " GM_FIRE "HALF" GM_BASE ".");
    GAP();
    int qu = letterValueEn('q' - 'a'), iv = letterValueEn('i' - 'a'), zv = letterValueEn('z' - 'a');
    L(GM_ACCENT "QU" GM_BASE " IS ONE GEM BUT TWO LETTERS LONG.");
    L("QU+I+Z: " GM_GOLD "%d+%d+%d" GM_BASE " = " GM_GOLD "%d" GM_BASE ", X" GM_GOLD "%d" GM_BASE " X" GM_GOLD "%d" GM_BASE " = " GM_GOLD "+%d" GM_BASE,
      qu, iv, zv, qu + iv + zv, boardLenMult(4), SCORE_UNIT, boardWordPoints(qu + iv + zv, 4, false, 0, false));
    return n;
}

static int pagePeek(Page p)
{
    int n = 0;
    L(GM_ACCENT "HOLD L OR R" GM_BASE " TO PEEK, OR X, OR TAP " GM_ACCENT "PEEK" GM_BASE);
    L("TOUCH A GEM TO ARM IT, LIFT TO " GM_ACCENT "PEEL" GM_BASE ".");
    L("SLIDE OFF THE GEM FIRST TO CANCEL.");
    GAP();
    L("A PEELED CELL IS A HOLE WITH A NUMBER:");
    L("HOW MANY OF THE " GM_GOLD "6" GM_BASE " CELLS AROUND IT HIDE");
    L("A PASS LETTER:  " GM_DIM "0  " GM_ACCENT "1  " GM_GOLD "2  " GM_FIRE "3  " GM_WHITE "4+" GM_BASE);
    L("A " GM_ACCENT "DIAMOND" GM_BASE " IS A PASS LETTER YOU FOUND.");
    GAP();
    L(GM_FIRE "RED GEM" GM_BASE ": UNDER A FLAME. PEELING IT");
    L(GM_FIRE "SCORCHES" GM_BASE " THE CELL, AND A PASS LETTER");
    L("THERE IS BURNT FOR GOOD.");
    L("EVERY PEEL IS A TURN: " GM_GOLD "+%d" GM_BASE " HEAT.", boardPeelHeat(false));
    GAP();
    L(GM_ACCENT "TRY IT:" GM_BASE " PEEK ONE GEM ON SHELF 1.");
    return n;
}

static int pagePass(Page p)
{
    int n = 0;
    char runs[GUIDE_LINE_BUF];
    L("EVERY SHELF HIDES A " GM_ACCENT "PASSWORD" GM_BASE " BEHIND");
    L("THE GEMS: ITS LETTERS LIE IN A TRAIL.");
    L("THE TOP SCREEN SHOWS SLOTS AND THEME:");
    L(GM_GOLD "%s" GM_BASE ", " GM_GOLD "%s" GM_BASE ", " GM_GOLD "%s" GM_BASE "... " GM_GOLD "%d" GM_BASE " IN ALL.", poolNameEn(0), poolNameEn(8), poolNameEn(17), poolThemeCount());
    GAP();
    runList(runs, 1, 20, boardPassLenFor, "", NULL, "  ", NULL, ":");
    L(GM_ACCENT "LETTERS" GM_BASE " IN THE PASS, BY SHELF:");
    L("%s", runs);
    GAP();
    L("EACH LETTER FOUND PAYS " GM_GOLD "%d" GM_BASE " X SHELF.", PASS_LETTER_PER_SHELF);
    L("FIND THEM ALL TO " GM_ACCENT "CLEAR THE SHELF" GM_BASE ":");
    L(GM_GOLD "+%d" GM_BASE " X SHELF, AND " GM_GOLD "+%d" GM_BASE " X SHELF FOR", SHELF_BONUS_PER_SHELF, SPEED_PER_SHELF_TURN);
    L("EACH TURN UNDER " GM_GOLD "%d" GM_BASE ".", SPEED_TURNS);
    L(GM_ACCENT "PERFECT" GM_BASE ", NO WASTED PEEK: " GM_GOLD "+%d" GM_BASE " X SHELF", PERFECT_PER_SHELF);
    L(GM_ACCENT "CLEAN" GM_BASE ", " GM_GOLD "%d" GM_BASE " SPARE PEEKS: " GM_GOLD "+%d" GM_BASE " X SHELF", CLEAN_EXTRA_PEELS, CLEAN_PER_SHELF);
    return n;
}

static bool burnsEveryTurn(int s) { return burnEveryOf(s) == 1; }

static int pageFire(Page p)
{
    int n = 0;
    char heat[GUIDE_LINE_BUF];
    runList(heat, 3, MAX_CHAIN, heatOf, "+", "FREE", ", ", " LETTERS", " ");
    L("SHORT WORDS BUILD " GM_FIRE "HEAT" GM_BASE ", PER WORD:");
    L("%s", heat);
    L("A PEEL ADDS " GM_GOLD "+%d" GM_BASE " (" GM_GOLD "+%d" GM_BASE " IN LOCKDOWN).", boardPeelHeat(false), boardPeelHeat(true));
    L("A FULL METER SETS A TOP GEM " GM_FIRE "ALIGHT" GM_BASE ".");
    L("A FLAME FALLS A ROW EVERY TURN, BUT");
    L("EVERY " GM_GOLD "%d" GM_BASE " TURNS ON SHELF 1-" GM_GOLD "%d" GM_BASE ".", burnEveryOf(1), firstShelf(burnsEveryTurn) - 1);
    L("SPELL THROUGH A FLAME TO " GM_ACCENT "PUT IT OUT" GM_BASE ".");
    L("HOLES: CHIMNEYS. " GM_ACCENT "DIAMONDS" GM_BASE " NEVER BURN.");
    L(GM_FIRE "SCORCH" GM_BASE ": PEELING A FLAME OR UNDER ONE.");
    GAP();
    L("THE SHELF IS LOST WHEN:");
    L(GM_FIRE "%s" GM_BASE ": %s", boardFailName(FAIL_FIRE), "FLAME ON THE FLOOR");
    L(GM_FIRE "%s" GM_BASE ": %s", boardFailName(FAIL_SCORCH), "A PASS LETTER BURNED");
    L(GM_FIRE "%s" GM_BASE ": %s", boardFailName(FAIL_WORDS), "NO WORD LEFT");
    return n;
}

static int pageShelves(Page p)
{
    int n = 0;
    int lock = firstShelf(isLockdown), tier1 = firstShelf(isTier1), tier2 = 0, tier3 = 0, twin = firstShelf(isTwinFire);
    for (int s = lock; s <= 40; s++) { ShelfRules r; boardShelfRules(s, &r); if (!tier2 && r.tier >= 2) tier2 = s; if (!tier3 && r.tier >= 3) tier3 = s; }
    int t[3] = { 0, 0, 0 }, found = 0;
    for (int s = 1; s <= 40 && found < 3; s++) if (boardTwistShelf(s)) t[found++] = s;
    L(GM_GOLD "%d" GM_BASE " " GM_ACCENT "BOOKMARKS" GM_BASE " START A RUN. A LOST SHELF", BOOKMARKS_MAX);
    L("COSTS ONE, NONE LEFT ENDS THE RUN.");
    L("CLEAR A LOCKDOWN SHELF: WIN ONE BACK.");
    GAP();
    L(GM_ACCENT "TWISTS" GM_BASE ", SHELF " GM_GOLD "%d" GM_BASE ", " GM_GOLD "%d" GM_BASE ", " GM_GOLD "%d" GM_BASE " AND SO ON:", t[0], t[1], t[2]);
    L(GM_ACCENT "%s" GM_BASE ": %d HOLES ARE ALREADY OPEN", boardTwistName(TWIST_INK), INK_SPILL_HOLES);
    L(GM_ACCENT "%s" GM_BASE ": SLOWER FIRE, METER -%d", boardTwistName(TWIST_DAMP), TWIST_DAMP_METER);
    L(GM_ACCENT "%s" GM_BASE ": PASS +%d LONGER, LOW DOWN", boardTwistName(TWIST_BURIED), TWIST_BURIED_EXTRA);
    GAP();
    L(GM_FIRE "LOCKDOWN" GM_BASE " FROM SHELF " GM_GOLD "%d" GM_BASE ": FIRE EVERY TURN", lock);
    L("AND A HEAT PULSE EVERY " GM_GOLD "%d" GM_BASE " TURNS. IT", pulseOf(lock));
    L("TIGHTENS AT SHELF " GM_GOLD "%d" GM_BASE ", " GM_GOLD "%d" GM_BASE ", " GM_GOLD "%d" GM_BASE ". FROM " GM_GOLD "%d" GM_BASE, tier1, tier2, tier3, twin);
    L("TWO COLUMNS CATCH FIRE AT ONCE.");
    L(GM_FIRE "OVERTIME" GM_BASE ": AT TURN " GM_GOLD "%d" GM_BASE " (" GM_GOLD "%d" GM_BASE " IN LOCKDOWN)", OVERTIME_TURNS + 1, OVERTIME_TURNS_LOCKDOWN + 1);
    L("EVERY TURN ADDS HEAT OF ITS OWN.");
    return n;
}

static int pageModes(Page p)
{
    int n = 0;
    L(GM_ACCENT "CLASSIC" GM_BASE ": CLIMB THE SHELVES, FIRE AND");
    L("PASS DEPTH RISING AS YOU GO.");
    GAP();
    L(GM_ACCENT "DAILY PASS" GM_BASE ": ONE SHELF FOR EVERYONE,");
    L("FROM THE DS CLOCK DATE. THE FIRST RUN");
    L("OF A DAY IS " GM_GOLD "SCORED" GM_BASE ", EVEN IF YOU QUIT.");
    L("AFTER THAT IT IS " GM_ACCENT "PRACTICE" GM_BASE ": NOTHING IS");
    L("RECORDED.");
    L("MODIFIERS: %s, %s, %s, %s", boardDailyModName(DAILYMOD_STANDARD), boardDailyModName(DAILYMOD_EMBER),
      boardDailyModName(DAILYMOD_DEEP), boardDailyModName(DAILYMOD_LONG));
    L("SCORED DAYS IN A ROW BUILD A " GM_ACCENT "STREAK" GM_BASE ".");
    GAP();
    L("THE " GM_ACCENT "LEDGER" GM_BASE " (X ON THE TITLE) KEEPS YOUR");
    L("CAREER: WORDS, PEEKS, BEST WORD.");
    return n;
}

static int pageControls(Page p)
{
    int n = 0;
    L(GM_ACCENT "TOUCH" GM_BASE ": DRAG TO CHAIN, TAP THE LAST GEM");
    L("TO SUBMIT. " GM_ACCENT "HOLD L OR R" GM_BASE " TO PEEK.");
    L(GM_ACCENT "D-PAD" GM_BASE ": MOVE THE CURSOR, A CHAINS AND");
    L("SUBMITS, B BACKS UP, Y CLEARS.");
    L("X LATCHES PEEK, THEN A PEELS THE GEM.");
    L(GM_ACCENT "START" GM_BASE " OR THE II BUTTON PAUSES.");
    GAP();
    L(GM_ACCENT "OPTIONS" GM_BASE " HAS " GM_ACCENT "HAND" GM_BASE " (MIRRORS THE BOARD),");
    L(GM_ACCENT "LIBRARY" GM_BASE " (COMMON SKIPS ODD WORDS) AND");
    L(GM_ACCENT "SPELLING" GM_BASE " (AMERICAN, BRITISH OR BOTH).");
    L(GM_ACCENT "CLEAR" GM_BASE " THEME: COLOUR-BLIND SAFE GEMS.");
    return n;
}

int guideLines(int page, char out[GUIDE_MAX_LINES][GUIDE_LINE_BUF])
{
    for (int i = 0; i < GUIDE_MAX_LINES; i++) out[i][0] = 0;
    switch (page) {
        case 0: return pageBoard(out);
        case 1: return pageScoring(out);
        case 2: return pagePeek(out);
        case 3: return pagePass(out);
        case 4: return pageFire(out);
        case 5: return pageShelves(out);
        case 6: return pageModes(out);
        default: return pageControls(out);
    }
}
