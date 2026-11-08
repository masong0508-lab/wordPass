// Software renderer: Bejeweled gems in an N+ frame, two 16-bit bitmap screens,
// particles, floating popups, hardware fades and screen shake.
#include "game.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// The touch screen is rendered straight into VRAM: bank A and bank B each hold a
// 256x256x16 bitmap, and the main BG flips between them at vblank. The top screen
// (bank C) is drawn in 48-row slices inside vblank, see gfxFrame.
static u16 *vramSub;
static int bgMain;
static u16 *vramFront = (u16 *)0x06000000, *vramBack = (u16 *)0x06020000;
static int backBank = 1;                      // which bank vramBack points at
static u16 *cur;                              // current render target (a VRAM bank)
static int frame;
// Vertical clip band honoured by every primitive. The top screen has no back buffer,
// so its HUD is drawn straight into VRAM_C inside vblank, one 48-row slice per frame.
static int clipY0 = 0, clipY1 = SCREEN_H;
// The framed board strip is BOARD_W pixels wide: x 0..173 right-handed, 82..255 left-handed.
// Every frame repaints the strip edge to edge, since VRAM holds whatever was there last.
#define BOARD_W 174
// Pause/results/fail: the board strip is dimmed by the 2D engine (brightness-decrease
// colour effect on BG3, windowed to the strip with the card cut out), not by the CPU.
// drawOverlay records the card rectangle here and gfxFrame programs the windows at vblank.
static bool dimOn;
static int dimCardX, dimCardY, dimCardW, dimCardH;
#define REG_WIN0H (*(vu16 *)0x04000040)
#define REG_WIN1H (*(vu16 *)0x04000042)
#define REG_WIN0V (*(vu16 *)0x04000044)
#define REG_WIN1V (*(vu16 *)0x04000046)
#define WIN_BG3        0x08             // layer bits for WIN_IN / WIN_OUT
#define WIN_BG3_EFFECT 0x28             // BG3 with the colour effect enabled
// The HUD on the top screen has two layouts (title/options and in-game). When the layout
// changes, all four slices are redrawn over two frames (two per vblank) so the whole screen
// updates at once without crowding the vblank.
static int lastTopKind = -1, topNeed;   // topNeed: bit per 48-row slice that must be redrawn (a layout change sets all four)
// Build with -DWP_PROFILE to expose per-stage render timings in dbgT[]
#ifdef WP_PROFILE
u32 dbgT[10];
#define PROF(i) dbgT[i] = cpuEndTiming(); cpuStartTiming(0)
#define PROF_START() cpuStartTiming(0)
#define PROF_SET(i) dbgT[i] = cpuEndTiming()
#else
#define PROF(i)
#define PROF_START()
#define PROF_SET(i)
#endif

// presentation state
static int bright, shakeFrames, invalidTimer;
static bool chainValidNow;      // computed once per frame
static char chainWordNow[WORD_BUF];
static u8 prevFound[12], letterPop[12];
static int pressSubmit, pressClear, pressPeek, pressPause, submitFlash;   // panel button feedback, frames left
static int shelfAge;            // frames since the shelf began (the twist name rides the HUD label for the first 120)
static int fireLow;             // boardLowestFireRow(g), once per frame
static int chainScoreNow;       // boardChainScore(g) while the chain is a word, else 0
// the board callout: one big word that pops over the middle of the strip (kind 0 gold, 1 fire, 2 accent)
typedef struct { char txt[20]; u8 life, kind; } Callout;
static Callout callout;
static int shownScore;          // the score on the HUD: it is g->score except while the results tally counts up
static int tallyPrev = -1, cardFlash;   // the state last frame; frames of white card border left
static int tipIndex, tipTimer;  // the title's tip card and the frames it has been up
static char lastMsg[MSG_LEN];   // the message line as last frame saw it, and how long it has stood (the sticky prompts slide in)
static int msgAge;
static int frameDanger, frameMood;   // 0..12: the gold frame pulses toward fire as the flame nears the floor (at most 2.5 Hz); toward the accent in peek
static bool rainAlive;          // the title's gem rain may have particles in flight

#define C(r,g,b) ARGB16(1,(r),(g),(b))
#define BLACK      C(1,1,2)
#define INK        C(24,24,26)
static u16 frameMid = C(28,22,6), frameDk = C(16,11,2), frameLt = C(31,29,18);
#define GOLD       frameMid
#define GOLD_DK    frameDk
#define GOLD_LT    frameLt
static u16 accent = C(6,28,24);
static u16 bgTint = C(20,5,27);    // the board dots' second colour: the theme's amethyst slot, set in setupTheme
#define ACCENT     accent
// one accent per shelf so the library changes colour as you climb
static const u16 SHELF_ACCENTS[ACCENT_COUNT - 1] = {
    C(6,28,24), C(28,8,24), C(16,30,6), C(31,16,4), C(8,20,31), C(20,10,31), C(31,12,18), C(10,31,16)
};
#define WHITE      C(31,31,31)
#define DIM        C(6,6,8)
#define DIM_TEXT   C(13,13,16)     // dim text that must still be read; DIM stays for decorative outlines
#define DIM2       C(3,3,5)
#define FIRE1      C(31,12,2)
#define FIRE2      C(31,24,4)

typedef struct { u16 dark, mid, light; } GemPal;
// five letter groups per theme: vowels, common consonants, rare, soft, other
static const GemPal THEMES[THEME_COUNT][5] = {
    { // JEWEL
      { C(12,0,3),  C(27,3,8),   C(31,16,20) }, { C(1,3,13),  C(4,11,28),  C(16,22,31) },
      { C(13,9,0),  C(30,22,3),  C(31,30,16) }, { C(9,1,13),  C(20,5,27),  C(28,18,31) },
      { C(0,9,3),   C(3,22,9),   C(16,31,19) } },
    { // INK: N+ slate with a tinted edge per group
      { C(2,2,3),   C(6,6,8),    C(14,26,24) }, { C(2,2,3),   C(6,6,8),    C(18,18,22) },
      { C(2,2,3),   C(6,6,8),    C(28,22,8)  }, { C(2,2,3),   C(6,6,8),    C(22,14,28) },
      { C(2,2,3),   C(6,6,8),    C(12,12,14) } },
    { // CANDY: pastels
      { C(22,8,14), C(31,17,23), C(31,27,29) }, { C(6,14,20), C(14,25,31), C(24,30,31) },
      { C(22,20,4), C(31,30,12), C(31,31,24) }, { C(14,8,22), C(22,16,31), C(28,25,31) },
      { C(6,20,12), C(14,30,20), C(24,31,27) } },
    { // EMBER: warm library
      { C(10,1,1),  C(22,4,3),   C(31,14,8)  }, { C(10,6,1),  C(21,13,3),  C(31,24,12) },
      { C(12,10,1), C(24,20,4),  C(31,30,16) }, { C(8,2,6),   C(17,5,12),  C(28,14,22) },
      { C(6,8,1),   C(13,16,3),  C(24,28,10) } },
    { // CLEAR: colour-blind safe. Five groups that differ in lightness as well as hue (yellow, blue, magenta,
      // teal, grey), none of them leaning on a red/green split. Burning gems read by their light plate and dark letter.
      { C(18,15,0), C(31,26,4),  C(31,31,18) }, { C(0,3,14),  C(0,8,30),   C(14,20,31) },
      { C(14,3,16), C(28,12,30), C(31,22,31) }, { C(0,10,8),  C(0,22,18),  C(14,31,26) },
      { C(3,4,5),   C(9,10,12),  C(18,19,22) } }
};
static const GemPal GEM_DIAMOND  = { C(16,18,21),C(27,28,30), C(31,31,31) };
static const GemPal GEM_FIRE     = { C(12,0,0),  C(31,6,0),   C(31,22,4) };
// theme slots: 0 ruby (vowels), 1 sapphire, 2 amber, 3 amethyst, 4 emerald; index THEMES[S.theme][slot]
enum { TH_RUBY = 0, TH_SAPPHIRE, TH_AMBER, TH_AMETHYST, TH_EMERALD };

static int groupFor(int letter)
{
    switch ('a' + letter) {
        case 'a': case 'e': case 'i': case 'o': case 'u': return 0;
        case 'r': case 's': case 't': case 'l': case 'n': case 'd': return 1;
        case 'j': case 'k': case 'q': case 'x': case 'z': return 2;
        case 'v': case 'w': case 'y': case 'f': case 'b': return 3;
        default: return 4;
    }
}
static const GemPal *gemFor(int letter) { return &THEMES[S.theme][groupFor(letter)]; }

// ---------------------------------------------------------------- primitives

static inline void px(int x, int y, u16 col)
{
    if ((unsigned)x < SCREEN_W && y >= clipY0 && y < clipY1) cur[y * SCREEN_W + x] = col;
}

static ITCM_CODE __attribute__((noinline)) void fillRect(int x, int y, int w, int h, u16 col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < clipY0) { h += y - clipY0; y = clipY0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > clipY1) h = clipY1 - y;
    if (w <= 0 || h <= 0) return;
    u32 col2 = col | ((u32)col << 16);
    int rows = h, span = w;
    if (x == 0 && w == SCREEN_W) { rows = 1; span = w * h; }   // full-width rows are one contiguous run
    for (int j = 0; j < rows; j++) {
        u16 *p = cur + (y + j) * SCREEN_W + x;
        int n = span;
        if (((uintptr_t)p & 2) && n) { *p++ = col; n--; }
        u32 *p32 = (u32 *)p;
        if (n >= 16) {
            // eight words per stmia: one instruction per 32 bytes instead of a store and
            // loop step per word, which is what the screen clears and the backdrop spend
            // most of their time on (VRAM takes the words at the same bus rate either way)
            int k = n >> 4;
            u32 c1 = col2, c2 = col2, c3 = col2, c4 = col2, c5 = col2, c6 = col2, c7 = col2;
            asm volatile(
                "1: stmia %0!, {%2, %3, %4, %5, %6, %7, %8, %9}\n\t"
                "subs %1, %1, #1\n\t"
                "bne 1b"
                : "+r"(p32), "+r"(k), "+r"(col2), "+r"(c1), "+r"(c2), "+r"(c3), "+r"(c4), "+r"(c5), "+r"(c6), "+r"(c7)
                : : "memory", "cc");
            n &= 15;
        }
        for (; n >= 2; n -= 2) *p32++ = col2;
        if (n) *(u16 *)p32 = col;
    }
}

static void hline(int x, int y, int w, u16 col) { fillRect(x, y, w, 1, col); }

static ITCM_CODE __attribute__((noinline)) void rectOutline(int x, int y, int w, int h, u16 col)
{
    hline(x, y, w, col); hline(x, y + h - 1, w, col);
    // sides: one bounds test, then direct writes down the clipped row range
    int j0 = y + 1, j1 = y + h - 1, xr = x + w - 1;
    if (j0 < clipY0) j0 = clipY0;
    if (j1 > clipY1) j1 = clipY1;
    if (j0 >= j1) return;
    if (x >= 0 && xr < SCREEN_W) {
        u16 *p = cur + j0 * SCREEN_W;
        for (int j = j0; j < j1; j++, p += SCREEN_W) { p[x] = col; p[xr] = col; }
    } else {
        for (int j = j0; j < j1; j++) { px(x, j, col); px(xr, j, col); }
    }
}

static void roundRect(int x, int y, int w, int h, u16 col)
{
    fillRect(x + 2, y, w - 4, h, col);
    fillRect(x, y + 2, w, h - 4, col);
    fillRect(x + 1, y + 1, w - 2, h - 2, col);
}

// blend two RGB15 colours, t in 0..16 (signed, truncating division: the gem gradients and
// every cached sprite depend on this exact rounding)
static inline u16 lerp15(u16 a, u16 b, int t)
{
    int ar = a & 31, ag = (a >> 5) & 31, ab = (a >> 10) & 31;
    int br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31;
    return C(ar + ((br - ar) * t) / 16, ag + ((bg - ag) * t) / 16, ab + ((bb - ab) * t) / 16);
}

// one 5x7 glyph at any scale: fully inside the screen and slice it is written directly,
// otherwise it falls back to clipped pixels and rects
static ITCM_CODE __attribute__((noinline)) void drawGlyph(int x, int y, const u8 *gl, u16 col, int scale)
{
    int gw = 5 * scale, gh = 7 * scale;
    if (y >= clipY1 || y + gh <= clipY0) return;
    if (x >= 0 && x + gw <= SCREEN_W && y >= clipY0 && y + gh <= clipY1) {
        // fully inside: one bounds test for the glyph, then direct unrolled stores
        u16 *row = cur + y * SCREEN_W + x;
        if (scale == 1) {
            for (int j = 0; j < 7; j++, row += SCREEN_W) {
                u8 bits = gl[j];
                if (bits & 0x10) row[0] = col;
                if (bits & 0x08) row[1] = col;
                if (bits & 0x04) row[2] = col;
                if (bits & 0x02) row[3] = col;
                if (bits & 0x01) row[4] = col;
            }
        } else if (scale == 2) {
            for (int j = 0; j < 7; j++, row += 2 * SCREEN_W) {
                u8 bits = gl[j];
                if (!bits) continue;
                u16 *r1 = row + SCREEN_W;
                if (bits & 0x10) { row[0] = col; row[1] = col; r1[0] = col; r1[1] = col; }
                if (bits & 0x08) { row[2] = col; row[3] = col; r1[2] = col; r1[3] = col; }
                if (bits & 0x04) { row[4] = col; row[5] = col; r1[4] = col; r1[5] = col; }
                if (bits & 0x02) { row[6] = col; row[7] = col; r1[6] = col; r1[7] = col; }
                if (bits & 0x01) { row[8] = col; row[9] = col; r1[8] = col; r1[9] = col; }
            }
        } else {
            for (int j = 0; j < 7; j++) {
                u8 bits = gl[j];
                if (!bits) { row += SCREEN_W * scale; continue; }
                for (int sy = 0; sy < scale; sy++, row += SCREEN_W) {
                    u16 *p = row;
                    for (int i = 0; i < 5; i++, p += scale)
                        if (bits & (0x10 >> i)) for (int k = 0; k < scale; k++) p[k] = col;
                }
            }
        }
        return;
    }
    if (x >= 0 && x + gw <= SCREEN_W) {
        // inside the screen sideways but cut by the clip band (a glyph on a slice boundary): write the rows
        // that fall inside the band directly instead of one clipped rect per lit pixel
        for (int j = 0; j < 7; j++) {
            u8 bits = gl[j];
            if (!bits) continue;
            for (int sy = 0; sy < scale; sy++) {
                int yy = y + j * scale + sy;
                if (yy < clipY0 || yy >= clipY1) continue;
                u16 *p = cur + yy * SCREEN_W + x;
                for (int i = 0; i < 5; i++, p += scale)
                    if (bits & (0x10 >> i)) for (int k = 0; k < scale; k++) p[k] = col;
            }
        }
        return;
    }
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 5; i++)
            if (gl[j] & (0x10 >> i)) {
                if (scale == 1) px(x + i, y + j, col);
                else fillRect(x + i * scale, y + j * scale, scale, scale, col);
            }
}

static void drawChar(int x, int y, char ch, u16 col, int scale) { drawGlyph(x, y, font5x7[fontIndex(ch)], col, scale); }

static void drawText(int x, int y, const char *s, u16 col, int scale)
{
    if (y >= clipY1 || y + 7 * scale <= clipY0) return;   // whole line outside the slice
    for (; *s; s++) { drawChar(x, y, *s, col, scale); x += 6 * scale; }
}

// decimal itoa for the per-frame HUD counters (siprintf, the integer-only newlib formatter,
// stays for the multi-field overlay strings: no float printf or dtoa in the ROM)
static char *fmtInt(char *buf, int v)
{
    char tmp[12]; int n = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    char *p = buf;
    if (v < 0) *p++ = '-';
    while (n) *p++ = tmp[--n];
    *p = 0;
    return buf;
}

static int textWidth(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

// a score for a tight spot: below 100000 in full, then in thousands ("123K")
static char *fmtScoreShort(char *buf, int v)
{
    if (v < 100000) return fmtInt(buf, v);
    fmtInt(buf, v / 1000); strcat(buf, "K");
    return buf;
}

static void drawTextCenter(int cx, int y, const char *s, u16 col, int scale)
{
    drawText(cx - textWidth(s, scale) / 2, y, s, col, scale);
}

// text with a dark drop shadow
static void drawTextShadow(int x, int y, const char *s, u16 col, int scale)
{
    drawText(x + scale, y + scale, s, DIM2, scale);
    drawText(x, y, s, col, scale);
}

// the wordmark: lowercase "word" in ink, "Pass" in gold
static void drawLogo(int x, int y, int scale)
{
    const char *w = "word";
    for (; *w; w++) { drawGlyph(x, y, font5x7[fontLogoIndex(*w)], INK, scale); x += 6 * scale; }
    drawText(x, y, "PASS", GOLD, scale);
}

// Faceted glossy gem: vertical gradient, highlight band, specular, dark base.
static ITCM_CODE __attribute__((noinline)) void drawGemSized(int x, int y, int w, int h, const GemPal *p, bool lit)
{
    if (y >= clipY1 || y + h <= clipY0) return;
    // body with rounded corners: light at the top, dark at the bottom. One divide per
    // gem (not per row) and direct row writes when the gem is inside the screen.
    int step = (16 << 8) / h;
    bool inside = x >= 0 && x + w <= SCREEN_W;
    int j0 = clipY0 - y, j1 = clipY1 - y;
    if (j0 < 1) j0 = 1;
    if (j1 > h - 1) j1 = h - 1;
    for (int j = j0; j < j1; j++) {
        int inset = (j == 1 || j == h - 2) ? 2 : 1;
        int t = 16 - ((j * step) >> 8);
        u16 col = (t > 10) ? lerp15(p->mid, p->light, (t - 10) * 16 / 6) : lerp15(p->dark, p->mid, t * 16 / 10);
        if (inside) { u16 *row = cur + (y + j) * SCREEN_W + x; for (int i = inset; i < w - inset; i++) row[i] = col; }
        else hline(x + inset, y + j, w - inset * 2, col);
    }
    // highlight band and specular
    hline(x + 3, y + 2, w - 7, p->light);
    hline(x + 4, y + 3, w - 10, lerp15(p->light, WHITE, 8));
    px(x + 4, y + 3, WHITE); px(x + 5, y + 3, WHITE);
    // left facet, right shade: vertical runs clipped once
    u16 facet = lerp15(p->mid, p->light, 8);
    int f0 = y + 3, f1 = y + h - 2;
    if (f0 < clipY0) f0 = clipY0;
    if (f1 > clipY1) f1 = clipY1;
    if (inside) {
        for (int yy = f0; yy < f1; yy++) {
            u16 *row = cur + yy * SCREEN_W + x;
            if (yy < y + h - 4) row[2] = facet;
            row[w - 3] = p->dark;
        }
    } else {
        for (int yy = f0; yy < f1; yy++) { if (yy < y + h - 4) px(x + 2, yy, facet); px(x + w - 3, yy, p->dark); }
    }
    hline(x + 2, y + h - 3, w - 4, p->dark);
    hline(x + 3, y + h - 2, w - 6, lerp15(p->dark, BLACK, 8));
    // rim
    rectOutline(x + 1, y + 1, w - 2, h - 2, lerp15(p->dark, BLACK, 6));
    px(x + 1, y + 1, BLACK); px(x + w - 2, y + 1, BLACK); px(x + 1, y + h - 2, BLACK); px(x + w - 2, y + h - 2, BLACK);
    if (lit) {
        rectOutline(x, y, w, h, WHITE);
        rectOutline(x + 1, y + 1, w - 2, h - 2, lerp15(WHITE, p->light, 8));
    }
}


// ---- gem sprite cache: each palette rendered once, then blitted with word copies
#define NGEMPAL 7
static DTCM_BSS ALIGN(4) u16 gemCache[NGEMPAL][TILE_H][TILE_W];   // 7.4 KB in zero-wait DTCM
static bool gemCacheReady;
static const GemPal *GEM_PALS[NGEMPAL];
// the pass plaque's found-letter diamond (22x26), rendered once and blitted per HUD slice
#define PLAQUE_W 22
#define PLAQUE_H 26
static ALIGN(4) u16 plaqueCache[PLAQUE_H][PLAQUE_W];
_Static_assert((PLAQUE_W * 2) % 4 == 0, "plaque rows must be word multiples");

// gem cache index per letter (replaces groupFor + a palette search in the board loop)
static u8 palOf[26];
// heat bar gradient, one entry per bar pixel, rebuilt when the frame colours change
#define HEAT_W 70
static u16 heatTop[HEAT_W], heatBody[HEAT_W];
static void buildHeatTables(void)
{
    for (int i = 0; i < HEAT_W; i++) {
        heatTop[i] = lerp15(GOLD, FIRE1, i * 16 / HEAT_W);
        heatBody[i] = lerp15(GOLD_DK, FIRE1, i * 16 / HEAT_W);
    }
}
// Letter-on-gem composites: rows 1..20 of every letter gem and every diamond letter,
// fully opaque, copied to the board with 12-word row moves. Rows 0 and 21 are left to the
// backdrop because the accent dot grid is painted after the per-cell fills and shows
// through those (transparent) rows; columns 0 and 23 are black, which matches the
// backdrop since no dot column coincides with a gem edge column in either hand layout.
#define COMP_ROWS 20
typedef struct { u32 w[TILE_W / 2]; } GemRow;
static ALIGN(4) GemRow gemLetter[26][COMP_ROWS], diamondLetter[26][COMP_ROWS];

static void setupTheme(void)
{
    const GemPal *t = THEMES[S.theme];
    GEM_PALS[0] = &t[0]; GEM_PALS[1] = &t[1]; GEM_PALS[2] = &t[4]; GEM_PALS[3] = &t[2]; GEM_PALS[4] = &t[3];
    GEM_PALS[5] = &GEM_DIAMOND; GEM_PALS[6] = &GEM_FIRE;
    switch (S.frame) {
        case FRAME_SILVER: frameMid = C(20,21,24); frameDk = C(10,11,13); frameLt = C(30,30,31); break;
        case FRAME_ACCENT: frameMid = accent; frameDk = lerp15(accent, BLACK, 9); frameLt = lerp15(accent, WHITE, 8); break;
        default:           frameMid = C(28,22,6); frameDk = C(16,11,2); frameLt = C(31,29,18); break;
    }
    bgTint = t[TH_AMETHYST].light;
    static const u8 groupToPal[5] = { 0, 1, 3, 4, 2 };   // letter group -> GEM_PALS slot
    for (int l = 0; l < 26; l++) palOf[l] = groupToPal[groupFor(l)];
    buildHeatTables();
}

static void drawLetter(int x, int y, int letter, u16 col, u16 shadow);

// clear a w x h block at the top-left of the render target to 0 (transparent)
static void clearScratch(int w, int h)
{
    for (int j = 0; j < h; j++) for (int k = 0; k < w; k++) cur[j * SCREEN_W + k] = 0;
}

// render one letter gem at (0,0) and keep rows 1..20 as an opaque composite
static void buildComposite(GemRow *out, const GemPal *p, int letter, u16 col, u16 shadow)
{
    clearScratch(TILE_W, TILE_H);
    drawGemSized(0, 0, TILE_W, TILE_H, p, false);
    drawLetter(0, 0, letter, col, shadow);
    for (int j = 0; j < COMP_ROWS; j++) {
        u16 *row = (u16 *)out[j].w;
        const u16 *src = cur + (j + 1) * SCREEN_W;
        for (int k = 0; k < TILE_W; k++) row[k] = src[k] ? src[k] : BLACK;
    }
}

// Sprites are rendered into the hidden VRAM bank at (0,0) and copied out; the next frame
// repaints the whole bank, so no RAM scratch buffer is needed.
static void buildGemCache(void)
{
    setupTheme();
    u16 *saved = cur;
    int c0 = clipY0, c1 = clipY1;
    cur = vramBack; clipY0 = 0; clipY1 = SCREEN_H;
    for (int i = 0; i < NGEMPAL; i++) {
        clearScratch(TILE_W, TILE_H);
        drawGemSized(0, 0, TILE_W, TILE_H, GEM_PALS[i], false);
        for (int j = 0; j < TILE_H; j++) for (int k = 0; k < TILE_W; k++) gemCache[i][j][k] = cur[j * SCREEN_W + k];
    }
    clearScratch(PLAQUE_W, PLAQUE_H);
    drawGemSized(0, 0, PLAQUE_W, PLAQUE_H, &GEM_DIAMOND, false);
    for (int j = 0; j < PLAQUE_H; j++) for (int k = 0; k < PLAQUE_W; k++) plaqueCache[j][k] = cur[j * SCREEN_W + k];
    for (int l = 0; l < 26; l++) {
        const GemPal *p = GEM_PALS[palOf[l]];
        buildComposite(gemLetter[l], p, l, C(31,31,29), p->dark);
        buildComposite(diamondLetter[l], &GEM_DIAMOND, l, C(4,6,10), C(20,22,26));
    }
    cur = saved; clipY0 = c0; clipY1 = c1;
    gemCacheReady = true;
}

// copy a 20-row composite to (x, y): x is even in both layouts, so every row is one
// aligned 48-byte move (two ldm/stm pairs); the caller has checked the clip band
static ITCM_CODE __attribute__((noinline)) void blitComposite(const GemRow *src, int x, int y)
{
    u32 *d = (u32 *)(cur + y * SCREEN_W + x);
    const u32 *sp = (const u32 *)src;
    for (int j = 0; j < COMP_ROWS; j++, d += SCREEN_W / 2) {
        u32 *dd = d, r0, r1, r2, r3, r4, r5;
        asm volatile(
            "ldmia %0!, {%2, %3, %4, %5, %6, %7}\n\t"
            "stmia %1!, {%2, %3, %4, %5, %6, %7}\n\t"
            "ldmia %0!, {%2, %3, %4, %5, %6, %7}\n\t"
            "stmia %1, {%2, %3, %4, %5, %6, %7}"
            : "+r"(sp), "+r"(dd), "=&r"(r0), "=&r"(r1), "=&r"(r2), "=&r"(r3), "=&r"(r4), "=&r"(r5)
            : : "memory");
    }
}

// blit a cached gem; colour 0 is transparent (the rounded corners)
static ITCM_CODE __attribute__((noinline)) void blitGem(int x, int y, int pal)
{
    if (x < 0 || x + TILE_W > SCREEN_W) {
        drawGemSized(x, y, TILE_W, TILE_H, GEM_PALS[pal], false);   // clipped sideways: slow path (never in practice)
        return;
    }
    // rows outside the clip band (gems falling in from above the screen, HUD slices) are skipped
    int j0 = clipY0 - y, j1 = clipY1 - y;
    if (j0 < 0) j0 = 0;
    if (j1 > TILE_H) j1 = TILE_H;
    for (int j = j0; j < j1; j++) {
        const u16 *src = gemCache[pal][j];
        u16 *dst = cur + (y + j) * SCREEN_W + x;
        if (j == 0 || j == TILE_H - 1 || j == 1 || j == TILE_H - 2) {
            for (int k = 0; k < TILE_W; k++) if (src[k]) dst[k] = src[k];
        } else if (((uintptr_t)dst & 2) == 0) {
            const u32 *src32 = (const u32 *)src; u32 *d32 = (u32 *)dst;
            for (int k = 0; k < TILE_W / 2; k++) d32[k] = src32[k];
        } else {
            for (int k = 0; k < TILE_W; k++) dst[k] = src[k];
        }
    }
}

// blit the cached plaque diamond, clipped to the slice; colour 0 is transparent
static void blitPlaque(int x, int y)
{
    if (x < 0 || x + PLAQUE_W > SCREEN_W) { drawGemSized(x, y, PLAQUE_W, PLAQUE_H, &GEM_DIAMOND, false); return; }
    int j0 = clipY0 - y, j1 = clipY1 - y;
    if (j0 < 0) j0 = 0;
    if (j1 > PLAQUE_H) j1 = PLAQUE_H;
    for (int j = j0; j < j1; j++) {
        const u16 *src = plaqueCache[j];
        u16 *dst = cur + (y + j) * SCREEN_W + x;
        if (j < 2 || j >= PLAQUE_H - 2) { for (int k = 0; k < PLAQUE_W; k++) if (src[k]) dst[k] = src[k]; }
        else if (((uintptr_t)dst & 2) == 0) { const u32 *src32 = (const u32 *)src; u32 *d32 = (u32 *)dst; for (int k = 0; k < PLAQUE_W / 2; k++) d32[k] = src32[k]; }
        else for (int k = 0; k < PLAQUE_W; k++) dst[k] = src[k];
    }
}

// 2x glyph with a one-pixel drop shadow, written directly (no per-rect clipping)
static ITCM_CODE __attribute__((noinline)) void drawChar2Shadow(int x, int y, char ch, u16 col, u16 shadow)
{
    if (y >= clipY1 || y + 15 <= clipY0) return;
    if (x < 1 || y < clipY0 || x + 11 >= SCREEN_W || y + 15 >= clipY1) { drawChar(x + 1, y + 1, ch, shadow, 2); drawChar(x, y, ch, col, 2); return; }
    const u8 *gl = font5x7[fontIndex(ch)];
    for (int j = 0; j < 7; j++) {
        u8 bits = gl[j];
        if (!bits) continue;
        u16 *r0 = cur + (y + j * 2) * SCREEN_W + x, *r1 = r0 + SCREEN_W;
        u16 *s0 = r0 + SCREEN_W + 1, *s1 = s0 + SCREEN_W;
        for (int i = 0; i < 5; i++) if (bits & (0x10 >> i)) {
            int k = i * 2;
            s0[k] = shadow; s0[k + 1] = shadow; s1[k] = shadow; s1[k + 1] = shadow;
        }
        for (int i = 0; i < 5; i++) if (bits & (0x10 >> i)) {
            int k = i * 2;
            r0[k] = col; r0[k + 1] = col; r1[k] = col; r1[k + 1] = col;
        }
    }
}

// 3x glyph with a one-pixel drop shadow, written directly (the top-screen chain word)
static ITCM_CODE __attribute__((noinline)) void drawChar3Shadow(int x, int y, char ch, u16 col, u16 shadow)
{
    if (y >= clipY1 || y + 22 <= clipY0) return;
    if (x < 0 || y < clipY0 || x + 16 > SCREEN_W || y + 22 > clipY1) { drawChar(x + 1, y + 1, ch, shadow, 3); drawChar(x, y, ch, col, 3); return; }
    const u8 *gl = font5x7[fontIndex(ch)];
    for (int j = 0; j < 7; j++) {
        u8 bits = gl[j];
        if (!bits) continue;
        u16 *r0 = cur + (y + j * 3) * SCREEN_W + x, *r1 = r0 + SCREEN_W, *r2 = r1 + SCREEN_W;
        u16 *s0 = r0 + SCREEN_W + 1, *s1 = s0 + SCREEN_W, *s2 = s1 + SCREEN_W;
        for (int i = 0; i < 5; i++) if (bits & (0x10 >> i)) {
            int k = i * 3;
            s0[k] = shadow; s0[k + 1] = shadow; s0[k + 2] = shadow;
            s1[k] = shadow; s1[k + 1] = shadow; s1[k + 2] = shadow;
            s2[k] = shadow; s2[k + 1] = shadow; s2[k + 2] = shadow;
        }
        for (int i = 0; i < 5; i++) if (bits & (0x10 >> i)) {
            int k = i * 3;
            r0[k] = col; r0[k + 1] = col; r0[k + 2] = col;
            r1[k] = col; r1[k + 1] = col; r1[k + 2] = col;
            r2[k] = col; r2[k + 1] = col; r2[k + 2] = col;
        }
    }
}

static void drawGemOutline(int x, int y)
{
    rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, DIM);
}

