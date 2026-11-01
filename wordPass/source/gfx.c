// Software renderer: Bejeweled gems in an N+ frame, two 16-bit bitmap screens,
// particles, floating popups, hardware fades and screen shake.
#include "game.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// The touch screen is rendered straight into VRAM: bank A and bank B each hold a
// 256x256x16 bitmap, and the main BG flips between them at vblank.
static u16 *vramSub;
static int bgMain;
static u16 *vramFront = (u16 *)0x06000000, *vramBack = (u16 *)0x06020000;
static int backBank = 1;                      // which bank vramBack points at
static u16 ALIGN(4) bufSub[SCREEN_W * SCREEN_H];
static u16 *cur;                              // current render target (VRAM or bufSub)
static int frame;
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
static char chainWordNow[24];
static u8 prevFound[12], letterPop[12];

#define C(r,g,b) ARGB16(1,(r),(g),(b))
#define BLACK      C(1,1,2)
#define INK        C(24,24,26)
static u16 frameMid = C(28,22,6), frameDk = C(16,11,2), frameLt = C(31,29,18);
#define GOLD       frameMid
#define GOLD_DK    frameDk
#define GOLD_LT    frameLt
#define TRUE_GOLD  C(28,22,6)
static u16 accent = C(6,28,24);
#define ACCENT     accent
// one accent per shelf so the library changes colour as you climb
static const u16 SHELF_ACCENTS[8] = {
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
static const GemPal *GEM_RUBY, *GEM_SAPPHIRE, *GEM_AMBER, *GEM_AMETHYST, *GEM_EMERALD;

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
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) cur[y * SCREEN_W + x] = col;
}

static ITCM_CODE __attribute__((noinline)) void fillRect(int x, int y, int w, int h, u16 col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    u32 col2 = col | ((u32)col << 16);
    for (int j = 0; j < h; j++) {
        u16 *p = cur + (y + j) * SCREEN_W + x;
        int n = w;
        if (((uintptr_t)p & 2) && n) { *p++ = col; n--; }
        u32 *p32 = (u32 *)p;
        for (; n >= 8; n -= 8) { p32[0] = col2; p32[1] = col2; p32[2] = col2; p32[3] = col2; p32 += 4; }
        for (; n >= 2; n -= 2) *p32++ = col2;
        if (n) *(u16 *)p32 = col;
    }
}

static void hline(int x, int y, int w, u16 col) { fillRect(x, y, w, 1, col); }

static void rectOutline(int x, int y, int w, int h, u16 col)
{
    hline(x, y, w, col); hline(x, y + h - 1, w, col);
    for (int j = 1; j < h - 1; j++) { px(x, y + j, col); px(x + w - 1, y + j, col); }
}

static void roundRect(int x, int y, int w, int h, u16 col)
{
    fillRect(x + 2, y, w - 4, h, col);
    fillRect(x, y + 2, w, h - 4, col);
    fillRect(x + 1, y + 1, w - 2, h - 2, col);
}

// blend two RGB15 colours, t in 0..16
static u16 lerp15(u16 a, u16 b, int t)
{
    int ar = a & 31, ag = (a >> 5) & 31, ab = (a >> 10) & 31;
    int br = b & 31, bg = (b >> 5) & 31, bb = (b >> 10) & 31;
    return C(ar + ((br - ar) * t) / 16, ag + ((bg - ag) * t) / 16, ab + ((bb - ab) * t) / 16);
}

// dither-darken a region (checkerboard of black)
static void dimRect(int x, int y, int w, int h)
{
    for (int j = y; j < y + h; j++) {
        u16 *row = cur + j * SCREEN_W;
        for (int i = x + (j & 1); i < x + w; i += 2) row[i] = BLACK;
    }
}

static void drawChar(int x, int y, char ch, u16 col, int scale)
{
    const u8 *gl = font5x7[fontIndex(ch)];
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 5; i++)
            if (gl[j] & (0x10 >> i)) {
                if (scale == 1) px(x + i, y + j, col);
                else fillRect(x + i * scale, y + j * scale, scale, scale, col);
            }
}

static void drawText(int x, int y, const char *s, u16 col, int scale)
{
    for (; *s; s++) { drawChar(x, y, *s, col, scale); x += 6 * scale; }
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
    for (; *w; w++) {
        const u8 *gl = font5x7[fontLogoIndex(*w)];
        for (int j = 0; j < 7; j++)
            for (int i = 0; i < 5; i++)
                if (gl[j] & (0x10 >> i)) fillRect(x + i * scale, y + j * scale, scale, scale, INK);
        x += 6 * scale;
    }
    drawText(x, y, "PASS", GOLD, scale);
}

