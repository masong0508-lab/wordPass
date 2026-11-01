// Software renderer: Bejeweled gems in an N+ frame, two 16-bit bitmap screens,
// particles, floating popups, hardware fades and screen shake.
#include "game.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static u16 *vramMain, *vramSub;
static int bgMain;
static u16 bufMain[SCREEN_W * SCREEN_H];
static u16 bufSub[SCREEN_W * SCREEN_H];
static u16 *cur;
static int frame;

// presentation state
static int bright, shakeFrames;
static u8 prevFound[12], letterPop[12];

#define C(r,g,b) ARGB16(1,(r),(g),(b))
#define BLACK      C(1,1,2)
#define INK        C(24,24,26)
#define GOLD       C(28,22,6)
#define GOLD_DK    C(16,11,2)
#define GOLD_LT    C(31,29,18)
#define ACCENT     C(6,28,24)
#define WHITE      C(31,31,31)
#define DIM        C(6,6,8)
#define DIM2       C(3,3,5)
#define FIRE1      C(31,12,2)
#define FIRE2      C(31,24,4)

typedef struct { u16 dark, mid, light; } GemPal;
static const GemPal GEM_RUBY     = { C(12,0,3),  C(27,3,8),   C(31,16,20) };
static const GemPal GEM_SAPPHIRE = { C(1,3,13),  C(4,11,28),  C(16,22,31) };
static const GemPal GEM_EMERALD  = { C(0,9,3),   C(3,22,9),   C(16,31,19) };
static const GemPal GEM_AMBER    = { C(13,9,0),  C(30,22,3),  C(31,30,16) };
static const GemPal GEM_AMETHYST = { C(9,1,13),  C(20,5,27),  C(28,18,31) };
static const GemPal GEM_DIAMOND  = { C(16,18,21),C(27,28,30), C(31,31,31) };
static const GemPal GEM_FIRE     = { C(12,0,0),  C(31,6,0),   C(31,22,4) };

static const GemPal *gemFor(int letter)
{
    switch ('a' + letter) {
        case 'a': case 'e': case 'i': case 'o': case 'u': return &GEM_RUBY;
        case 'r': case 's': case 't': case 'l': case 'n': case 'd': return &GEM_SAPPHIRE;
        case 'j': case 'k': case 'q': case 'x': case 'z': return &GEM_AMBER;
        case 'v': case 'w': case 'y': case 'f': case 'b': return &GEM_AMETHYST;
        default: return &GEM_EMERALD;
    }
}

// ---------------------------------------------------------------- primitives

static inline void px(int x, int y, u16 col)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) cur[y * SCREEN_W + x] = col;
}

static void fillRect(int x, int y, int w, int h, u16 col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u16 *p = cur + (y + j) * SCREEN_W + x;
        for (int i = 0; i < w; i++) p[i] = col;
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
    for (int j = y; j < y + h; j++)
        for (int i = x + (j & 1); i < x + w; i += 2) px(i, j, BLACK);
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

static void drawGem(int x, int y, const GemPal *p, bool lit) { drawGemSized(x, y, TILE_W, TILE_H, p, lit); }

static void drawGemOutline(int x, int y)
{
    rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, DIM);
}

// ---------------------------------------------------------------- particles

enum { P_SHARD = 0, P_SPARK, P_STAR };
typedef struct { s16 x, y, vx, vy; u8 life, maxLife, kind; u16 col; } Particle;   // 1/8 px units
#define MAXP 112
static Particle P[MAXP];

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

static void drawParticles(void)
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
static Popup pops[6];

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

static void cellOrigin(int c, int r, int *x, int *y)
{
    *x = GRID_X + c * TILE_W;
    *y = GRID_Y + r * TILE_H + ((c & 1) ? ODD_SHIFT : 0);
}