// ---------------------------------------------------------------- particles

enum { P_SHARD = 0, P_SPARK, P_STAR, P_RAIN, P_CONF, P_FLYSCORE, P_FLYPASS };   // P_RAIN: the title's falling gems (constant speed, no gravity); P_CONF: confetti; P_FLY*: a gem chip that flies to the score or the pass tracker
typedef struct { s16 x, y, vx, vy; u8 life, maxLife, kind; u16 col; } Particle;   // 1/8 px units
#define MAXP 112
static DTCM_BSS Particle P[MAXP];
static int flyScoreX = 210, flyScoreY = 30, flyPassX = 210, flyPassY = 46;   // where the flying chips land (set from the panel layout)
static int scoreGlint, passGlint;   // frames the score / pass row glow after a chip arrives

static void spawn(int kind, int x, int y, int vx, int vy, int life, u16 col)
{
    for (int i = 0; i < MAXP; i++) if (!P[i].life) {
        P[i].kind = kind; P[i].x = x * 8; P[i].y = y * 8; P[i].vx = vx; P[i].vy = vy;
        P[i].life = P[i].maxLife = life; P[i].col = col;
        return;
    }
}

static void stepParticles(void)
{
    for (int i = 0; i < MAXP; i++) {
        Particle *q = &P[i];
        if (!q->life) continue;
        if (q->kind >= P_FLYSCORE) {     // an eased flight: a seventh of the way to the target each frame, never slower than 1 px
            int tx = (q->kind == P_FLYSCORE ? flyScoreX : flyPassX) * 8, ty = (q->kind == P_FLYSCORE ? flyScoreY : flyPassY) * 8;
            int dx = tx - q->x, dy = ty - q->y;
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (ax + ay < 40) { q->life = 1; if (q->kind == P_FLYSCORE) scoreGlint = 7; else passGlint = 7; }
            else {
                int mx = dx / 6, my = dy / 6;
                if (mx == 0 && dx) mx = dx < 0 ? -8 : 8;
                if (my == 0 && dy) my = dy < 0 ? -8 : 8;
                q->x += mx; q->y += my + (q->life > q->maxLife - 4 ? -10 : 0);     // a small hop first
            }
            q->life--;
            continue;
        }
        q->x += q->vx; q->y += q->vy;
        if (q->kind == P_SHARD) q->vy += 3;
        if (q->kind == P_SPARK) q->vx += (rand() % 5) - 2;
        if (q->kind == P_CONF) { if (q->vy < 14) q->vy += 1; q->vx += (rand() % 3) - 1; }
        q->life--;
    }
}

static ITCM_CODE __attribute__((noinline)) void drawParticles(void)
{
    for (int i = 0; i < MAXP; i++) {
        Particle *q = &P[i];
        if (!q->life) continue;
        int x = q->x >> 3, y = q->y >> 3;
        int fade = (q->life * 16) / q->maxLife;
        u16 col = lerp15(BLACK, q->col, fade);
        switch (q->kind) {
            case P_SHARD: fillRect(x, y, 2, 2, col); break;
            case P_SPARK: px(x, y, col); if (fade > 8) px(x, y + 1, lerp15(BLACK, col, 8)); break;
            case P_STAR:  px(x, y, col); px(x - 1, y, col); px(x + 1, y, col); px(x, y - 1, col); px(x, y + 1, col); break;
            case P_RAIN:  fillRect(x, y, 3, 3, col); px(x, y, lerp15(col, WHITE, 8)); break;
            case P_CONF:  fillRect(x, y, (q->life & 2) ? 4 : 2, (q->life & 2) ? 2 : 4, q->life < 10 ? col : q->col); break;     // flutters: wide, then tall
            default:      fillRect(x - 1, y - 1, 3, 3, q->col); px(x, y, WHITE); break;                                     // P_FLYSCORE / P_FLYPASS
        }
    }
}

// ---------------------------------------------------------------- popups

typedef struct { s16 x, y; u8 life; char txt[12]; u16 col; } Popup;
static DTCM_BSS Popup pops[6];

static void stepPopups(void)
{
    for (int i = 0; i < 6; i++) if (pops[i].life) { pops[i].life--; if (pops[i].life & 1) pops[i].y--; }
}

static void drawPopups(void)
{
    for (int i = 0; i < 6; i++) if (pops[i].life) {
        int sc = pops[i].life > 34 ? 2 : 1;
        drawTextShadow(pops[i].x - textWidth(pops[i].txt, sc) / 2, pops[i].y, pops[i].txt, pops[i].life > 6 ? pops[i].col : DIM, sc);
    }
}

// ---------------------------------------------------------------- layout

static int gridX = GRID_X, panelX = 176, boardX0 = 0;   // boardX0: left edge of the framed board area
#define BX(x) ((x) + boardX0)

static void cellOrigin(int c, int r, int *x, int *y)
{
    *x = gridX + c * TILE_W;
    *y = GRID_Y + r * TILE_H + ((c & 1) ? ODD_SHIFT : 0);
}

// Hex-aware hit test: the gem whose centre is nearest the point, within a 12 px radius. The
// rectangular cell plus the overlapping cells of the two neighbouring columns compete, so the
// three-way gaps between hexes are a dead zone. Ties go to the lower row.
int gfxCellAt(int pxl, int pyl, int *c, int *r)
{
    int cc = (pxl - gridX + 2 * TILE_W) / TILE_W - 2;       // floor division for points left of the grid
    int best = 145, bc = -1, br = -1;
    for (int k = cc - 1; k <= cc + 1; k++) {
        if (k < 0 || k >= COLS) continue;
        int yy = pyl - GRID_Y - ((k & 1) ? ODD_SHIFT : 0);
        int rk = (yy + 2 * TILE_H) / TILE_H - 2;
        for (int rr = rk - 1; rr <= rk + 1; rr++) {
            if (rr < 0 || rr >= ROWS) continue;
            int x, y; cellOrigin(k, rr, &x, &y);
            int dx = pxl - (x + 12), dy = pyl - (y + 11), d2 = dx * dx + dy * dy;
            if (d2 < best || (d2 == best && bc >= 0 && rr < br)) { best = d2; bc = k; br = rr; }
        }
    }
    if (bc < 0) return 0;
    *c = bc; *r = br;
    return 1;
}

#define PANEL_X panelX
#define SUBMIT_Y 120
#define CLEAR_Y 150
bool gfxInSubmit(int x, int y) { return x >= PANEL_X && x < PANEL_X + 76 && y >= SUBMIT_Y && y < SUBMIT_Y + 24; }
bool gfxInClear(int x, int y)  { return x >= PANEL_X && x < PANEL_X + 76 && y >= CLEAR_Y && y < CLEAR_Y + 16; }
bool gfxInPeekButton(int x, int y) { return x >= PANEL_X && x < PANEL_X + 76 && y >= 174 && y < 190; }
#define PAUSE_X (PANEL_X + 64)        // the 12 x 12 pause button; it answers to two more pixels on every side
#define PAUSE_Y 6
bool gfxInPauseButton(int x, int y) { return x >= PAUSE_X - 2 && x < PAUSE_X + 14 && y >= PAUSE_Y - 2 && y < PAUSE_Y + 14; }
void gfxPress(int which)
{
    switch (which) {
        case PRESS_SUBMIT: pressSubmit = 6; break;
        case PRESS_CLEAR:  pressClear = 6; break;
        case PRESS_PEEK:   pressPeek = 6; break;
        case PRESS_PAUSE:  pressPause = 6; break;
    }
}
void gfxSubmitFlash(void) { submitFlash = 3; }
int gfxPressBits(void) { return (pressSubmit ? 1 : 0) | (pressClear ? 2 : 0) | (pressPeek ? 4 : 0) | (pressPause ? 8 : 0) | (submitFlash ? 16 : 0); }

void gfxApplySettings(void)
{
    if (S.hand == HAND_LEFT) { boardX0 = SCREEN_W - BOARD_W; gridX = boardX0 + GRID_X; panelX = 4; }   // gridX 86: even, so gem blits stay word aligned
    else { boardX0 = 0; gridX = GRID_X; panelX = 176; }
    buildGemCache();
}

// ---------------------------------------------------------------- effects API

void gfxBurst(int c, int r, int kind, int letter)
{
    int x, y; cellOrigin(c, r, &x, &y);
    int cx = x + TILE_W / 2, cy = y + TILE_H / 2;
    if (kind == BURST_SHATTER) {
        const GemPal *p = gemFor(letter);
        for (int i = 0; i < 10; i++)
            spawn(P_SHARD, cx + (rand() % 12) - 6, cy + (rand() % 10) - 5, (rand() % 25) - 12, -(rand() % 20) - 4, 18 + rand() % 10, (i & 1) ? p->light : p->mid);
        spawn(P_FLYSCORE, cx, cy, 0, 0, 40, p->light);        // one chip per gem flies to the score on the panel
    } else if (kind == BURST_REVEAL) {
        for (int i = 0; i < 16; i++) {
            int a = i * 4;  // 16 directions
            static const s8 dirx[16] = {8,7,6,3,0,-3,-6,-7,-8,-7,-6,-3,0,3,6,7};
            static const s8 diry[16] = {0,3,6,7,8,7,6,3,0,-3,-6,-7,-8,-7,-6,-3};
            spawn(P_STAR, cx, cy, dirx[a / 4] * 2, diry[a / 4] * 2, 16 + (i & 3) * 2, (i & 1) ? WHITE : GOLD_LT);
        }
        for (int i = 0; i < 8; i++) spawn(P_SPARK, cx + (rand() % 16) - 8, cy, 0, -(rand() % 8) - 2, 20, WHITE);
        spawn(P_FLYPASS, cx, cy, 0, 0, 44, GOLD_LT); spawn(P_FLYPASS, cx, cy, 0, 0, 40, WHITE);     // the found letter flies to the pass tracker
    } else if (kind == BURST_SCORCH) {
        for (int i = 0; i < 14; i++)
            spawn(P_SPARK, cx + (rand() % 16) - 8, cy + (rand() % 8), (rand() % 9) - 4, -(rand() % 14) - 4, 14 + rand() % 10, (i & 1) ? FIRE2 : FIRE1);
    } else if (kind == BURST_FIRE) {
        for (int i = 0; i < 6; i++)
            spawn(P_SPARK, cx + (rand() % 16) - 8, y + 2, (rand() % 5) - 2, -(rand() % 10) - 3, 12 + rand() % 8, FIRE2);
    }
}

void gfxPopup(int c, int r, const char *txt, int gold)
{
    int x, y; cellOrigin(c, r, &x, &y);
    for (int i = 0; i < 6; i++) if (!pops[i].life) {
        pops[i].x = x + TILE_W / 2; pops[i].y = y - 12; pops[i].life = 44;
        if (pops[i].y < 2) pops[i].y = 2;
        pops[i].col = gold ? GOLD_LT : WHITE;
        strncpy(pops[i].txt, txt, 11); pops[i].txt[11] = 0;
        return;
    }
}

static int wipeT;      // frames of iris left (a shelf or the title opening)
#define WIPE_FRAMES 9
void gfxFadeIn(void)  { bright = -8; wipeT = WIPE_FRAMES; shelfAge = 0; callout.life = 0; }
void gfxFlash(void)   { bright = 10; }
void gfxShake(int frames) { shakeFrames = frames; }
void gfxInvalidWord(void) { invalidTimer = 24; }
void gfxCallout(const char *txt, int kind)
{
    strncpy(callout.txt, txt, sizeof(callout.txt) - 1); callout.txt[sizeof(callout.txt) - 1] = 0;
    callout.life = 40; callout.kind = (u8)kind;
}
int gfxCalloutLife(void) { return callout.life; }
const char *gfxCalloutText(void) { return callout.txt; }
static int nopeC, nopeR, nopeTimer;
void gfxNope(int c, int r) { nopeC = c; nopeR = r; nopeTimer = 6; }

// title buttons on the touch screen
#define TB_Y 134
bool gfxInTitleClassic(int x, int y) { return x >= BX(20) && x < BX(84) && y >= TB_Y && y < TB_Y + 22; }
bool gfxInTitleDaily(int x, int y)   { return x >= BX(88) && x < BX(152) && y >= TB_Y && y < TB_Y + 22; }
bool gfxInTitleOptions(int x, int y) { return x >= BX(54) && x < BX(118) && y >= TB_Y + 26 && y < TB_Y + 46; }
bool gfxInTitleLedger(int x, int y)  { return x >= BX(12) && x < BX(160) && y >= TB_Y + 46 && y < SCREEN_H - 1; }   // the BEST line, just under OPTIONS
// the GUIDE button sits in the free corner above the logo band (it ends at BX 162, y 6..24); its touch zone is a little roomier
#define GUIDE_BTN_X 112
#define GUIDE_BTN_W 50
// the language tag is the GUIDE button's twin in the other top corner (BX 16..108)
#define LANG_BTN_X 16
#define LANG_BTN_W 92
bool gfxInTitleLanguage(int x, int y) { return x >= BX(LANG_BTN_X - 4) && x < BX(LANG_BTN_X + LANG_BTN_W) && y >= 3 && y < 28; }
bool gfxInTitleGuide(int x, int y)   { return x >= BX(GUIDE_BTN_X - 4) && x < BX(GUIDE_BTN_X + GUIDE_BTN_W + 4) && y >= 3 && y < 28; }

// Options list rows on the touch screen. The list sits at a fixed x (never BX): flipping HAND must
// not move it under the stylus. Each row has a left half (previous value) and a right half (next).
#define OPT_Y0 32
#define OPT_DY 13                       // nine rows end at y 149; BACK sits at 153..169 and the footer at 176, clear of the frame at 190
static int nudgeDir, nudgeTimer;
void gfxOptionNudge(int dir) { nudgeDir = dir; nudgeTimer = 6; }
int gfxOptionRowAt(int x, int y)
{
    if (x < 12 || x >= 160) return -1;
    if (y >= 150 && y < 173) return OPT_ROWS * 2;      // BACK
    int row = (y - OPT_Y0) / OPT_DY;
    if (y < OPT_Y0 || row < 0 || row >= OPT_ROWS) return -1;
    return row * 2 + (x >= 86);
}

// ---------------------------------------------------------------- board

// chain membership as a bit set (bit c*ROWS+r), built once per frame in gfxFrame
static u64 chainMask;
static inline bool inChain(int c, int r) { return (u32)(chainMask >> (c * ROWS + r)) & 1; }

static void drawLetter(int x, int y, int letter, u16 col, u16 shadow)
{
    if (letter == 'q' - 'a') {
        drawChar2Shadow(x + 2, y + 5, 'Q', col, shadow);
        drawChar(x + 14, y + 10, 'U', shadow, 1); drawChar(x + 13, y + 9, 'U', col, 1);
    } else {
        drawChar2Shadow(x + 7, y + 4, 'A' + letter, col, shadow);
    }
}

// Floor glow as fire gets low: red added to the bottom twelve rows of the strip. Where the
// band is backdrop it is painted as a fill in drawBackdrop (no VRAM reads); the three
// odd-column bottom gems that reach into it get an additive pass in drawDanger.
#define DANGER_ROWS 12
#define DANGER_W 169                 // x BX(2) .. BX(170): 21 quads + 1 pixel
static u8 dangerA[DANGER_ROWS];      // red to add per band row j (y = SCREEN_H - 2 - j)
static int dangerRows;               // active rows this frame (0 = no glow)

static void dangerSetup(const Game *g)
{
    dangerRows = 0;
    frameDanger = 0;
    frameMood = (g->state == ST_PLAY && g->peekHeld) ? 10 : 0;
    if (g->state != ST_PLAY) return;
    int low = fireLow;
    if (low >= 5) {       // the frame pulses red on a 24-frame (2.5 Hz) triangle, deeper the lower the fire
        int ph = frame % 24, tri = ph < 12 ? ph : 24 - ph;
        frameDanger = tri * (low - 4) / 3;
        if (frameDanger > 12) frameDanger = 12;
    }
    if (low < 4) return;
    int strength = (low - 3) * 4 + ((frame & 16) ? 2 : 0);   // 4..18
    for (int j = 0; j < DANGER_ROWS; j++) {
        int a = (strength * (DANGER_ROWS - j)) / DANGER_ROWS;
        if (a <= 0) break;
        dangerA[j] = (u8)(a > 12 ? 12 : a);
        dangerRows = j + 1;
    }
}

// The callout: pops in big (three times, or twice for a long word), settles at 2x, and in its
// last eight frames drops to 1x and rises a pixel a frame. It is centred on the strip, never
// wider than 166 px, and stays out of the way while the player is reading the back layer.
static void drawCallout(const Game *g)
{
    if (!callout.life || g->peekHeld) return;
    int life = callout.life, base = textWidth(callout.txt, 2) <= 166 ? 2 : 1, sc;
    if (life > 32) sc = textWidth(callout.txt, 3) <= 160 ? 3 : base;
    else if (life > 8) sc = base;
    else sc = 1;
    u16 col = callout.kind == 1 ? FIRE1 : callout.kind == 2 ? ACCENT : GOLD_LT;
    if (life <= 8) col = lerp15(BLACK, col, life * 2);
    int y = 84 - (7 * sc) / 2 - (life <= 8 ? 8 - life : 0), tw = textWidth(callout.txt, sc), x = BX(86) - tw / 2;
    if (life > 8) fillRect(x - 4, y - 3, tw + 8, 7 * sc + 6, BLACK);   // one dark plate keeps the word readable over busy gems
    drawTextShadow(x, y, callout.txt, col, sc);
}

// N+ dot grid behind the gems (dots under solid gems are hidden anyway)
static ITCM_CODE __attribute__((noinline)) void drawBoardDots(void)
{
    u16 dotA = lerp15(BLACK, ACCENT, frameMood ? 7 : 3), dotB = lerp15(BLACK, bgTint, frameMood ? 8 : 5);   // alternate dots: accent and the theme's own tint
    for (int y = 6; y < SCREEN_H; y += 8) {
        if (y < clipY0 || y >= clipY1) continue;
        u16 *row = cur + y * SCREEN_W + boardX0;
        for (int x = 6; x < 172; x += 8) row[x] = (((x >> 3) + (y >> 3)) & 1) ? dotB : dotA;
    }
}

// gold frame around the board strip, with a shimmer running along the top edge
static void drawBoardFrame(void)
{
    u16 fdk = GOLD_DK, fmd = GOLD;
    if (frameDanger) { fdk = lerp15(fdk, FIRE1, frameDanger); fmd = lerp15(fmd, FIRE2, frameDanger); }
    else if (frameMood) { fdk = lerp15(fdk, lerp15(ACCENT, BLACK, 6), frameMood); fmd = lerp15(fmd, ACCENT, frameMood); }
    rectOutline(BX(0), 0, BOARD_W, SCREEN_H, fdk);
    rectOutline(BX(1), 1, BOARD_W - 2, SCREEN_H - 2, fmd);
    int sh = (frame * 2) % 240 - 20;
    for (int i = 0; i < 14; i++) {
        int x = 1 + sh + i;
        if (x < 1 || x > BOARD_W - 2) continue;      // the shimmer runs along the frame only
        px(BX(x), 1, frameDanger ? FIRE2 : GOLD_LT); px(BX(x), 0, fmd);
    }
}

// Clears the board area, except under gems that will be fully overdrawn.
static ITCM_CODE __attribute__((noinline)) void drawBackdrop(const Game *g)
{
    // margins around the grid
    fillRect(boardX0, 0, GRID_X, SCREEN_H, BLACK);
    fillRect(gridX + COLS * TILE_W, 0, BOARD_W - GRID_X - COLS * TILE_W, SCREEN_H, BLACK);
    for (int c = 0; c < COLS; c++) {
        int x = gridX + c * TILE_W, top = GRID_Y + ((c & 1) ? ODD_SHIFT : 0);
        fillRect(x, 0, TILE_W, top, BLACK);
        fillRect(x, top + ROWS * TILE_H, TILE_W, SCREEN_H - top - ROWS * TILE_H, BLACK);
        for (int r = 0; r < ROWS; r++) {
            const Cell *cl = &g->cell[c][r];
            int y = top + r * TILE_H;
            bool solid = (cl->state == CELL_TILE || cl->state == CELL_SECRET) && cl->yoff == 0 && !cl->squash && !cl->flash
                         && !g->peekHeld && !inChain(c, r);
            if (solid) {
                // drawBoard paints such a cell with an opaque 20-row composite (rows 1..20,
                // edge columns included), so only rows 0 and 21 are left to the backdrop
                fillRect(x, y, TILE_W, 1, BLACK); fillRect(x, y + TILE_H - 1, TILE_W, 1, BLACK);
            } else {
                fillRect(x, y, TILE_W, TILE_H, BLACK);
            }
        }
    }
    // floor glow over the backdrop (gems drawn later cover their part of it)
    for (int j = 0; j < dangerRows; j++) fillRect(BX(2), SCREEN_H - 2 - j, DANGER_W, 1, C(1 + dangerA[j], 1, 2));
    drawBoardDots();
}

