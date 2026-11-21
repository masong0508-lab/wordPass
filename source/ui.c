// The interface text layer: every user-visible string of the game is an id (UI_*, generated into ui_strings.h from
// tools/lang/ui_strings.tsv) and ui() returns the active language's text. uiFmt fills the printf style slots of
// such a string (%d, %s, %02d, and positional %2$d so a language may reorder them).
#include "game.h"
#include <stdarg.h>
#include <string.h>

extern const char *const UI_TEXT[UI_COUNT][LANG_COUNT];

const char *uiIn(int id, int lang)
{
    if (id < 0 || id >= UI_COUNT) return "";
    if (lang < 0 || lang >= LANG_COUNT) lang = 0;
    return UI_TEXT[id][lang];
}

static int forced = -1;
void uiTestLang(int lang) { forced = lang; }
const char *ui(int id) { return uiIn(id, forced >= 0 ? forced : langCur()); }

typedef struct { char kind; int width; bool zero; int pos; const char *start; } UiSlot;   // kind 'd' or 's'; pos is 1 based

// the slots of a format in the order they appear; at most UI_MAX_SLOTS
static int uiScan(const char *f, UiSlot *sl)
{
    int n = 0, seq = 1;
    for (; *f; f++) {
        if (*f != '%') continue;
        const char *p = f + 1;
        UiSlot s = { 0, 0, false, 0, f };
        if (p[0] >= '1' && p[0] <= '9' && p[1] == '$') { s.pos = p[0] - '0'; p += 2; }
        if (*p == '0') { s.zero = true; p++; }
        while (*p >= '0' && *p <= '9') s.width = s.width * 10 + (*p++ - '0');
        if (*p != 'd' && *p != 's') continue;
        s.kind = *p;
        if (!s.pos) s.pos = seq++;
        // Never index the vararg tables past UI_MAX_SLOTS, even if a bad
        // translation somehow bypasses the generator's slot validation.
        if (s.pos < 1 || s.pos > UI_MAX_SLOTS || n >= UI_MAX_SLOTS) continue;
        sl[n++] = s;
        f = p;
    }
    return n;
}

static int uiPut(char *out, int cap, int len, const char *s)
{
    while (*s && len < cap - 1) out[len++] = *s++;
    return len;
}

int uiFmtV(char *out, int cap, const char *f, va_list ap)
{
    if (!out || cap <= 0 || !f) return 0;
    UiSlot sl[UI_MAX_SLOTS];
    int n = uiScan(f, sl);
    int ival[UI_MAX_SLOTS + 1] = { 0 };
    const char *sval[UI_MAX_SLOTS + 1] = { 0 };
    char kind[UI_MAX_SLOTS + 1] = { 0 };
    for (int i = 0; i < n; i++) kind[sl[i].pos] = sl[i].kind;
    for (int p = 1; p <= UI_MAX_SLOTS; p++) {          // the caller passes the arguments in position order
        if (kind[p] == 'd') ival[p] = va_arg(ap, int);
        else if (kind[p] == 's') sval[p] = va_arg(ap, const char *);
    }
    int len = 0, k = 0;
    for (const char *c = f; *c; c++) {
        if (k >= n || c != sl[k].start) { if (len < cap - 1) out[len++] = *c; continue; }
        UiSlot s = sl[k++];
        while (*c != 'd' && *c != 's') c++;             // past the spec
        char tmp[16];
        const char *t;
        if (s.kind == 'd') {
            int v = ival[s.pos], i = 15, neg = v < 0;
            unsigned u = neg ? 0u - (unsigned)v : (unsigned)v; // also handles INT_MIN safely
            tmp[i] = 0;
            do { tmp[--i] = (char)('0' + u % 10); u /= 10; } while (u);
            int digits = 15 - i;
            while (digits < s.width - (neg ? 1 : 0) && s.zero && i > 1) { tmp[--i] = '0'; digits++; }
            if (neg) tmp[--i] = '-';
            t = tmp + i;
            if (!s.zero) for (int pad = (int)strlen(t); pad < s.width; pad++) if (len < cap - 1) out[len++] = ' ';
        } else t = sval[s.pos] ? sval[s.pos] : "";
        len = uiPut(out, cap, len, t);
    }
    out[len < cap ? len : cap - 1] = 0;
    return len;
}

int uiFmt(char *out, int cap, int id, ...)
{
    va_list ap;
    va_start(ap, id);
    int n = uiFmtV(out, cap, ui(id), ap);
    va_end(ap);
    return n;
}