int gfxCellAt(int pxl, int pyl, int *c, int *r)
{
    int cc = (pxl - GRID_X) / TILE_W;
    if (pxl < GRID_X || cc < 0 || cc >= COLS) return 0;
    int yy = pyl - GRID_Y - ((cc & 1) ? ODD_SHIFT : 0);
    if (yy < 0) return 0;
    int rr = yy / TILE_H;
    if (rr >= ROWS) return 0;
    int lx = (pxl - GRID_X) % TILE_W, ly = yy % TILE_H;
    if (lx < 3 || lx > TILE_W - 4 || ly < 3 || ly > TILE_H - 4) return 0;
    *c = cc; *r = rr;
    return 1;
}

#define PANEL_X 176
#define SUBMIT_Y 120
#define CLEAR_Y 150
bool gfxInSubmit(int x, int y) { return x >= PANEL_X && x < 252 && y >= SUBMIT_Y && y < SUBMIT_Y + 24; }
bool gfxInClear(int x, int y)  { return x >= PANEL_X && x < 252 && y >= CLEAR_Y && y < CLEAR_Y + 20; }

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

// ---------------------------------------------------------------- board

static bool inChain(const Game *g, int c, int r)
{
    for (int i = 0; i < g->chainLen; i++) if (g->chainC[i] == c && g->chainR[i] == r) return true;
    return false;
}

static void drawLetter(int x, int y, int letter, u16 col, u16 shadow)
{
    if (letter == 'q' - 'a') {
        drawChar(x + 3, y + 6, 'Q', shadow, 2); drawChar(x + 2, y + 5, 'Q', col, 2);
        drawChar(x + 14, y + 10, 'U', shadow, 1); drawChar(x + 13, y + 9, 'U', col, 1);
    } else {
        drawChar(x + 8, y + 5, 'A' + letter, shadow, 2);
        drawChar(x + 7, y + 4, 'A' + letter, col, 2);
    }
}

static void drawBackdrop(void)
{
    fillRect(0, 0, 173, SCREEN_H, BLACK);
    // N+ dot grid behind the gems
    for (int y = 6; y < SCREEN_H; y += 8)
        for (int x = 6; x < 172; x += 8) px(x, y, DIM2);
}

static void drawBoard(const Game *g)
{
    bool peek = g->peekHeld;
    for (int c = 0; c < COLS; c++) {
        for (int r = 0; r < ROWS; r++) {
            const Cell *cl = &g->cell[c][r];
            int x, y; cellOrigin(c, r, &x, &y);
            int yd = y + cl->yoff;
            bool lit = inChain(g, c, r);
            if (lit) yd -= 2;
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
                    drawGem(x, yd, &GEM_DIAMOND, lit);
                    drawLetter(x, yd, cl->letter, C(4,6,10), C(20,22,26));
                    // travelling sparkle across the diamond
                    int sx = x + 3 + ((frame + c * 5 + r * 9) % 40) / 2;
                    if (sx < x + TILE_W - 3) { px(sx, yd + 2, WHITE); px(sx - 1, yd + 3, WHITE); }
                    break;
                }
                case CELL_BURN: {
                    int ph = (frame + c * 7 + r * 3) & 15;
                    int pulse = ph < 8 ? ph : 15 - ph;
                    GemPal fp = GEM_FIRE;
                    fp.mid = lerp15(C(31,4,0), C(31,14,0), pulse * 2);
                    drawGem(x, yd, &fp, lit);
                    drawLetter(x, yd, cl->letter, WHITE, C(14,2,0));
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
                        drawGem(x, yd, p, lit);
                        drawLetter(x, yd, cl->letter, lit ? WHITE : C(31,31,29), lerp15(p->dark, BLACK, 8));
                    }
            }
            if (cl->flash) {
                rectOutline(x, y, TILE_W, TILE_H, lerp15(BLACK, WHITE, cl->flash * 16 / 14));
            }
        }
    }
    // chain links: glowing gold rope between lifted gems
    for (int i = 1; i < g->chainLen; i++) {
        int x0, y0, x1, y1;
        cellOrigin(g->chainC[i - 1], g->chainR[i - 1], &x0, &y0);
        cellOrigin(g->chainC[i], g->chainR[i], &x1, &y1);
        x0 += TILE_W / 2; y0 += TILE_H / 2 - 2; x1 += TILE_W / 2; y1 += TILE_H / 2 - 2;
        for (int t = 0; t <= 10; t++) {
            int xx = x0 + (x1 - x0) * t / 10, yy = y0 + (y1 - y0) * t / 10;
            fillRect(xx - 2, yy - 2, 4, 4, GOLD_DK);
            fillRect(xx - 1, yy - 1, 2, 2, ((t + (frame >> 1)) % 5 == 0) ? WHITE : GOLD_LT);
        }
    }
    // chain order numbers float above the first gem
    if (g->chainLen) {
        int x, y; cellOrigin(g->chainC[0], g->chainR[0], &x, &y);
        fillRect(x + TILE_W / 2 - 2, y - 4, 4, 2, GOLD_LT);
    }
}