// ---- single-cell renderers shared by the board and the options screen's sample board, so the sample is the real thing

// a peeled cell: the back-layer number in its colour (scorched cells get a burnt rim)
static void drawHoleCell(int x, int y, int n, bool scorched, bool chimney)
{
    rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, scorched ? C(12,4,0) : ACCENT);
    rectOutline(x + 3, y + 3, TILE_W - 6, TILE_H - 6, DIM2);
    u16 dcol = n == 0 ? C(10,10,12) : n == 1 ? ACCENT : n == 2 ? GOLD_LT : n == 3 ? FIRE2 : WHITE;
    if (n && !(frame & 16)) dcol = lerp15(dcol, WHITE, 8);
    drawChar2Shadow(x + 7, y + 4, '0' + n, dcol, BLACK);
    if (chimney) rectOutline(x, y, TILE_W, TILE_H, FIRE1);
}

// A burning gem. Fire is told apart by light, not hue: a light plate under a dark letter, a bright bar
// across the top and tongues that rise over the edge (a flame reads the same without any colour).
static void drawBurnCell(int x, int yd, int sq, int letter, int ph, bool lit, bool peek)
{
    int pulse = ph < 8 ? ph : 15 - ph;
    GemPal firePal = GEM_FIRE;
    firePal.mid = lerp15(C(31,4,0), C(31,14,0), pulse * 2);
    drawGemSized(x, yd + sq, TILE_W, TILE_H - sq, &firePal, lit);
    int py = yd + sq / 2;
    if (letter == 'q' - 'a') fillRect(x + 1, py + 3, 19, 16, firePal.light);
    else fillRect(x + 5, py + 3, 14, 16, firePal.light);
    drawLetter(x, py, letter, C(10,0,0), lerp15(firePal.light, BLACK, 8));
    fillRect(x + 3, yd + 1, TILE_W - 6, 2, FIRE2);
    // pulsing red rim and flame tongues licking over the top edge
    rectOutline(x, yd, TILE_W, TILE_H, lerp15(C(20,2,0), FIRE2, pulse * 2));
    for (int i = 0; i < 3; i++) {
        int fx = x + 5 + i * 7 + ((ph + i * 5) & 3), fh = 5 + ((ph + i * 3) & 3);
        for (int j = 0; j < fh; j++) px(fx, yd - j, j == fh - 1 ? FIRE2 : lerp15(FIRE1, FIRE2, j * 3));
        px(fx + 1, yd - 1, FIRE1);
    }
    if (peek && (frame & 16)) drawChar(x + 17, yd + 3, '!', FIRE2, 1);
}

// a gem seen through peek: an outline with its letter in the group colour (a red double rim when a peel would scorch)
static void drawGhostCell(int x, int yd, int letter, bool risk)
{
    const GemPal *pp = GEM_PALS[palOf[letter]];
    if (risk) {
        int ph = frame & 15, pu = ph < 8 ? ph : 15 - ph;
        rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, lerp15(C(12,3,0), FIRE1, pu * 2));
        rectOutline(x + 2, yd + 2, TILE_W - 4, TILE_H - 4, C(12,3,0));
        if (frame & 16) drawChar(x + 17, yd + 3, '!', FIRE2, 1);
    } else rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, lerp15(pp->mid, BLACK, 8));
    drawLetter(x, yd, letter, lerp15(pp->light, BLACK, 6), BLACK);
}

static ITCM_CODE __attribute__((noinline)) void drawBoard(const Game *g)
{
    bool peek = g->peekHeld;
    int outerClip = clipY0;
    for (int c = 0; c < COLS; c++) {
        for (int r = 0; r < ROWS; r++) {
            const Cell *cl = &g->cell[c][r];
            int x, y; cellOrigin(c, r, &x, &y);
            int yd = y + YOFF_PX(cl);
            // A gem refilled under a hole, diamond or flame drops in from behind it: clip
            // everything below a separator to the top of the next cell, so a falling gem
            // never paints over the separator above it (the composite fast path tests the
            // same clipY0, and blitGem/drawGemSized clip to it).
            if (r > 0) {
                int above = g->cell[c][r - 1].state;
                if (above == CELL_HOLE || above == CELL_SECRET || above == CELL_BURN) clipY0 = (y > outerClip) ? y : outerClip;
            }
            bool lit = inChain(c, r);
            if (lit) yd -= 2;
            int sq = cl->squash;           // landing squash: shorter, sits on the slot floor
            if (sq == SQUASH_MAX && g->state == ST_PLAY) {
                // dust puff on a hard landing
                for (int i = 0; i < 3; i++)
                    spawn(P_SPARK, x + 3 + rand() % (TILE_W - 6), y + TILE_H - 2, (i - 1) * 6, -(rand() % 6) - 1, 8, lerp15(BLACK, WHITE, 10));
            }
            bool chimney = peek && boardFireAbove(g, c, r) && (cl->state == CELL_EMPTY || cl->state == CELL_HOLE);
            if (chimney) fillRect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, ((frame >> 3) & 1) ? C(12,3,0) : C(8,2,0));
            switch (cl->state) {
                case CELL_EMPTY:
                    if (peek) drawGemOutline(x, y);
                    break;
                case CELL_HOLE:
                    drawHoleCell(x, y, boardBackNumber(g, c, r), g->scorched[c][r], chimney);
                    break;
                case CELL_SECRET: {
                    if (!sq && !lit && !cl->flash && yd + 1 >= clipY0 && yd + 1 + COMP_ROWS <= clipY1) {
                        blitComposite(diamondLetter[cl->letter], x, yd + 1);   // gem and letter in one move
                    } else {
                        if (sq) drawGemSized(x, yd + sq, TILE_W, TILE_H - sq, &GEM_DIAMOND, lit);
                        else { blitGem(x, yd, 5); if (lit) { rectOutline(x, yd, TILE_W, TILE_H, WHITE); rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, WHITE); } }
                        drawLetter(x, yd + sq / 2, cl->letter, C(4,6,10), C(20,22,26));
                    }
                    // travelling sparkle across the diamond
                    int sx = x + 3 + ((frame + c * 5 + r * 9) % 40) / 2;
                    if (sx < x + TILE_W - 3) { px(sx, yd + 2, WHITE); px(sx - 1, yd + 3, WHITE); }
                    break;
                }
                case CELL_BURN: {
                    int ph = (frame + c * 7 + r * 3) & 15;
                    drawBurnCell(x, yd, sq, cl->letter, ph, lit, peek);
                    // glow bleeding below
                    if (r < ROWS - 1) {
                        int gy = yd + TILE_H;
                        hline(x + 3, gy, TILE_W - 6, lerp15(BLACK, FIRE1, 8 + (ph >> 1)));
                        hline(x + 5, gy + 1, TILE_W - 10, lerp15(BLACK, FIRE1, 4 + (ph >> 2)));
                    }
                    break;
                }
                default:
                    if (peek) {
                        drawGhostCell(x, yd, cl->letter, r > 0 && g->cell[c][r - 1].state == CELL_BURN);   // a peel under a flame scorches
                    } else if (!sq && !lit && !cl->flash && yd + 1 >= clipY0 && yd + 1 + COMP_ROWS <= clipY1) {
                        blitComposite(gemLetter[cl->letter], x, yd + 1);   // the common case: one 48-byte move per row
                    } else {
                        int pal = palOf[cl->letter];
                        const GemPal *p = GEM_PALS[pal];
                        if (sq) drawGemSized(x, yd + sq, TILE_W, TILE_H - sq, p, lit);
                        else {
                            blitGem(x, yd, pal);
                            if (lit) { rectOutline(x, yd, TILE_W, TILE_H, WHITE); rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, lerp15(WHITE, p->light, 8)); }
                        }
                        drawLetter(x, yd + sq / 2, cl->letter, lit ? WHITE : C(31,31,29), p->dark);
                    }
            }
            if (cl->flash) {
                rectOutline(x, y, TILE_W, TILE_H, lerp15(BLACK, WHITE, cl->flash > 14 ? 16 : cl->flash + 2));
            }
            if (lit && invalidTimer) {                         // a rejected word: the chained gems' lit rims go red
                rectOutline(x, yd, TILE_W, TILE_H, FIRE1);
                rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, lerp15(FIRE1, WHITE, 4));
            }
            if (nopeTimer > 0 && c == nopeC && r == nopeR) {   // the drag could not reach this gem
                rectOutline(x, yd, TILE_W, TILE_H, FIRE1);
                rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, lerp15(FIRE1, WHITE, nopeTimer * 2));
            }
        }
        clipY0 = outerClip;
    }
    // the d-pad cursor: a 2 px rim alternating white and accent, with a notch above it
    if (g->curOn) {
        int x, y; cellOrigin(g->curC, g->curR, &x, &y);
        bool risk = peek && g->curR > 0 && g->cell[g->curC][g->curR - 1].state == CELL_BURN;
        u16 cc = risk ? FIRE2 : ((frame >> 3) & 1) ? ACCENT : WHITE;
        rectOutline(x, y, TILE_W, TILE_H, cc);
        rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, cc);
        fillRect(x + TILE_W / 2 - 2, y - 2, 4, 2, cc);
    }
    // the armed peel: a pulsing double rim and a PEEL label that the release will act on
    if (g->armed) {
        int x, y; cellOrigin(g->armC, g->armR, &x, &y);
        u16 ac = lerp15(ACCENT, WHITE, (frame & 7) * 2);
        rectOutline(x, y, TILE_W, TILE_H, ac);
        rectOutline(x + 2, y + 2, TILE_W - 4, TILE_H - 4, ac);
        const char *peelTag = ui(UI_PEELTAG);
        int tw = textWidth(peelTag, 1), ly = y - 9;
        if (ly < 3) ly = 3;
        int lx = x + TILE_W / 2 - tw / 2;
        fillRect(lx - 1, ly - 1, tw + 2, 9, BLACK);
        drawText(lx, ly, peelTag, WHITE, 1);
    }
    // chain links: gold rope that turns to the shelf accent once the word is valid
    bool chainValid = chainValidNow;
    u16 ropeDk = chainValid ? lerp15(ACCENT, BLACK, 8) : GOLD_DK, ropeLt = chainValid ? ACCENT : GOLD_LT;
    if (invalidTimer) { ropeDk = lerp15(FIRE1, BLACK, 8); ropeLt = FIRE1; }     // a rejected word: the rope and rims flash red
    {   // the rope: 5 px links brightening toward the tail, with a spark running the whole chain
        int segs = g->chainLen - 1;
        for (int i = 1; i < g->chainLen; i++) {
            int x0, y0, x1, y1;
            cellOrigin(g->chainC[i - 1], g->chainR[i - 1], &x0, &y0);
            cellOrigin(g->chainC[i], g->chainR[i], &x1, &y1);
            x0 += TILE_W / 2; y0 += TILE_H / 2 - 2; x1 += TILE_W / 2; y1 += TILE_H / 2 - 2;
            u16 core = lerp15(ropeLt, invalidTimer ? FIRE2 : WHITE, (i - 1) * 7 / (segs > 1 ? segs - 1 : 1));
            for (int t = 0; t <= 5; t++) {
                int xx = x0 + (x1 - x0) * t / 5, yy = y0 + (y1 - y0) * t / 5;
                fillRect(xx - 2, yy - 2, 5, 5, ropeDk);
                fillRect(xx - 1, yy - 1, 3, 3, core);
            }
        }
        if (segs > 0) {
            int total = segs * 6, pos = (frame * 2) % (total + 14);
            if (pos < total) {
                int i = pos / 6 + 1, t = pos % 6;
                int x0, y0, x1, y1;
                cellOrigin(g->chainC[i - 1], g->chainR[i - 1], &x0, &y0);
                cellOrigin(g->chainC[i], g->chainR[i], &x1, &y1);
                int xx = x0 + TILE_W / 2 + (x1 - x0) * t / 6, yy = y0 + TILE_H / 2 - 2 + (y1 - y0) * t / 6;
                u16 sp = invalidTimer ? FIRE2 : WHITE;
                fillRect(xx - 1, yy - 1, 3, 3, sp); px(xx, yy - 2, sp); px(xx, yy + 2, sp); px(xx - 2, yy, sp); px(xx + 2, yy, sp);
            }
        }
    }
    // a valid word: a gold ring on the tail gem advertises the tap that submits it
    if (chainValid) {
        int x, y; cellOrigin(g->chainC[g->chainLen - 1], g->chainR[g->chainLen - 1], &x, &y);
        y -= 2;
        rectOutline(x, y, TILE_W, TILE_H, GOLD_LT);
        rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, GOLD_LT);
    }
    // chain order numbers float above the first gem
    if (g->chainLen) {
        int x, y; cellOrigin(g->chainC[0], g->chainR[0], &x, &y);
        fillRect(x + TILE_W / 2 - 2, y - 4, 4, 2, GOLD_LT);
    }
}

typedef struct { u32 w[4]; } Quad;
// add a (same value in both halves) to the red channel of two pixels, saturating at 31
static inline u32 glow2(u32 v, u32 a2)
{
    u32 r = (v & 0x001F001Fu) + a2;                  // at most 31 + 12 per half: no carry across
    u32 m = r & 0x00200020u;                         // overflow bit per half ...
    r = (r | (m - (m >> 5))) & 0x001F001Fu;          // ... becomes a 0x1F saturation mask
    return (v & 0xFFE0FFE0u) | r;
}
// Additive glow over the gems that reach into the band: only the odd-column bottom row
// does (composite rows y+1..y+20 = 168..187; the band starts at 179). A gem drawn through
// the opaque path (composite, or a burning gem with its full rim) is tinted in place; a
// lit, squashed or flashing gem there briefly goes untinted rather than reading more VRAM.
static ITCM_CODE __attribute__((noinline)) void drawDanger(const Game *g)
{
    if (!dangerRows || g->peekHeld) return;
    for (int c = 1; c < COLS; c += 2) {
        const Cell *cl = &g->cell[c][ROWS - 1];
        bool gem = cl->state == CELL_TILE || cl->state == CELL_SECRET || cl->state == CELL_BURN;
        bool opaque = gem && !cl->squash && !cl->flash && (cl->state == CELL_BURN || !inChain(c, ROWS - 1));
        if (!opaque) continue;
        int x, y; cellOrigin(c, ROWS - 1, &x, &y);
        int top = y + YOFF_PX(cl) + 1, bot = y + YOFF_PX(cl) + COMP_ROWS;   // first and last opaque row
        for (int j = 0; j < dangerRows; j++) {
            int yy = SCREEN_H - 2 - j;
            if (yy > bot || yy < top) continue;
            u32 a2 = dangerA[j] | ((u32)dangerA[j] << 16);
            Quad *p = (Quad *)(cur + yy * SCREEN_W + x);           // x is even: aligned quads
            for (int i = 0; i < TILE_W / 8; i++) {                 // eight pixels per ldm/stm burst
                Quad q = p[i];
                q.w[0] = glow2(q.w[0], a2); q.w[1] = glow2(q.w[1], a2);
                q.w[2] = glow2(q.w[2], a2); q.w[3] = glow2(q.w[3], a2);
                p[i] = q;
            }
        }
    }
}

// a label on the left of a panel row, its value right-aligned to the panel edge
static void panelRow(int y, const char *label, const char *val, u16 vcol)
{
    drawText(PANEL_X, y, label, DIM_TEXT, 1);
    drawText(PANEL_X + 76 - textWidth(val, 1), y, val, vcol, 1);
}

// Panel rows (every string is at most 12 characters, 71 px, so it fits the 76 px panel):
//   6 logo and pause   22 score   42 pass tracker   55 PEELS   64 TURN   74 chain word (or the peek hint)
//   84 chain value   94 HEAT / OVERTIME and the heat preview   102 heat bar   111 fire countdown
//   120 SUBMIT   150 CLEAR   166 RED: SCORCH   174 PEEK
static ITCM_CODE __attribute__((noinline)) void drawPanel(const Game *g)
{
    drawBoardFrame();

    if (S.hand == HAND_LEFT) fillRect(0, 0, boardX0, SCREEN_H, BLACK);
    else fillRect(PANEL_X - 2, 0, SCREEN_W - PANEL_X + 2, SCREEN_H, BLACK);
    drawLogo(PANEL_X, 6, 1);
    {   // pause button: two bars in a 12 x 12 box
        bool pr = pressPause > 0;
        if (pr) fillRect(PAUSE_X, PAUSE_Y, 12, 12, lerp15(BLACK, INK, 3));
        rectOutline(PAUSE_X, PAUSE_Y, 12, 12, pr ? INK : DIM_TEXT);
        fillRect(PAUSE_X + 3, PAUSE_Y + 3, 2, 6, INK); fillRect(PAUSE_X + 7, PAUSE_Y + 3, 2, 6, INK);
    }
    char s[32];
    fmtInt(s, shownScore);
    flyScoreX = PANEL_X + 24; flyScoreY = 28; flyPassX = PANEL_X + 4 + 8 * (g->passLen > 0 ? 0 : 0); flyPassY = 47;
    u16 scoreCol = scoreGlint ? lerp15(GOLD, WHITE, scoreGlint * 2) : GOLD;
    if (textWidth(s, 2) <= 80) drawTextShadow(PANEL_X, 22, s, scoreCol, 2);   // 80 px of panel: six digits at 2x
    else drawTextShadow(PANEL_X, 26, s, scoreCol, 1);                           // a million and up drops to 1x

    // the pass: one 7 x 10 slot per letter (nine fit the 76 px panel), a diamond once found
    for (int i = 0; i < g->passLen; i++) {
        int x = PANEL_X + i * 8;
        if (g->found[i]) {
            fillRect(x, 42, 7, 10, (letterPop[i] > 12 || passGlint) ? GOLD_LT : GEM_DIAMOND.light);
            drawChar(x + 1, 44, 'A' + (g->pass[i] - 'a'), C(4,6,10), 1);
        } else {
            rectOutline(x, 42, 7, 10, GOLD_DK);
            hline(x + 2, 50, 3, GOLD);
        }
    }
    fmtInt(s, g->peels); panelRow(55, ui(UI_PEELS), s, WHITE);
    fmtInt(s, g->turn);  panelRow(64, ui(UI_TURN), s, g->overtime ? FIRE1 : WHITE);

    char w[WORD_BUF]; strcpy(w, chainWordNow);
    for (char *p = w; *p; p++) *p -= 32;
    bool valid = chainValidNow, peek = g->peekHeld != 0;
    if (strlen(w) > 12) { memmove(w + 1, w + strlen(w) - 11, 12); w[0] = '.'; }   // the last 11 letters fit the panel
    if (w[0]) drawText(PANEL_X, 74, w, valid ? GOLD_LT : WHITE, 1);
    else if (!peek) { char hp[24]; uiFmt(hp, sizeof hp, UI_P_HOLDPEEK, S.hand == HAND_LEFT ? "R" : "L"); drawText(PANEL_X, 74, hp, DIM_TEXT, 1); }   // the shoulder under the idle hand
    if (g->chainLen >= 3 && !peek) {
        if (valid) {   // what the word is worth, and its length multiplier
            s[0] = '+'; fmtInt(s + 1, chainScoreNow);
            drawText(PANEL_X, 84, s, GOLD_LT, 1);
            int mult = boardLenMult((int)strlen(chainWordNow));
            if (mult > 1) { s[0] = 'X'; s[1] = (char)('0' + mult); s[2] = 0; drawText(PANEL_X + 76 - textWidth(s, 1), 84, s, ACCENT, 1); }
        } else drawText(PANEL_X, 84, "...", DIM_TEXT, 1);
    }

    // heat: OVERTIME replaces the label past the shelf's turn budget, and a valid chain previews its cost
    int hw = HEAT_W * g->heat / (g->heatThreshold ? g->heatThreshold : 1);
    if (hw > HEAT_W) hw = HEAT_W;
    const char *hl = ui(g->overtime ? UI_P_OVERTIME : UI_P_HEAT);
    drawText(PANEL_X, 94, hl, g->overtime ? ((frame & 16) ? FIRE1 : lerp15(BLACK, FIRE1, 8)) : DIM_TEXT, 1);
    bool preview = valid && !peek;
    if (preview) {
        int cost = boardTurnHeat(g, boardHeatFor((int)strlen(chainWordNow)));
        char pv[24];
        if (cost > 0) uiFmt(pv, sizeof pv, UI_P_HEATPLUS, cost); else strcpy(pv, ui(UI_P_NOHEAT));
        if (textWidth(hl, 1) + 5 + textWidth(pv, 1) > 76) { pv[0] = '+'; fmtInt(pv + 1, cost); }   // OVERTIME leaves room for the number only
        drawText(PANEL_X + 76 - textWidth(pv, 1), 94, pv, cost > 0 ? FIRE2 : ACCENT, 1);
    }
    rectOutline(PANEL_X, 102, 72, 6, DIM);
    {   // rows 103..106, precomputed gradient (the panel is always inside the touch screen)
        u16 *row = cur + 103 * SCREEN_W + PANEL_X + 1;
        for (int i = 0; i < hw; i++) { row[i] = heatTop[i]; row[SCREEN_W + i] = heatBody[i]; row[2 * SCREEN_W + i] = heatBody[i]; row[3 * SCREEN_W + i] = heatBody[i]; }
    }
    if (hw > 50 && (frame & 8) && !preview && !g->overtime) drawText(PANEL_X + textWidth(hl, 1) + 6, 94, "!", FIRE2, 1);

    // fire countdown: one pip per turn between fire steps, filled for the turns left; the last one blinks NEXT
    if (fireLow >= 0) {
        int left = g->burnEvery - g->burnTimer, n = g->burnEvery;
        if (left < 1) left = 1;
        if (n > 6) n = 6;                                   // a poked burnEvery of 99 still fits the panel
        bool next = left == 1 && (frame & 8);
        const char *fl = ui(next ? UI_NEXT : UI_FIRE);
        int pip0 = textWidth(fl, 1) + 4 > 30 ? textWidth(fl, 1) + 4 : 30;       // the pips start right of the label
        drawText(PANEL_X, 111, fl, next ? FIRE1 : FIRE2, 1);
        for (int i = 0; i < n; i++) {
            if (i < left) fillRect(PANEL_X + pip0 + i * 7, 112, 5, 5, FIRE1);
            else rectOutline(PANEL_X + pip0 + i * 7, 112, 5, 5, DIM_TEXT);
        }
    }

    // submit button: jeweled, pulses when a valid word is chained; sunk by two pixels while pressed
    u16 bc = valid ? lerp15(GOLD_DK, GOLD, ((frame & 31) < 16) ? (frame & 15) : 15 - (frame & 15)) : DIM;
    if (submitFlash) bc = WHITE;
    bool pr = pressSubmit > 0;
    int in = pr ? 2 : 0;
    if (pr) fillRect(PANEL_X, SUBMIT_Y, 76, 24, DIM2);
    roundRect(PANEL_X + in, SUBMIT_Y + in, 76 - in * 2, 24 - in * 2, bc);
    if (valid || submitFlash) {
        hline(PANEL_X + 3 + in, SUBMIT_Y + 2 + in, 70 - in * 2, pr ? GOLD_DK : GOLD_LT);
        hline(PANEL_X + 2 + in, SUBMIT_Y + 21 - in, 72 - in * 2, pr ? GOLD_LT : GOLD_DK);
    }
    drawTextCenter(PANEL_X + 38, SUBMIT_Y + 8 + (pr ? 1 : 0), ui(UI_P_SUBMIT), (valid || submitFlash) ? BLACK : INK, 1);
    if (pressClear > 0) {
        fillRect(PANEL_X, CLEAR_Y, 76, 16, lerp15(BLACK, INK, 3));
        rectOutline(PANEL_X + 2, CLEAR_Y + 2, 72, 12, INK);
    } else rectOutline(PANEL_X, CLEAR_Y, 76, 16, DIM);
    drawTextCenter(PANEL_X + 38, CLEAR_Y + 4 + (pressClear > 0 ? 1 : 0), ui(UI_P_CLEAR), pressClear > 0 ? WHITE : INK, 1);

    if (peek && fireLow >= 0) drawTextCenter(PANEL_X + 38, 166, ui(UI_P_REDSCORCH), FIRE1, 1);
    if (peek) {      // PEEK button: filled while a shoulder is held or the latch is on
        fillRect(PANEL_X, 174, 76, 16, ACCENT);
        drawTextCenter(PANEL_X + 38, 178, ui(g->peekLatch ? UI_P_PEEKON : UI_PEEK), BLACK, 1);
    } else if (pressPeek > 0) {
        fillRect(PANEL_X, 174, 76, 16, lerp15(BLACK, ACCENT, 5));
        rectOutline(PANEL_X + 2, 176, 72, 12, ACCENT);
        drawTextCenter(PANEL_X + 38, 179, ui(UI_PEEK), WHITE, 1);
    } else {
        rectOutline(PANEL_X, 174, 76, 16, ACCENT);
        drawTextCenter(PANEL_X + 38, 178, ui(UI_PEEK), INK, 1);
    }
}

// the title, options and ledger screens: no board, no panel, the title layout on the top screen
static inline bool menuState(int st) { return st == ST_TITLE || st == ST_OPTIONS || st == ST_STATS || st == ST_GUIDE || st == ST_LANGPICK; }

