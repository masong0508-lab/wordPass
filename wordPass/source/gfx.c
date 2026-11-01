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
static int lastTopKind = -1, topBurst;
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

#define C(r,g,b) ARGB16(1,(r),(g),(b))
#define BLACK      C(1,1,2)
#define INK        C(24,24,26)
static u16 frameMid = C(28,22,6), frameDk = C(16,11,2), frameLt = C(31,29,18);
#define GOLD       frameMid
#define GOLD_DK    frameDk
#define GOLD_LT    frameLt
static u16 accent = C(6,28,24);
#define ACCENT     accent
// one accent per shelf so the library changes colour as you climb
static const u16 SHELF_ACCENTS[ACCENT_COUNT - 1] = {
    C(6,28,24), C(28,8,24), C(16,30,6), C(31,16,4), C(8,20,31), C(20,10,31), C(31,12,18), C(10,31,16)
};
#define WHITE      C(31,31,31)
#define DIM        C(6,6,8)
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
      { C(6,8,1),   C(13,16,3),  C(24,28,10) } }
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

enum { P_SHARD = 0, P_SPARK, P_STAR };
typedef struct { s16 x, y, vx, vy; u8 life, maxLife, kind; u16 col; } Particle;   // 1/8 px units
#define MAXP 112
static DTCM_BSS Particle P[MAXP];

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
        q->x += q->vx; q->y += q->vy;
        if (q->kind == P_SHARD) q->vy += 3;
        if (q->kind == P_SPARK) q->vx += (rand() % 5) - 2;
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

int gfxCellAt(int pxl, int pyl, int *c, int *r)
{
    int cc = (pxl - gridX) / TILE_W;
    if (pxl < gridX || cc < 0 || cc >= COLS) return 0;
    int yy = pyl - GRID_Y - ((cc & 1) ? ODD_SHIFT : 0);
    if (yy < 0) return 0;
    int rr = yy / TILE_H;
    if (rr >= ROWS) return 0;
    int lx = (pxl - gridX) % TILE_W, ly = yy % TILE_H;
    if (lx < 3 || lx > TILE_W - 4 || ly < 3 || ly > TILE_H - 4) return 0;
    *c = cc; *r = rr;
    return 1;
}

