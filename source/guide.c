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

const char *guideTitle(int page) { return ui(UI_GT_BOARD + ((page >= 0 && page < GUIDE_PAGES) ? page : 0)); }

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
static bool isTwinFire(int s) { ShelfRules r; boardShelfRules(s, &r); return r.twinEighths > 0; }
static bool isTier1(int s) { ShelfRules r; boardShelfRules(s, &r); return r.tier >= 1; }

// The gems of an example word (a UI_EX_* string of the active language): Qu is one gem when the language has the Qu tile.
// Returns the gem count (at most 4) and the word's length in letters.
int guideGems(int id, u8 gems[4], int *wordLen)
{
    const char *w = ui(id);
    int n = 0;
    for (const char *c = w; *c && n < 4;) {
        if (c[0] == 'Q' && c[1] == 'U' && langQuTile()) { gems[n++] = 'q' - 'a'; c += 2; continue; }
        gems[n++] = (u8)((*c >= 'A' && *c <= 'Z' ? *c - 'A' : 0));
        c++;
    }
    if (wordLen) *wordLen = (int)strlen(w);
    return n;
}

// letter values of the active language, grouped by value ("1 AEILNORSTU  2 DG ...") and packed into lines of at most GUIDE_MAX_CHARS
static int valueLines(char lines[][GUIDE_LINE_BUF], int max)
{
    int n = 0, last = 0;
    lines[0][0] = 0;
    for (int v = 1; v <= 10; v++) {
        char grp[28]; int k = 0;
        for (int l = 0; l < 26; l++) if (letterValue(l) == v) grp[k++] = (char)('A' + l);
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

// ---- the pages. Each one fills `p` and returns its line count. Every line is a UI_G* id of the string layer (colour codes
// and %d / %s slots inside, see tools/lang/ui_strings.tsv); the numbers come from the rules. Fit: GUIDE_MAX_CHARS visible
// characters a line and at most GUIDE_MAX_H pixels of lines (10 a line, 4 a gap), which tools/check asserts for every
// page in every language.
#define L(id, ...) do { if (n < GUIDE_MAX_LINES) { uiFmt(p[n], GUIDE_LINE_BUF, id, ##__VA_ARGS__); n++; } } while (0)
#define LS(s)  do { if (n < GUIDE_MAX_LINES) { bld(p[n], "%s", s); n++; } } while (0)
#define GAP()  do { if (n < GUIDE_MAX_LINES) { p[n][0] = 0; n++; } } while (0)

static int pageBoard(Page p)
{
    int n = 0;
    u8 gem[4]; int wl, ng = guideGems(UI_EX_WORD, gem, &wl), ink = 0;                            // the example word, in this language
    for (int i = 0; i < ng; i++) ink += letterValue(gem[i]);
    L(UI_GB_1);
    L(UI_GB_2, MAX_CHAIN);
    L(UI_GB_3);
    GAP();
    L(UI_GB_4);
    L(UI_GB_5);
    L(UI_GB_6);
    GAP();
    L(UI_GB_7);
    L(UI_GB_8);
    GAP();
    L(UI_GB_9, ui(UI_EX_WORD), boardWordPoints(ink, wl, false, 0, false));
    L(UI_GB_10);
    return n;
}

static int pageScoring(Page p)
{
    int n = 0;
    char vl[3][GUIDE_LINE_BUF], runs[GUIDE_LINE_BUF];
    int nv = valueLines(vl, 3);
    L(UI_GS_1);
    for (int i = 0; i < nv; i++) LS(vl[i]);
    GAP();
    runList(runs, 3, MAX_CHAIN, boardLenMult, "X", NULL, "  ", NULL, " ");
    L(UI_GS_3, SCORE_UNIT);
    LS(runs);
    L(UI_GS_4, LONG_WORD_LEN, LONG_WORD_BONUS);
    L(UI_GS_5, BURN_BONUS_PER_LETTER);
    L(UI_GS_6, DIAMOND_CAP);
    L(UI_GS_7);
    GAP();
    u8 gem[4]; int wl, ng = guideGems(UI_EX_SCORE, gem, &wl), v[3] = { 0, 0, 0 };    // the example word's three gems
    char label[16]; label[0] = 0;
    for (int i = 0; i < 3 && i < ng; i++) {
        v[i] = letterValue(gem[i]);
        if (i) strcat(label, "+");
        if (gem[i] == 'q' - 'a' && langQuTile()) strcat(label, "QU"); else { char c[2] = { (char)('A' + gem[i]), 0 }; strcat(label, c); }
    }
    int sum = v[0] + v[1] + v[2];
    L(UI_GS_8);
    L(UI_GS_9, label, v[0], v[1], v[2], sum, boardLenMult(wl), SCORE_UNIT, boardWordPoints(sum, wl, false, 0, false));
    return n;
}

static int pagePeek(Page p)
{
    int n = 0;
    L(UI_GE_1);
    L(UI_GE_2);
    L(UI_GE_3);
    GAP();
    L(UI_GE_4);
    L(UI_GE_5);
    L(UI_GE_6);
    L(UI_GE_7);
    GAP();
    L(UI_GE_8);
    L(UI_GE_9);
    L(UI_GE_10);
    L(UI_GE_11, boardPeelHeat(false));
    GAP();
    L(UI_GE_12);
    return n;
}

static int pagePass(Page p)
{
    int n = 0;
    char runs[GUIDE_LINE_BUF];
    L(UI_GA_1);
    L(UI_GA_2);
    L(UI_GA_3);
    L(UI_GA_4, poolName(0), poolName(8), poolName(17), poolThemeCount());
    GAP();
    runList(runs, 1, 20, boardPassLenFor, "", NULL, "  ", NULL, ":");
    L(UI_GA_5);
    LS(runs);
    GAP();
    L(UI_GA_6, PASS_LETTER_PER_SHELF);
    L(UI_GA_7);
    L(UI_GA_8, SHELF_BONUS_PER_SHELF, SPEED_PER_SHELF_TURN);
    L(UI_GA_9, SPEED_TURNS);
    L(UI_GA_10, PERFECT_PER_SHELF);
    L(UI_GA_11, CLEAN_EXTRA_PEELS, CLEAN_PER_SHELF);
    return n;
}

static bool burnsEveryTurn(int s) { return burnEveryOf(s) == 1; }

static int pageFire(Page p)
{
    int n = 0;
    char heat[GUIDE_LINE_BUF], unit[16];
    strcpy(unit, " "); strcat(unit, ui(UI_GP_UNIT));
    runList(heat, 3, MAX_CHAIN, heatOf, "+", ui(UI_GP_FREE), ", ", unit, " ");
    L(UI_GF_1);
    LS(heat);
    L(UI_GF_2, boardPeelHeat(false), boardPeelHeat(true));
    L(UI_GF_3);
    L(UI_GF_4);
    L(UI_GF_5, burnEveryOf(1), firstShelf(burnsEveryTurn) - 1);
    L(UI_GF_6);
    L(UI_GF_7);
    L(UI_GF_8);
    GAP();
    L(UI_GF_9);
    L(UI_GF_10);
    L(UI_GF_11);
    L(UI_GF_12);
    return n;
}

static int pageShelves(Page p)
{
    int n = 0;
    int lock = firstShelf(isLockdown), tier1 = firstShelf(isTier1), tier2 = 0, tier3 = 0, twin = firstShelf(isTwinFire);
    for (int s = lock; s <= 40; s++) { ShelfRules r; boardShelfRules(s, &r); if (!tier2 && r.tier >= 2) tier2 = s; if (!tier3 && r.tier >= 3) tier3 = s; }
    int t[3] = { 0, 0, 0 }, found = 0;
    for (int s = 1; s <= 40 && found < 3; s++) if (boardTwistShelf(s)) t[found++] = s;
    L(UI_GH_1, BOOKMARKS_MAX);
    L(UI_GH_2);
    L(UI_GH_3);
    GAP();
    L(UI_GH_4, t[0], t[1], t[2]);
    L(UI_GH_5, boardTwistName(TWIST_INK), INK_SPILL_HOLES);
    L(UI_GH_6, boardTwistName(TWIST_DAMP), TWIST_DAMP_METER);
    L(UI_GH_7, boardTwistName(TWIST_BURIED), TWIST_BURIED_EXTRA);
    GAP();
    L(UI_GH_8, lock);
    L(UI_GH_9, pulseOf(lock));
    L(UI_GH_10, tier1, tier2, tier3, twin);
    L(UI_GH_11);
    L(UI_GH_12, OVERTIME_TURNS + 1, OVERTIME_TURNS_LOCKDOWN + 1);
    L(UI_GH_13);
    return n;
}

static int pageModes(Page p)
{
    int n = 0;
    L(UI_GM_1);
    L(UI_GM_2);
    GAP();
    L(UI_GM_3);
    L(UI_GM_4);
    L(UI_GM_5);
    L(UI_GM_6);
    L(UI_GM_7);
    L(UI_GM_8, boardDailyModName(DAILYMOD_STANDARD), boardDailyModName(DAILYMOD_EMBER),
      boardDailyModName(DAILYMOD_DEEP), boardDailyModName(DAILYMOD_LONG));
    L(UI_GM_9);
    GAP();
    L(UI_GM_10);
    L(UI_GM_11);
    return n;
}

static int pageControls(Page p)
{
    int n = 0;
    L(UI_GC_1);
    L(UI_GC_2);
    L(UI_GC_3);
    L(UI_GC_4);
    L(UI_GC_5);
    L(UI_GC_6);
    GAP();
    L(UI_GC_7);
    L(UI_GC_8);
    L(UI_GC_9);
    L(UI_GC_10);
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