// ---------------------------------------------------------------- top screen
// Everything below draws on the top screen, which is never mirrored for the left hand:
// never use BX() here.

static void drawTipCard(int which);

static const short OPT_NAMES[OPT_ROWS] = { UI_O_THEME, UI_O_FRAME, UI_O_ACCENT, UI_O_HAND, UI_FIRE, UI_O_MUSIC, UI_O_LIBRARY, UI_O_SPELLING, UI_O_LANGUAGE };
static const short LIB_NAMES[LIB_COUNT] = { UI_V_FULL, UI_V_COMMON };
static const short SPELL_NAMES[SPELL_COUNT] = { UI_V_BOTH, UI_V_AMERICAN, UI_V_BRITISH };
static const short THEME_NAMES[THEME_COUNT] = { UI_V_JEWEL, UI_V_INK, UI_V_CANDY, UI_V_EMBER, UI_V_CLEAR };
static const short FRAME_NAMES[FRAME_COUNT] = { UI_V_GOLD, UI_V_SILVER, UI_V_ACCENT };
static const short ACCENT_NAMES[ACCENT_COUNT] = { UI_V_AUTO, UI_V_TEAL, UI_V_MAGENTA, UI_V_LIME, UI_V_ORANGE, UI_V_SKY, UI_V_VIOLET, UI_V_PINK, UI_V_MINT };
static const short FIRE_NAMES[FIRE_COUNT] = { UI_V_RELAXED, UI_V_NORMAL, UI_V_FAST };

// The options screen's sample board (top screen): one gem of every palette, a diamond, the cell states
// (a numbered hole, a burning gem, a chained one, an empty cell, a peeked ghost), all drawn by the very helpers
// the real board uses so a change of theme, accent or frame shows exactly what play will look like.
static void drawMiniBoard(void)
{
    const int bx = 56, by = 70, bw = 6 * TILE_W, bh = 2 * TILE_H + ODD_SHIFT;
    fillRect(bx - 2, by - 2, bw + 4, bh + 4, BLACK);
    rectOutline(bx - 2, by - 2, bw + 4, bh + 4, GOLD_DK);
    rectOutline(bx - 1, by - 1, bw + 2, bh + 2, GOLD);
    u16 dot = lerp15(BLACK, ACCENT, 3);
    for (int y = by + 6; y < by + bh; y += 8)
        if (y >= clipY0 && y < clipY1) for (int x = bx + 6; x < bx + bw; x += 8) px(x, y, dot);
    for (int c = 0; c < 6; c++) {
        int x = bx + c * TILE_W, y0 = by + ((c & 1) ? ODD_SHIFT : 0), y1 = y0 + TILE_H;
        if (c < 5) { blitGem(x, y0, c); drawLetter(x, y0, "AIRTS"[c] - 'A', C(31,31,29), GEM_PALS[c]->dark); }
        else { blitGem(x, y0, 5); drawLetter(x, y0, 'B' - 'A', C(4,6,10), C(20,22,26)); }
        switch (c) {
            case 0: drawHoleCell(x, y1, 2, false, false); break;
            case 1: drawBurnCell(x, y1, 0, 'r' - 'a', 6, false, false); break;
            case 2: {   // a chained gem, lifted two pixels, white rims
                int pal = palOf['e' - 'a'];
                blitGem(x, y1 - 2, pal);
                rectOutline(x, y1 - 2, TILE_W, TILE_H, WHITE); rectOutline(x + 1, y1 - 1, TILE_W - 2, TILE_H - 2, lerp15(WHITE, GEM_PALS[pal]->light, 8));
                drawLetter(x, y1 - 2, 'e' - 'a', WHITE, GEM_PALS[pal]->dark);
                break;
            }
            case 3: drawGemOutline(x, y1); break;
            case 4: drawHoleCell(x, y1, 0, false, false); break;
            default: drawGhostCell(x, y1, 'k' - 'a', false); break;
        }
    }
}

// one line of help for the lit options row (two when CLEAR is picked), each short enough for the plaque
static void drawOptionsHelp(void)
{
    static const short OPT_HELP[OPT_ROWS] = {
        UI_H_THEME, UI_H_FRAME, UI_H_ACCENT, UI_H_HAND, UI_H_FIRE, UI_H_MUSIC, UI_H_LIBRARY, UI_H_SPELL, UI_H_LANG
    };
    char h[48];
    const char *t = ui(OPT_HELP[G.optRow]);
    if (G.optRow == 2 && S.accent) { uiFmt(h, sizeof h, UI_H_ACCENTNAME, ui(ACCENT_NAMES[S.accent])); t = h; }
    if (G.optRow == 7 && langCur() != LANG_EN) t = ui(UI_H_SPELL_EN);
    drawTextCenter(128, 132, t, INK, 1);
    if (G.optRow == 8) drawTextCenter(128, 141, ui(UI_H_LANGNOTE), ACCENT, 1);
    if (G.optRow == 0 && S.theme == THEME_CLEAR) drawTextCenter(128, 141, ui(UI_H_CLEAR), ACCENT, 1);
}

static void drawTopTitle(void)
{
    rectOutline(8, 8, 240, 176, GOLD_DK);
    rectOutline(9, 9, 238, 174, GOLD);
    drawLogo(16, 16, 2);
    if (G.state == ST_OPTIONS) {
        drawTextCenter(128, 44, ui(UI_O_MAKE), GOLD_LT, 1);
        drawMiniBoard();
        drawOptionsHelp();
        char s[48];
        if (S.bestClassic) { uiFmt(s, sizeof s, UI_O_BESTLINE, S.bestClassic, S.bestClassicShelf); drawTextCenter(128, 150, s, DIM_TEXT, 1); }
        if (S.bestDaily)   { uiFmt(s, sizeof s, UI_O_BESTDAILY, S.bestDaily, S.bestDailyDate); drawTextCenter(128, 160, s, DIM_TEXT, 1); }
        drawTextCenter(128, 172, ui(UI_O_TAPHINT), DIM_TEXT, 1);
        return;
    }
    if (G.state == ST_STATS) {
        drawTextCenter(128, 84, ui(UI_S_CAREER), GOLD_LT, 1);
        drawTextCenter(128, 100, ui(UI_S_WORDSPEEKS), INK, 1);
        drawTextCenter(128, 120, ui(UI_S_CLOSE), DIM_TEXT, 1);
        return;
    }
    drawTipCard(tipIndex);
    if (G.msg[0]) drawTextCenter(128, 158, G.msg, GOLD_LT, 1);   // e.g. SET THE DS CLOCK FOR DAILY
    else if (!S.tutorDone) drawTextCenter(128, 158, ui(UI_T_NEWHERE), ACCENT, 1);   // until the first guided shelf is done
}

// accent dot grid over the whole top screen, restricted to the slice being drawn
static void drawTopDots(void)
{
    u16 dot = lerp15(BLACK, ACCENT, 3);
    for (int y = 4; y < SCREEN_H; y += 8) {
        if (y < clipY0 || y >= clipY1) continue;
        u16 *row = cur + y * SCREEN_W;
        for (int x = 4; x < SCREEN_W; x += 8) row[x] = dot;
    }
}

// ---------------------------------------------------------------- motion helpers (the guide and the guided-shelf cues)
// Everything here is drawn from the game's own palette with its own primitives. Easing is fixed point, 0..16.
static int ease16(int num, int den)      // ease-out cubic: fast start, soft landing
{
    if (num <= 0) return 0;
    if (num >= den) return 16;
    int x = 16 - 16 * num / den;
    return 16 - x * x * x / 256;
}

static int tri16(int phase, int period)  // a triangle wave 0..16 over `period` frames: breathing, never faster than the period says
{
    int h = period / 2, p = phase % period;
    return 16 * (p < h ? p : period - p) / h;
}

// the board's own drop (boardAnimate): 10/16 px per frame of gravity, terminal 10 px, a squash on a hard landing
static void gDrop(int age, int height, int *dy, int *sq)
{
    int yoff = -height * 16, yv = 0, squash = 0;
    for (int f = 0; f < age && f < 60; f++) {
        if (yoff < 0) {
            yv += 10; if (yv > 160) yv = 160;
            yoff += yv;
            if (yoff >= 0) { yoff = 0; squash = yv > 60 ? SQUASH_MAX : (yv > 30 ? 3 : 0); yv = 0; }
        } else if (squash) squash--;
    }
    *dy = yoff >> 4; *sq = squash;
}

static void gRing(int cx, int cy, int r, u16 col)
{
    static const s8 dx[16] = { 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3, 0, 3, 6, 7 };
    static const s8 dy[16] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };
    for (int i = 0; i < 16; i++) px(cx + dx[i] * r / 8, cy + dy[i] * r / 8, col);
}

static void gRipple(int cx, int cy, int age)   // the press ripple: a ring that opens and fades over five ticks
{
    if (age < 0 || age > 4) return;
    u16 col = lerp15(BLACK, WHITE, 16 - age * 3);
    gRing(cx, cy, 3 + age * 3, col);
    if (age) gRing(cx, cy, 2 + age * 3, col);
}

static void gPointer(int x, int y, bool down)   // a stylus: the tip at (x, y), the body running down and to the right
{
    for (int i = 2; i < 13; i++) {
        u16 c = i < 5 ? ACCENT : (i < 10 ? WHITE : INK);
        px(x + i, y + i, c); px(x + i + 1, y + i, lerp15(BLACK, c, 9)); px(x + i, y + i + 1, lerp15(BLACK, c, 6));
    }
    px(x, y, WHITE); px(x + 1, y + 1, WHITE); px(x + 1, y, ACCENT); px(x, y + 1, ACCENT);
    if (down) { px(x - 1, y, GOLD_LT); px(x, y - 1, GOLD_LT); px(x - 1, y - 1, GOLD_LT); }
}

static void gSparkle(int x, int y, int salt)    // the board's travelling diamond sparkle (it needs no more than the frame counter)
{
    int sx = x + 3 + (((frame + salt) & 31) * 17) / 32;
    if (sx < x + TILE_W - 3) { px(sx, y + 2, WHITE); px(sx - 1, y + 3, WHITE); }
}

static void drawTextBox(int x, int y, const char *s, u16 col, int scale, int x0, int x1)   // only the whole glyphs inside [x0, x1)
{
    for (; *s; s++, x += 6 * scale) if (x >= x0 && x + 5 * scale <= x1) drawChar(x, y, *s, col, scale);
}

// ---------------------------------------------------------------- the guide (HOW TO PLAY)
// Touch screen: the page's words, typed in line by line (guide.c builds them from the rules). Top screen: a
// looping story per page drawn with the board's own helpers (blitGem, drawLetter, the hole, burning and ghost
// cells, the plaque diamonds, the heat bar) in the usual 48-row slices. The loop advances every fourth frame: the
// four slices of one refresh cycle share a value of frame >> 2, so they always show the same moment, and while a
// diagram is drawn `frame` itself is replaced by the loop's tick, so every pulse in the game's own helpers loops
// with it. GUIDE_LOOP ticks is a power of two dividing the 32 frame pulses: the loop closes seamlessly.
#define GUIDE_LOOP 64                      // ticks of four frames: just over four seconds
static int guideBase;                      // frame >> 2 when the page opened
static int guidePageSeen = -1;             // the page the top screen was last laid out for
static int guideAge;                       // frames since the page opened: the words type in over the first ~50
static int guideNavUsed;                   // the prev/next buttons pulse until one has been used
static char guideCache[GUIDE_MAX_LINES][GUIDE_LINE_BUF];
static int guideCachePage = -1;

static int gTick(void) { return ((frame >> 2) - guideBase) & (GUIDE_LOOP - 1); }
void gfxGuideNavReset(void) { guideNavUsed = 0; }
void gfxGuideSkip(void) { guideAge = 999; }          // a key or a tap finishes the typing at once
int gfxGuideAge(void) { return guideAge; }

// ---- the touch screen
#define GD_TEXT_X   14                     // 38 characters run from x 14 to 241
#define GD_TEXT_Y   29
#define GD_BACK_W   40
#define GD_NAV_Y    168                    // the prev/next buttons (24 x 20) and the page dots
#define GD_DOT_C0   79                     // centre of the first of eight dots, 14 apart (they centre on x 128)
static int guideBackX(void) { return S.hand == HAND_LEFT ? 6 : SCREEN_W - 6 - GD_BACK_W; }   // BACK sits on the stylus side

int gfxGuideHit(int x, int y)
{
    int bx = guideBackX();
    if (y < 27 && x >= bx - 6 && x < bx + GD_BACK_W + 6) return GH_BACK;
    if (y >= GD_NAV_Y - 4) {
        if (x < 40) return GH_PREV;
        if (x >= SCREEN_W - 40) return GH_NEXT;
        int i = (x - (GD_DOT_C0 - 7)) / 14;
        if (x >= GD_DOT_C0 - 7 && i < GUIDE_PAGES) return GH_DOT + i;
    }
    return GH_NONE;
}

// one line with colour codes (guide.c GM_*): accent for key terms, gold numbers, fire warnings. `a` is how far the
// line has faded in (0..16): the colours come up from black.
static void drawMarked(int x, int y, const char *s, int a)
{
    u16 col = INK;
    for (; *s; s++) {
        switch (*s) {
            case 1: col = ACCENT; continue;
            case 2: col = GOLD_LT; continue;
            case 3: col = FIRE2; continue;
            case 4: col = INK; continue;
            case 5: col = DIM_TEXT; continue;
            case 6: col = WHITE; continue;
        }
        drawChar(x, y, *s, a >= 16 ? col : lerp15(BLACK, col, a), 1);
        x += 6;
    }
}

static void drawGuide(const Game *g)
{
    int page = g->guidePage;
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawTopDots();
    rectOutline(0, 0, SCREEN_W, SCREEN_H, GOLD_DK);
    rectOutline(1, 1, SCREEN_W - 2, SCREEN_H - 2, GOLD);
    if (guideCachePage != page) { guideLines(page, guideCache); guideCachePage = page; }

    const char *title = guideTitle(page);
    drawTextShadow(SCREEN_W / 2 - textWidth(title, 2) / 2, 8, title, GOLD_LT, 2);
    int bx = guideBackX();
    rectOutline(bx, 6, GD_BACK_W, 15, DIM);
    drawTextCenter(bx + GD_BACK_W / 2, 10, ui(UI_OP_BACK), INK, 1);
    char cnt[8]; fmtInt(cnt, page + 1); strcat(cnt, "/"); fmtInt(cnt + strlen(cnt), GUIDE_PAGES);
    drawText(S.hand == HAND_LEFT ? SCREEN_W - 8 - textWidth(cnt, 1) : 8, 10, cnt, DIM_TEXT, 1);   // the page counter takes the other corner
    hline(GD_TEXT_X, 25, SCREEN_W - 2 * GD_TEXT_X, GOLD_DK);

    int y = GD_TEXT_Y;
    for (int i = 0; i < GUIDE_MAX_LINES; i++) {
        if (!guideCache[i][0]) { y += GUIDE_GAP_H; continue; }
        int a = (guideAge - 3 * i - 2) * 16 / 6;               // each line fades up over six frames, three after the one above
        if (a > 16) a = 16;
        if (a > 0) drawMarked(GD_TEXT_X + (16 - a) * 6 / 16, y, guideCache[i], a);   // and settles in from a few pixels to the right
        y += GUIDE_LINE_H;
    }

    u16 nav = guideNavUsed ? ACCENT : lerp15(ACCENT, WHITE, tri16(frame, 48));        // a gentle pulse until used once
    rectOutline(6, GD_NAV_Y, 24, 20, nav);
    drawChar(13, GD_NAV_Y + 3, '<', INK, 2);
    rectOutline(SCREEN_W - 30, GD_NAV_Y, 24, 20, nav);
    drawChar(SCREEN_W - 23, GD_NAV_Y + 3, '>', INK, 2);
    int breathe = tri16(frame, 48) / 8;                                                // 0..2: the active dot breathes
    for (int i = 0; i < GUIDE_PAGES; i++) {
        int cx = GD_DOT_C0 + 14 * i, cy = GD_NAV_Y + 10;
        if (i == page) {
            fillRect(cx - 3, cy - 3, 7, 7, GOLD_LT);
            rectOutline(cx - 5 - breathe / 2, cy - 5 - breathe / 2, 11 + breathe, 11 + breathe, ACCENT);
            if (guideAge < 10) { int e = guideAge / 2; rectOutline(cx - 5 - e, cy - 5 - e, 11 + 2 * e, 11 + 2 * e, lerp15(BLACK, WHITE, 16 - e * 3)); }   // a ring opens on the page just reached
        } else rectOutline(cx - 2, cy - 2, 5, 5, DIM_TEXT);
    }
}

// ---- the top screen: shared pieces
static void gGem(int x, int y, int letter, bool lit)        // a letter gem as the board draws it; lit = chained (lifted, white rims)
{
    int pal = palOf[letter];
    const GemPal *p = GEM_PALS[pal];
    if (lit) {
        blitGem(x, y - 2, pal);
        rectOutline(x, y - 2, TILE_W, TILE_H, WHITE);
        rectOutline(x + 1, y - 1, TILE_W - 2, TILE_H - 2, lerp15(WHITE, p->light, 8));
        drawLetter(x, y - 2, letter, WHITE, p->dark);
    } else {
        blitGem(x, y, pal);
        drawLetter(x, y, letter, C(31,31,29), p->dark);
    }
}

// a gem dropping into its slot with the board's own fall and landing squash; `age` is frames since it was let go
// (negative: not yet), `top` the first row it may be seen on, so it never shows above the frame it falls in
static void gGemDrop(int x, int y, int letter, int age, int top)
{
    if (age < 0) return;
    int dy, sq;
    gDrop(age, 56, &dy, &sq);
    if (!dy && !sq) { gGem(x, y, letter, false); return; }
    int c0 = clipY0;
    if (clipY0 < top) clipY0 = top;
    if (clipY0 < clipY1) {
        int pal = palOf[letter];
        const GemPal *p = GEM_PALS[pal];
        if (sq) drawGemSized(x, y + sq, TILE_W, TILE_H - sq, p, false);
        else blitGem(x, y + dy, pal);
        drawLetter(x, y + dy + sq / 2, letter, C(31,31,29), p->dark);
    }
    clipY0 = c0;
}

static void gDiamond(int x, int y, int letter)
{
    blitGem(x, y, 5);
    drawLetter(x, y, letter, C(4,6,10), C(20,22,26));
    gSparkle(x, y, x * 3 + y);
}

// the chain rope from gem origin to gem origin, drawn `steps` of its 10 links; a spark rides the head while it grows
static void gRope(int x0, int y0, int x1, int y1, bool valid, int steps)
{
    u16 dk = valid ? lerp15(ACCENT, BLACK, 8) : GOLD_DK, lt = valid ? ACCENT : GOLD_LT;
    x0 += TILE_W / 2; y0 += TILE_H / 2 - 2; x1 += TILE_W / 2; y1 += TILE_H / 2 - 2;
    for (int t = 0; t <= steps && t <= 10; t++) {
        int xx = x0 + (x1 - x0) * t / 10, yy = y0 + (y1 - y0) * t / 10;
        fillRect(xx - 2, yy - 2, 4, 4, dk);
        fillRect(xx - 1, yy - 1, 2, 2, ((t + (frame >> 2)) & 3) == 0 ? WHITE : lt);
    }
    if (steps < 10) { int xx = x0 + (x1 - x0) * steps / 10, yy = y0 + (y1 - y0) * steps / 10; fillRect(xx - 2, yy - 2, 4, 4, WHITE); }   // the spark
}

static void gShards(int cx, int cy, int age, int letter)          // a gem breaking up, age 0..11 ticks
{
    static const s8 vx[8] = { -6, -4, -2, 0, 2, 4, 6, 3 }, vy[8] = { -5, -8, -10, -6, -9, -7, -4, -11 };
    if (age > 10) return;
    const GemPal *p = GEM_PALS[palOf[letter]];
    for (int k = 0; k < 8; k++)
        fillRect(cx + vx[k] * age / 2, cy + vy[k] * age / 2 + age * age / 2, 2, 2, (k & 1) ? p->light : p->mid);
}

static void gPatchFrame(int ox, int oy, int cols, int rows)       // the framed patch of board the diagrams draw gems in (the screen is already black: every fill that is not needed costs a slice a lot of VRAM time)
{
    int w = cols * TILE_W, h = rows * TILE_H + ODD_SHIFT;
    rectOutline(ox - 4, oy - 4, w + 8, h + 8, GOLD_DK);
    rectOutline(ox - 3, oy - 3, w + 6, h + 6, GOLD);
    u16 dot = lerp15(BLACK, ACCENT, 3);
    for (int y = oy + 6; y < oy + h; y += 8)
        if (y >= clipY0 && y < clipY1) for (int x = ox + 6; x < ox + w; x += 8) px(x, y, dot);
}

static void gCaption(const char *txt, u16 col) { drawTextCenter(SCREEN_W / 2, 166, txt, col, 1); }
#define HX(ox, c)     ((ox) + (c) * TILE_W)
#define HY(oy, c, r)  ((oy) + (r) * TILE_H + (((c) & 1) ? ODD_SHIFT : 0))

// ---- 1 THE BOARD: gems drop in, a stylus drags I, N, K (the rope grows with a spark), the word glows, a tap cashes it in
static void gdBoard(int t)
{
    char PATCH[5][2] = { { 'I', 'S' }, { 'N', 'R' }, { 'E', 'K' }, { 'A', 'T' }, { 'D', 'E' } };               // [column][row]
    static const u8 pc[3] = { 0, 1, 2 }, pr[3] = { 0, 0, 1 };                                                    // the path: the example word
    u8 gem[4]; int wl, ng = guideGems(UI_EX_WORD, gem, &wl);                                                     // its three gems, in this language
    for (int k = 0; k < 3 && k < ng; k++) PATCH[pc[k]][pr[k]] = (char)('A' + gem[k]);
    static const u8 tl[3] = { 12, 18, 24 };                                                                      // the tick each gem joins the chain
    const int ox = 68, oy = 36;      // two rows keep the patch above y 96, so its gems sit in two slices only
    int n = t >= tl[2] ? 3 : t >= tl[1] ? 2 : t >= tl[0] ? 1 : 0;
    bool gone = t >= 40 && t < 52, valid = t >= 26 && t < 40;
    gPatchFrame(ox, oy, 5, 2);
    for (int c = 0; c < 5; c++)
        for (int r = 0; r < 2; r++) {
            int k = -1;
            for (int j = 0; j < 3; j++) if (pc[j] == c && pr[j] == r) k = j;
            int x = HX(ox, c), y = HY(oy, c, r), letter = PATCH[c][r] - 'A';
            if (k >= 0 && gone) continue;                                                 // cashed in: the gem is gone
            if (k >= 0 && t >= tl[k] && t < 40) { gGem(x, y, letter, true); continue; }
            // the first drop at the loop's start, a refill after the cash-in
            int age = (k >= 0 && t >= 52) ? (t - 52) * 4 - k * 3 : (t < 14 ? (t - c) * 4 - r * 3 : 99);
            gGemDrop(x, y, letter, age, oy - 3);
        }
    for (int k = 0; k < 2; k++) {
        if (t < tl[k] || t >= 40) continue;
        int steps = t >= tl[k + 1] ? 10 : 10 * ease16(t - tl[k], tl[k + 1] - tl[k]) / 16;
        gRope(HX(ox, pc[k]), HY(oy, pc[k], pr[k]), HX(ox, pc[k + 1]), HY(oy, pc[k + 1], pr[k + 1]), valid, steps);
    }
    if (valid) {         // a good word: the gold ring on the tail gem, breathing
        int x = HX(ox, pc[2]), y = HY(oy, pc[2], pr[2]) - 2, e = tri16(t * 4, 32) / 12;
        rectOutline(x - e, y - e, TILE_W + 2 * e, TILE_H + 2 * e, GOLD_LT); rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, GOLD_LT);
    }
    if (gone) for (int k = 0; k < 3; k++) gShards(HX(ox, pc[k]) + 12, HY(oy, pc[k], pr[k]) + 11, t - 40, PATCH[pc[k]][pr[k]] - 'A');
    // the stylus: it slides in, presses I and drags along, lifts off the tail and taps it, then leaves
    int cxk[3], cyk[3];
    for (int k = 0; k < 3; k++) { cxk[k] = HX(ox, pc[k]) + 10; cyk[k] = HY(oy, pc[k], pr[k]) + 10; }
    int sx = cxk[0], sy = cyk[0]; bool down = false;
    if (t < tl[0]) { int e = ease16(t - 4, tl[0] - 4); sx = cxk[0] - 30 + 30 * e / 16; sy = cyk[0] + 40 - 40 * e / 16; }
    else if (t < tl[2]) { int k = t < tl[1] ? 0 : 1, e = ease16(t - tl[k], tl[k + 1] - tl[k]); sx = cxk[k] + (cxk[k + 1] - cxk[k]) * e / 16; sy = cyk[k] + (cyk[k + 1] - cyk[k]) * e / 16; down = true; }
    else if (t < 40) { sx = cxk[2]; sy = cyk[2] - (t >= 34 && t < 37 ? 5 : 0); down = t >= 37; }
    else { int e = ease16(t - 40, 10); sx = cxk[2] + 36 * e / 16; sy = cyk[2] + 30 * e / 16; }
    if (t >= 4 && t < 50) gPointer(sx, sy, down);
    gRipple(cxk[0], cyk[0], t - tl[0]);
    gRipple(cxk[2], cyk[2], t - 37);
    // the word on the line below, and once it is cashed in what it paid
    {
        int ink = 0;
        for (int k = 0; k < 3 && k < ng; k++) ink += letterValue(gem[k]);
        char sc[12]; sc[0] = '+'; fmtInt(sc + 1, boardWordPoints(ink, wl, false, 0, false) * ease16(t - 40, 6) / 16);
        int ww = n * 12 - 2, sw = textWidth(sc, 2), total = t >= 40 ? 3 * 12 - 2 + 14 + sw : ww;
        int wx = SCREEN_W / 2 - total / 2;
        for (int i = 0; i < n; i++) drawChar2Shadow(wx + i * 12, 106, PATCH[pc[i]][pr[i]], valid || t >= 40 ? GOLD_LT : WHITE, DIM2);
        if (t >= 40) drawTextShadow(wx + 3 * 12 - 2 + 14, 106, sc, GOLD_LT, 2);
    }
    gCaption(ui(t < 26 ? UI_C_DRAG : t < 40 ? UI_G_RING : UI_G_CASHED), t < 40 ? INK : ACCENT);
}