// Faceted glossy gem: vertical gradient, highlight band, specular, dark base.
static void drawGemSized(int x, int y, int w, int h, const GemPal *p, bool lit)
{
    // body with rounded corners
    for (int j = 1; j < h - 1; j++) {
        int inset = (j == 1 || j == h - 2) ? 2 : 1;
        // light at the top, dark at the bottom
        int t = 16 - (j * 16) / h;
        u16 col = (t > 10) ? lerp15(p->mid, p->light, (t - 10) * 16 / 6) : lerp15(p->dark, p->mid, t * 16 / 10);
        hline(x + inset, y + j, w - inset * 2, col);
    }
    // highlight band and specular
    hline(x + 3, y + 2, w - 7, p->light);
    hline(x + 4, y + 3, w - 10, lerp15(p->light, WHITE, 8));
    px(x + 4, y + 3, WHITE); px(x + 5, y + 3, WHITE);
    // left facet
    for (int j = 3; j < h - 4; j++) px(x + 2, y + j, lerp15(p->mid, p->light, 8));
    // right and bottom shade
    for (int j = 3; j < h - 2; j++) px(x + w - 3, y + j, p->dark);
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

static int gemIndex(const GemPal *p)
{
    for (int i = 0; i < NGEMPAL; i++) if (GEM_PALS[i] == p) return i;
    return 0;
}

static void setupTheme(void)
{
    const GemPal *t = THEMES[S.theme];
    GEM_RUBY = &t[0]; GEM_SAPPHIRE = &t[1]; GEM_AMBER = &t[2]; GEM_AMETHYST = &t[3]; GEM_EMERALD = &t[4];
    GEM_PALS[0] = &t[0]; GEM_PALS[1] = &t[1]; GEM_PALS[2] = &t[4]; GEM_PALS[3] = &t[2]; GEM_PALS[4] = &t[3];
    GEM_PALS[5] = &GEM_DIAMOND; GEM_PALS[6] = &GEM_FIRE;
    switch (S.frame) {
        case FRAME_SILVER: frameMid = C(20,21,24); frameDk = C(10,11,13); frameLt = C(30,30,31); break;
        case FRAME_ACCENT: frameMid = accent; frameDk = lerp15(accent, BLACK, 9); frameLt = lerp15(accent, WHITE, 8); break;
        default:           frameMid = C(28,22,6); frameDk = C(16,11,2); frameLt = C(31,29,18); break;
    }
}

static void buildGemCache(void)
{
    setupTheme();
    static u16 scratch[TILE_H][SCREEN_W];   // row stride must match SCREEN_W for the primitives
    u16 *saved = cur;
    for (int i = 0; i < NGEMPAL; i++) {
        cur = &scratch[0][0];
        for (int j = 0; j < TILE_H; j++) for (int k = 0; k < TILE_W; k++) scratch[j][k] = 0;   // 0 = transparent
        drawGemSized(0, 0, TILE_W, TILE_H, GEM_PALS[i], false);
        for (int j = 0; j < TILE_H; j++) for (int k = 0; k < TILE_W; k++) gemCache[i][j][k] = scratch[j][k];
    }
    cur = saved;
    gemCacheReady = true;
}

// blit a cached gem; colour 0 is transparent (the rounded corners)
static ITCM_CODE __attribute__((noinline)) void blitGem(int x, int y, int pal)
{
    if (y < 0 || y + TILE_H > SCREEN_H || x < 0 || x + TILE_W > SCREEN_W) {
        drawGemSized(x, y, TILE_W, TILE_H, GEM_PALS[pal], false);   // clipped: slow path
        return;
    }
    for (int j = 0; j < TILE_H; j++) {
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

// 2x glyph with a one-pixel drop shadow, written directly (no per-rect clipping)
static ITCM_CODE __attribute__((noinline)) void drawChar2Shadow(int x, int y, char ch, u16 col, u16 shadow)
{
    if (x < 1 || y < 1 || x + 11 >= SCREEN_W || y + 15 >= SCREEN_H) { drawChar(x + 1, y + 1, ch, shadow, 2); drawChar(x, y, ch, col, 2); return; }
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

// The framed board strip is BOARD_W pixels wide: x 0..173 right-handed, 82..255 left-handed.
// Every frame repaints the strip edge to edge, since VRAM holds whatever was there last.
#define BOARD_W 174
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

void gfxBurst(int c, int r, int kind)
{
    int x, y; cellOrigin(c, r, &x, &y);
    int cx = x + TILE_W / 2, cy = y + TILE_H / 2;
    if (kind == BURST_SHATTER) {
        const GemPal *p = gemFor(G.cell[c][r].letter);
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
#define OPT_Y0 38
#define OPT_DY 15
int gfxOptionRowAt(int x, int y)
{
    if (x < BX(12) || x >= BX(160)) return -1;
    if (y >= 160 && y < 180) return OPT_ROWS;          // BACK
    int row = (y - OPT_Y0) / OPT_DY;
    if (y < OPT_Y0 || row < 0 || row >= OPT_ROWS) return -1;
    return row;
}

// ---------------------------------------------------------------- board

static bool inChain(const Game *g, int c, int r)
{
    for (int i = 0; i < g->chainLen; i++) if (g->chainC[i] == c && g->chainR[i] == r) return true;
    return false;
}

static void drawLetter(int x, int y, int letter, u16 col, u16 shadow)
{
    if (letter == 'q' - 'a') {
        drawChar2Shadow(x + 2, y + 5, 'Q', col, shadow);
        drawChar(x + 14, y + 10, 'U', shadow, 1); drawChar(x + 13, y + 9, 'U', col, 1);
    } else {
        drawChar2Shadow(x + 7, y + 4, 'A' + letter, col, shadow);
    }
}

static bool inChain(const Game *g, int c, int r);

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
                         && g->state == ST_PLAY && !g->peekHeld && !inChain(g, c, r);
            if (solid) {
                // only the rounded-corner pixels and the one-pixel rim show through
                fillRect(x, y, TILE_W, 1, BLACK); fillRect(x, y + TILE_H - 1, TILE_W, 1, BLACK);
                for (int j = 1; j < TILE_H - 1; j++) { cur[(y + j) * SCREEN_W + x] = BLACK; cur[(y + j) * SCREEN_W + x + TILE_W - 1] = BLACK; }
                cur[(y + 1) * SCREEN_W + x + 1] = BLACK; cur[(y + 1) * SCREEN_W + x + TILE_W - 2] = BLACK;
                cur[(y + TILE_H - 2) * SCREEN_W + x + 1] = BLACK; cur[(y + TILE_H - 2) * SCREEN_W + x + TILE_W - 2] = BLACK;
            } else {
                fillRect(x, y, TILE_W, TILE_H, BLACK);
            }
        }
    }
    // N+ dot grid behind the gems (dots under solid gems are hidden anyway)
    u16 dot = lerp15(BLACK, ACCENT, 3);
    for (int y = 6; y < SCREEN_H; y += 8) {
        u16 *row = cur + y * SCREEN_W + boardX0;
        for (int x = 6; x < 172; x += 8) row[x] = dot;
    }
}

static ITCM_CODE __attribute__((noinline)) void drawBoard(const Game *g)
{
    bool peek = g->peekHeld;
    for (int c = 0; c < COLS; c++) {
        for (int r = 0; r < ROWS; r++) {
            const Cell *cl = &g->cell[c][r];
            int x, y; cellOrigin(c, r, &x, &y);
            int yd = y + YOFF_PX(cl);
            bool lit = inChain(g, c, r);
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
                    if (sq) drawGemSized(x, yd + sq, TILE_W, TILE_H - sq, &GEM_DIAMOND, lit);
                    else { blitGem(x, yd, 5); if (lit) { rectOutline(x, yd, TILE_W, TILE_H, WHITE); rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, WHITE); } }
                    drawLetter(x, yd + sq / 2, cl->letter, C(4,6,10), C(20,22,26));
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
                    } else {
                        const GemPal *p = gemFor(cl->letter);
                        if (sq) drawGemSized(x, yd + sq, TILE_W, TILE_H - sq, p, lit);
                        else {
                            blitGem(x, yd, gemIndex(p));
                            if (lit) { rectOutline(x, yd, TILE_W, TILE_H, WHITE); rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, lerp15(WHITE, p->light, 8)); }
                        }
                        drawLetter(x, yd + sq / 2, cl->letter, lit ? WHITE : C(31,31,29), p->dark);
                    }
            }
            if (cl->flash) {
                rectOutline(x, y, TILE_W, TILE_H, lerp15(BLACK, WHITE, cl->flash > 14 ? 16 : cl->flash + 2));
            }
        }
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

// red glow creeping up from the floor as fire gets low
static void drawDanger(const Game *g)
{
    int low = boardLowestFireRow(g);
    if (low < 4 || g->state != ST_PLAY) return;
    int strength = (low - 3) * 4 + ((frame & 16) ? 2 : 0);   // 4..18
    for (int j = 0; j < 12; j++) {
        int a = (strength * (12 - j)) / 12;
        if (a <= 0) continue;
        int y = SCREEN_H - 2 - j;
        for (int x = BX(2); x < BX(171); x += 1 + (j & 1)) px(x, y, lerp15(cur[y * SCREEN_W + x], FIRE1, a > 16 ? 16 : a));
    }
}

static ITCM_CODE __attribute__((noinline)) void drawPanel(const Game *g)
{
    // gold frame around the grid, with a shimmer running along the top edge
    rectOutline(BX(0), 0, BOARD_W, SCREEN_H, GOLD_DK);
    rectOutline(BX(1), 1, BOARD_W - 2, SCREEN_H - 2, GOLD);
    int sh = (frame * 2) % 240 - 20;
    for (int i = 0; i < 14; i++) { px(BX(1 + sh + i), 1, GOLD_LT); px(BX(1 + sh + i), 0, GOLD); }

    if (S.hand == HAND_LEFT) fillRect(0, 0, boardX0, SCREEN_H, BLACK);
    else fillRect(PANEL_X - 2, 0, SCREEN_W - PANEL_X + 2, SCREEN_H, BLACK);
    drawLogo(PANEL_X, 6, 1);
    char s[32];
    sprintf(s, "%d", g->score);
    drawTextShadow(PANEL_X, 22, s, GOLD, 2);
    sprintf(s, "SHELF %d", g->shelf);
    drawText(PANEL_X, 42, s, INK, 1);
    if (g->mode == MODE_DAILY) drawText(PANEL_X, 52, "DAILY", GOLD_LT, 1);
    else drawText(PANEL_X, 52, g->lockdown ? "LOCKDOWN" : "CLASSIC", g->lockdown ? FIRE1 : ACCENT, 1);

    char w[24]; strcpy(w, chainWordNow);
    for (char *p = w; *p; p++) *p -= 32;
    bool valid = chainValidNow;
    drawText(PANEL_X, 70, w, valid ? GOLD_LT : WHITE, 1);
    if (g->chainLen >= 3 && !g->peekHeld) drawText(PANEL_X, 82, valid ? "VALID" : "...", valid ? ACCENT : DIM, 1);

    // heat: small flame glyph grows with the bar
    drawText(PANEL_X, 98, "HEAT", DIM, 1);
    int hw = 70 * g->heat / (g->heatThreshold ? g->heatThreshold : 1);
    rectOutline(PANEL_X, 108, 72, 6, DIM);
    for (int i = 0; i < hw; i++) px(PANEL_X + 1 + i, 109, lerp15(GOLD, FIRE1, i * 16 / 70)), fillRect(PANEL_X + 1 + i, 110, 1, 3, lerp15(GOLD_DK, FIRE1, i * 16 / 70));
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
        drawTextCenter(128, 80, "THEME CHANGES THE GEMS", INK, 1);
        drawTextCenter(128, 90, "FRAME AND ACCENT CHANGE THE LIBRARY", INK, 1);
        drawTextCenter(128, 100, "HAND MIRRORS THE BOARD FOR LEFTIES", INK, 1);
        drawTextCenter(128, 110, "FIRE SETS HOW FAST THE SHELF BURNS", INK, 1);
        drawTextCenter(128, 130, "TAP A ROW OR USE THE D-PAD", DIM, 1);
        char s[40];
        if (S.bestClassic) { sprintf(s, "BEST CLASSIC %d  SHELF %d", S.bestClassic, S.bestClassicShelf); drawTextCenter(128, 150, s, DIM, 1); }
        if (S.bestDaily)   { sprintf(s, "BEST DAILY %d ON %d", S.bestDaily, S.bestDailyDate); drawTextCenter(128, 160, s, DIM, 1); }
        return;
    }
    drawTipCard((frame / 150) % 3);
}