#define PANEL_X panelX
#define SUBMIT_Y 120
#define CLEAR_Y 150
bool gfxInSubmit(int x, int y) { return x >= PANEL_X && x < PANEL_X + 76 && y >= SUBMIT_Y && y < SUBMIT_Y + 24; }
bool gfxInClear(int x, int y)  { return x >= PANEL_X && x < PANEL_X + 76 && y >= CLEAR_Y && y < CLEAR_Y + 20; }

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
    } else if (kind == BURST_REVEAL) {
        for (int i = 0; i < 16; i++) {
            int a = i * 4;  // 16 directions
            static const s8 dirx[16] = {8,7,6,3,0,-3,-6,-7,-8,-7,-6,-3,0,3,6,7};
            static const s8 diry[16] = {0,3,6,7,8,7,6,3,0,-3,-6,-7,-8,-7,-6,-3};
            spawn(P_STAR, cx, cy, dirx[a / 4] * 2, diry[a / 4] * 2, 16 + (i & 3) * 2, (i & 1) ? WHITE : GOLD_LT);
        }
        for (int i = 0; i < 8; i++) spawn(P_SPARK, cx + (rand() % 16) - 8, cy, 0, -(rand() % 8) - 2, 20, WHITE);
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

void gfxFadeIn(void)  { bright = -16; }
void gfxFlash(void)   { bright = 10; }
void gfxShake(int frames) { shakeFrames = frames; }
void gfxInvalidWord(void) { invalidTimer = 24; }

// title buttons on the touch screen
#define TB_Y 134
bool gfxInTitleClassic(int x, int y) { return x >= BX(20) && x < BX(84) && y >= TB_Y && y < TB_Y + 22; }
bool gfxInTitleDaily(int x, int y)   { return x >= BX(88) && x < BX(152) && y >= TB_Y && y < TB_Y + 22; }
bool gfxInTitleOptions(int x, int y) { return x >= BX(54) && x < BX(118) && y >= TB_Y + 26 && y < TB_Y + 46; }

// options list rows on the touch screen
#define OPT_Y0 36
#define OPT_DY 12
int gfxOptionRowAt(int x, int y)
{
    if (x < BX(12) || x >= BX(160)) return -1;
    if (y >= 160 && y < 180) return OPT_ROWS;          // BACK
    int row = (y - OPT_Y0) / OPT_DY;
    if (y < OPT_Y0 || row < 0 || row >= OPT_ROWS) return -1;
    return row;
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
    if (g->state != ST_PLAY) return;
    int low = boardLowestFireRow(g);
    if (low < 4) return;
    int strength = (low - 3) * 4 + ((frame & 16) ? 2 : 0);   // 4..18
    for (int j = 0; j < DANGER_ROWS; j++) {
        int a = (strength * (DANGER_ROWS - j)) / DANGER_ROWS;
        if (a <= 0) break;
        dangerA[j] = (u8)(a > 12 ? 12 : a);
        dangerRows = j + 1;
    }
}

// N+ dot grid behind the gems (dots under solid gems are hidden anyway)
static ITCM_CODE __attribute__((noinline)) void drawBoardDots(void)
{
    u16 dot = lerp15(BLACK, ACCENT, 3);
    for (int y = 6; y < SCREEN_H; y += 8) {
        if (y < clipY0 || y >= clipY1) continue;
        u16 *row = cur + y * SCREEN_W + boardX0;
        for (int x = 6; x < 172; x += 8) row[x] = dot;
    }
}

// gold frame around the board strip, with a shimmer running along the top edge
static void drawBoardFrame(void)
{
    rectOutline(BX(0), 0, BOARD_W, SCREEN_H, GOLD_DK);
    rectOutline(BX(1), 1, BOARD_W - 2, SCREEN_H - 2, GOLD);
    int sh = (frame * 2) % 240 - 20;
    for (int i = 0; i < 14; i++) { px(BX(1 + sh + i), 1, GOLD_LT); px(BX(1 + sh + i), 0, GOLD); }
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
                case CELL_HOLE: {
                    bool sc = g->scorched[c][r];
                    rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, sc ? C(12,4,0) : ACCENT);
                    rectOutline(x + 3, y + 3, TILE_W - 6, TILE_H - 6, DIM2);
                    int n = boardBackNumber(g, c, r);
                    char s[2] = { '0' + n, 0 };
                    drawChar(x + 10, y + 8, s[0], n ? ACCENT : DIM, 1);
                    if (n) drawChar(x + 10, y + 8, s[0], (frame & 16) ? ACCENT : lerp15(ACCENT, WHITE, 8), 1);
                    if (chimney) rectOutline(x, y, TILE_W, TILE_H, FIRE1);
                    break;
                }
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
                    int pulse = ph < 8 ? ph : 15 - ph;
                    GemPal firePal = GEM_FIRE;
                    firePal.mid = lerp15(C(31,4,0), C(31,14,0), pulse * 2);
                    drawGemSized(x, yd + sq, TILE_W, TILE_H - sq, &firePal, lit);
                    drawLetter(x, yd + sq / 2, cl->letter, WHITE, C(14,2,0));
                    // pulsing red rim and flame tongues licking over the top edge
                    rectOutline(x, yd, TILE_W, TILE_H, lerp15(C(20,2,0), FIRE2, pulse * 2));
                    for (int i = 0; i < 3; i++) {
                        int fx = x + 5 + i * 7 + ((ph + i * 5) & 3), fh = 3 + ((ph + i * 3) & 3);
                        for (int j = 0; j < fh; j++) px(fx, yd - j, j == fh - 1 ? FIRE2 : lerp15(FIRE1, FIRE2, j * 4));
                        px(fx + 1, yd - 1, FIRE1);
                    }
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
                        drawGemOutline(x, yd);
                        drawChar(x + 10, yd + 8, 'A' + cl->letter, DIM, 1);
                        if (cl->letter == 'q' - 'a') drawChar(x + 15, yd + 8, 'U', DIM, 1);
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
        }
        clipY0 = outerClip;
    }
    // chain links: gold rope that turns to the shelf accent once the word is valid
    bool chainValid = chainValidNow;
    u16 ropeDk = chainValid ? lerp15(ACCENT, BLACK, 8) : GOLD_DK, ropeLt = chainValid ? ACCENT : GOLD_LT;
    for (int i = 1; i < g->chainLen; i++) {
        int x0, y0, x1, y1;
        cellOrigin(g->chainC[i - 1], g->chainR[i - 1], &x0, &y0);
        cellOrigin(g->chainC[i], g->chainR[i], &x1, &y1);
        x0 += TILE_W / 2; y0 += TILE_H / 2 - 2; x1 += TILE_W / 2; y1 += TILE_H / 2 - 2;
        for (int t = 0; t <= 10; t++) {
            int xx = x0 + (x1 - x0) * t / 10, yy = y0 + (y1 - y0) * t / 10;
            fillRect(xx - 2, yy - 2, 4, 4, ropeDk);
            fillRect(xx - 1, yy - 1, 2, 2, ((t + (frame >> 1)) % 5 == 0) ? WHITE : ropeLt);
        }
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

static ITCM_CODE __attribute__((noinline)) void drawPanel(const Game *g)
{
    drawBoardFrame();

    if (S.hand == HAND_LEFT) fillRect(0, 0, boardX0, SCREEN_H, BLACK);
    else fillRect(PANEL_X - 2, 0, SCREEN_W - PANEL_X + 2, SCREEN_H, BLACK);
    drawLogo(PANEL_X, 6, 1);
    char s[32];
    fmtInt(s, g->score);
    if (textWidth(s, 2) <= 80) drawTextShadow(PANEL_X, 22, s, GOLD, 2);   // 80 px of panel: six digits at 2x
    else drawTextShadow(PANEL_X, 26, s, GOLD, 1);                           // a million and up drops to 1x
    strcpy(s, "SHELF "); fmtInt(s + 6, g->shelf);
    drawText(PANEL_X, 42, s, INK, 1);
    if (g->mode == MODE_DAILY) drawText(PANEL_X, 52, "DAILY", GOLD_LT, 1);
    else if (!g->lockdown) drawText(PANEL_X, 52, "CLASSIC", ACCENT, 1);
    else {
        static const char *const TIER[4] = { "LOCKDOWN", "LOCKDOWN II", "LOCKDOWN III", "LOCKDOWN IV" };
        drawText(PANEL_X, 52, TIER[g->lockTier & 3], FIRE1, 1);   // 71 px at most: inside the 76 px panel
    }

    char w[WORD_BUF]; strcpy(w, chainWordNow);
    for (char *p = w; *p; p++) *p -= 32;
    bool valid = chainValidNow;
    if (strlen(w) > 12) { memmove(w + 1, w + strlen(w) - 11, 12); w[0] = '.'; }   // the last 11 letters fit the panel
    drawText(PANEL_X, 70, w, valid ? GOLD_LT : WHITE, 1);
    if (g->chainLen >= 3 && !g->peekHeld) drawText(PANEL_X, 82, valid ? "VALID" : "...", valid ? ACCENT : DIM, 1);

    // heat: small flame glyph grows with the bar
    drawText(PANEL_X, 98, "HEAT", DIM, 1);
    int hw = HEAT_W * g->heat / (g->heatThreshold ? g->heatThreshold : 1);
    if (hw > HEAT_W) hw = HEAT_W;
    rectOutline(PANEL_X, 108, 72, 6, DIM);
    {   // rows 109..112, precomputed gradient (the panel is always inside the touch screen)
        u16 *row = cur + 109 * SCREEN_W + PANEL_X + 1;
        for (int i = 0; i < hw; i++) { row[i] = heatTop[i]; row[SCREEN_W + i] = heatBody[i]; row[2 * SCREEN_W + i] = heatBody[i]; row[3 * SCREEN_W + i] = heatBody[i]; }
    }
    if (hw > 50 && (frame & 8)) drawText(PANEL_X + 30, 98, "!", FIRE2, 1);

    // submit button: jeweled, pulses when a valid word is chained
    u16 bc = valid ? lerp15(GOLD_DK, GOLD, ((frame & 31) < 16) ? (frame & 15) : 15 - (frame & 15)) : DIM;
    roundRect(PANEL_X, SUBMIT_Y, 76, 24, bc);
    if (valid) { hline(PANEL_X + 3, SUBMIT_Y + 2, 70, GOLD_LT); hline(PANEL_X + 2, SUBMIT_Y + 21, 72, GOLD_DK); }
    drawTextCenter(PANEL_X + 38, SUBMIT_Y + 8, "SUBMIT", valid ? BLACK : INK, 1);
    rectOutline(PANEL_X, CLEAR_Y, 76, 20, DIM);
    drawTextCenter(PANEL_X + 38, CLEAR_Y + 6, "CLEAR", INK, 1);

    if (g->peekHeld) {
        fillRect(PANEL_X, 176, 76, 12, ACCENT);
        drawTextCenter(PANEL_X + 38, 178, "PEEK", BLACK, 1);
    } else {
        drawTextCenter(PANEL_X + 38, 178, "HOLD L:PEEK", DIM, 1);
    }
}

// ---------------------------------------------------------------- top screen
// Everything below draws on the top screen, which is never mirrored for the left hand:
// never use BX() here.

static void drawTipCard(int which);

static void drawTopTitle(void)
{
    rectOutline(8, 8, 240, 176, GOLD_DK);
    rectOutline(9, 9, 238, 174, GOLD);
    drawLogo(16, 16, 2);
    drawText(170, 18, "BOOKWORM", INK, 1);
    drawText(170, 28, "X MINESWEEPER", INK, 1);
    if (G.state == ST_OPTIONS) {
        drawTextCenter(128, 60, "MAKE IT YOURS", GOLD_LT, 1);
        drawTextCenter(128, 78, "THEME CHANGES THE GEMS", INK, 1);
        drawTextCenter(128, 88, "FRAME AND ACCENT CHANGE THE LIBRARY", INK, 1);
        drawTextCenter(128, 98, "HAND MIRRORS THE BOARD FOR LEFTIES", INK, 1);
        drawTextCenter(128, 108, "FIRE SETS HOW FAST THE SHELF BURNS", INK, 1);
        drawTextCenter(128, 118, "LIBRARY: COMMON REJECTS OBSCURE WORDS", INK, 1);
        drawTextCenter(128, 128, "SPELLING: COLOR OR COLOUR, OR BOTH", INK, 1);
        drawTextCenter(128, 140, "TAP A ROW OR USE THE D-PAD", DIM, 1);
        char s[40];
        if (S.bestClassic) { siprintf(s, "BEST CLASSIC %d  SHELF %d", S.bestClassic, S.bestClassicShelf); drawTextCenter(128, 156, s, DIM, 1); }
        if (S.bestDaily)   { siprintf(s, "BEST DAILY %d ON %d", S.bestDaily, S.bestDailyDate); drawTextCenter(128, 166, s, DIM, 1); }
        return;
    }
    drawTipCard((frame / 150) % 3);
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

static void drawTop(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    if (g->state == ST_TITLE || g->state == ST_OPTIONS) {
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
    if (g->mode == MODE_DAILY) { strcpy(s, "DAILY "); fmtInt(s + 6, g->dailySeed % 10000); }   // MMDD
    else { strcpy(s, "SHELF "); fmtInt(s + 6, g->shelf); }
    drawText(170, 18, s, INK, 1);
    drawText(170, 28, fmtInt(s, g->score), GOLD, 1);

    // the pass: gem-cut blanks that pop when found
    int n = g->passLen, bw = PLAQUE_W, gap = 4;
    int x0 = ((SCREEN_W - (n * bw + (n - 1) * gap)) / 2) & ~1, y0 = 60;   // x even: word-aligned blits; the popped gem spans y 50..95, inside HUD slice 1
    strcpy(s, "PASS - "); strcpy(s + 7, boardPassTheme(g));
    drawTextCenter(SCREEN_W / 2, 50, s, DIM, 1);
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
        if (wl <= 13) {            // 3x letters, 18 px apart, up to 13 across
            int cx3 = SCREEN_W / 2 - textWidth(w, 3) / 2 + jx;
            for (const char *p = w; *p; p++, cx3 += 18) drawChar3Shadow(cx3, 100, *p, wc, DIM2);
        } else {                   // longer words drop to 2x, 12 px apart: 20 letters take 240 px
            int cx2 = SCREEN_W / 2 - textWidth(w, 2) / 2 + jx;
            for (const char *p = w; *p; p++, cx2 += 12) drawChar2Shadow(cx2, 104, *p, wc, DIM2);
        }
    } else if (g->lastWord[0]) {
        char lw[MAX_WORD + 1]; strcpy(lw, g->lastWord);
        for (char *p = lw; *p; p++) *p -= 32;
        drawTextCenter(SCREEN_W / 2, 100, lw, INK, textWidth(lw, 2) <= 224 ? 2 : 1);
        s[0] = '+'; fmtInt(s + 1, g->lastWordScore);
        drawTextCenter(SCREEN_W / 2, 118, s, GOLD, 1);
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
    drawText(222, 46, "FIRE", DIM, 1);
    rectOutline(226, 54, 8, ROWS * 10 + 2, DIM);   // rows 54..135: clear of the ledger line below
    int low = boardLowestFireRow(g);
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
            drawText(x, 137, lw, age == 0 ? WHITE : lerp15(INK, DIM, age * 4), 1);
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
        drawTextShadow(SCREEN_W / 2 - textWidth(g->msg, sc) / 2, sc == 2 ? 162 : 166, g->msg, g->msgTimer > 0 ? GOLD_LT : INK, sc);
    }
}

// ---------------------------------------------------------------- overlays

static void drawTitle(const Game *g)
{
    (void)g;
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoardDots();
    drawBoardFrame();
    // the logo as a row of gems
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
    fillRect(BX(16), 72, 140, 56, BLACK);
    rectOutline(BX(16), 72, 140, 56, GOLD);
    drawTextCenter(BX(86), 80, "CRACK THE PASS", ACCENT, 1);
    drawTextCenter(BX(86), 92, "DON'T BURN THE LIBRARY", INK, 1);
    drawTextCenter(BX(86), 108, "DRAG SPELLS. L PEEKS", DIM, 1);
    // mode buttons
    bool blink = frame & 32;
    roundRect(BX(20), TB_Y, 64, 22, blink ? GOLD : GOLD_DK);
    drawTextCenter(BX(52), TB_Y + 7, "CLASSIC", BLACK, 1);
    roundRect(BX(88), TB_Y, 64, 22, DIM);
    rectOutline(BX(88), TB_Y, 64, 22, GOLD_DK);
    drawTextCenter(BX(120), TB_Y + 7, "DAILY", GOLD_LT, 1);
    fillRect(BX(54), TB_Y + 26, 64, 20, BLACK);
    rectOutline(BX(54), TB_Y + 26, 64, 20, DIM);
    drawTextCenter(BX(86), TB_Y + 32, "OPTIONS", INK, 1);
    char s[32];
    if (S.bestClassic) { siprintf(s, "BEST %d", S.bestClassic); drawText(BX(20), 180, s, DIM, 1); }
    if (S.bestDaily)   { siprintf(s, "DAILY %d", S.bestDaily); drawText(BX(152) - textWidth(s, 1), 180, s, DIM, 1); }
}

static const char *const OPT_NAMES[OPT_ROWS] = { "THEME", "FRAME", "ACCENT", "HAND", "FIRE", "MUSIC", "LIBRARY", "SPELLING" };
static const char *const LIB_NAMES[LIB_COUNT] = { "FULL", "COMMON" };
static const char *const SPELL_NAMES[SPELL_COUNT] = { "BOTH", "AMERICAN", "BRITISH" };
static const char *const THEME_NAMES[THEME_COUNT] = { "JEWEL", "INK", "CANDY", "EMBER" };
static const char *const FRAME_NAMES[FRAME_COUNT] = { "GOLD", "SILVER", "ACCENT" };
static const char *const ACCENT_NAMES[ACCENT_COUNT] = { "AUTO", "TEAL", "MAGENTA", "LIME", "ORANGE", "SKY", "VIOLET", "PINK", "MINT" };
static const char *const FIRE_NAMES[FIRE_COUNT] = { "RELAXED", "NORMAL", "FAST" };

static void drawOptions(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoardDots();
    drawBoardFrame();
    drawTextShadow(BX(86) - textWidth("OPTIONS", 2) / 2, 14, "OPTIONS", GOLD_LT, 2);
    int sel = g->optRow;
    for (int i = 0; i < OPT_ROWS; i++) {
        int y = OPT_Y0 + i * OPT_DY;
        const char *val = "";
        switch (i) {
            case 0: val = THEME_NAMES[S.theme]; break;
            case 1: val = FRAME_NAMES[S.frame]; break;
            case 2: val = ACCENT_NAMES[S.accent]; break;
            case 3: val = S.hand ? "LEFT" : "RIGHT"; break;
            case 4: val = FIRE_NAMES[S.fire]; break;
            case 5: val = S.music ? "ON" : "OFF"; break;
            case 6: val = LIB_NAMES[S.library]; break;
            case 7: val = SPELL_NAMES[S.spelling]; break;
        }
        if (i == sel) { fillRect(BX(12), y - 2, 148, OPT_DY - 1, lerp15(BLACK, ACCENT, 4)); rectOutline(BX(12), y - 2, 148, OPT_DY - 1, ACCENT); }
        drawText(BX(18), y + 2, OPT_NAMES[i], i == sel ? WHITE : INK, 1);
        char v[16]; siprintf(v, "< %s >", val);
        drawText(BX(154) - textWidth(v, 1), y + 2, v, i == sel ? ACCENT : INK, 1);
    }
    // swatches: the five letter gems in the current theme
    for (int i = 0; i < 5; i++) { blitGem(BX(16) + i * 28, 136, i); drawLetter(BX(16) + i * 28, 136, "AIRTS"[i] - 'A', WHITE, GEM_PALS[i]->dark); }
    rectOutline(BX(54), 160, 64, 20, DIM);
    drawTextCenter(BX(86), 166, "BACK", INK, 1);
    drawTextCenter(BX(86), 182, settingsPersistent() ? "SAVED TO CARD" : "NOT SAVED: NO CARD", DIM, 1);
}

// Title top screen: three tip cards that cycle. This draws on the TOP screen, which has
// no hand offset: never use BX() here (it would push the card off the right edge in the
// left-hand layout).
static void drawTipCard(int which)
{
    const GemPal *th = THEMES[S.theme];
    int cx = 128, y = 52;
    if (which == 0) {
        drawTextCenter(cx, y, "SPELL", GOLD_LT, 1);
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
        drawTextCenter(cx, y + 56, "DRAG ACROSS TOUCHING GEMS", INK, 1);
        drawTextCenter(cx, y + 66, "THREE LETTERS OR MORE, THEN SUBMIT", INK, 1);
        drawTextCenter(cx, y + 80, "LONG WORDS SCORE BIG AND KEEP HEAT DOWN", DIM, 1);
    } else if (which == 1) {
        drawTextCenter(cx, y, "PEEK", ACCENT, 1);
        int gx = cx - 40;
        for (int i = 0; i < 3; i++) {
            int x = gx + i * 28, gy = y + 14 + ((i & 1) ? 10 : 0);
            if (i == 0) { rectOutline(x + 1, gy + 1, TILE_W - 2, TILE_H - 2, ACCENT); drawChar(x + 10, gy + 8, '2', ACCENT, 1); }
            else if (i == 1) { drawGemSized(x, gy, TILE_W, TILE_H, &GEM_DIAMOND, false); drawChar(x + 7, gy + 4, 'B', C(4,6,10), 2); }
            else { rectOutline(x + 1, gy + 1, TILE_W - 2, TILE_H - 2, DIM); drawChar(x + 10, gy + 8, 'X', DIM, 1); }
        }
        drawTextCenter(cx, y + 56, "HOLD L AND TAP A GEM YOU CAN SPARE", INK, 1);
        drawTextCenter(cx, y + 66, "A NUMBER COUNTS HIDDEN PASS LETTERS", INK, 1);
        drawTextCenter(cx, y + 76, "IN THE SIX CELLS AROUND IT", INK, 1);
        drawTextCenter(cx, y + 94, "EVERY PEEL IS A TURN", DIM, 1);
    } else {
        drawTextCenter(cx, y, "FIRE", FIRE2, 1);
        int gx = cx - 40;
        GemPal firePal = GEM_FIRE; firePal.mid = (frame & 8) ? C(31,8,0) : C(31,14,0);
        int fx = gx, fy = y + 14;   // the burning gem; the tongues above share its base
        drawGemSized(fx, fy, TILE_W, TILE_H, &firePal, false); drawChar(fx + 7, fy + 4, 'R', WHITE, 2);
        rectOutline(fx + 1, fy + TILE_H + 1, TILE_W - 2, TILE_H - 2, DIM);
        for (int j = 0; j < 3; j++) px(fx + 6 + j * 6, fy - 2 - ((frame >> 2) + j) % 3, FIRE2);
        drawGemSized(gx + 28, y + 24, TILE_W, TILE_H, &GEM_DIAMOND, false); drawChar(gx + 35, y + 28, 'O', C(4,6,10), 2);
        drawGemSized(gx + 56, y + 14, TILE_W, TILE_H, &th[TH_EMERALD], false); drawChar(gx + 63, y + 18, 'P', WHITE, 2);
        drawTextCenter(cx, y + 62, "SHORT WORDS BUILD HEAT AND LIGHT A GEM", INK, 1);
        drawTextCenter(cx, y + 72, "FIRE WAITS A TURN, THEN CREEPS DOWN.", INK, 1);
        drawTextCenter(cx, y + 82, "HOLES ARE CHIMNEYS. DIAMONDS BLOCK IT", INK, 1);
        drawTextCenter(cx, y + 96, "SPELL THROUGH A FLAME TO PUT IT OUT", DIM, 1);
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

static void drawOverlay(const Game *g)
{
    if (g->state == ST_PLAY) return;
    if (g->state == ST_TITLE) { drawTitle(g); return; }
    if (g->state == ST_OPTIONS) { drawOptions(g); return; }
    if (g->state == ST_PASSED) {
        // results card slides up from the bottom and settles
        int t = g->stateTimer;
        int slide = t < 12 ? (12 - t) * (12 - t) : 0;
        int cx = 86, cy = 28 + slide, cw = 150, ch = 140;
        overlayCard(BX(cx - cw / 2), cy, cw, ch);
        rectOutline(BX(cx - cw / 2), cy, cw, ch, GOLD_LT);
        rectOutline(BX(cx - cw / 2 + 2), cy + 2, cw - 4, ch - 4, GOLD_DK);
        char title[32];
        if (g->mode == MODE_DAILY) strcpy(title, "DAILY PASS DONE");
        else if (g->twist) siprintf(title, "PASS ACCEPTED: %s", boardTwistName(g->twist));   // 24 chars at most: 143 px
        else strcpy(title, "PASS ACCEPTED");
        drawTextShadow(BX(cx - textWidth(title, 1) / 2), cy + 8, title, GOLD_LT, 1);
        // the pass itself as small diamonds
        int n = g->passLen, bw = 14, gap = 2, x0 = cx - (n * bw + (n - 1) * gap) / 2;
        for (int i = 0; i < n; i++) {
            int lx = x0 + i * (bw + gap);
            drawGemSized(BX(lx), cy + 20, bw, 16, &GEM_DIAMOND, (t / 4) % (n + 4) == i);
            drawChar(BX(lx + 5), cy + 24, 'A' + (g->pass[i] - 'a'), C(4,6,10), 1);
        }
        // rows at a 9 px pitch: label DIM on the left, value right-aligned
        char s[32], lw[MAX_WORD + 1];
        int lx = cx - cw / 2 + 10, rx = cx + cw / 2 - 10;
        hline(BX(lx), cy + 41, cw - 20, DIM);
        strcpy(lw, g->bestWord[0] ? g->bestWord : "-");
        for (char *p = lw; *p; p++) if (*p >= 'a') *p -= 32;
        drawText(BX(lx), cy + 46, "BEST WORD", DIM, 1);
        if (strlen(lw) > 14) { lw[13] = '.'; lw[14] = 0; }    // a long best word keeps the label readable
        drawText(BX(rx - textWidth(lw, 1)), cy + 46, lw, WHITE, 1);
        if (g->bestWordScore) { siprintf(s, "+%d", g->bestWordScore); drawText(BX(rx - textWidth(s, 1)), cy + 55, s, GOLD, 1); }
        else drawText(BX(rx - textWidth("-", 1)), cy + 55, "-", DIM, 1);
        drawText(BX(lx), cy + 64, "WORDS", DIM, 1);
        if (g->uniqueWords < g->wordsThisShelf) siprintf(s, "%d (%d UNIQUE)", g->wordsThisShelf, g->uniqueWords);
        else siprintf(s, "%d", g->wordsThisShelf);
        drawText(BX(rx - textWidth(s, 1)), cy + 64, s, WHITE, 1);
        drawText(BX(lx), cy + 73, "PEELS", DIM, 1); siprintf(s, "%d", g->peels);
        u16 pc = g->peels == g->passLen ? GOLD_LT : g->peels <= g->passLen + 2 ? ACCENT : WHITE;
        drawText(BX(rx - textWidth(s, 1)), cy + 73, s, pc, 1);
        drawText(BX(lx), cy + 82, "TURNS", DIM, 1); siprintf(s, "%d", g->turn);
        drawText(BX(rx - textWidth(s, 1)), cy + 82, s, WHITE, 1);
        hline(BX(lx), cy + 90, cw - 20, DIM);
        drawText(BX(lx), cy + 92, "SHELF BONUS", DIM, 1); siprintf(s, "+%d", 1000 * g->shelf);
        drawText(BX(rx - textWidth(s, 1)), cy + 92, s, GOLD_LT, 1);
        drawText(BX(lx), cy + 101, "SPEED", DIM, 1);
        if (g->speedBonus) { siprintf(s, "+%d", g->speedBonus); drawText(BX(rx - textWidth(s, 1)), cy + 101, s, GOLD_LT, 1); }
        else drawText(BX(rx - textWidth("-", 1)), cy + 101, "-", DIM, 1);
        if (g->passBonusKind == 2) {
            drawText(BX(lx), cy + 110, "PERFECT PASS", GOLD_LT, 1); siprintf(s, "+%d", g->peelBonus);
            drawText(BX(rx - textWidth(s, 1)), cy + 110, s, GOLD_LT, 1);
        } else if (g->passBonusKind == 1) {
            drawText(BX(lx), cy + 110, "CLEAN PASS", ACCENT, 1); siprintf(s, "+%d", g->peelBonus);
            drawText(BX(rx - textWidth(s, 1)), cy + 110, s, ACCENT, 1);
        } else {
            drawText(BX(lx), cy + 110, "PEELS", DIM, 1);
            drawText(BX(rx - textWidth("-", 1)), cy + 110, "-", DIM, 1);
        }
        if (g->mode == MODE_DAILY) { siprintf(s, "FINAL %d", g->score); drawTextCenter(BX(cx), cy + 120, s, GOLD_LT, 1); }
        else { siprintf(s, "SHELF %d OPEN", g->shelf + 1); drawTextCenter(BX(cx), cy + 120, s, ACCENT, 1); }
        if (t > 30 && (frame & 32)) drawTextCenter(BX(cx), cy + 130, "TAP TO CONTINUE", INK, 1);
    } else if (g->state == ST_FAIL) {
        overlayCard(BX(6), 66, 162, 60);
        rectOutline(BX(6), 66, 162, 60, (frame & 8) ? FIRE1 : FIRE2);
        const char *t = g->fail == FAIL_FIRE ? "LIBRARY LOST" : g->fail == FAIL_SCORCH ? "PASS DESTROYED" : "OUT OF WORDS";
        drawTextShadow(BX(86 - textWidth(t, 1) / 2), 76, t, FIRE2, 1);
        const char *why = g->fail == FAIL_FIRE ? "FIRE REACHED THE FLOOR" : g->fail == FAIL_SCORCH ? "A PASS LETTER BURNED" : "NO WORDS LEFT TO SPELL";
        drawTextCenter(BX(86), 90, why, INK, 1);
        if (frame & 32) {
            char s[32];
            if (g->mode == MODE_DAILY) strcpy(s, "TAP FOR TITLE");
            else if (g->lives <= 0) strcpy(s, "RUN OVER - TAP FOR TITLE");
            else siprintf(s, "TAP TO RETRY (%d BOOKMARK%s)", g->lives, g->lives == 1 ? "" : "S");   // 26 chars: 155 px inside the 162 px card
            drawTextCenter(BX(86), 110, s, WHITE, 1);
        }
    } else if (g->state == ST_PAUSE) {
        overlayCard(BX(26), 66, 120, 64);
        rectOutline(BX(26), 66, 120, 64, ACCENT);
        drawTextCenter(BX(86), 74, "PAUSED", ACCENT, 1);
        drawText(BX(36), 90, "START", GOLD_LT, 1);  drawText(BX(80), 90, "RESUME", INK, 1);
        drawText(BX(36), 102, "SELECT", GOLD_LT, 1); drawText(BX(80), 102, "RESTART", INK, 1);
        drawText(BX(36), 114, "B", GOLD_LT, 1);      drawText(BX(80), 114, "TITLE", INK, 1);
    }
}

// ---------------------------------------------------------------- frame

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

void gfxFrame(const Game *g)
{
    frame++;
    accent = S.accent ? SHELF_ACCENTS[(S.accent - 1) % (ACCENT_COUNT - 1)] : SHELF_ACCENTS[(g->shelf > 0 ? g->shelf - 1 : 0) % (ACCENT_COUNT - 1)];
    if (S.frame == FRAME_ACCENT && frameMid != accent) {   // the shelf accent changed
        frameMid = accent; frameDk = lerp15(accent, BLACK, 9); frameLt = lerp15(accent, WHITE, 8);
        buildHeatTables();
    }
    boardChainWord(g, chainWordNow);
    dangerSetup(g);
    chainMask = 0;
    for (int i = 0; i < g->chainLen; i++) chainMask |= (u64)1 << (g->chainC[i] * ROWS + g->chainR[i]);
    chainValidNow = g->chainLen >= 3 && boardWordOk(chainWordNow, strlen(chainWordNow));
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
    stepParticles();
    stepPopups();
    if (invalidTimer) invalidTimer--;

    PROF_START();
    cur = vramBack;            // draw the touch screen straight into the hidden VRAM bank
    clipY0 = 0; clipY1 = SCREEN_H;
    // The hardware dim is re-requested by overlayCard every frame it is wanted; a state
    // that draws no card (play) must not inherit it from the previous frame's card.
    dimOn = false;
    if (g->state == ST_TITLE || g->state == ST_OPTIONS) {
        // menus paint their own black strip and frame: no board, no panel. Particles and
        // popups that outlive the game go on top of the menu, not under it.
        PROF(0); PROF(1); PROF(2); PROF(3);
        drawOverlay(g);   PROF(4);
        drawParticles();
        drawPopups();
    } else if (g->state == ST_PLAY) {
        drawBackdrop(g);  PROF(0);
        drawBoard(g);     PROF(1);
        drawParticles();
        drawPopups();
        drawDanger(g);    PROF(2);
        drawPanel(g);     PROF(3);
        PROF(4);
    } else {
        // pause / results / fail: the live board (gems keep falling and flashing) under a
        // card. The strip is dimmed by the 2D engine at vblank, so this costs the same as
        // a play frame plus the card.
        drawBackdrop(g);  PROF(0);
        drawBoard(g);     PROF(1);
        drawParticles();
        drawPopups();     PROF(2);
        drawPanel(g);     PROF(3);
        drawOverlay(g);   PROF(4);
    }
    PROF(5);

    // fade and shake
    if (bright < 0) { bright += 2; if (bright > 0) bright = 0; }
    else if (bright > 0) { bright -= 2; if (bright < 0) bright = 0; }
    int sx = 0, sy = 0;
    if (shakeFrames > 0) { shakeFrames--; sx = (rand() % 5) - 2; sy = (rand() % 5) - 2; }

    PROF_SET(8);
    swiWaitForVBlank();
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
    PROF(6);

    // The top screen HUD is drawn straight into VRAM_C while the beam is in vblank,
    // one 48-row slice per frame (every region still refreshes at 15 Hz). When the
    // layout changes (title/options <-> in-game) all four slices are redrawn, two per
    // frame, so the screen updates at once while each vblank stays well under budget.
    cur = vramSub;
    int kind = (g->state == ST_TITLE || g->state == ST_OPTIONS);
    if (kind != lastTopKind) { lastTopKind = kind; topBurst = 2; }
    int s0 = frame & 3, s1 = s0;
    if (topBurst) { topBurst--; s0 = topBurst ? 0 : 2; s1 = s0 + 1; }
    for (int slice = s0; slice <= s1; slice++) {
        clipY0 = slice * 48; clipY1 = clipY0 + 48;
        drawTop(g);
    }
    clipY0 = 0; clipY1 = SCREEN_H;
    PROF_SET(7);
}