// ---- 2 SCORING: Qu, I, Z drop in and each floats up its value, the sum meets the length multiplier, the total counts up
static void gdScoring(int t)
{
    static const u8 cc[3] = { 0, 1, 2 }, rr[3] = { 0, 0, 1 }, tl[3] = { 12, 18, 24 };
    const int ox = 84, oy = 44;
    u8 L[4]; int len, sum = 0, vals[3] = { 0, 0, 0 }, ng = guideGems(UI_EX_SCORE, L, &len);   // the example word's gems; Qu is one gem, two letters
    for (int i = 0; i < 3 && i < ng; i++) { vals[i] = letterValue(L[i]); sum += vals[i]; }
    int mult = boardLenMult(len), total = boardWordPoints(sum, len, false, 0, false);
    int n = t >= tl[2] ? 3 : t >= tl[1] ? 2 : t >= tl[0] ? 1 : 0;
    for (int i = 1; i < n; i++) gRope(HX(ox, cc[i - 1]), HY(oy, cc[i - 1], rr[i - 1]), HX(ox, cc[i]), HY(oy, cc[i], rr[i]), false, 10);
    for (int i = 0; i < 3; i++) {
        int x = HX(ox, cc[i]), y = HY(oy, cc[i], rr[i]);
        if (t >= tl[i]) gGem(x, y, L[i], true); else gGemDrop(x, y, L[i], t < 12 ? (t - i) * 4 : 99, 30);
        if (t >= tl[i]) {   // the value rises off its gem on an ease and settles
            char v[4]; fmtInt(v, vals[i]);
            int e = ease16(t - tl[i], 6), rise = 8 * e / 16;
            drawTextShadow(x + 12 - textWidth(v, 1) / 2, y - 9 - rise, v, lerp15(BLACK, GOLD_LT, e), 1);
        }
    }
    char a[24], b[12];
    a[0] = 0;
    for (int i = 0; i < n; i++) { if (i) strcat(a, " + "); fmtInt(a + strlen(a), vals[i]); }
    b[0] = 0;
    if (t >= 28) { strcpy(b, " = "); fmtInt(b + 3, sum); }
    int w = textWidth(a, 1) + (b[0] ? textWidth(b, 1) + 6 : 0);
    if (n) { drawText(SCREEN_W / 2 - w / 2, 100, a, WHITE, 1); if (b[0]) drawText(SCREEN_W / 2 - w / 2 + textWidth(a, 1) + 6, 100, b, lerp15(BLACK, GOLD_LT, ease16(t - 28, 3)), 1); }
    if (t >= 32) {       // the length badge opens from its middle
        char m[32]; uiFmt(m, sizeof m, UI_G_BADGE, len, mult);
        int e = ease16(t - 32, 4), bw = 104 * e / 16;
        if (bw > 8) rectOutline(SCREEN_W / 2 - bw / 2, 112, bw, 14, ACCENT);
        if (e >= 14) drawTextCenter(SCREEN_W / 2, 116, m, ACCENT, 1);
    }
    if (t >= 38) {       // the total counts up, fast then slow
        char s[12]; s[0] = '+'; fmtInt(s + 1, total * ease16(t - 38, 14) / 16);
        drawTextShadow(SCREEN_W / 2 - textWidth(s, 2) / 2, 134, s, GOLD_LT, 2);
    }
    char cap[48];
    if (t < 28) strcpy(cap, ui(UI_G_VALUE));
    else if (t < 38) strcpy(cap, ui(UI_G_ADD));
    else uiFmt(cap, sizeof cap, UI_G_WORTH, SCORE_UNIT, total);
    gCaption(cap, t < 38 ? INK : ACCENT);
}

// ---- 3 PEEKING: gems drop in, L is held and they go to outlines, a stylus reads the numbers and stops at the gem that would scorch.
// The numbers are worked out from two hidden pass letters with the game's own neighbour function, so every digit on
// the diagram is the digit the game would show
static void gdPeek(int t)
{
    enum { K_GHOST, K_HOLE, K_DIAMOND, K_BURN, K_RISK };
    static const u8 kind[5][3] = { { K_BURN, K_RISK, K_GHOST }, { K_GHOST, K_HOLE, K_HOLE }, { K_HOLE, K_DIAMOND, K_HOLE },
                                   { K_HOLE, K_GHOST, K_GHOST }, { K_GHOST, K_HOLE, K_GHOST } };
    static const char LT[5][3] = { { 'R', 'S', 'A' }, { 'E', 'N', 'T' }, { 'O', 'B', 'L' }, { 'M', 'T', 'O' }, { 'D', 'I', 'E' } };
    static const u8 hidC[2] = { 2, 3 }, hidR[2] = { 1, 1 };         // the two pass letters: the diamond (found) and one still under a gem
    static const u8 rc[3] = { 1, 2, 0 }, rrw[3] = { 1, 2, 1 };      // where the stylus reads: a 1, a 2, the gem that would scorch
    const int ox = 68, oy = 46;
    bool peek = t >= 14;
    gPatchFrame(ox, oy, 5, 3);
    for (int c = 0; c < 5; c++)
        for (int r = 0; r < 3; r++) {
            int x = HX(ox, c), y = HY(oy, c, r), letter = LT[c][r] - 'A', k = kind[c][r];
            if (k == K_GHOST || k == K_RISK) {
                if (!peek) gGemDrop(x, y, letter, t < 12 ? (t - c) * 4 - r * 3 : 99, oy - 3);
                else drawGhostCell(x, y, letter, k == K_RISK);
            } else if (k == K_BURN) drawBurnCell(x, y, 0, letter, (t + c * 7) & 15, false, peek);
            else if (k == K_DIAMOND) gDiamond(x, y, letter);
            else {
                int nc[6], nr[6], m = boardNeighbors(c, r, nc, nr), d = 0;
                for (int i = 0; i < m; i++) for (int h = 0; h < 2; h++) if (nc[i] == hidC[h] && nr[i] == hidR[h]) d++;
                drawHoleCell(x, y, d, false, false);
            }
        }
    // the L key, held from tick 14 (it sinks and fills)
    rectOutline(20, 42 + (peek ? 1 : 0), 32, 12, peek ? lerp15(ACCENT, WHITE, tri16(t * 4, 32) / 2) : DIM_TEXT);
    if (peek) fillRect(21, 44, 30, 9, lerp15(BLACK, ACCENT, 6));
    drawTextCenter(36, 45 + (peek ? 1 : 0), "L", peek ? WHITE : INK, 1);
    int seg = peek ? (t - 14) / 16 : 0;
    if (seg > 2) seg = 2;
    int x = HX(ox, rc[seg]), y = HY(oy, rc[seg], rrw[seg]);
    if (peek) {
        u16 ac = seg == 2 ? FIRE2 : lerp15(ACCENT, WHITE, tri16(t * 4, 32));
        rectOutline(x - 2, y - 2, TILE_W + 4, TILE_H + 4, ac);
    }
    int tp = (t - 14) - 16 * seg;                       // ticks into this reading
    int px0 = peek && seg > 0 ? HX(ox, rc[seg - 1]) : 20, py0 = peek && seg > 0 ? HY(oy, rc[seg - 1], rrw[seg - 1]) : 60;
    int e = peek ? ease16(tp, 5) : 0;
    int sx = (peek ? px0 + (x - px0) * e / 16 : 36) + 4, sy = (peek ? py0 + (y - py0) * e / 16 : 60) + 8;
    if (t >= 4) gPointer(sx, sy, peek && tp >= 5 && tp < 8);
    if (peek) gRipple(sx, sy, tp - 5);
    if (!peek) gCaption(ui(UI_G_HOLDLR), INK);
    else if (seg < 2) {
        int nc[6], nr[6], m = boardNeighbors(rc[seg], rrw[seg], nc, nr), d = 0;
        for (int i = 0; i < m; i++) for (int h = 0; h < 2; h++) if (nc[i] == hidC[h] && nr[i] == hidR[h]) d++;
        char cap[48]; uiFmt(cap, sizeof cap, d == 1 ? UI_G_HERE1 : UI_G_HERE2, d);
        gCaption(cap, INK);
    } else gCaption(ui(UI_G_REDRIM), FIRE2);
}

// ---- 4 THE PASS: the slots fill with diamonds one by one; the plaque cycles three themes, and the slot count,
// the letters and the prizes are the game's (boardPassLenFor, poolWord, boardPassLetterReward, boardShelfBonus)
static void gdPass(int t)
{
    static const u8 SHELVES[3] = { 1, 9, 18 };
    int ph = t / 21; if (ph > 2) ph = 2;
    int tp = t - ph * 21, shelf = SHELVES[ph];
    int theme = (shelf - 1) % (poolThemeCount() > 0 ? poolThemeCount() : 1), len = boardPassLenFor(shelf);
    char word[12];
    int wl = len;
    poolWord(theme, len, 0, word);                                                  // the first (most familiar) word of the language's own theme
    for (int d = 1; !word[0] && d < 6; d++) {                                       // a theme short at this length: the nearest length that has one
        if (len + d <= POOL_MAX_LEN) { poolWord(theme, len + d, 0, word); wl = len + d; }
        if (!word[0] && len - d >= POOL_MIN_LEN) { poolWord(theme, len - d, 0, word); wl = len - d; }
    }
    if (!word[0]) { memset(word, 'a', 9); word[len] = 0; wl = len; }
    len = wl;
    int found = tp < 3 ? 0 : (tp - 3) / 2 + 1; if (found > len) found = len;
    rectOutline(24, 34, 208, 70, GOLD_DK); rectOutline(25, 35, 206, 68, GOLD);
    char s[48]; uiFmt(s, sizeof s, UI_H_PASSPLAQUE, poolName(theme));
    int le = ease16(tp, 4), lx = SCREEN_W / 2 - textWidth(s, 1) / 2 + (16 - le);      // the plaque's name slides in from the right
    drawTextBox(lx, 42, s, lerp15(BLACK, DIM_TEXT, le), 1, 28, 228);
    int gap = len >= 9 ? 1 : 4, bw = PLAQUE_W, x0 = ((SCREEN_W - (len * bw + (len - 1) * gap)) / 2) & ~1, y0 = 58;
    static const s8 POP[5] = { 4, 5, 2, 1, 0 };                                       // the grow of a diamond as it lands: over, then settle
    for (int i = 0; i < len; i++) {
        int x = x0 + i * (bw + gap);
        if (i < found) {
            int age = tp - (3 + 2 * i), g = age >= 0 && age < 5 ? POP[age] : 0;
            if (g) drawGemSized(x - g, y0 - g, bw + 2 * g, PLAQUE_H + 2 * g, &GEM_DIAMOND, true); else blitPlaque(x, y0);
            drawChar(x + 6, y0 + 6, (char)(word[i] - 32), C(4,6,10), 2);
            gSparkle(x, y0 + 2, i * 7);
        } else {
            rectOutline(x, y0, bw, 26, GOLD_DK);
            fillRect(x + 4, y0 + 22, bw - 8, 1, GOLD);
            if (i == found && tp >= 1) fillRect(x + 4 + ((tp * 4) % (bw - 10)), y0 + 22, 2, 1, GOLD_LT);   // a glint travels along the next blank
        }
    }
    uiFmt(s, sizeof s, UI_G_SHELFN, shelf, len);
    drawTextCenter(SCREEN_W / 2, 112, s, lerp15(BLACK, ACCENT, le), 1);
    uiFmt(s, sizeof s, UI_G_PAYS, boardPassLetterReward(shelf));
    if (found > 0) drawTextCenter(SCREEN_W / 2, 124, s, INK, 1);
    int up = ease16(tp - (3 + 2 * (len - 1)) - 1, 4);
    if (found >= len && up > 0) {
        uiFmt(s, sizeof s, UI_G_CLEARED, boardShelfBonus(shelf) * up / 16);
        drawTextCenter(SCREEN_W / 2, 140, s, (tp & 4) ? GOLD_LT : WHITE, 1);
    }
    gCaption(ui(UI_G_FIND), INK);
}

// ---- 5 FIRE: short words fill the heat meter, the top gem of a column ignites, the flame slides a tile at a time (the board's
// own flame cell, new gems dropping in behind it) until a diamond stops it. Heat per word is boardHeatFor, the meter boardShelfRules(1)
static void gdFire(int t)
{
    const char *const WORDS[4] = { ui(UI_EX_F1), ui(UI_EX_F2), ui(UI_EX_F3), ui(UI_EX_F4) };      // the example words, 3, 3, 4 and 3 letters
    static const u8 COL[4] = { 's' - 'a', 'e' - 'a', 'a' - 'a', 't' - 'a' };
    static const u8 TW[4] = { 6, 14, 22, 30 }, SLIDE[3] = { 44, 52, 58 };
    ShelfRules sr; boardShelfRules(1, &sr);
    const int cx = 40, cy = 36;
    int heat16 = 0, shown = 0;                                      // heat in sixteenths
    for (int i = 0; i < 4; i++)
        if (t >= TW[i]) { heat16 += boardHeatFor((int)strlen(WORDS[i])) * ease16(t - TW[i], 4); shown = i + 1; }
    bool lit = t >= 36;
    if (lit) heat16 = heat16 * (16 - ease16(t - 36, 4)) / 16;       // the meter empties as the gem catches
    int pos = 0;                                                    // the flame's place, in sixteenths of a row
    for (int k = 0; k < 3; k++) if (t >= SLIDE[k]) pos = 16 * k + ease16(t - SLIDE[k], 4);
    for (int r = 0; r < 4; r++) {
        int y = cy + r * TILE_H;
        bool left = r < 3 && t >= SLIDE[r];                         // the flame has left this row: a new gem drops into it
        if (left) gGemDrop(cx, y, COL[(r + 1) & 3], (t - SLIDE[r]) * 4, cy);
        else if (!lit || pos < 16 * r) gGem(cx, y, COL[r], false);  // not reached yet
    }
    if (lit) drawBurnCell(cx, cy + pos * TILE_H / 16, 0, COL[(pos / 16) & 3], t & 15, false, false);
    gDiamond(cx, cy + 4 * TILE_H, 'k' - 'a');
    drawText(cx + TILE_W + 6, cy + 4 * TILE_H + 7, ui(UI_G_NEVER), ACCENT, 1);
    drawText(112, 38, ui(UI_P_HEAT), DIM_TEXT, 1);
    int hv = heat16 > sr.threshold * 16 ? sr.threshold * 16 : heat16;
    char s[16]; fmtInt(s, hv / 16); strcat(s, "/"); fmtInt(s + strlen(s), sr.threshold);
    drawText(236 - textWidth(s, 1), 38, s, lit ? DIM_TEXT : FIRE2, 1);
    int hw = HEAT_W * hv / (sr.threshold * 16);
    rectOutline(112, 48, 72, 6, DIM);
    for (int i = 0; i < hw; i++) { fillRect(113 + i, 49, 1, 1, heatTop[i]); fillRect(113 + i, 50, 1, 3, heatBody[i]); }
    if (hw > 1 && hw < HEAT_W && ((t / 5) & 1)) fillRect(113 + hw - 1, 49, 1, 4, GOLD_LT);          // the tip of the bar flickers (about 3 Hz at most)
    for (int i = 0; i < shown; i++) {                           // the words that fed it slide in from the right
        int len = (int)strlen(WORDS[i]), h = boardHeatFor(len), e = ease16(t - TW[i], 4), dx = (16 - e) * 2;
        char hs[8]; hs[0] = '+'; fmtInt(hs + 1, h);
        drawTextBox(112 + dx, 64 + 11 * i, WORDS[i], lerp15(BLACK, WHITE, e), 1, 112, 236);
        drawTextBox(112 + 30 + dx, 64 + 11 * i, hs, lerp15(BLACK, h ? FIRE2 : ACCENT, e), 1, 112, 236);
    }
    if (lit && ((t / 5) & 1)) drawText(112, 114, ui(UI_G_FLAME), FIRE1, 1);
    gCaption(ui(t < 36 ? UI_G_FIRE1 : t < 44 ? UI_G_FIRE2 : UI_G_FIRE3), t < 36 ? INK : FIRE2);
}

// ---- 6 SHELVES: bookmarks (one is lost, one won back), a twist banner sliding in, and the Lockdown tiers with their numbers
static int tierShelf(int tier)                  // the first Classic shelf of a Lockdown tier
{
    for (int s = 1; s <= 60; s++) { ShelfRules r; boardShelfRules(s, &r); if (r.lockdown && r.tier >= tier) return s; }
    return 0;
}

static void gdShelves(int t)
{
    for (int i = 0; i < BOOKMARKS_MAX; i++) {                  // the HUD's bookmarks at 2x
        int bx = 28 + i * 18, by = 34;
        bool last = i == BOOKMARKS_MAX - 1;
        int h = 16, sh = 0;
        if (last && t >= 16 && t < 44) { h = t < 20 ? 16 : 0; if (t < 20) sh = (t & 1) ? 1 : -1; }   // lost on tick 16: it shakes, then drops out of the row
        if (last && t >= 44) h = 16 * ease16(t - 44, 4) / 16;                                         // won back on tick 44: it grows up from the shelf
        if (h > 0) {
            fillRect(bx + sh, by + 16 - h, 10, h, GOLD); fillRect(bx + 2 + sh, by + 16 - h, 2, h, GOLD_DK);
            if (h > 4) { px(bx + 6 + sh, by + 18 - h, GOLD_LT); px(bx + 7 + sh, by + 18 - h, GOLD_LT); }
        }
        if (h < 16) rectOutline(bx, by, 10, 16, DIM);
    }
    drawText(28, 54, ui(UI_G_BOOKMARKS), ACCENT, 1);
    if (t >= 16 && t < 44) drawText(28, 64, ui(UI_G_LOSTSHELF), lerp15(BLACK, FIRE2, ease16(t - 16, 4)), 1);
    else if (t >= 44) drawText(28, 64, ui(UI_G_LDCLEARED), lerp15(BLACK, GOLD_LT, ease16(t - 44, 4)), 1);
    int i = t / 21; if (i > 2) i = 2;                           // the twist banner as it arrives
    int shelf = 4;
    for (int k = 0, s2 = 1; s2 <= 60 && k <= i; s2++) if (boardTwistShelf(s2)) { shelf = s2; k++; }
    int tp = t - 21 * i, e = ease16(tp, 4), dx = (16 - e) * 5;
    char s[32]; uiFmt(s, sizeof s, UI_G_SHELFC, shelf);
    rectOutline(140, 34, 96, 26, GOLD);
    drawTextBox(188 - textWidth(s, 1) / 2 + dx, 40, s, GOLD_LT, 1, 142, 234);
    drawTextBox(188 - textWidth(boardTwistName(TWIST_INK + i), 1) / 2 + dx, 50, boardTwistName(TWIST_INK + i), ACCENT, 1, 142, 234);
    drawText(28, 84, ui(UI_LD1), DIM_TEXT, 1);
    static const short H[4] = { UI_G_H_SHELF, UI_G_H_METER, UI_G_H_PULSE, UI_G_H_FIRES };
    static const int HXS[4] = { 124, 157, 190, 223 };
    for (int c = 0; c < 4; c++) drawTextCenter(HXS[c], 84, ui(H[c]), DIM_TEXT, 1);
    int hi = t / 16, from = (hi + 3) & 3, hp = ease16(t - 16 * hi, 4);       // the highlight slides from tier to tier
    int hy = (98 + 13 * from) + (13 * (hi - from)) * hp / 16;
    if (hi == 0) hy = 98 + 13 * 3 + (13 * (0 - 3)) * hp / 16;
    fillRect(22, hy - 3, 216, 13, lerp15(BLACK, FIRE1, 3)); rectOutline(22, hy - 3, 216, 13, FIRE1);
    for (int tier = 0; tier < 4; tier++) {
        int y = 98 + 13 * tier, sh = tierShelf(tier);
        ShelfRules r; boardShelfRules(sh, &r);
        bool on = tier == hi;
        drawText(28, y, boardLockdownName(tier), on ? WHITE : FIRE2, 1);
        char v[8];
        fmtInt(v, sh); strcat(v, "+"); drawTextCenter(HXS[0], y, v, on ? WHITE : INK, 1);
        fmtInt(v, r.threshold); drawTextCenter(HXS[1], y, v, on ? WHITE : INK, 1);
        fmtInt(v, r.pulseEvery); drawTextCenter(HXS[2], y, v, on ? WHITE : INK, 1);
        fmtInt(v, r.tier >= LOCKDOWN_TWIN_FIRE_TIER ? 2 : 1); drawTextCenter(HXS[3], y, v, r.tier >= LOCKDOWN_TWIN_FIRE_TIER ? FIRE2 : (on ? WHITE : INK), 1);
    }
    gCaption(ui(UI_G_TIER), INK);
}

// ---- 7 MODES: a stylus taps the title's two buttons, the Daily's calendar tile shows its modifier (sliding between them), a streak fills
static void gdModes(const Game *g, int t)
{
    int cls = t >= 8 && t < 32, dly = t >= 34;                  // which button the stylus has pressed
    int press = (t >= 12 && t < 15) || (t >= 38 && t < 41);     // a button sinks for a moment
    roundRect(52, 34 + (cls && press ? 1 : 0), 64, 22, GOLD);
    drawTextCenter(84, 41 + (cls && press ? 1 : 0), ui(UI_T_CLASSIC), BLACK, 1);
    roundRect(140, 34 + (dly && press ? 1 : 0), 64, 22, DIM);
    rectOutline(140, 34 + (dly && press ? 1 : 0), 64, 22, GOLD_DK);
    int day = g->clockOk ? g->dailySeed % 100 : 12;
    char s[32];
    uiFmt(s, sizeof s, UI_T_DAILYDATE, g->clockOk ? g->dailySeed / 100 % 100 : 11, day);
    drawTextCenter(172, 37 + (dly && press ? 1 : 0), s, GOLD_LT, 1);
    int mod = (t / 16) & 3, tp = t & 15, e = ease16(tp, 4);   // the modifier changes every 16 ticks and slides in
    drawTextCenter(172, 46 + (dly && press ? 1 : 0), boardDailyModName(mod), INK, 1);
    if (cls) rectOutline(51, 33, 66, 24, tri16(t * 4, 40) > 8 ? WHITE : ACCENT);
    if (dly) rectOutline(139, 33, 66, 24, tri16(t * 4, 40) > 8 ? WHITE : ACCENT);
    // the calendar tile
    rectOutline(52, 70, 56, 56, GOLD);
    fillRect(53, 71, 54, 13, ACCENT);
    drawTextCenter(80, 74, ui(UI_T_DAILY), BLACK, 1);
    fmtInt(s, day); if (day < 10) { s[1] = s[0]; s[0] = '0'; s[2] = 0; }
    drawTextCenter(80, 92, s, GOLD_LT, 3);
    drawTextBox(80 - textWidth(boardDailyModName(mod), 1) / 2 + (16 - e) * 2, 116, boardDailyModName(mod), ACCENT, 1, 54, 106);
    // what the modifier does, from the Daily rules
    ShelfRules r; boardDailyRules(mod, &r);
    char l1[40], l2[40];
    if (mod == DAILYMOD_EMBER) { uiFmt(l1, sizeof l1, UI_G_MOD_METER, r.threshold); strcpy(l2, ui(r.burnEvery == 1 ? UI_G_MOD_FIRE1 : UI_G_MOD_FIRE2)); }
    else if (mod == DAILYMOD_DEEP) { strcpy(l1, ui(UI_G_MOD_DEEP1)); uiFmt(l2, sizeof l2, UI_G_MOD_DEEP2, ROWS - r.minRow); }
    else if (mod == DAILYMOD_LONG) { uiFmt(l1, sizeof l1, UI_G_MOD_LONG1, DAILY_LONG_LEN); strcpy(l2, ui(UI_G_MOD_LONG2)); }
    else { strcpy(l1, ui(UI_G_MOD_PLAIN1)); strcpy(l2, ui(UI_G_MOD_PLAIN2)); }
    int dx = (16 - e) * 3;
    drawTextBox(124 + dx, 74, boardDailyModName(mod), lerp15(BLACK, GOLD_LT, e), 2, 124, 236);
    drawTextBox(124 + dx, 94, l1, lerp15(BLACK, INK, e), 1, 124, 236);
    drawTextBox(124 + dx, 104, l2, lerp15(BLACK, INK, e), 1, 124, 236);
    // seven days: the streak builds, each box popping as it fills
    int streak = (t - 6) / 8; if (streak > 5) streak = 5; if (t < 6) streak = 0;
    for (int i = 0; i < 7; i++) {
        int x = 64 + i * 18;
        if (i < streak) { fillRect(x, 138, 14, 10, ACCENT); if (i == streak - 1 && ((t - 6) & 7) < 2) rectOutline(x - 1, 137, 16, 12, WHITE); }
        else rectOutline(x, 138, 14, 10, DIM);
    }
    if (streak >= 2) { uiFmt(s, sizeof s, UI_T_STREAKL, streak); drawTextCenter(SCREEN_W / 2, 152, s, GOLD_LT, 1); }
    // the stylus: to CLASSIC, a tap, then to DAILY, a tap
    int sx, sy;
    if (t < 34) { int ee = ease16(t - 2, 8); sx = 150 + (84 - 150) * ee / 16; sy = 90 + (44 - 90) * ee / 16; }
    else { int ee = ease16(t - 30, 6); sx = 84 + (172 - 84) * ee / 16; sy = 44; }
    if (t >= 2 && t < 52) gPointer(sx + 10, sy + 6, press);
    gRipple(94, 50, t - 12);
    gRipple(182, 50, t - 38);
    gCaption(ui(UI_G_ONEDAILY), INK);
}

