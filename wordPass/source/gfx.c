// Software renderer: Bejeweled gems in an N+ frame, two 16-bit bitmap screens.
#include "game.h"
#include <string.h>
#include <stdio.h>

static u16 *vramMain, *vramSub;
static u16 bufMain[SCREEN_W * SCREEN_H];
static u16 bufSub[SCREEN_W * SCREEN_H];
static u16 *cur;
static int frame;

#define C(r,g,b) ARGB16(1,(r),(g),(b))
#define BLACK      C(1,1,2)
#define INK        C(24,24,26)
#define GOLD       C(28,22,6)
#define GOLD_DK    C(16,11,2)
#define ACCENT     C(6,28,24)
#define WHITE      C(31,31,31)
#define DIM        C(6,6,8)
#define FIRE1      C(31,12,2)
#define FIRE2      C(31,24,4)

typedef struct { u16 dark, mid, light; } GemPal;
static const GemPal GEM_RUBY     = { C(14,1,4),  C(26,3,8),   C(31,14,18) };
static const GemPal GEM_SAPPHIRE = { C(2,4,14),  C(4,10,27),  C(14,20,31) };
static const GemPal GEM_EMERALD  = { C(1,10,4),  C(3,21,9),   C(14,31,18) };
static const GemPal GEM_AMBER    = { C(14,8,0),  C(28,17,2),  C(31,27,12) };
static const GemPal GEM_AMETHYST = { C(10,2,14), C(19,5,26),  C(27,17,31) };
static const GemPal GEM_DIAMOND  = { C(18,20,22),C(27,28,30), C(31,31,31) };
static const GemPal GEM_FIRE     = { C(16,2,0),  C(31,10,0),  C(31,26,6) };

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
    for (int j = 0; j < h; j++) {
        u16 *p = cur + (y + j) * SCREEN_W + x;
        for (int i = 0; i < w; i++) p[i] = col;
    }
}

static void rectOutline(int x, int y, int w, int h, u16 col)
{
    for (int i = 0; i < w; i++) { px(x + i, y, col); px(x + i, y + h - 1, col); }
    for (int j = 0; j < h; j++) { px(x, y + j, col); px(x + w - 1, y + j, col); }
}

static void roundRect(int x, int y, int w, int h, u16 col)
{
    fillRect(x + 2, y, w - 4, h, col);
    fillRect(x, y + 2, w, h - 4, col);
    fillRect(x + 1, y + 1, w - 2, h - 2, col);
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

static int textWidth(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

static void drawTextCenter(int cx, int y, const char *s, u16 col, int scale)
{
    drawText(cx - textWidth(s, scale) / 2, y, s, col, scale);
}

// Faceted glossy gem with a highlight band
static void drawGem(int x, int y, const GemPal *p, bool lit, bool outlineOnly)
{
    if (outlineOnly) { rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, DIM); return; }
    roundRect(x + 1, y + 1, TILE_W - 2, TILE_H - 2, p->dark);
    roundRect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, p->mid);
    // facets
    for (int j = 0; j < TILE_H - 6; j++) px(x + 3 + j / 3, y + 3 + j, p->light);
    fillRect(x + 4, y + 3, TILE_W - 9, 2, p->light);
    fillRect(x + 5, y + 5, TILE_W - 11, 1, p->light);
    fillRect(x + 3, y + TILE_H - 5, TILE_W - 6, 2, p->dark);
    if (lit) rectOutline(x, y, TILE_W, TILE_H, WHITE), rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, WHITE);
}

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
    // ignore the extreme edges so drags between tiles feel deliberate
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

static bool inChain(const Game *g, int c, int r)
{
    for (int i = 0; i < g->chainLen; i++) if (g->chainC[i] == c && g->chainR[i] == r) return true;
    return false;
}