static void drawPanel(const Game *g)
{
    // gold frame around the grid, with a shimmer running along the top edge
    rectOutline(0, 0, 173, SCREEN_H, GOLD_DK);
    rectOutline(1, 1, 171, SCREEN_H - 2, GOLD);
    int sh = (frame * 2) % 240 - 20;
    for (int i = 0; i < 14; i++) { px(1 + sh + i, 1, GOLD_LT); px(1 + sh + i, 0, GOLD); }

    fillRect(PANEL_X - 2, 0, SCREEN_W - PANEL_X + 2, SCREEN_H, BLACK);
    drawLogo(PANEL_X, 6, 1);
    char s[32];
    sprintf(s, "%d", g->score);
    drawTextShadow(PANEL_X, 22, s, GOLD, 2);
    sprintf(s, "SHELF %d", g->shelf);
    drawText(PANEL_X, 42, s, INK, 1);
    drawText(PANEL_X, 52, g->lockdown ? "LOCKDOWN" : "CLASSIC", g->lockdown ? FIRE1 : ACCENT, 1);

    char w[24]; boardChainWord(g, w);
    for (char *p = w; *p; p++) *p -= 32;
    bool valid = g->chainLen >= 3 && dictLookup(w, strlen(w));
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

static void drawTop(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    for (int y = 4; y < SCREEN_H; y += 8)
        for (int x = 4; x < SCREEN_W; x += 8) px(x, y, DIM2);

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
    sprintf(s, "SHELF %d", g->shelf);
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
    char w[24]; boardChainWord(g, w);
    for (char *p = w; *p; p++) *p -= 32;
    if (w[0]) {
        bool valid = strlen(w) >= 3 && dictLookup(w, strlen(w));
        drawTextShadow(SCREEN_W / 2 - textWidth(w, 3) / 2, 100, w, valid ? GOLD_LT : WHITE, 3);
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
    dimRect(0, 0, 173, SCREEN_H);
    // the logo as a row of gems
    static const char *letters = "wordPASS";
    static const GemPal *pals[8] = { &GEM_RUBY, &GEM_SAPPHIRE, &GEM_EMERALD, &GEM_AMBER, &GEM_AMETHYST, &GEM_RUBY, &GEM_SAPPHIRE, &GEM_EMERALD };
    int gw = 20, gh = 22, lx = 86 - (8 * gw) / 2;
    for (int i = 0; i < 8; i++) {
        int bob = ((frame + i * 6) & 31) < 16 ? ((frame + i * 6) & 15) / 6 : (15 - ((frame + i * 6) & 15)) / 6;
        int x = lx + i * gw, y = 40 - bob;
        drawGemSized(x, y, gw - 1, gh, i < 4 ? pals[i] : &GEM_DIAMOND, false);
        char ch = letters[i];
        if (ch >= 'a') {
            const u8 *gl = font5x7[fontLogoIndex(ch)];
            for (int j = 0; j < 7; j++) for (int k = 0; k < 5; k++) if (gl[j] & (0x10 >> k)) fillRect(x + 5 + k * 2, y + 5 + j * 2, 2, 2, WHITE);
        } else drawChar(x + 5, y + 5, ch, C(4,6,10), 2);
    }
    fillRect(16, 72, 140, 64, BLACK);
    rectOutline(16, 72, 140, 64, GOLD);
    drawTextCenter(86, 80, "CRACK THE PASS", ACCENT, 1);
    drawTextCenter(86, 92, "DON'T BURN THE LIBRARY", INK, 1);
    drawTextCenter(86, 108, "DRAG  : SPELL", DIM, 1);
    drawTextCenter(86, 118, "HOLD L: PEEK", DIM, 1);
    if (frame & 32) drawTextShadow(86 - textWidth("TAP TO START", 1) / 2, 150, "TAP TO START", WHITE, 1);
}

static void drawOverlay(const Game *g)
{
    if (g->state == ST_PLAY) return;
    if (g->state == ST_TITLE) { drawTitle(g); return; }
    dimRect(0, 0, 173, SCREEN_H);
    if (g->state == ST_PASSED) {
        int t = g->stateTimer, grow = t < 8 ? (8 - t) * 3 : 0;
        fillRect(16 - grow, 70 - grow, 140 + grow * 2, 50 + grow * 2, BLACK);
        rectOutline(16 - grow, 70 - grow, 140 + grow * 2, 50 + grow * 2, GOLD_LT);
        drawTextShadow(86 - textWidth("PASS ACCEPTED", 1) / 2, 80, "PASS ACCEPTED", GOLD_LT, 1);
        char s[24]; sprintf(s, "SHELF %d OPEN", g->shelf + 1);
        drawTextCenter(86, 94, s, ACCENT, 1);
        if (frame & 32) drawTextCenter(86, 106, "TAP TO CONTINUE", INK, 1);
    } else if (g->state == ST_FAIL) {
        fillRect(16, 66, 140, 60, BLACK);
        rectOutline(16, 66, 140, 60, (frame & 8) ? FIRE1 : FIRE2);
        const char *t = g->fail == FAIL_FIRE ? "LIBRARY LOST" : g->fail == FAIL_SCORCH ? "PASS DESTROYED" : "OUT OF WORDS";
        drawTextShadow(86 - textWidth(t, 1) / 2, 76, t, FIRE2, 1);
        const char *why = g->fail == FAIL_FIRE ? "FIRE REACHED THE FLOOR" : g->fail == FAIL_SCORCH ? "A PASS LETTER BURNED" : "NO WORDS LEFT TO SPELL";
        drawTextCenter(86, 90, why, INK, 1);
        if (frame & 32) drawTextCenter(86, 110, "TAP TO RETRY SHELF", WHITE, 1);
    } else if (g->state == ST_PAUSE) {
        fillRect(40, 80, 92, 30, BLACK);
        rectOutline(40, 80, 92, 30, ACCENT);
        drawTextCenter(86, 92, "PAUSED", ACCENT, 1);
    }
}

// ---------------------------------------------------------------- frame

void gfxInit(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    bgMain = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vramMain = bgGetGfxPtr(bgMain);
    vramSub = bgGetGfxPtr(bgs);
    lcdMainOnBottom();
    bright = -16;
}

void gfxFrame(const Game *g)
{
    frame++;
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

    cur = bufMain;
    drawBackdrop();
    drawBoard(g);
    drawParticles();
    drawPopups();
    drawPanel(g);
    drawOverlay(g);
    cur = bufSub;
    drawTop(g);

    // fade and shake
    if (bright < 0) { bright += 2; if (bright > 0) bright = 0; }
    else if (bright > 0) { bright -= 2; if (bright < 0) bright = 0; }
    int sx = 0, sy = 0;
    if (shakeFrames > 0) { shakeFrames--; sx = (rand() % 5) - 2; sy = (rand() % 5) - 2; }

    swiWaitForVBlank();
    setBrightness(3, bright);
    bgSetScroll(bgMain, sx, sy);
    bgUpdate();
    dmaCopy(bufMain, vramMain, sizeof(bufMain));
    dmaCopy(bufSub, vramSub, sizeof(bufSub));
}