// ---- 8 CONTROLS: the DS, each control pressed in turn (it sinks, a ripple opens) with what it does; the stylus draws a trail on the screen
static void gdControls(int t)
{
    static const short CAP[8] = { UI_G_C_STYLUS, UI_G_C_DPAD, UI_G_C_A, UI_G_C_B, UI_G_C_X, UI_G_C_Y, UI_G_C_LR, UI_G_C_START };
    int hi = (t / 8) & 7, tp = t & 7;
    u16 body = C(5,5,7), lit = lerp15(ACCENT, WHITE, tri16(t * 4, 32));
    fillRect(25, 32, 206, 124, body);          // one fill (roundRect paints the middle three times: too dear for a top-screen slice)
    fillRect(24, 33, 1, 122, body); fillRect(231, 33, 1, 122, body);
    rectOutline(24, 32, 208, 124, DIM_TEXT);
    // shoulders L and R: both sink while held
    for (int k = 0; k < 2; k++) {
        int x = k ? 188 : 28, sink = hi == 6 ? 1 : 0;
        bool on = hi == 6;
        fillRect(x, 35 + sink, 40, 11, on ? ACCENT : DIM2); rectOutline(x, 35 + sink, 40, 11, on ? lit : DIM_TEXT);
        drawTextCenter(x + 20, 37 + sink, k ? "R" : "L", on ? BLACK : INK, 1);
    }
    // the touch screen, with the stylus trail inside it
    bool st = hi == 0;
    rectOutline(90, 56, 76, 57, st ? lit : ACCENT);
    fillRect(91, 57, 74, 55, BLACK);
    if (!st) { drawTextCenter(128, 80, ui(UI_G_TOUCH), DIM_TEXT, 1); drawTextCenter(128, 90, ui(UI_G_SCREEN), DIM_TEXT, 1); }
    else {
        static const u8 TX[5] = { 100, 112, 124, 136, 148 }, TY[5] = { 98, 74, 96, 72, 94 };
        for (int k = 0; k < 4; k++) {               // the trail grows segment by segment, the pointer at its head
            int seg = tp * 4 / 8 + 1;               // 1..4 segments over the eight ticks
            if (k >= seg) break;
            for (int s2 = 0; s2 <= 6; s2++) fillRect(TX[k] + (TX[k + 1] - TX[k]) * s2 / 6, TY[k] + (TY[k + 1] - TY[k]) * s2 / 6, 2, 2, GOLD_LT);
        }
        int seg = tp / 2, ee = ease16(tp & 1, 2) ;
        if (seg > 3) seg = 3;
        int hx = TX[seg] + (TX[seg + 1] - TX[seg]) * ee / 16, hy = TY[seg] + (TY[seg + 1] - TY[seg]) * ee / 16;
        gPointer(hx, hy, true);
    }
    // the d-pad
    bool dp = hi == 1, dpress = dp ? 1 : 0;
    fillRect(42, 86 + dpress, 24, 8, dp ? ACCENT : DIM2); fillRect(50, 78 + dpress, 8, 24, dp ? ACCENT : DIM2);
    rectOutline(42, 86 + dpress, 24, 8, dp ? lit : DIM_TEXT); rectOutline(50, 78 + dpress, 8, 24, dp ? lit : DIM_TEXT);
    fillRect(51, 87 + dpress, 6, 6, dp ? ACCENT : DIM2);
    if (dp) gRipple(54, 90, tp);
    // A B X Y on a diamond
    static const char BN[4] = { 'X', 'Y', 'A', 'B' };
    static const u8 bxp[4] = { 196, 180, 212, 196 }, byp[4] = { 70, 86, 86, 102 }, bid[4] = { 4, 5, 2, 3 };
    for (int k = 0; k < 4; k++) {
        bool on = hi == bid[k];
        int sink = on ? 1 : 0;
        roundRect(bxp[k], byp[k] + sink, 14, 14, on ? ACCENT : DIM2);
        rectOutline(bxp[k], byp[k] + sink, 14, 14, on ? lit : DIM_TEXT);
        drawChar(bxp[k] + 4, byp[k] + 4 + sink, BN[k], on ? BLACK : INK, 1);
        if (on) gRipple(bxp[k] + 7, byp[k] + 7, tp);
    }
    // START and SELECT
    bool sp = hi == 7;
    fillRect(190, 126 + (sp ? 1 : 0), 24, 8, sp ? ACCENT : DIM2); rectOutline(190, 126 + (sp ? 1 : 0), 24, 8, sp ? lit : DIM_TEXT);
    fillRect(190, 138, 24, 8, DIM2); rectOutline(190, 138, 24, 8, DIM_TEXT);
    drawText(150, 126, "START", sp ? ACCENT : DIM_TEXT, 1);                 // the console prints these two words on its buttons: they stay as printed
    drawText(138, 138, "SELECT", DIM_TEXT, 1);
    {   // the language's own word beside each, in the free band to its left, when it differs and fits
        const char *ls = ui(UI_G_KEY_START), *lt = ui(UI_G_KEY_SELECT);
        if (strcmp(ls, "START") && textWidth(ls, 1) <= 66) drawText(144 - textWidth(ls, 1), 126, ls, sp ? ACCENT : lerp15(BLACK, ACCENT, 11), 1);
        if (strcmp(lt, "SELECT") && textWidth(lt, 1) <= 66) drawText(132 - textWidth(lt, 1), 138, lt, lerp15(BLACK, ACCENT, 11), 1);
    }
    if (sp) gRipple(202, 130, tp);
    gCaption(ui(CAP[hi]), ACCENT);
}

static void drawTopGuide(const Game *g)
{
    drawTopDots();
    rectOutline(8, 8, 240, 176, GOLD_DK);
    rectOutline(9, 9, 238, 174, GOLD);
    drawTextCenter(SCREEN_W / 2, 15, ui(UI_PA_GUIDE), GOLD_LT, 1);
    hline(16, 25, 224, GOLD_DK);
    int t = gTick(), sf = frame;
    frame = t * 4;                              // the game's pulses (hole digits, flames, sparkles) now follow the loop, not the clock
    switch (g->guidePage) {
        case 0: gdBoard(t); break;
        case 1: gdScoring(t); break;
        case 2: gdPeek(t); break;
        case 3: gdPass(t); break;
        case 4: gdFire(t); break;
        case 5: gdShelves(t); break;
        case 6: gdModes(g, t); break;
        default: gdControls(t); break;
    }
    frame = sf;
}

// ---- the guided first shelf's in-world cues (touch screen, over the board): a breathing ring and a bobbing pointer on what
// to act on, and a ghost drag that traces a real word once. All of it comes from G.cue / G.cueAge, which main.c keeps:
// they vanish the instant the player acts and never show while the stylus is down.
static void drawCues(const Game *g)
{
    if (!g->cue || g->state != ST_PLAY) return;
    int age = g->cueAge;
    int bob = tri16(age, 36) / 5;                          // 0..3 px, 36 frames a bob (under 2 Hz)
    int breathe = tri16(age, 48);                          // the ring's breath: 48 frames
    u16 rc = lerp15(ACCENT, WHITE, breathe);
    int fade = age < 8 ? age * 2 : 16;                     // cues come up from black instead of popping
    switch (g->cue) {
        case CUE_TAIL: {
            int x, y; cellOrigin(g->chainC[g->chainLen - 1], g->chainR[g->chainLen - 1], &x, &y);
            y -= 2;
            int e = 2 + breathe / 6;
            rectOutline(x - e, y - e, TILE_W + 2 * e, TILE_H + 2 * e, lerp15(BLACK, rc, fade));
            gPointer(x + TILE_W - 8, y + TILE_H - 4 + bob, false);   // the tip rests on the gem's lower right: the letter stays clear
            break;
        }
        case CUE_PEEKBTN: {
            int e = 1 + breathe / 8;
            rectOutline(PANEL_X - e, 174 - e, 76 + 2 * e, 16 + 2 * e, lerp15(BLACK, rc, fade));
            gPointer(PANEL_X + 58, 172 + bob, false);               // beside the label, never over it
            break;
        }
        case CUE_ARMED: {
            int x, y; cellOrigin(g->armC, g->armR, &x, &y);
            int lift = (age % 36) < 18 ? (age % 36) / 3 : 6;        // the stylus lifts off the armed gem: that is the peel
            gPointer(x + TILE_W - 8, y + TILE_H - 4 - lift, true);
            break;
        }
        case CUE_GHOST: {
            int n = g->hintLen;
            if (n < 3) break;
            int seg = age / GHOST_GEM_FRAMES, within = age % GHOST_GEM_FRAMES;
            if (seg > n - 1) { seg = n - 1; within = GHOST_GEM_FRAMES; }
            u16 dk = lerp15(BLACK, ACCENT, 8) , lt = lerp15(BLACK, ACCENT, 14);
            for (int i = 0; i < n; i++) {
                if (i > seg) break;
                int x, y; cellOrigin(g->hintC[i], g->hintR[i], &x, &y);
                rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, lerp15(BLACK, rc, fade));
                if (i == 0) continue;
                int px0, py0, px1, py1; cellOrigin(g->hintC[i - 1], g->hintR[i - 1], &px0, &py0); px0 += TILE_W / 2; py0 += TILE_H / 2;
                px1 = x + TILE_W / 2; py1 = y + TILE_H / 2;
                int steps = (i < seg || (age >= n * GHOST_GEM_FRAMES)) ? 10 : 10 * ease16(within, GHOST_GEM_FRAMES) / 16;
                for (int s2 = 0; s2 <= steps; s2++) {
                    int xx = px0 + (px1 - px0) * s2 / 10, yy = py0 + (py1 - py0) * s2 / 10;
                    fillRect(xx - 1, yy - 1, 3, 3, dk); px(xx, yy, lt);
                }
            }
            int cx0, cy0, cx1, cy1;
            cellOrigin(g->hintC[seg > 0 ? seg - 1 : 0], g->hintR[seg > 0 ? seg - 1 : 0], &cx0, &cy0);
            cellOrigin(g->hintC[seg], g->hintR[seg], &cx1, &cy1);
            int e = seg > 0 ? ease16(within, GHOST_GEM_FRAMES - 4) : 16;
            int hx = cx0 + (cx1 - cx0) * e / 16 + TILE_W / 2 + 5, hy = cy0 + (cy1 - cy0) * e / 16 + TILE_H / 2 + 4;
            gPointer(hx, hy, true);
            if (within < 6 && age < n * GHOST_GEM_FRAMES) gRing(cx1 + TILE_W / 2, cy1 + TILE_H / 2, 4 + within, lerp15(BLACK, WHITE, 16 - within * 2));
            break;
        }
    }
}

static void drawTop(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    if (g->state == ST_GUIDE) { drawTopGuide(g); return; }
    if (menuState(g->state)) {
        drawTopDots();
        drawTopTitle();
        return;
    }
    drawTopDots();

    // gold plaque with a shimmer running around the border
    rectOutline(8, 8, 240, 176, GOLD_DK);
    rectOutline(9, 9, 238, 174, GOLD);
    int per = 2 * (238 + 174), sp = (frame * 3) % per;
    for (int i = 0; i < 18; i++) {
        int t = (sp + i) % per, x, y;
        if (t < 238) { x = 9 + t; y = 9; }
        else if (t < 238 + 174) { x = 246; y = 9 + (t - 238); }
        else if (t < 2 * 238 + 174) { x = 246 - (t - 238 - 174); y = 182; }
        else { x = 9; y = 182 - (t - 2 * 238 - 174); }
        px(x, y, GOLD_LT);
    }

    drawLogo(16, 16, 2);
    char s[40];
    {   // the mode under the logo: CLASSIC, LOCKDOWN (tiers), DAILY <modifier> or PRACTICE <modifier>, then the twist for a moment
        const char *ml; u16 mc;
        if (g->mode == MODE_DAILY) {
            uiFmt(s, sizeof s, g->practice ? UI_H_MODE_PRACTICE : UI_H_MODE_DAILY, boardDailyModName(g->dailyMod));
            ml = s; mc = g->practice ? DIM_TEXT : GOLD_LT;
        } else if (g->lockdown) { ml = boardLockdownName(g->lockTier); mc = FIRE1; }
        else { ml = ui(UI_T_CLASSIC); mc = ACCENT; }
        drawText(16, 36, ml, mc, 1);
        if (g->twist && shelfAge < 120) drawText(16 + (int)(strlen(ml) + 1) * 6, 36, boardTwistName(g->twist), GOLD_LT, 1);   // 131 px at most with LOCKDOWN III
    }
    if (g->mode == MODE_DAILY) uiFmt(s, sizeof s, UI_T_DAILYDATE, g->dailySeed / 100 % 100, g->dailySeed % 100);   // MMDD
    else uiFmt(s, sizeof s, UI_H_SHELF, g->shelf);
    drawText(170, 18, s, INK, 1);
    drawText(170, 28, fmtInt(s, shownScore), GOLD, 1);

    // the pass: gem-cut blanks that pop when found
    int n = g->passLen, bw = PLAQUE_W, gap = n >= 9 ? 1 : 4;   // nine letters squeeze to one pixel between gems
    int x0 = ((SCREEN_W - (n * bw + (n - 1) * gap)) / 2) & ~1, y0 = 60;   // x even: word-aligned blits; the popped gem spans y 50..95, inside HUD slice 1
    if (x0 + n * bw + (n - 1) * gap > 220) x0 = (220 - (n * bw + (n - 1) * gap)) & ~1;   // a long pass slides left of the fire meter (x 222..)
    uiFmt(s, sizeof s, UI_H_PASSPLAQUE, boardPassTheme(g));
    drawTextCenter(SCREEN_W / 2, 50, s, DIM_TEXT, 1);
    for (int i = 0; i < n; i++) {
        int x = x0 + i * (bw + gap);
        if (g->found[i]) {
            int pop = letterPop[i];
            int grow = pop > 14 ? (pop - 14) : 0;   // capped at 10 px
            if (pop) drawGemSized(x - grow, y0 - grow, bw + grow * 2, PLAQUE_H + grow * 2, &GEM_DIAMOND, true);
            else blitPlaque(x, y0);
            drawChar(x + 6, y0 + 6, 'A' + (g->pass[i] - 'a'), C(4,6,10), 2);
        } else {
            rectOutline(x, y0, bw, 26, GOLD_DK);
            fillRect(x + 4, y0 + 22, bw - 8, 1, GOLD);
            if (((frame >> 4) % n) == i) px(x + 4 + ((frame >> 1) % (bw - 8)), y0 + 22, GOLD_LT);
        }
    }

    // current chain large, or the last word and its score
    char w[WORD_BUF]; strcpy(w, chainWordNow);
    for (char *p = w; *p; p++) *p -= 32;
    if (w[0]) {
        bool valid = chainValidNow;
        int jx = invalidTimer ? ((invalidTimer & 2) ? 3 : -3) : 0;
        u16 wc = invalidTimer ? ((invalidTimer & 4) ? FIRE1 : WHITE) : (valid ? GOLD_LT : WHITE);
        int wl = (int)strlen(w);
        // The word lives left of the fire meter (x 222..): 3x letters 18 px apart up to eleven across,
        // then 2x letters at up to 12 px (twenty letters squeeze to 10 px). It is centred on the screen
        // and slides left only as far as it must to stay inside x 12..220.
        if (wl <= 11) {
            int wd = wl * 18 - 3, cx3 = SCREEN_W / 2 - wd / 2;
            if (cx3 + wd > 220) cx3 = 220 - wd;
            cx3 += jx;
            for (const char *p = w; *p; p++, cx3 += 18) drawChar3Shadow(cx3, 100, *p, wc, DIM2);
        } else {
            int pitch = 198 / (wl - 1); if (pitch > 12) pitch = 12;
            int wd = (wl - 1) * pitch + 10, cx2 = SCREEN_W / 2 - wd / 2;
            if (cx2 + wd > 220) cx2 = 220 - wd;
            cx2 += jx;
            for (const char *p = w; *p; p++, cx2 += pitch) drawChar2Shadow(cx2, 104, *p, wc, DIM2);
        }
    } else if (g->lastWord[0]) {
        char lw[MAX_WORD + 1]; strcpy(lw, g->lastWord);
        for (char *p = lw; *p; p++) *p -= 32;
        int lsc = textWidth(lw, 2) <= 200 ? 2 : 1, lx = SCREEN_W / 2 - textWidth(lw, lsc) / 2;   // 17 letters at 2x; the fire meter starts at x 222
        if (lx + textWidth(lw, lsc) > 220) lx = 220 - textWidth(lw, lsc);
        drawText(lx, 100, lw, INK, lsc);
        s[0] = '+'; fmtInt(s + 1, g->lastWordScore);
        drawTextCenter(SCREEN_W / 2, 118, s, GOLD, 1);
    }
    if (w[0] && chainScoreNow > 0) {   // what the chained word is worth, under the big letters (rows 122..135; the ledger line is at 137)
        s[0] = '+'; fmtInt(s + 1, chainScoreNow);
        drawTextCenter(SCREEN_W / 2, 122, s, GOLD, 2);
    }

    // bookmarks: three small books, filled while you still hold them (Classic only)
    if (g->mode == MODE_CLASSIC)
        for (int i = 0; i < 3; i++) {
            int bx = 214 + i * 8, by = 38;
            if (i < g->lives) {
                fillRect(bx, by, 5, 8, GOLD);
                fillRect(bx + 1, by, 1, 8, GOLD_DK);     // spine
                px(bx + 3, by + 1, GOLD_LT);             // page highlight
            } else rectOutline(bx, by, 5, 8, DIM);
        }

    // fire meter with a flame cap
    int low = fireLow;
    bool fireNext = low >= 0 && g->burnEvery - g->burnTimer <= 1;   // the fire steps on the next turn
    {   // the label sits at x 222 and slides left when a language needs more than 23 px, so it ends inside the frame
        const char *fl = ui(fireNext && (frame & 8) ? UI_NEXT : UI_FIRE);
        int fw = textWidth(fl, 1), fx = 222 + fw > 245 ? 245 - fw : 222;
        drawText(fx, 46, fl, fireNext && (frame & 8) ? FIRE2 : DIM_TEXT, 1);
    }
    rectOutline(226, 54, 8, ROWS * 10 + 2, DIM);   // rows 54..135: clear of the ledger line below
    for (int r = 0; r <= low; r++) fillRect(227, 55 + r * 10, 6, 9, lerp15(FIRE2, FIRE1, r * 2));
    if (low >= 0) { int fy = 55 + low * 10 + 9; px(229 + (frame & 1), fy - 12 - (frame & 3), FIRE2); px(230, fy - 11, FIRE2); }

    // word ledger: the last five words of the shelf, newest (with its points) at the right,
    // fading with age. One 1x line without a shadow: rows 137..143, inside HUD slice 2.
    if (g->ledgerN && clipY0 <= 143 && clipY1 > 137) {
        int x = 238;
        int shown = g->ledgerN < 5 ? g->ledgerN : 5;
        for (int age = 0; age < shown; age++) {
            int k = ledgerIndex(g, age);
            char lw[MAX_WORD + 1]; strcpy(lw, g->ledger[k]);
            for (char *p = lw; *p; p++) if (*p >= 'a') *p -= 32;
            if (age == 0) {
                s[0] = '+'; fmtInt(s + 1, g->ledgerPts[k]);
                x -= textWidth(s, 1); drawText(x, 137, s, GOLD, 1); x -= 4;
            }
            int tw = textWidth(lw, 1);
            if (x - tw < 8) break;
            x -= tw;
            drawText(x, 137, lw, age == 0 ? WHITE : lerp15(INK, DIM_TEXT, age * 4), 1);
            x -= 8;
        }
    }

    // shelf map: twenty boxes, wrapping to the next page of twenty past shelf 20
    int mapBase = ((g->shelf - 1) / 20) * 20;
    for (int i = 0; i < 20; i++) {
        int x = 18 + i * 11, y = 150, sh = mapBase + i + 1;
        if (sh < g->shelf) fillRect(x, y, 8, 8, ACCENT);
        else if (sh == g->shelf) rectOutline(x, y, 8, 8, (frame & 16) ? WHITE : ACCENT);
        else rectOutline(x, y, 8, 8, DIM);
    }

    // message: pops in at double size, then settles
    if (g->msg[0]) {
        int sc = (g->msgTimer > 46 && textWidth(g->msg, 2) <= 230) ? 2 : 1;
        int mx = SCREEN_W / 2 - textWidth(g->msg, sc) / 2, mc = g->msgTimer != 0 ? 1 : 0;
        u16 col = mc ? GOLD_LT : INK;
        if (g->msgTimer < 0 && msgAge < 20) {     // a sticky prompt slides in from the right with a little overshoot, fading up, and settles (it refreshes at 15 Hz: five steps)
            int e = msgAge * 16 / 20, dx = (16 - e) * 10 / 16 - (e > 12 ? (e - 12) : 0);
            if (mx + dx + textWidth(g->msg, sc) > 240) dx = 240 - textWidth(g->msg, sc) - mx;
            mx += dx; col = lerp15(BLACK, col, e);
        }
        drawTextShadow(mx, sc == 2 ? 162 : 166, g->msg, col, sc);   // msgTimer -1 is a sticky prompt
    }
}

// ---------------------------------------------------------------- overlays

static void drawTitle(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoardDots();
    drawBoardFrame();
    drawParticles();     // the gem rain falls behind the band, the card and the buttons: it never covers the logo
    // The logo band: a solid black strip with gold rules, the logo gems riding inside it.
    fillRect(BX(8), 30, 157, 44, BLACK);
    hline(BX(8), 30, 157, GOLD_DK); hline(BX(8), 73, 157, GOLD_DK);
    static const char *letters = "wordPASS";
    const GemPal *th = THEMES[S.theme];
    const GemPal *pals[4] = { &th[TH_RUBY], &th[TH_SAPPHIRE], &th[TH_EMERALD], &th[TH_AMBER] };
    int gw = 20, gh = 22, lx = 86 - (8 * gw) / 2;
    for (int i = 0; i < 8; i++) {
        int bob = ((frame + i * 6) & 31) < 16 ? ((frame + i * 6) & 15) / 6 : (15 - ((frame + i * 6) & 15)) / 6;
        int x = lx + i * gw, y = 40 - bob;
        drawGemSized(BX(x), y, gw - 1, gh, i < 4 ? pals[i] : &GEM_DIAMOND, false);
        char ch = letters[i];
        if (ch >= 'a') drawGlyph(BX(x + 5), y + 5, font5x7[fontLogoIndex(ch)], WHITE, 2);
        else drawChar(BX(x + 5), y + 5, ch, C(4,6,10), 2);
    }
    {   // a twinkle crosses one logo gem every 11 frames (a different one each time) and the diamond gets a glint
        int k = frame / 11, ph = frame % 11, gi = (k * 5 + (k >> 2)) & 7;
        int r = ph < 6 ? ph / 2 : (10 - ph) / 2;                 // 0 1 2 2 1 0
        int sx = BX(lx + gi * gw + 4 + (k * 7) % 10), sy = 44 + (k * 3) % 9 - 2;
        if (r > 0) {
            for (int d = -r - 1; d <= r + 1; d++) { px(sx + d, sy, d == 0 ? WHITE : lerp15(BLACK, WHITE, 12 - 3 * (d < 0 ? -d : d))); px(sx, sy + d, d == 0 ? WHITE : lerp15(BLACK, WHITE, 12 - 3 * (d < 0 ? -d : d))); }
        }
    }
    {   // GUIDE: pulses until the first guided shelf has been done (S.tutorDone), then rests
        bool cue = !S.tutorDone;
        int pp = (frame & 31) < 16 ? (frame & 15) : 15 - (frame & 15);
        fillRect(BX(GUIDE_BTN_X), 7, GUIDE_BTN_W, 17, cue ? lerp15(BLACK, ACCENT, pp / 3) : BLACK);
        rectOutline(BX(GUIDE_BTN_X), 7, GUIDE_BTN_W, 17, cue ? lerp15(GOLD_DK, WHITE, pp) : GOLD_DK);
        drawTextCenter(BX(GUIDE_BTN_X + GUIDE_BTN_W / 2), 12, ui(UI_T_GUIDE), cue ? lerp15(ACCENT, WHITE, pp / 2) : INK, 1);
    }
    {   // the language tag: tap for the next language (the name in gold once it is not English)
        bool en = langCur() == LANG_EN;
        fillRect(BX(LANG_BTN_X), 7, LANG_BTN_W, 17, BLACK);
        rectOutline(BX(LANG_BTN_X), 7, LANG_BTN_W, 17, en ? GOLD_DK : GOLD);
        drawTextCenter(BX(LANG_BTN_X + (LANG_BTN_W - 10) / 2), 12, langName(langCur()), en ? INK : GOLD_LT, 1);
        drawChar(BX(LANG_BTN_X + LANG_BTN_W - 11), 12, '>', ACCENT, 1);
    }
    fillRect(BX(16), 76, 140, 56, BLACK);
    rectOutline(BX(16), 76, 140, 56, GOLD);
    drawTextCenter(BX(86), 84, ui(UI_T_CRACK), ACCENT, 1);
    drawTextCenter(BX(86), 96, ui(UI_T_DONTBURN), INK, 1);
    char dh[40]; uiFmt(dh, sizeof dh, UI_T_DRAGHINT, S.hand == HAND_LEFT ? "R" : "L");
    drawTextCenter(BX(86), 112, dh, DIM_TEXT, 1);
    // mode buttons
    bool blink = frame & 32;
    roundRect(BX(20), TB_Y, 64, 22, blink ? GOLD : GOLD_DK);
    drawTextCenter(BX(52), TB_Y + 7, ui(UI_T_CLASSIC), BLACK, 1);
    // DAILY: two 1x lines inside the 22 px button (nothing reaches y 157, where OPTIONS starts)
    bool played = g->clockOk && S.dailyPlayedDate == g->dailySeed;
    roundRect(BX(88), TB_Y, 64, 22, g->clockOk ? DIM : BLACK);
    rectOutline(BX(88), TB_Y, 64, 22, g->clockOk ? GOLD_DK : DIM2);
    char d1[24], d2[24];
    if (!g->clockOk) {
        drawTextCenter(BX(120), TB_Y + 3, ui(UI_T_DAILY), DIM_TEXT, 1);
        drawTextCenter(BX(120), TB_Y + 12, ui(UI_T_SETCLOCK), DIM_TEXT, 1);
    } else if (played) {
        drawTextCenter(BX(120), TB_Y + 3, ui(UI_T_PRACTICE), ACCENT, 1);
        uiFmt(d2, sizeof d2, UI_T_DONE, S.dailyLastScore);
        if (textWidth(d2, 1) > 58) siprintf(d2, "%d", S.dailyLastScore);
        drawTextCenter(BX(120), TB_Y + 12, d2, INK, 1);
    } else {
        uiFmt(d1, sizeof d1, UI_T_DAILYDATE, g->dailySeed / 100 % 100, g->dailySeed % 100);
        drawTextCenter(BX(120), TB_Y + 3, d1, GOLD_LT, 1);
        drawTextCenter(BX(120), TB_Y + 12, boardDailyModName(g->dailyMod), INK, 1);
    }
    fillRect(BX(54), TB_Y + 26, 64, 20, BLACK);
    rectOutline(BX(54), TB_Y + 26, 64, 20, DIM);
    drawTextCenter(BX(86), TB_Y + 32, ui(UI_T_OPTIONS), INK, 1);
    // the BEST line is also the way into THE LEDGER (tap it, or press X)
    // Scores of six digits and up read as thousands (123K) so the two ends of the line never meet.
    char s[32], n[16];
    int streak = g->clockOk ? settingsLiveStreak(g->dailySeed) : 0;
    if (S.bestClassic) { fmtScoreShort(n, S.bestClassic); uiFmt(s, sizeof s, UI_T_BESTL, n); } else strcpy(s, ui(UI_T_LEDGER));
    drawText(BX(20), 180, s, DIM_TEXT, 1);
    int leftEnd = BX(20) + textWidth(s, 1);
    char r[32]; u16 rc = DIM_TEXT; r[0] = 0;
    if (streak >= 2) { uiFmt(r, sizeof r, UI_T_STREAKL, streak); rc = GOLD_LT; }
    else if (S.bestDaily) { fmtScoreShort(n, S.bestDaily); uiFmt(r, sizeof r, UI_T_DAILYL, n); }
    if (r[0] && BX(152) - textWidth(r, 1) >= leftEnd + 6) drawText(BX(152) - textWidth(r, 1), 180, r, rc, 1);
}