static void drawLetter(int x, int y, int letter, u16 col)
{
    if (letter == 'q' - 'a') {
        drawChar(x + 2, y + 5, 'Q', col, 2);
        drawChar(x + 13, y + 9, 'U', col, 1);
    } else {
        drawChar(x + 7, y + 4, 'A' + letter, col, 2);
    }
}

static void drawBoard(const Game *g)
{
    bool peek = g->peekHeld;
    // columns with fire above glow red in peek mode
    for (int c = 0; c < COLS; c++) {
        for (int r = 0; r < ROWS; r++) {
            const Cell *cl = &g->cell[c][r];
            int x, y; cellOrigin(c, r, &x, &y);
            int yd = y + cl->yoff;
            bool lit = inChain(g, c, r);
            if (peek && cl->state != CELL_SECRET && boardFireAbove(g, c, r) && (cl->state == CELL_EMPTY || cl->state == CELL_HOLE))
                fillRect(x + 2, y + 2, TILE_W - 4, TILE_H - 4, C(10,2,0));
            switch (cl->state) {
                case CELL_EMPTY:
                    if (peek) rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, DIM);
                    break;
                case CELL_HOLE: {
                    rectOutline(x + 1, y + 1, TILE_W - 2, TILE_H - 2, g->scorched[c][r] ? C(12,4,0) : ACCENT);
                    int n = boardBackNumber(g, c, r);
                    char s[2] = { '0' + n, 0 };
                    drawText(x + 10, y + 7, s, n ? ACCENT : DIM, 1);
                    if (peek && boardFireAbove(g, c, r)) rectOutline(x, y, TILE_W, TILE_H, FIRE1);
                    break;
                }
                case CELL_SECRET:
                    drawGem(x, yd, &GEM_DIAMOND, lit, false);
                    drawLetter(x, yd, cl->letter, C(4,6,10));
                    if ((frame & 31) < 4) rectOutline(x + 1, yd + 1, TILE_W - 2, TILE_H - 2, WHITE);
                    break;
                case CELL_BURN: {
                    bool flick = ((frame + c * 7 + r * 3) & 8);
                    GemPal fp = GEM_FIRE; if (flick) fp.mid = FIRE2;
                    drawGem(x, yd, &fp, lit, false);
                    drawLetter(x, yd, cl->letter, WHITE);
                    // flame licks
                    for (int i = 0; i < 4; i++) px(x + 4 + i * 5 + (flick ? 1 : 0), yd - 1 - ((frame >> 2) + i) % 3, FIRE2);
                    break;
                }
                default:
                    if (peek) {
                        drawGem(x, yd, gemFor(cl->letter), false, true);
                        drawChar(x + 10, yd + 8, 'A' + cl->letter, DIM, 1);
                        if (cl->letter == 'q' - 'a') drawChar(x + 15, yd + 8, 'U', DIM, 1);
                    } else {
                        drawGem(x, yd, gemFor(cl->letter), lit, false);
                        drawLetter(x, yd, cl->letter, lit ? WHITE : C(31,31,28));
                    }
            }
            if (cl->flash) rectOutline(x, y, TILE_W, TILE_H, WHITE);
        }
    }
    // chain links
    for (int i = 1; i < g->chainLen; i++) {
        int x0, y0, x1, y1;
        cellOrigin(g->chainC[i - 1], g->chainR[i - 1], &x0, &y0);
        cellOrigin(g->chainC[i], g->chainR[i], &x1, &y1);
        x0 += TILE_W / 2; y0 += TILE_H / 2; x1 += TILE_W / 2; y1 += TILE_H / 2;
        for (int t = 0; t <= 8; t++) {
            int xx = x0 + (x1 - x0) * t / 8, yy = y0 + (y1 - y0) * t / 8;
            fillRect(xx - 1, yy - 1, 2, 2, GOLD);
        }
    }
}