static void drawTop(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    if (g->state == ST_TITLE || g->state == ST_OPTIONS) {
        u16 dot = lerp15(BLACK, ACCENT, 3);
        for (int y = 4; y < SCREEN_H; y += 8)
            for (int x = 4; x < SCREEN_W; x += 8) px(x, y, dot);
        drawTopTitle();
        return;
    }
    u16 dot = lerp15(BLACK, ACCENT, 3);
    for (int y = 4; y < SCREEN_H; y += 8) { u16 *row = cur + y * SCREEN_W; for (int x = 4; x < SCREEN_W; x += 8) row[x] = dot; }

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
    if (g->mode == MODE_DAILY) sprintf(s, "DAILY %d", g->dailySeed % 10000);   // MMDD
    else sprintf(s, "SHELF %d", g->shelf);
    drawText(170, 18, s, INK, 1);
    sprintf(s, "%d", g->score);
    drawText(170, 28, s, GOLD, 1);

    // the pass: gem-cut blanks that pop when found
    int n = g->passLen, bw = 22, gap = 4;
    int x0 = (SCREEN_W - (n * bw + (n - 1) * gap)) / 2, y0 = 58;
    drawTextCenter(SCREEN_W / 2, 46, "PASS", DIM, 1);
    for (int i = 0; i < n; i++) {
        int x = x0 + i * (bw + gap);
        if (g->found[i]) {
            int pop = letterPop[i];
            int grow = pop > 12 ? (pop - 12) : 0;
            drawGemSized(x - grow, y0 - grow, bw + grow * 2, 26 + grow * 2, &GEM_DIAMOND, pop > 0);
            drawChar(x + 6, y0 + 6, 'A' + (g->pass[i] - 'a'), C(4,6,10), 2);
        } else {
            rectOutline(x, y0, bw, 26, GOLD_DK);
            fillRect(x + 4, y0 + 22, bw - 8, 1, GOLD);
            if (((frame >> 4) % n) == i) px(x + 4 + ((frame >> 1) % (bw - 8)), y0 + 22, GOLD_LT);
        }
    }

    // current chain large, or the last word and its score
    char w[24]; strcpy(w, chainWordNow);
    for (char *p = w; *p; p++) *p -= 32;
    if (w[0]) {
        bool valid = chainValidNow;
        int jx = invalidTimer ? ((invalidTimer & 2) ? 3 : -3) : 0;
        u16 wc = invalidTimer ? ((invalidTimer & 4) ? FIRE1 : WHITE) : (valid ? GOLD_LT : WHITE);
        drawTextShadow(SCREEN_W / 2 - textWidth(w, 3) / 2 + jx, 100, w, wc, 3);
    } else if (g->lastWord[0]) {
        char lw[16]; strcpy(lw, g->lastWord);
        for (char *p = lw; *p; p++) *p -= 32;
        drawTextCenter(SCREEN_W / 2, 100, lw, INK, 2);
        sprintf(s, "+%d", g->lastWordScore);
        drawTextCenter(SCREEN_W / 2, 118, s, GOLD, 1);
    }

    // fire meter with a flame cap
    drawText(222, 48, "FIRE", DIM, 1);
    rectOutline(226, 58, 8, ROWS * 10 + 2, DIM);
    int low = boardLowestFireRow(g);
    for (int r = 0; r <= low; r++) fillRect(227, 59 + r * 10, 6, 9, lerp15(FIRE2, FIRE1, r * 2));
    if (low >= 0) { int fy = 59 + low * 10 + 9; px(229 + (frame & 1), fy - 12 - (frame & 3), FIRE2); px(230, fy - 11, FIRE2); }

    // shelf map
    for (int i = 0; i < 20; i++) {
        int x = 18 + i * 11, y = 150;
        if (i + 1 < g->shelf) fillRect(x, y, 8, 8, ACCENT);
        else if (i + 1 == g->shelf) rectOutline(x, y, 8, 8, (frame & 16) ? WHITE : ACCENT);
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
    dimRect(boardX0, 0, BOARD_W, SCREEN_H);
    // the logo as a row of gems
    static const char *letters = "wordPASS";
    const GemPal *pals[8] = { GEM_RUBY, GEM_SAPPHIRE, GEM_EMERALD, GEM_AMBER, GEM_AMETHYST, GEM_RUBY, GEM_SAPPHIRE, GEM_EMERALD };
    int gw = 20, gh = 22, lx = 86 - (8 * gw) / 2;
    for (int i = 0; i < 8; i++) {
        int bob = ((frame + i * 6) & 31) < 16 ? ((frame + i * 6) & 15) / 6 : (15 - ((frame + i * 6) & 15)) / 6;
        int x = lx + i * gw, y = 40 - bob;
        drawGemSized(BX(x), y, gw - 1, gh, i < 4 ? pals[i] : &GEM_DIAMOND, false);
        char ch = letters[i];
        if (ch >= 'a') {
            const u8 *gl = font5x7[fontLogoIndex(ch)];
            for (int j = 0; j < 7; j++) for (int k = 0; k < 5; k++) if (gl[j] & (0x10 >> k)) fillRect(BX(x + 5 + k * 2), y + 5 + j * 2, 2, 2, WHITE);
        } else drawChar(BX(x + 5), y + 5, ch, C(4,6,10), 2);
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
    if (S.bestClassic) { sprintf(s, "BEST %d", S.bestClassic); drawText(BX(20), 180, s, DIM, 1); }
    if (S.bestDaily)   { sprintf(s, "DAILY %d", S.bestDaily); drawText(BX(152) - textWidth(s, 1), 180, s, DIM, 1); }
}

static const char *const OPT_NAMES[OPT_ROWS] = { "THEME", "FRAME", "ACCENT", "HAND", "FIRE", "MUSIC" };
static const char *const THEME_NAMES[THEME_COUNT] = { "JEWEL", "INK", "CANDY", "EMBER" };
static const char *const FRAME_NAMES[FRAME_COUNT] = { "GOLD", "SILVER", "ACCENT" };
static const char *const ACCENT_NAMES[9] = { "AUTO", "TEAL", "MAGENTA", "LIME", "ORANGE", "SKY", "VIOLET", "PINK", "MINT" };
static const char *const FIRE_NAMES[FIRE_COUNT] = { "RELAXED", "NORMAL", "FAST" };

static void drawOptions(const Game *g)
{
    fillRect(boardX0, 0, BOARD_W, SCREEN_H, BLACK);
    rectOutline(BX(0), 0, BOARD_W, SCREEN_H, GOLD_DK);
    rectOutline(BX(1), 1, BOARD_W - 2, SCREEN_H - 2, GOLD);
    drawTextShadow(BX(86) - textWidth("OPTIONS", 2) / 2, 14, "OPTIONS", GOLD_LT, 2);
    int sel = g->stateTimer;
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
        }
        if (i == sel) { fillRect(BX(12), y - 2, 148, OPT_DY - 1, lerp15(BLACK, ACCENT, 4)); rectOutline(BX(12), y - 2, 148, OPT_DY - 1, ACCENT); }
        drawText(BX(18), y + 2, OPT_NAMES[i], i == sel ? WHITE : INK, 1);
        char v[16]; sprintf(v, "< %s >", val);
        drawText(BX(154) - textWidth(v, 1), y + 2, v, i == sel ? ACCENT : INK, 1);
    }
    // swatches: the five letter gems in the current theme
    for (int i = 0; i < 5; i++) { blitGem(BX(16) + i * 28, 132, i); drawLetter(BX(16) + i * 28, 132, "AIRTS"[i] - 'A', WHITE, GEM_PALS[i]->dark); }
    rectOutline(BX(54), 160, 64, 20, DIM);
    drawTextCenter(BX(86), 166, "BACK", INK, 1);
    drawTextCenter(BX(86), 182, settingsPersistent() ? "SAVED TO CARD" : "NOT SAVED: NO CARD", DIM, 1);
}