static const char *optValue(int row)
{
    switch (row) {
        case 0: return ui(THEME_NAMES[S.theme]);
        case 1: return ui(FRAME_NAMES[S.frame]);
        case 2: return ui(ACCENT_NAMES[S.accent]);
        case 3: return ui(S.hand ? UI_V_LEFT : UI_V_RIGHT);
        case 4: return ui(FIRE_NAMES[S.fire]);
        case 5: return ui(S.music ? UI_V_ON : UI_V_OFF);
        case 6: return ui(LIB_NAMES[S.library]);
        case 7: return ui(langCur() == LANG_EN ? SPELL_NAMES[S.spelling] : UI_V_ENONLY);
        default: return langName(langCur());
    }
}

// the options list: label on the left, "< VALUE >" on the right with the chevrons at fixed columns
// (the left half of the row steps back, the right half forwards)
static void drawOptions(const Game *g)
{
    int savedX0 = boardX0;
    boardX0 = 0;                       // the frame and the dot grid stay put too, whatever HAND says
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoardDots();
    drawBoardFrame();
    boardX0 = savedX0;
    drawTextShadow(86 - textWidth(ui(UI_T_OPTIONS_H), 2) / 2, 14, ui(UI_T_OPTIONS_H), GOLD_LT, 2);
    int sel = g->optRow;
    for (int i = 0; i < OPT_ROWS; i++) {
        int y = OPT_Y0 + i * OPT_DY;
        bool on = i == sel;
        if (on) { fillRect(12, y, 148, OPT_DY - 1, lerp15(BLACK, ACCENT, 4)); rectOutline(12, y, 148, OPT_DY - 1, ACCENT); }
        drawText(18, y + 3, ui(OPT_NAMES[i]), on ? WHITE : INK, 1);
        const char *val = optValue(i);
        u16 lc = on ? ((nudgeTimer > 0 && nudgeDir < 0) ? WHITE : ACCENT) : DIM_TEXT;
        u16 rc = on ? ((nudgeTimer > 0 && nudgeDir > 0) ? WHITE : ACCENT) : DIM_TEXT;
        bool fixed = i == 7 && langCur() != LANG_EN;       // SPELLING in another language is a note, not a choice: no chevrons
        if (!fixed) { drawChar(76, y + 3, '<', lc, 1); drawChar(149, y + 3, '>', rc, 1); }
        drawText(112 - textWidth(val, 1) / 2 + 3, y + 3, val, fixed ? DIM_TEXT : on ? WHITE : INK, 1);     // centred between the chevrons
    }
    rectOutline(54, 153, 64, 17, DIM);
    drawTextCenter(86, 158, ui(UI_OP_BACK), INK, 1);
    if (!settingsPersistent()) drawTextCenter(86, 176, ui(UI_OP_NOSAVE), DIM_TEXT, 1);
    else if (settingsSaveFailed()) drawTextCenter(86, 176, ui(UI_OP_SAVEFAIL), FIRE2, 1);
    else drawTextCenter(86, 176, ui(UI_OP_SAVED), DIM_TEXT, 1);
}

// THE LEDGER: lifetime stats from the saved records, on the touch screen like the options
#define STAT_LABEL C(15,15,18)       // brighter than DIM: twelve labels on one screen have to be readable
static void statRow(int y, const char *label, const char *val, u16 col)
{
    drawText(BX(14), y, label, STAT_LABEL, 1);
    drawText(BX(158) - textWidth(val, 1), y, val, col, 1);
}

static void upperCopy(char *dst, const char *src)
{
    for (; *src; src++, dst++) *dst = (*src >= 'a' && *src <= 'z') ? (char)(*src - 32) : *src;
    *dst = 0;
}

static void drawStats(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoardDots();
    drawBoardFrame();
    drawTextShadow(BX(86) - textWidth(ui(UI_T_LEDGER_H), 2) / 2, 12, ui(UI_T_LEDGER_H), GOLD_LT, 2);
    char s[32], w[SETTINGS_WORD_BUF];
    int y = 32;
    siprintf(s, "%d", S.gamesPlayed);     statRow(y, ui(UI_L_GAMES), s, WHITE);          y += 10;
    siprintf(s, "%d", S.shelvesCleared);  statRow(y, ui(UI_L_SHELVES), s, WHITE);        y += 10;
    siprintf(s, "%d", S.wordsSpelled);    statRow(y, ui(UI_R_WORDS), s, WHITE);          y += 10;
    siprintf(s, "%d", S.perfectPasses);   statRow(y, ui(UI_L_PERFECT), s, GOLD_LT);      y += 10;
    siprintf(s, "%d", S.rareWords);       statRow(y, ui(UI_L_RARE), s, ACCENT);    y += 10;
    siprintf(s, "%d%%", S.peelsHit * 100 / (S.peels > 0 ? S.peels : 1)); statRow(y, ui(UI_L_PEEKACC), s, WHITE);   y += 10;
    int streak = g->clockOk ? settingsLiveStreak(g->dailySeed) : 0;
    siprintf(s, "%d", streak);            statRow(y, ui(UI_L_STREAK), s, streak >= 2 ? GOLD_LT : WHITE);   y += 10;
    if (S.bestDaily) {
        siprintf(s, "%d  %02d-%02d", S.bestDaily, S.bestDailyDate / 100 % 100, S.bestDailyDate % 100);
        if (textWidth(s, 1) > 80) siprintf(s, "%d", S.bestDaily);
    } else strcpy(s, "-");
    statRow(y, ui(UI_L_DAILYBEST), s, WHITE);   y += 10;
    // the two word records are a label line and the word on its own line: a 20-letter word fits
    hline(BX(14), y + 1, 144, DIM);
    y += 6;
    if (S.longestLen) uiFmt(s, sizeof s, UI_L_LETTERS, S.longestLen); else strcpy(s, "-");
    statRow(y, ui(UI_L_LONGEST), s, WHITE);
    upperCopy(w, S.longestLen ? S.longestWord : "-");
    drawTextCenter(BX(86), y + 9, w, S.longestLen ? GOLD_LT : DIM_TEXT, 1);
    y += 21;
    if (S.bestWordPts) siprintf(s, "+%d", S.bestWordPts); else strcpy(s, "-");
    statRow(y, ui(UI_R_BESTWORD), s, GOLD);
    upperCopy(w, S.bestWordPts ? S.bestWordTxt : "-");
    drawTextCenter(BX(86), y + 9, w, S.bestWordPts ? GOLD_LT : DIM_TEXT, 1);
    rectOutline(BX(54), 162, 64, 20, DIM);
    drawTextCenter(BX(86), 168, ui(UI_OP_BACK), INK, 1);
}

// Title top screen: three tip cards that cycle. This draws on the TOP screen, which has
// no hand offset: never use BX() here (it would push the card off the right edge in the
// left-hand layout).
static void drawTipCard(int which)
{
    const GemPal *th = THEMES[S.theme];
    int cx = 128, y = 52;
    if (which == 0) {
        drawTextCenter(cx, y, ui(UI_C_SPELL), GOLD_LT, 1);
        static const char L[3] = { 'I', 'N', 'K' };
        const GemPal *P3[3] = { &th[TH_RUBY], &th[TH_SAPPHIRE], &th[TH_AMBER] };
        int gx = cx - 40;
        for (int i = 0; i < 3; i++) {
            int x = gx + i * 28, gy = y + 14 + ((i & 1) ? 10 : 0);
            drawGemSized(x, gy, TILE_W, TILE_H, P3[i], true);
            drawChar(x + 7, gy + 4, L[i], WHITE, 2);
        }
        for (int i = 1; i < 3; i++) {
            int x0 = gx + (i - 1) * 28 + TILE_W / 2, y0 = y + 14 + (((i - 1) & 1) ? 10 : 0) + TILE_H / 2;
            int x1 = gx + i * 28 + TILE_W / 2, y1 = y + 14 + ((i & 1) ? 10 : 0) + TILE_H / 2;
            for (int t = 0; t <= 8; t++) fillRect(x0 + (x1 - x0) * t / 8 - 1, y0 + (y1 - y0) * t / 8 - 1, 2, 2, GOLD_LT);
        }
        drawTextCenter(cx, y + 56, ui(UI_C_DRAG), INK, 1);
        drawTextCenter(cx, y + 66, ui(UI_C_3LETTERS), INK, 1);
        drawTextCenter(cx, y + 80, ui(UI_C_LONG), DIM_TEXT, 1);
    } else if (which == 1) {
        drawTextCenter(cx, y, ui(UI_PEEK), ACCENT, 1);
        int gx = cx - 40;
        for (int i = 0; i < 3; i++) {
            int x = gx + i * 28, gy = y + 14 + ((i & 1) ? 10 : 0);
            if (i == 0) { rectOutline(x + 1, gy + 1, TILE_W - 2, TILE_H - 2, ACCENT); drawChar2Shadow(x + 7, gy + 4, '2', ACCENT, BLACK); }
            else if (i == 1) { drawGemSized(x, gy, TILE_W, TILE_H, &GEM_DIAMOND, false); drawChar(x + 7, gy + 4, 'B', C(4,6,10), 2); }
            else { rectOutline(x + 1, gy + 1, TILE_W - 2, TILE_H - 2, DIM); drawChar(x + 10, gy + 8, 'X', DIM_TEXT, 1); }
        }
        drawTextCenter(cx, y + 56, ui(UI_C_HOLD), INK, 1);
        drawTextCenter(cx, y + 66, ui(UI_C_NUM), INK, 1);
        drawTextCenter(cx, y + 76, ui(UI_C_SIX), INK, 1);
        drawTextCenter(cx, y + 94, ui(UI_C_LETGO), DIM_TEXT, 1);
    } else {
        drawTextCenter(cx, y, ui(UI_FIRE), FIRE2, 1);
        int gx = cx - 40;
        int fx = gx, fy = y + 14;   // the burning gem, drawn the way the board draws it (light plate, dark letter, tongues)
        drawBurnCell(fx, fy, 0, 'r' - 'a', (frame >> 1) & 15, false, false);
        rectOutline(fx + 1, fy + TILE_H + 1, TILE_W - 2, TILE_H - 2, DIM);
        drawGemSized(gx + 28, y + 24, TILE_W, TILE_H, &GEM_DIAMOND, false); drawChar(gx + 35, y + 28, 'O', C(4,6,10), 2);
        drawGemSized(gx + 56, y + 14, TILE_W, TILE_H, &th[TH_EMERALD], false); drawChar(gx + 63, y + 18, 'P', WHITE, 2);
        drawTextCenter(cx, y + 62, ui(UI_C_F1), INK, 1);
        drawTextCenter(cx, y + 72, ui(UI_C_F2), INK, 1);
        drawTextCenter(cx, y + 82, ui(UI_C_F3), INK, 1);
        drawTextCenter(cx, y + 96, ui(UI_C_F4), DIM_TEXT, 1);
    }
    for (int i = 0; i < 3; i++) {
        if (i == which) fillRect(118 + i * 8, 170, 4, 4, GOLD_LT); else rectOutline(118 + i * 8, 170, 4, 4, DIM);
    }
}

// the card over the dimmed board: drawn black and excluded from the dim window
static void overlayCard(int x, int y, int w, int h)
{
    fillRect(x, y, w, h, BLACK);
    dimOn = true; dimCardX = x; dimCardY = y; dimCardW = w; dimCardH = h;
}

// ---- the cards. Results and fail share one geometry: a 150 x 154 card centred on the strip that
// slides up in 12 frames. Rows are 9 px apart and every string is checked against the 130 px between the
// card's inner margins (a 20-letter best word is centred on a row of its own).
#define CARD_CX 86
#define CARD_W 150
#define CARD_H 154
#define CARD_ROW_L (CARD_CX - CARD_W / 2 + 10)       // label edge
#define CARD_ROW_R (CARD_CX + CARD_W / 2 - 10)       // value edge
#define CARD_FIT 134                                 // widest string that clears the inner border

static int cardTop(const Game *g)
{
    // slides up with a small overshoot and settles (a spring: 11 frames to rest, 8 px past the final row at the deepest)
    static const s8 spring[11] = { 120, 84, 54, 30, 12, 0, -6, -8, -5, -2, -1 };
    int t = g->stateTimer;
    return 22 + (t < 11 ? spring[t] : 0);
}

// One row of a card: a label at the left edge, its value right-aligned to the right edge.
static void drawStatRow(int lx, int rx, int y, const char *label, u16 lcol, const char *value, u16 vcol)
{
    drawText(BX(lx), y, label, lcol, 1);
    drawText(BX(rx - textWidth(value, 1)), y, value, vcol, 1);
}

// the pass as 14 x 16 diamonds. Results: they light one every 6 frames from frame 12. Fail: the ones you
// found sit lit, the others are red-rimmed blanks that flip to their letter one every 8 frames after frame 30.
static void drawPassDiamonds(const Game *g, int y, bool fail)
{
    int n = g->passLen, bw = 14, gap = n >= 8 ? 1 : 2, x0 = CARD_CX - (n * bw + (n - 1) * gap) / 2, t = g->stateTimer, missed = 0;
    for (int i = 0; i < n; i++) {
        int lx = BX(x0 + i * (bw + gap));
        char ch = (char)('A' + (g->pass[i] - 'a'));
        if (!fail) {
            int tl = t - (12 + 6 * i);
            if (tl < 0) rectOutline(lx, y, bw, 16, GOLD_DK);
            else { drawGemSized(lx, y, bw, 16, &GEM_DIAMOND, tl < 6); drawChar(lx + 5, y + 4, ch, C(4,6,10), 1); }
        } else if (g->found[i]) {
            drawGemSized(lx, y, bw, 16, &GEM_DIAMOND, false); drawChar(lx + 5, y + 4, ch, C(4,6,10), 1);
        } else {
            int tl = t - 30 - 8 * missed++;
            if (tl < 0) rectOutline(lx, y, bw, 16, FIRE1);
            else { drawGemSized(lx, y, bw, 16, &GEM_DIAMOND, false); rectOutline(lx, y, bw, 16, FIRE1); drawChar(lx + 5, y + 4, ch, C(22,2,0), 1); }
        }
    }
}

// The best word of the shelf: its label and points on one row, the word itself centred on the next.
static void drawBestWord(const Game *g, int y)
{
    char s[24], lw[MAX_WORD + 1];
    strcpy(lw, g->bestWord[0] ? g->bestWord : "-");
    for (char *p = lw; *p; p++) if (*p >= 'a') *p -= 32;
    if (g->bestWordScore) siprintf(s, "+%d", g->bestWordScore); else strcpy(s, "-");
    drawStatRow(CARD_ROW_L, CARD_ROW_R, y, ui(UI_R_BESTWORD), DIM_TEXT, s, g->bestWordScore ? GOLD : DIM_TEXT);
    if (g->bestWord[0]) drawTextCenter(BX(CARD_CX), y + 9, lw, WHITE, 1);
}

static void drawResultsCard(const Game *g)
{
    int t = g->stateTimer, cy = cardTop(g), cx = CARD_CX, cw = CARD_W, ch = CARD_H;
    int lx = CARD_ROW_L, rx = CARD_ROW_R;
    overlayCard(BX(cx - cw / 2), cy, cw, ch);
    rectOutline(BX(cx - cw / 2), cy, cw, ch, cardFlash ? WHITE : GOLD_LT);
    rectOutline(BX(cx - cw / 2 + 2), cy + 2, cw - 4, ch - 4, cardFlash ? GOLD_LT : GOLD_DK);
    char title[48], s[48];
    if (g->mode == MODE_DAILY) strcpy(title, ui(g->practice ? UI_R_PRACTICEDONE : UI_R_PASSDONE_D));
    else if (g->twist) {
        uiFmt(title, sizeof title, UI_R_ACCEPTED_T, boardTwistName(g->twist));
        if (textWidth(title, 1) > CARD_FIT) uiFmt(title, sizeof title, UI_R_PASSED_T, boardTwistName(g->twist));   // INK SPILL is too wide with the full title
    } else strcpy(title, ui(UI_R_ACCEPTED));
    drawTextShadow(BX(cx - textWidth(title, 1) / 2), cy + 8, title, GOLD_LT, 1);
    drawPassDiamonds(g, cy + 20, false);
    hline(BX(lx), cy + 40, cw - 20, DIM);
    drawBestWord(g, cy + 45);
    siprintf(s, "%d", g->wordsThisShelf);
    if (g->uniqueWords < g->wordsThisShelf) uiFmt(s, sizeof s, UI_R_UNIQUE, g->wordsThisShelf, g->uniqueWords);
    drawStatRow(lx, rx, cy + 63, ui(UI_R_WORDS), DIM_TEXT, s, WHITE);
    siprintf(s, "%d", g->peels);
    drawStatRow(lx, rx, cy + 72, ui(UI_PEELS), DIM_TEXT, s, g->peels == g->passLen ? GOLD_LT : g->peels <= g->passLen + 2 ? ACCENT : WHITE);
    siprintf(s, "%d", g->turn);
    drawStatRow(lx, rx, cy + 81, ui(UI_TURNS), DIM_TEXT, s, WHITE);
    hline(BX(lx), cy + 90, cw - 20, DIM);
    // the three bonus rows count up with the score on the panel: shelf first, then speed, then the peel bonus
    int shelfB = boardShelfBonus(g->shelf), shown[3];
    boardTallySplit(shownScore - (g->score - shelfB - g->speedBonus - g->peelBonus), shelfB, g->speedBonus, g->peelBonus, shown);
    siprintf(s, "+%d", shown[0]);
    drawStatRow(lx, rx, cy + 93, ui(UI_R_SHELFBONUS), DIM_TEXT, s, GOLD_LT);
    if (g->speedBonus) siprintf(s, "+%d", shown[1]); else strcpy(s, "-");
    drawStatRow(lx, rx, cy + 102, ui(UI_R_SPEED), DIM_TEXT, s, g->speedBonus ? GOLD_LT : DIM_TEXT);
    if (g->passBonusKind) {
        siprintf(s, "+%d", shown[2]);
        u16 bc = g->passBonusKind == 2 ? GOLD_LT : ACCENT;
        drawStatRow(lx, rx, cy + 111, ui(g->passBonusKind == 2 ? UI_R_PERFECT : UI_R_CLEAN), bc, s, bc);
    } else drawStatRow(lx, rx, cy + 111, ui(UI_R_PEELBONUS), DIM_TEXT, "-", DIM_TEXT);
    if (g->mode == MODE_DAILY) {
        uiFmt(s, sizeof s, UI_R_FINAL, shownScore);
        if (!g->practice && S.dailyStreak >= 2) {
            char s2[48]; uiFmt(s2, sizeof s2, UI_R_FINALSTREAK, shownScore, S.dailyStreak);
            if (textWidth(s2, 1) <= CARD_FIT) strcpy(s, s2);          // a long score drops the streak: the title shows it
        }
        drawTextCenter(BX(cx), cy + 122, s, GOLD_LT, 1);
    } else { uiFmt(s, sizeof s, UI_R_SHELFOPEN, g->shelf + 1); drawTextCenter(BX(cx), cy + 122, s, ACCENT, 1); }
    if (g->practice) drawTextCenter(BX(cx), cy + 132, ui(UI_R_NOTSCORED), DIM_TEXT, 1);
    else if (g->newBest && (frame & 16)) drawTextCenter(BX(cx), cy + 132, ui(UI_R_NEWBEST), GOLD_LT, 1);
    if (t > CARD_GATE_PASSED && (frame & 32)) drawTextCenter(BX(cx), cy + 141, ui(UI_R_TAPCONT), INK, 1);
}

static void drawFailCard(const Game *g)
{
    int t = g->stateTimer, cy = cardTop(g), cx = CARD_CX, cw = CARD_W, ch = CARD_H;
    int lx = CARD_ROW_L, rx = CARD_ROW_R;
    overlayCard(BX(cx - cw / 2), cy, cw, ch);
    rectOutline(BX(cx - cw / 2), cy, cw, ch, (frame & 8) ? FIRE1 : FIRE2);
    rectOutline(BX(cx - cw / 2 + 2), cy + 2, cw - 4, ch - 4, lerp15(FIRE1, BLACK, 8));
    const char *title = boardFailName(g->fail);
    drawTextShadow(BX(cx - textWidth(title, 1) / 2), cy + 8, title, FIRE2, 1);
    const char *why = boardFailWhy(g->fail);
    drawTextCenter(BX(cx), cy + 20, why, INK, 1);
    drawPassDiamonds(g, cy + 34, true);
    char s[40];
    hline(BX(lx), cy + 54, cw - 20, DIM);
    siprintf(s, "%d", g->wordsThisShelf);
    drawStatRow(lx, rx, cy + 58, ui(UI_R_WORDS), DIM_TEXT, s, WHITE);
    drawBestWord(g, cy + 67);
    siprintf(s, "%d", g->peels);
    drawStatRow(lx, rx, cy + 85, ui(UI_PEELS), DIM_TEXT, s, WHITE);
    siprintf(s, "%d", g->turn);
    drawStatRow(lx, rx, cy + 94, ui(UI_TURNS), DIM_TEXT, s, WHITE);
    hline(BX(lx), cy + 103, cw - 20, DIM);
    bool runOver = g->mode == MODE_CLASSIC && g->lives <= 0;
    if (g->mode == MODE_CLASSIC && !runOver) {
        uiFmt(s, sizeof s, UI_R_RETRY, g->shelfStartScore);
        drawTextCenter(BX(cx), cy + 107, s, ACCENT, 1);
        if (g->lives == 1) strcpy(s, ui(UI_R_BM1)); else uiFmt(s, sizeof s, UI_R_BMN, g->lives);
        drawTextCenter(BX(cx), cy + 117, s, INK, 1);
    } else {
        uiFmt(s, sizeof s, UI_R_FINAL, g->score);
        drawTextCenter(BX(cx), cy + 107, s, GOLD_LT, 1);
        if (runOver) drawTextCenter(BX(cx), cy + 117, ui(UI_R_RUNOVER), FIRE2, 1);
        else if (g->practice) drawTextCenter(BX(cx), cy + 117, ui(UI_R_PRACTICENS), DIM_TEXT, 1);
    }
    if (g->newBest && (frame & 16)) drawTextCenter(BX(cx), cy + 132, ui(UI_R_NEWBEST), GOLD_LT, 1);
    if (t > CARD_GATE_FAIL && (frame & 32)) drawTextCenter(BX(cx), cy + 141, ui((g->mode == MODE_CLASSIC && !runOver) ? UI_R_TAPRETRY : UI_R_TAPTITLE), WHITE, 1);
}

// The pause menu: a card with one 16 px row per entry (RESUME, RESTART SHELF, HOW TO PLAY, QUIT TO TITLE).
// The highlighted row is boxed; RESTART and QUIT ask for a second tap within 90 frames.
#define PAUSE_X0 26
#define PAUSE_Y0 88
#define PAUSE_W 120
int gfxPauseRowAt(int x, int y, int rows)
{
    if (x < BX(PAUSE_X0) || x >= BX(PAUSE_X0 + PAUSE_W) || y < PAUSE_Y0) return -1;
    int r = (y - PAUSE_Y0) / 16;
    return r < rows ? r : -1;
}

