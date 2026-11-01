// The interface text layer: every string of every language fits the slot it is drawn in (pixel width at its scale against
// its budget, from the compiled-in tables that tools/lang/gen_ui.py wrote), uses only glyphs the font has, has the same
// slots as the English text, and the formatter fills %d %s %02d and positional slots. Runs without the game: the test
// stands in for the active language.
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern const u8 UI_SCALE[];
extern const u16 UI_BUDGET[];
extern const char *const UI_SAMPLE[];
extern const char *const UI_IDNAME[];

static const char *CODES[LANG_COUNT] = { "en", "es", "fr", "de", "it", "pt", "nl", "sv", "pl", "da", "fi" };
const char *langCode(int idx) { return CODES[idx]; }
int uiFmtV(char *out, int cap, const char *f, va_list ap);
static int fmt(char *out, int cap, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = uiFmtV(out, cap, f, ap);
    va_end(ap);
    return n;
}

static int curLang;
int langCur(void) { return curLang; }

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

// the slots of a format: kinds in position order ('d' / 's'), 0 terminated
static int slots(const char *f, char *kinds)
{
    int n = 0, seq = 1;
    memset(kinds, 0, 8);
    for (; *f; f++) {
        if (*f != '%') continue;
        const char *p = f + 1;
        int pos = 0;
        if (p[0] >= '1' && p[0] <= '9' && p[1] == '$') { pos = p[0] - '0'; p += 2; }
        if (*p == '0') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p != 'd' && *p != 's') continue;
        if (!pos) pos = seq++;
        kinds[pos] = *p;
        n++;
        f = p;
    }
    return n;
}

// the text with its slots filled from the sample (the same rule as gen_ui.py)
static void expand(const char *f, const char *samp, char *out)
{
    char toks[4][16];
    int nt = 0;
    if (strcmp(samp, "-")) {
        const char *p = samp;
        while (*p && nt < 4) {
            int k = 0;
            while (*p && *p != '|' && k < 15) toks[nt][k++] = *p++;
            toks[nt++][k] = 0;
            if (*p == '|') p++;
        }
    }
    int seq = 0, len = 0;
    for (; *f; f++) {
        if (*f != '%') { out[len++] = *f; continue; }
        const char *p = f + 1;
        int pos = 0;
        if (p[0] >= '1' && p[0] <= '9' && p[1] == '$') { pos = p[0] - '0'; p += 2; }
        if (*p == '0') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p != 'd' && *p != 's') { out[len++] = *f; continue; }
        if (!pos) pos = ++seq;
        const char *t = pos - 1 < nt ? toks[pos - 1] : (*p == 'd' ? "99" : "WWWW");
        while (*t) out[len++] = *t++;
        f = p;
    }
    out[len] = 0;
}

static int fontHas(char c)
{
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return c && strchr(" :!?.-+_/@'<>(),=%", c) != NULL;
}

int main(void)
{
    char kinds[8], kinds0[8], buf[160];
    int total = 0, widest[LANG_COUNT] = { 0 };
    for (int id = 0; id < UI_COUNT; id++) {
        const char *en = uiIn(id, 0);
        slots(en, kinds0);
        for (int l = 0; l < LANG_COUNT; l++) {
            const char *s = uiIn(id, l);
            total++;
            CHECK(s && s[0], "%s lang %d is empty", UI_IDNAME[id], l);
            slots(s, kinds);
            CHECK(!memcmp(kinds, kinds0, 8), "%s lang %s: slots differ from English: %s", UI_IDNAME[id], langCode(l), s);
            expand(s, UI_SAMPLE[id], buf);
            for (const char *c = buf; *c; c++) CHECK(fontHas(*c), "%s lang %s: glyph %c in %s", UI_IDNAME[id], langCode(l), *c, s);
            int w = (int)strlen(buf) * 6 * UI_SCALE[id] - UI_SCALE[id];
            CHECK(w <= UI_BUDGET[id], "%s lang %s: %d px > %d px: %s", UI_IDNAME[id], langCode(l), w, UI_BUDGET[id], s);
            if (w > widest[l]) widest[l] = w;
        }
    }
    printf("ui: %d ids x %d languages, %d strings measured\n", UI_COUNT, LANG_COUNT, total);

    // the formatter
    char o[64];
    curLang = 0;
    uiFmt(o, sizeof o, UI_T_DAILYDATE, 11, 3);
    CHECK(!strcmp(o, "DAILY 1103"), "zero padded slots: %s", o);
    uiFmt(o, sizeof o, UI_O_BESTLINE, 1234, 5);
    CHECK(!strcmp(o, "BEST CLASSIC 1234  SHELF 5"), "two ints: %s", o);
    uiFmt(o, sizeof o, UI_MS_FIND, "BINDERY", 7);
    CHECK(!strcmp(o, "BINDERY: FIND THE 7 LETTER PASS"), "string then int: %s", o);
    uiFmt(o, sizeof o, UI_T_DRAGHINT, "R");
    CHECK(!strcmp(o, "DRAG SPELLS. R PEEKS"), "string slot: %s", o);
    uiFmt(o, sizeof o, UI_R_FINAL, -5);
    CHECK(!strcmp(o, "FINAL -5"), "negative: %s", o);
    uiFmt(o, 8, UI_O_BESTLINE, 123456, 99);
    CHECK(strlen(o) == 7, "truncates to the buffer: %s", o);
    CHECK(!strcmp(ui(UI_T_CLASSIC), "CLASSIC"), "English is the default");
    curLang = LANG_DE;
    CHECK(!strcmp(ui(UI_P_SUBMIT), "SENDEN"), "German text: %s", ui(UI_P_SUBMIT));
    curLang = 0;
    CHECK(strcmp(uiIn(UI_T_CLASSIC, LANG_ES), uiIn(UI_T_CLASSIC, 0)) != 0, "Spanish differs from English");
    // positional slots reorder the output, the arguments stay in position order
    fmt(o, sizeof o, "%2$s=%1$d", 7, "X");
    CHECK(!strcmp(o, "X=7"), "positional: %s", o);
    fmt(o, sizeof o, "%d/%d", 3, 4);
    CHECK(!strcmp(o, "3/4"), "sequential: %s", o);

    for (int l = 0; l < LANG_COUNT; l++) printf("  %s widest string %d px\n", langCode(l), widest[l]);
    printf(fails ? "UI TEST FAILED: %d failure(s)\n" : "UI TEST OK\n", fails);
    return fails != 0;
}