// Title top screen: three tip cards that cycle
static void drawTipCard(int which)
{
    int cx = 128, y = 52;
    if (which == 0) {
        drawTextCenter(BX(cx), y, "SPELL", GOLD_LT, 1);
        static const char L[3] = { 'I', 'N', 'K' };
        const GemPal *P3[3] = { GEM_RUBY, GEM_SAPPHIRE, GEM_AMBER };
        int gx = cx - 40;
        for (int i = 0; i < 3; i++) {
            int x = gx + i * 28, gy = y + 14 + ((i & 1) ? 10 : 0);
            drawGemSized(BX(x), gy, TILE_W, TILE_H, P3[i], true);
            drawChar(BX(x + 7), gy + 4, L[i], WHITE, 2);
        }
        for (int i = 1; i < 3; i++) {
            int x0 = gx + (i - 1) * 28 + TILE_W / 2, y0 = y + 14 + (((i - 1) & 1) ? 10 : 0) + TILE_H / 2;
            int x1 = gx + i * 28 + TILE_W / 2, y1 = y + 14 + ((i & 1) ? 10 : 0) + TILE_H / 2;
            for (int t = 0; t <= 8; t++) fillRect(BX(x0 + (x1 - x0) * t / 8 - 1), y0 + (y1 - y0) * t / 8 - 1, 2, 2, GOLD_LT);
        }
        drawTextCenter(BX(cx), y + 56, "DRAG ACROSS TOUCHING GEMS", INK, 1);
        drawTextCenter(BX(cx), y + 66, "THREE LETTERS OR MORE, THEN SUBMIT", INK, 1);
        drawTextCenter(BX(cx), y + 80, "LONG WORDS SCORE BIG AND KEEP HEAT DOWN", DIM, 1);
    } else if (which == 1) {
        drawTextCenter(BX(cx), y, "PEEK", ACCENT, 1);
        int gx = cx - 40;
        for (int i = 0; i < 3; i++) {
            int x = gx + i * 28, gy = y + 14 + ((i & 1) ? 10 : 0);
            if (i == 0) { rectOutline(BX(x + 1), gy + 1, TILE_W - 2, TILE_H - 2, ACCENT); drawChar(BX(x + 10), gy + 8, '2', ACCENT, 1); }
            else if (i == 1) { drawGemSized(BX(x), gy, TILE_W, TILE_H, &GEM_DIAMOND, false); drawChar(BX(x + 7), gy + 4, 'B', C(4,6,10), 2); }
            else { rectOutline(BX(x + 1), gy + 1, TILE_W - 2, TILE_H - 2, DIM); drawChar(BX(x + 10), gy + 8, 'X', DIM, 1); }
        }
        drawTextCenter(BX(cx), y + 56, "HOLD L AND TAP A GEM YOU CAN SPARE", INK, 1);
        drawTextCenter(BX(cx), y + 66, "A NUMBER COUNTS HIDDEN PASS LETTERS", INK, 1);
        drawTextCenter(BX(cx), y + 76, "IN THE SIX CELLS AROUND IT", INK, 1);
        drawTextCenter(BX(cx), y + 90, "PEELED GEMS NEVER COME BACK", DIM, 1);
    } else {
        drawTextCenter(BX(cx), y, "FIRE", FIRE2, 1);
        int gx = cx - 40;
        GemPal firePal = GEM_FIRE; firePal.mid = (frame & 8) ? C(31,8,0) : C(31,14,0);
        drawGemSized(BX(gx), y + 14, TILE_W, TILE_H, &firePal, false); drawChar(BX(gx + 7), y + 18, 'R', WHITE, 2);
        rectOutline(BX(gx + 1), y + 14 + TILE_H + 1, TILE_W - 2, TILE_H - 2, DIM);
        for (int j = 0; j < 3; j++) px(gx + 6 + j * 6, y + 12 - ((frame >> 2) + j) % 3, FIRE2);
        drawGemSized(BX(gx + 28), y + 24, TILE_W, TILE_H, &GEM_DIAMOND, false); drawChar(BX(gx + 35), y + 28, 'O', C(4,6,10), 2);
        drawGemSized(BX(gx + 56), y + 14, TILE_W, TILE_H, GEM_EMERALD, false); drawChar(BX(gx + 63), y + 18, 'P', WHITE, 2);
        drawTextCenter(BX(cx), y + 62, "SHORT WORDS BUILD HEAT AND LIGHT A GEM", INK, 1);
        drawTextCenter(BX(cx), y + 72, "FIRE DROPS A ROW EACH TURN. IT FALLS", INK, 1);
        drawTextCenter(BX(cx), y + 82, "THROUGH HOLES. DIAMONDS BLOCK IT", INK, 1);
        drawTextCenter(BX(cx), y + 96, "SPELL THROUGH A FLAME TO PUT IT OUT", DIM, 1);
    }
    for (int i = 0; i < 3; i++) {
        if (i == which) fillRect(BX(118 + i * 8), 170, 4, 4, GOLD_LT); else rectOutline(BX(118 + i * 8), 170, 4, 4, DIM);
    }
}