static void drawPanel(const Game *g)
{
    // gold frame around the grid
    rectOutline(0, 0, 173, SCREEN_H, GOLD_DK);
    rectOutline(1, 1, 171, SCREEN_H - 2, GOLD);
    // N+ panel
    fillRect(PANEL_X - 2, 0, SCREEN_W - PANEL_X + 2, SCREEN_H, BLACK);
    drawLogo(PANEL_X, 6, 1);
    char s[32];
    sprintf(s, "%d", g->score);
    drawText(PANEL_X, 22, s, GOLD, 2);
    sprintf(s, "SHELF %d", g->shelf);
    drawText(PANEL_X, 42, s, INK, 1);
    drawText(PANEL_X, 52, g->lockdown ? "LOCKDOWN" : "CLASSIC", g->lockdown ? FIRE1 : ACCENT, 1);

    // current chain
    char w[24]; boardChainWord(g, w);
    for (char *p = w; *p; p++) *p -= 32;
    drawText(PANEL_X, 70, w, WHITE, 1);
    if (g->chainLen >= 3 && !g->peekHeld) {
        bool ok = dictLookup(w, strlen(w));
        drawText(PANEL_X, 82, ok ? "VALID" : "...", ok ? ACCENT : DIM, 1);
    }
    // heat
    drawText(PANEL_X, 98, "HEAT", DIM, 1);
    int hw = 70 * g->heat / (g->heatThreshold ? g->heatThreshold : 1);
    rectOutline(PANEL_X, 108, 72, 6, DIM);
    if (hw > 0) fillRect(PANEL_X + 1, 109, hw, 4, hw > 50 ? FIRE1 : GOLD);

    // submit button: pulses when a valid word is chained
    bool valid = g->chainLen >= 3 && dictLookup(w, strlen(w));
    u16 bc = valid ? ((frame & 16) ? GOLD : GOLD_DK) : DIM;
    roundRect(PANEL_X, SUBMIT_Y, 76, 24, bc);
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

static void drawTop(const Game *g)
{
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    // gold plaque
    rectOutline(8, 8, 240, 176, GOLD_DK);
    rectOutline(9, 9, 238, 174, GOLD);
    drawLogo(16, 16, 2);
    char s[40];
    sprintf(s, "SHELF %d", g->shelf);
    drawText(170, 18, s, INK, 1);
    sprintf(s, "%d", g->score);
    drawText(170, 28, s, GOLD, 1);

    // the pass: gem-cut blanks
    int n = g->passLen, bw = 22, gap = 4;
    int x0 = (SCREEN_W - (n * bw + (n - 1) * gap)) / 2, y0 = 58;
    drawTextCenter(SCREEN_W / 2, 46, "PASS", DIM, 1);
    for (int i = 0; i < n; i++) {
        int x = x0 + i * (bw + gap);
        if (g->found[i]) {
            roundRect(x, y0, bw, 26, GEM_DIAMOND.mid);
            fillRect(x + 3, y0 + 3, bw - 8, 2, WHITE);
            drawChar(x + 6, y0 + 6, 'A' + (g->pass[i] - 'a'), C(4,6,10), 2);
        } else {
            rectOutline(x, y0, bw, 26, GOLD_DK);
            fillRect(x + 4, y0 + 22, bw - 8, 1, GOLD);
        }
    }

    // last word and current chain
    char w[24]; boardChainWord(g, w);
    for (char *p = w; *p; p++) *p -= 32;
    if (w[0]) drawTextCenter(SCREEN_W / 2, 100, w, WHITE, 3);
    else if (g->lastWord[0]) {
        char lw[16]; strcpy(lw, g->lastWord);
        for (char *p = lw; *p; p++) *p -= 32;
        drawTextCenter(SCREEN_W / 2, 100, lw, INK, 2);
        sprintf(s, "+%d", g->lastWordScore);
        drawTextCenter(SCREEN_W / 2, 118, s, GOLD, 1);
    }

    // fire meter
    drawText(222, 48, "FIRE", DIM, 1);
    rectOutline(226, 58, 8, ROWS * 10 + 2, DIM);
    int low = boardLowestFireRow(g);
    for (int r = 0; r <= low; r++) fillRect(227, 59 + r * 10, 6, 9, (r == low && (frame & 8)) ? FIRE2 : FIRE1);

    // shelf map
    for (int i = 0; i < 20; i++) {
        int x = 18 + i * 11, y = 150;
        if (i + 1 < g->shelf) fillRect(x, y, 8, 8, ACCENT);
        else if (i + 1 == g->shelf) rectOutline(x, y, 8, 8, (frame & 16) ? WHITE : ACCENT);
        else rectOutline(x, y, 8, 8, DIM);
    }
    if (g->msg[0]) drawTextCenter(SCREEN_W / 2, 166, g->msg, g->msgTimer > 0 ? GOLD : INK, 1);
}

static void drawOverlay(const Game *g)
{
    if (g->state == ST_PLAY) return;
    // dim the board
    for (int y = 0; y < SCREEN_H; y += 2)
        for (int x = (y >> 1) & 1; x < 173; x += 2) cur[y * SCREEN_W + x] = BLACK;
    if (g->state == ST_TITLE) {
        fillRect(16, 52, 140, 90, BLACK);
        rectOutline(16, 52, 140, 90, GOLD);
        drawLogo(40, 62, 2);
        drawTextCenter(86, 86, "CRACK THE PASS", ACCENT, 1);
        drawTextCenter(86, 98, "DON'T BURN THE", INK, 1);
        drawTextCenter(86, 106, "LIBRARY", INK, 1);
        if (frame & 32) drawTextCenter(86, 126, "TAP TO START", WHITE, 1);
    } else if (g->state == ST_PASSED) {
        fillRect(16, 70, 140, 50, BLACK);
        rectOutline(16, 70, 140, 50, GOLD);
        drawTextCenter(86, 80, "PASS ACCEPTED", GOLD, 1);
        char s[24]; sprintf(s, "SHELF %d OPEN", g->shelf + 1);
        drawTextCenter(86, 94, s, ACCENT, 1);
        if (frame & 32) drawTextCenter(86, 106, "TAP TO CONTINUE", INK, 1);
    } else if (g->state == ST_FAIL) {
        fillRect(16, 66, 140, 60, BLACK);
        rectOutline(16, 66, 140, 60, FIRE1);
        const char *t = g->fail == FAIL_FIRE ? "LIBRARY LOST" : g->fail == FAIL_SCORCH ? "PASS DESTROYED" : "OUT OF WORDS";
        drawTextCenter(86, 76, t, FIRE2, 1);
        const char *why = g->fail == FAIL_FIRE ? "FIRE REACHED THE FLOOR" : g->fail == FAIL_SCORCH ? "A PASS LETTER BURNED" : "NO WORDS LEFT TO SPELL";
        drawTextCenter(86, 90, why, INK, 1);
        if (frame & 32) drawTextCenter(86, 110, "TAP TO RETRY SHELF", WHITE, 1);
    } else if (g->state == ST_PAUSE) {
        fillRect(40, 80, 92, 30, BLACK);
        rectOutline(40, 80, 92, 30, ACCENT);
        drawTextCenter(86, 92, "PAUSED", ACCENT, 1);
    }
}

void gfxInit(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bgs = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    vramMain = bgGetGfxPtr(bg);
    vramSub = bgGetGfxPtr(bgs);
    lcdMainOnBottom();
}

void gfxFrame(const Game *g)
{
    frame++;
    cur = bufMain;
    fillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
    drawBoard(g);
    drawPanel(g);
    drawOverlay(g);
    cur = bufSub;
    drawTop(g);
    swiWaitForVBlank();
    dmaCopy(bufMain, vramMain, sizeof(bufMain));
    dmaCopy(bufSub, vramSub, sizeof(bufSub));
}