static void drawPauseCard(const Game *g)
{
    int ids[PAUSE_ROWS_MAX], n = pauseRows(g, ids), ch = 22 + 16 * n + 2;
    overlayCard(BX(PAUSE_X0), 66, PAUSE_W, ch);
    rectOutline(BX(PAUSE_X0), 66, PAUSE_W, ch, ACCENT);
    drawTextCenter(BX(86), 74, ui(UI_PA_PAUSED), ACCENT, 1);
    bool refused = g->msgTimer > 0 && !strcmp(g->msg, ui(UI_MS_LASTBOOK));
    for (int i = 0; i < n; i++) {
        int y = PAUSE_Y0 + 16 * i, id = ids[i];
        bool sel = i == g->pauseRow, armed = g->pauseArm > 0 && g->pauseArmId == id;
        const char *label = ui(id == PAUSE_RESUME ? UI_PA_RESUME : id == PAUSE_RESTART ? UI_PA_RESTART : id == PAUSE_GUIDE ? UI_PA_GUIDE : UI_PA_QUIT);
        u16 col = sel ? WHITE : INK;
        if (id == PAUSE_RESTART && g->mode == MODE_CLASSIC && g->lives <= 1) {      // the last bookmark cannot be spent
            label = ui(refused ? UI_MS_LASTBOOK : UI_PA_RESTART); col = refused ? FIRE2 : DIM_TEXT;
        } else if (armed) { label = ui(id == PAUSE_QUIT ? UI_PA_AGAINQ : UI_PA_AGAINR); col = FIRE2; }
        if (sel) { fillRect(BX(PAUSE_X0 + 2), y, PAUSE_W - 4, 15, lerp15(BLACK, ACCENT, 4)); rectOutline(BX(PAUSE_X0 + 2), y, PAUSE_W - 4, 15, ACCENT); }
        drawTextCenter(BX(86), y + 4, label, col, 1);
    }
}

// the language chooser: the eleven names in a list like the options, the highlighted one boxed
#define PICK_Y0 22
#define PICK_DY 13
int gfxLangPickRowAt(int x, int y)
{
    if (x < 12 || x >= 160 || y < PICK_Y0) return -1;
    int r = (y - PICK_Y0) / PICK_DY;
    return r < LANG_COUNT ? r : -1;
}

static void drawLangPick(const Game *g)
{
    int savedX0 = boardX0;
    boardX0 = 0;
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoardDots();
    drawBoardFrame();
    boardX0 = savedX0;
    drawTextShadow(86 - textWidth(ui(UI_LP_TITLE), 2) / 2, 7, ui(UI_LP_TITLE), GOLD_LT, 2);
    for (int i = 0; i < LANG_COUNT; i++) {
        int y = PICK_Y0 + i * PICK_DY;
        bool on = i == g->langPick, ok = langAvailable(i);
        if (on) { fillRect(12, y, 148, PICK_DY - 1, lerp15(BLACK, ACCENT, 4)); rectOutline(12, y, 148, PICK_DY - 1, ACCENT); }
        drawText(86 - textWidth(langName(i), 1) / 2, y + 3, langName(i), on ? WHITE : ok ? INK : DIM2, 1);
        if (i == langCur()) drawChar(18, y + 3, '*', GOLD_LT, 1);
    }
    drawTextCenter(86, 172, ui(UI_LP_HINT), DIM_TEXT, 1);
}

static void drawOverlay(const Game *g)
{
    if (g->state == ST_PLAY) return;
    if (g->state == ST_TITLE) { drawTitle(g); return; }
    if (g->state == ST_OPTIONS) { drawOptions(g); return; }
    if (g->state == ST_STATS) { drawStats(g); return; }
    if (g->state == ST_LANGPICK) { drawLangPick(g); return; }
    if (g->state == ST_GUIDE) { drawGuide(g); return; }
    if (g->state == ST_PASSED) drawResultsCard(g);
    else if (g->state == ST_FAIL) drawFailCard(g);
    else if (g->state == ST_PAUSE) drawPauseCard(g);
}

// ---------------------------------------------------------------- frame

int gfxShownScore(void) { return shownScore; }
void gfxTipFlip(int dir) { tipIndex = (tipIndex + 3 + dir) % 3; tipTimer = 0; }
int gfxTipIndex(void) { return tipIndex; }

// The results tally. Outside the results card the HUD shows the score itself. On the card it starts from
// the score before the bonuses, waits for the diamonds to start lighting (frame 18), then chases the final
// score; the card's bonus rows count along with it. A state change snaps it, so a tap that leaves the card
// early lands on the right number. When it arrives, stars burst from the bonus row and the border flashes.
static void stepTally(const Game *g)
{
    if (cardFlash) cardFlash--;
    if (g->state != ST_PASSED) { shownScore = g->score; tallyPrev = g->state; return; }
    if (tallyPrev != ST_PASSED) {
        tallyPrev = ST_PASSED;
        shownScore = g->score - (boardShelfBonus(g->shelf) + g->speedBonus + g->peelBonus);
        if (shownScore < 0) shownScore = 0;
    }
    if (g->stateTimer >= 2 && g->stateTimer <= 12) {       // shelf-clear confetti in the theme's colours, raining over the strip
        const GemPal *th = THEMES[S.theme];
        for (int i = 0; i < (g->passBonusKind == 2 ? 5 : 3); i++)
            spawn(P_CONF, BX(6 + rand() % 160), -2 - rand() % 8, (rand() % 9) - 4, 10 + rand() % 12, 46 + rand() % 24, (rand() & 3) ? th[rand() % 5].light : GOLD_LT);
    }
    if (g->stateTimer >= 18 && shownScore < g->score) {
        shownScore = boardTallyNext(shownScore, g->score);
        if ((g->stateTimer & 3) == 0) musicReveal(false);
        if (shownScore == g->score) {
            cardFlash = 4;
            for (int i = 0; i < 12; i++)
                spawn(P_STAR, BX(CARD_CX + (i - 6) * 10), 22 + 111, (i - 6) * 3, -(rand() % 10) - 6, 18 + (i & 3) * 3, GOLD_LT);
        }
    }
}

// The LOADING screen: both displays drawn whole and flipped at once, because the load that follows blocks for
// a while (a language's three files come off the ROM filesystem). The next gfxFrame redraws the top screen.
void gfxLoading(const char *name)
{
    const int savedX0 = boardX0;
    boardX0 = 0;
    for (int pass = 0; pass < 2; pass++) {
        cur = pass ? vramSub : vramBack;
        clipY0 = 0; clipY1 = SCREEN_H;
        fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
        rectOutline(8, 8, 240, 176, GOLD_DK);
        rectOutline(9, 9, 238, 174, GOLD);
        drawTextCenter(128, 70, ui(UI_LOADING), GOLD_LT, 2);
        drawTextCenter(128, 94, name, INK, 1);
        for (int i = 0; i < 5; i++) drawGemSized(128 - 62 + i * 26, 118, 24, 22, &THEMES[S.theme][i], false);
    }
    boardX0 = savedX0;
    dimOn = false;
    swiWaitForVBlank();
    setBrightness(3, 0);
    bgSetScroll(bgMain, 0, 0);
    bgSetMapBase(bgMain, backBank ? 8 : 0);
    bgUpdate();
    { u16 *t = vramFront; vramFront = vramBack; vramBack = t; backBank ^= 1; }
    if (REG_BLDCNT) { REG_DISPCNT &= ~(DISPLAY_WIN0_ON | DISPLAY_WIN1_ON); REG_BLDCNT = 0; }
    lastTopKind = -1;                 // the title's top screen is relaid in full afterwards
    bright = 0;
}

void gfxInit(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);   // front/back bitmap pair for the touch screen
    vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
    vramSetBankC(VRAM_C_SUB_BG);
    bgMain = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vramSub = bgGetGfxPtr(bgs);
    lcdMainOnBottom();
    bright = -16;
    cur = vramBack;            // scratch renders (gem cache) need a valid target
    gfxApplySettings();
}

#define LINES_PER_FRAME 263
#define VBLANK_LINE 192
#define TOUCH_POLL_LINE 80
#define TOUCH_SLACK 14
#define LATE_FLIP_MAX 256                              // a draw that ends inside vblank up to this line still flips at once

// The iris: a hardware window on BG3 that opens from the screen centre with an ease-out, on both screens, so it costs the
// CPU nothing; outside the window the backdrop is black. A card's dim uses the same windows, so a dimmed state ends the iris.
#define REG_DISPCNT_SUB_ (*(vu32 *)0x04001000)
#define REG_WIN0H_SUB (*(vu16 *)0x04001040)
#define REG_WIN0V_SUB (*(vu16 *)0x04001044)
#define REG_WININ_SUB (*(vu16 *)0x04001048)
#define REG_WINOUT_SUB (*(vu16 *)0x0400104A)
static bool wipeWasOn;
static void applyWipe(bool dim)
{
    if (dim) { wipeT = 0; wipeWasOn = false; return; }
    if (wipeT > 0) {
        int p = WIPE_FRAMES - wipeT + 1;                       // 1..WIPE_FRAMES
        int r = 256 - 256 * p / WIPE_FRAMES;                   // 256 -> 0
        int e = 256 - (r * r * r) / 65536;                     // ease-out cubic, 0..256
        int hw = (128 * e) >> 8, hh = (96 * e) >> 8;
        if (hw < 2) hw = 2;
        if (hh < 2) hh = 2;
        int x0 = 128 - hw, x1 = 128 + hw; if (x1 > 255) x1 = 255;
        int y0 = 96 - hh, y1 = 96 + hh;
        REG_WIN0H = (x0 << 8) | x1; REG_WIN0V = (y0 << 8) | y1; WIN_IN = WIN_BG3; WIN_OUT = 0; REG_DISPCNT |= DISPLAY_WIN0_ON;
        REG_WIN0H_SUB = (x0 << 8) | x1; REG_WIN0V_SUB = (y0 << 8) | y1; REG_WININ_SUB = WIN_BG3; REG_WINOUT_SUB = 0; REG_DISPCNT_SUB_ |= DISPLAY_WIN0_ON;
        wipeT--; wipeWasOn = true;
    } else if (wipeWasOn) {
        REG_DISPCNT &= ~DISPLAY_WIN0_ON; REG_DISPCNT_SUB_ &= ~DISPLAY_WIN0_ON; wipeWasOn = false;
    }
}

// ---- dirty top-screen slices. A signature per 48-row slice hashes everything static the slice shows; a slice whose
// signature differs from what it last drew is redrawn at once (see gfxFrame). Animation (shimmer, blinks) is not in
// the signatures: the 15 Hz rotation owns it. Layout changes use the burst instead.
static u32 topDrawn[4], topSigNow[4];
static inline u32 hmix(u32 h, u32 v) { return (h ^ v) * 16777619u; }
static u32 hstr(u32 h, const char *t) { while (*t) h = hmix(h, (u8)*t++); return h; }
static void topSigs(const Game *g, u32 sig[4])
{
    bool play = !menuState(g->state);
    u32 h0 = 2166136261u, h1 = h0, h2 = h0, h3 = h0;
    if (!play) {                           // title, options, ledger: the tip card is the only thing that changes by itself
        h0 = hmix(h0, tipIndex + 1); h1 = hmix(h1, tipIndex + 1); h2 = hmix(h2, tipIndex + 1); h3 = hmix(h3, tipIndex + 1);
        h0 = hmix(h0, g->state); h1 = hmix(h1, g->state); h2 = hmix(h2, g->state); h3 = hmix(h3, g->state);
        h0 = hmix(h0, S.language + 1); h2 = hmix(h2, g->clockOk);
    } else {
        bool fireNext = fireLow >= 0 && g->burnEvery - g->burnTimer <= 1;
        h0 = hmix(h0, shownScore); h0 = hmix(h0, g->shelf); h0 = hmix(h0, g->mode); h0 = hmix(h0, g->lives);
        h0 = hmix(h0, g->twist + (shelfAge < 120 ? 100 : 0)); h0 = hmix(h0, g->practice + g->lockdown * 2 + g->dailyMod * 4);
        h0 = hmix(h0, fireNext);
        h1 = hmix(h1, g->passLen);
        for (int i = 0; i < g->passLen; i++) h1 = hmix(h1, g->found[i] + letterPop[i] * 2);
        h1 = hmix(h1, fireLow + 2); h2 = hmix(h2, fireLow + 2);
        h2 = hstr(h2, chainWordNow); h2 = hmix(h2, chainScoreNow); h2 = hmix(h2, chainValidNow); h2 = hmix(h2, invalidTimer);
        h2 = hstr(h2, g->lastWord); h2 = hmix(h2, g->lastWordScore); h2 = hmix(h2, g->ledgerN);
        h2 = hmix(h2, g->ledgerN ? g->ledgerPts[ledgerIndex(g, 0)] : 0);
        h3 = hmix(h3, g->shelf); h3 = hstr(h3, g->msg);
        h3 = hmix(h3, g->msgTimer > 46); h3 = hmix(h3, g->msgTimer != 0); h3 = hmix(h3, msgAge < 20 ? msgAge + 1 : 0);
    }
    topSigNow[0] = sig[0] = h0; topSigNow[1] = sig[1] = h1; topSigNow[2] = sig[2] = h2; topSigNow[3] = sig[3] = h3;
}

// May slice `sl` (rows 48 sl..48 sl + 47) be drawn right now without the beam seeing it half done? The beam is past
// every row during vblank; a late flip leaves less of it, and slice 0 is the first the next frame scans.
#define SLICE_LINES 28                                   // a slice costs at most ~19 scanlines (26k..41k ticks), with margin
static bool sliceFits(int sl)
{
    int v = REG_VCOUNT;
    int remain = v >= VBLANK_LINE ? LINES_PER_FRAME - v + 48 * sl : 48 * sl - v;   // lines until the beam reaches the slice's first row
    return remain >= SLICE_LINES;
}

// ---- late input sampling. The loop used to read the pad right after the flip, then spend the frame drawing; the
// picture the input produced came out one vblank later and the CPU idled in swiWaitForVBlank. Now the loop waits
// (gfxLateSample) until just enough of the frame is left for the work it did last frames (logic + draw, in scanlines,
// with a margin), reads the pad, draws and flips at vblank: the same picture, 5 to 7 ms younger input.
// A frame that runs past line 192 still flips if it is inside vblank (lines 192..235 leave room for the top slices).
static int workStart, workCost[8], workIdx, lateLines;
void gfxLateSample(void)
{
    int worst = 0;
    for (int i = 0; i < 8; i++) if (workCost[i] > worst) worst = workCost[i];
    int reserve = worst + worst / 4 + 16;
    int target = VBLANK_LINE - reserve;
    int pollLine = TOUCH_POLL_LINE + TOUCH_SLACK;       // never before the ARM7 has read the stylus (libnds polls it at line 80)...
    if (target < pollLine && pollLine + worst + worst / 6 <= LATE_FLIP_MAX - 8) target = pollLine;   // ...unless the frame would then end too late to flip in vblank
    if (target < 0) target = 0;
    if (REG_VCOUNT >= VBLANK_LINE) {                   // in vblank: let it finish, then run up to the sample line
        while (REG_VCOUNT >= VBLANK_LINE) {}
        while (REG_VCOUNT < target) {}
    }                                                   // already inside the visible frame: the previous frame ran long, go at once
    workStart = REG_VCOUNT;
}
static void lateNoteCost(void)
{
    int c = (int)REG_VCOUNT - workStart; if (c < 0) c += LINES_PER_FRAME;
    workCost[workIdx++ & 7] = c;
    lateLines = c;
}
// wait for vblank, unless the draw already finished inside it (lines 192..235)
static void waitFlipPoint(void)
{
    int v = REG_VCOUNT;
    if (v >= VBLANK_LINE && v <= LATE_FLIP_MAX) return;
    swiWaitForVBlank();
}

void gfxFrame(const Game *g)
{
    frame++;
    accent = S.accent ? SHELF_ACCENTS[(S.accent - 1) % (ACCENT_COUNT - 1)] : SHELF_ACCENTS[(g->shelf > 0 ? g->shelf - 1 : 0) % (ACCENT_COUNT - 1)];
    if (S.frame == FRAME_ACCENT && frameMid != accent) {   // the shelf accent changed
        frameMid = accent; frameDk = lerp15(accent, BLACK, 9); frameLt = lerp15(accent, WHITE, 8);
        buildHeatTables();
    }
    boardChainWord(g, chainWordNow);
    fireLow = boardLowestFireRow(g);
    dangerSetup(g);
    chainMask = 0;
    for (int i = 0; i < g->chainLen; i++) chainMask |= (u64)1 << (g->chainC[i] * ROWS + g->chainR[i]);
    chainValidNow = g->chainLen >= 3 && boardWordOk(chainWordNow, strlen(chainWordNow));
    chainScoreNow = chainValidNow ? boardChainScore(g) : 0;
    // per-letter pop timers for the pass plaque
    for (int i = 0; i < g->passLen; i++) {
        if (g->found[i] && !prevFound[i]) letterPop[i] = 24;
        prevFound[i] = g->found[i];
        if (letterPop[i]) letterPop[i]--;
    }
    if (g->state == ST_TITLE) memset(prevFound, 0, sizeof(prevFound));

    // ambient flame sparks
    if ((frame & 3) == 0 && g->state == ST_PLAY)
        for (int c = 0; c < COLS; c++)
            for (int r = 0; r < ROWS; r++)
                if (g->cell[c][r].state == CELL_BURN && (rand() & 1)) {
                    int x, y; cellOrigin(c, r, &x, &y);
                    spawn(P_SPARK, x + 3 + rand() % (TILE_W - 6), y + 1, (rand() % 5) - 2, -(rand() % 8) - 4, 10 + rand() % 8, (rand() & 1) ? FIRE2 : FIRE1);
                }
    if (g->state == ST_TITLE && (frame % 6) == 0) {    // gem rain behind the title band
        spawn(P_RAIN, BX(10 + rand() % 153), 0, 0, 10, 100, THEMES[S.theme][rand() % 5].light);
        rainAlive = true;
    } else if (rainAlive && g->state != ST_TITLE) {    // it stops with the title: nothing falls over the options or the board
        for (int i = 0; i < MAXP; i++) if (P[i].kind == P_RAIN) P[i].life = 0;
        rainAlive = false;
    }
    if (g->state == ST_GUIDE) {      // a new page restarts its loop, relays the whole top screen, dips the brightness for ~6 frames and sparks at its dot
        if (guidePageSeen < 0) for (int i = 0; i < MAXP; i++) P[i].life = 0;     // nothing left over from the board drifts across the guide
        if (g->guidePage != guidePageSeen) {
            if (guidePageSeen >= 0) {
                guideNavUsed = 1;
                for (int i = 0; i < 6; i++) spawn(P_SPARK, GD_DOT_C0 + 14 * g->guidePage + (i - 3), GD_NAV_Y + 8, (i - 3) * 3, -(i & 1) * 3 - 4, 12 + (i & 3) * 2, (i & 1) ? WHITE : GOLD_LT);
            }
            bright = -8;
            guidePageSeen = g->guidePage; guideBase = frame >> 2; guideAge = 0; topNeed = 0xF;
        } else if (guideAge < 600) guideAge++;
    } else guidePageSeen = -1;
    if (strcmp(g->msg, lastMsg)) { strcpy(lastMsg, g->msg); msgAge = 0; }    // a new prompt slides in; the same one just settles
    else if (msgAge < 100) msgAge++;
    stepTally(g);
    stepParticles();
    stepPopups();
    if (invalidTimer) invalidTimer--;
    if (nopeTimer) nopeTimer--;
    if (nudgeTimer) nudgeTimer--;
    if (g->state == ST_TITLE && ++tipTimer >= 420) { tipTimer = 0; tipIndex = (tipIndex + 1) % 3; }
    if (pressSubmit) pressSubmit--;
    if (pressClear) pressClear--;
    if (pressPeek) pressPeek--;
    if (pressPause) pressPause--;
    if (submitFlash) submitFlash--;
    if (callout.life) callout.life--;
    if (scoreGlint) scoreGlint--;
    if (passGlint) passGlint--;
    if (shelfAge < 1000) shelfAge++;

    PROF_START();
    cur = vramBack;            // draw the touch screen straight into the hidden VRAM bank
    clipY0 = 0; clipY1 = SCREEN_H;
    // The hardware dim is re-requested by overlayCard every frame it is wanted; a state
    // that draws no card (play) must not inherit it from the previous frame's card.
    dimOn = false;
    if (menuState(g->state)) {
        // menus paint their own black strip and frame: no board, no panel. Particles and
        // popups that outlive the game go on top of the menu, not under it.
        PROF(0); PROF(1); PROF(2); PROF(3);
        drawOverlay(g);   PROF(4);
        if (g->state != ST_TITLE) drawParticles();       // the title draws its own rain behind the band
        if (g->state != ST_GUIDE) drawPopups();
    } else if (g->state == ST_PLAY) {
        drawBackdrop(g);  PROF(0);
        drawBoard(g);     PROF(1);
        drawParticles();
        drawPopups();
        drawCallout(g);
        drawDanger(g);    PROF(2);
        drawPanel(g);     PROF(3);
        drawCues(g);      PROF(4);
    } else {
        // pause / results / fail: the live board (gems keep falling and flashing) under a
        // card. The strip is dimmed by the 2D engine at vblank, so this costs the same as
        // a play frame plus the card.
        drawBackdrop(g);  PROF(0);
        drawBoard(g);     PROF(1);
        drawPopups();
        drawCallout(g);   PROF(2);
        drawPanel(g);     PROF(3);
        drawOverlay(g);                 // the card, then the sparks and stars over it (the tally's burst)
        drawParticles();  PROF(4);
    }
    PROF(5);

    // fade and shake
    if (bright < 0) { bright += 4; if (bright > 0) bright = 0; }      // a fade-in is four frames (it was eight)
    else if (bright > 0) { bright -= 2; if (bright < 0) bright = 0; }
    int sx = 0, sy = 0;
    if (shakeFrames > 0) { shakeFrames--; sx = (rand() % 5) - 2; sy = (rand() % 5) - 2; }

    PROF_SET(8);
    lateNoteCost();
    waitFlipPoint();
    PROF_START();
    setBrightness(3, bright);
    bgSetScroll(bgMain, sx, sy);
    bgSetMapBase(bgMain, backBank ? 8 : 0);   // bitmap base in 16 KB units: bank B sits at +128 KB
    bgUpdate();
    { u16 *t = vramFront; vramFront = vramBack; vramBack = t; backBank ^= 1; }
    // Pause/results/fail: halve the brightness of the board strip in hardware. BG3 is the
    // first blend target of a brightness-decrease effect (EVY 8/16), window 0 cuts the
    // card out of it, and window 1 keeps the effect off the panel (window 1 covers the
    // strip right-handed and the panel left-handed, so neither window ends at x = 256).
    // The windows follow the shake scroll, which moves the bitmap under them.
    if (dimOn) {
        int x0 = dimCardX - sx, y0 = dimCardY - sy, x1 = x0 + dimCardW, y1 = y0 + dimCardH;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
        if (y1 > SCREEN_H) y1 = SCREEN_H;
        REG_WIN0H = (x0 << 8) | x1;
        REG_WIN0V = (y0 << 8) | y1;
        int w1 = (boardX0 ? boardX0 : BOARD_W) - sx;
        if (w1 < 1) w1 = 1;
        if (w1 > SCREEN_W - 1) w1 = SCREEN_W - 1;
        REG_WIN1H = w1;
        REG_WIN1V = SCREEN_H;
        WIN_IN  = WIN_BG3 | ((boardX0 ? WIN_BG3 : WIN_BG3_EFFECT) << 8);
        WIN_OUT = boardX0 ? WIN_BG3_EFFECT : WIN_BG3;
        REG_BLDCNT = BLEND_FADE_BLACK | BLEND_SRC_BG3;
        REG_BLDY = 8;
        REG_DISPCNT |= DISPLAY_WIN0_ON | DISPLAY_WIN1_ON;
    } else if (REG_BLDCNT) {
        REG_DISPCNT &= ~(DISPLAY_WIN0_ON | DISPLAY_WIN1_ON);
        REG_BLDCNT = 0;
    }
    applyWipe(dimOn);
    PROF(6);

    // The top screen HUD is drawn straight into VRAM_C while the beam is in vblank,
    // one 48-row slice per frame (every region still refreshes at 15 Hz). When the
    // layout changes (title/options <-> in-game) all four slices are redrawn, two per
    // frame, so the screen updates at once while each vblank stays well under budget.
    cur = vramSub;
    int kind = menuState(g->state) ? (g->state == ST_GUIDE ? 2 : 1) : 0;   // title/options/ledger, the guide, in-game
    if (kind != lastTopKind) { lastTopKind = kind; topNeed = 0xF; }
    // Slices whose content changed (the chain word, the score, a callout, the plaque) are drawn this very vblank,
    // before the 15 Hz rotation: the top screen then shows an event on the same frame as the touch screen. At most
    // two slices a frame (the budget a layout change already uses); the rotation slice rides along while any is dirty.
    int list[2], n = 0;
    u32 sig[4]; topSigs(g, sig);
    for (int sl = 0; sl < 4; sl++) if (sig[sl] != topDrawn[sl]) topNeed |= 1 << sl;
    for (int sl = 0; sl < 4 && n < 2; sl++) if ((topNeed & (1 << sl)) && sliceFits(sl)) list[n++] = sl;
    if (n < 2 && (n || topNeed == 0) && sliceFits(frame & 3) && !(n && list[0] == (frame & 3))) list[n++] = frame & 3;
    for (int i = 0; i < n; i++) {
        clipY0 = list[i] * 48; clipY1 = clipY0 + 48;
        drawTop(g);
        topDrawn[list[i]] = topSigNow[list[i]];
        topNeed &= ~(1 << list[i]);
    }
    clipY0 = 0; clipY1 = SCREEN_H;
    PROF_SET(7);
}