static void drawOverlay(const Game *g)
{
    if (g->state == ST_PLAY) return;
    if (g->state == ST_TITLE) { drawTitle(g); return; }
    if (g->state == ST_OPTIONS) { drawOptions(g); return; }
    dimRect(boardX0, 0, BOARD_W, SCREEN_H);
    if (g->state == ST_PASSED) {
        // results card slides up from the bottom and settles
        int t = g->stateTimer;
        int slide = t < 12 ? (12 - t) * (12 - t) : 0;
        int cx = 86, cy = 28 + slide, cw = 150, ch = 140;
        fillRect(BX(cx - cw / 2), cy, cw, ch, BLACK);
        rectOutline(BX(cx - cw / 2), cy, cw, ch, GOLD_LT);
        rectOutline(BX(cx - cw / 2 + 2), cy + 2, cw - 4, ch - 4, GOLD_DK);
        drawTextShadow(BX(cx - textWidth("PASS ACCEPTED", 1) / 2), cy + 8, g->mode == MODE_DAILY ? "DAILY PASS DONE" : "PASS ACCEPTED", GOLD_LT, 1);
        // the pass itself as small diamonds
        int n = g->passLen, bw = 14, gap = 2, x0 = cx - (n * bw + (n - 1) * gap) / 2;
        for (int i = 0; i < n; i++) {
            int lx = x0 + i * (bw + gap);
            drawGemSized(BX(lx), cy + 20, bw, 16, &GEM_DIAMOND, (t / 4) % (n + 4) == i);
            drawChar(BX(lx + 5), cy + 24, 'A' + (g->pass[i] - 'a'), C(4,6,10), 1);
        }
        char s[32], lw[16];
        int rowy = cy + 46, lx = cx - cw / 2 + 10, rx = cx + cw / 2 - 10;
        hline(BX(lx), rowy - 5, cw - 20, DIM);
        strcpy(lw, g->bestWord[0] ? g->bestWord : "-");
        for (char *p = lw; *p; p++) if (*p >= 'a') *p -= 32;
        drawText(BX(lx), rowy, "BEST WORD", DIM, 1);
        drawText(BX(rx - textWidth(lw, 1)), rowy, lw, WHITE, 1); rowy += 10;
        sprintf(s, "+%d", g->bestWordScore);
        drawText(BX(rx - textWidth(s, 1)), rowy, s, GOLD, 1); rowy += 14;
        drawText(BX(lx), rowy, "WORDS", DIM, 1); sprintf(s, "%d", g->wordsThisShelf);
        drawText(BX(rx - textWidth(s, 1)), rowy, s, WHITE, 1); rowy += 10;
        drawText(BX(lx), rowy, "PEELS", DIM, 1); sprintf(s, "%d", g->peels);
        drawText(BX(rx - textWidth(s, 1)), rowy, s, g->peels <= g->passLen + 2 ? ACCENT : WHITE, 1); rowy += 10;
        drawText(BX(lx), rowy, "TURNS", DIM, 1); sprintf(s, "%d", g->turn);
        drawText(BX(rx - textWidth(s, 1)), rowy, s, WHITE, 1); rowy += 14;
        hline(BX(lx), rowy - 5, cw - 20, DIM);
        drawText(BX(lx), rowy, "SHELF BONUS", DIM, 1); sprintf(s, "+%d", 500 * g->shelf);
        drawText(BX(rx - textWidth(s, 1)), rowy, s, GOLD_LT, 1); rowy += 14;
        if (g->mode == MODE_DAILY) { sprintf(s, "FINAL %d", g->score); drawTextCenter(BX(cx), rowy, s, GOLD_LT, 1); }
        else { sprintf(s, "SHELF %d OPEN", g->shelf + 1); drawTextCenter(BX(cx), rowy, s, ACCENT, 1); }
        if (t > 30 && (frame & 32)) drawTextCenter(BX(cx), cy + ch - 12, "TAP TO CONTINUE", INK, 1);
    } else if (g->state == ST_FAIL) {
        fillRect(BX(16), 66, 140, 60, BLACK);
        rectOutline(BX(16), 66, 140, 60, (frame & 8) ? FIRE1 : FIRE2);
        const char *t = g->fail == FAIL_FIRE ? "LIBRARY LOST" : g->fail == FAIL_SCORCH ? "PASS DESTROYED" : "OUT OF WORDS";
        drawTextShadow(BX(86 - textWidth(t, 1) / 2), 76, t, FIRE2, 1);
        const char *why = g->fail == FAIL_FIRE ? "FIRE REACHED THE FLOOR" : g->fail == FAIL_SCORCH ? "A PASS LETTER BURNED" : "NO WORDS LEFT TO SPELL";
        drawTextCenter(BX(86), 90, why, INK, 1);
        if (frame & 32) drawTextCenter(BX(86), 110, g->mode == MODE_DAILY ? "TAP FOR TITLE" : "TAP TO RETRY SHELF", WHITE, 1);
    } else if (g->state == ST_PAUSE) {
        fillRect(BX(26), 66, 120, 64, BLACK);
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
    accent = S.accent ? SHELF_ACCENTS[(S.accent - 1) % 8] : SHELF_ACCENTS[(g->shelf > 0 ? g->shelf - 1 : 0) % 8];
    if (S.frame == FRAME_ACCENT) { frameMid = accent; frameDk = lerp15(accent, BLACK, 9); frameLt = lerp15(accent, WHITE, 8); }
    boardChainWord(g, chainWordNow);
    chainValidNow = g->chainLen >= 3 && dictLookup(chainWordNow, strlen(chainWordNow));
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
    drawBackdrop(g);  PROF(0);
    drawBoard(g);     PROF(1);
    drawParticles();
    drawPopups();
    drawDanger(g);    PROF(2);
    drawPanel(g);     PROF(3);
    drawOverlay(g);   PROF(4);
    // the top screen is a HUD: in play, draw it on one frame in four and copy it on
    // another, so no single frame carries both and the board stays at 60
    bool playing = g->state == ST_PLAY;
    bool drawSub = !playing || (frame & 3) == 1;
    bool copySub = !playing || (frame & 3) == 3;
    if (drawSub) { cur = bufSub; drawTop(g); }
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
    if (copySub) { DC_FlushRange(bufSub, sizeof(bufSub)); dmaCopy(bufSub, vramSub, sizeof(bufSub)); }
    PROF_SET(6);
}
