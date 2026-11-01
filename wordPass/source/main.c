// wordPass: Bookworm chaining on a hex grid, a hidden pass behind the board,
// and fire that punishes lazy words and careless peeling.
#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

Game G;

static void setMsg(const char *m, int frames)
{
    strncpy(G.msg, m, 31); G.msg[31] = 0;
    G.msgTimer = frames;
}

// a message queued behind the current one: shown when msgTimer runs out
static char pendingMsg[32];
static int pendingFrames;

static void queueMsg(const char *m, int frames)
{
    strncpy(pendingMsg, m, 31); pendingMsg[31] = 0;
    pendingFrames = frames;
}

static void startShelf(void)
{
    boardNewShelf(&G);
    G.newBest = 0;
    gfxFadeIn();
    musicNewShelf(G.shelf, G.lockdown);
    char s[32];
    siprintf(s, "%s: FIND THE %d LETTER PASS", boardPassTheme(&G), G.passLen);   // 31 chars at most
    pendingMsg[0] = 0;
    if (G.twist) {
        char t[32];
        siprintf(t, "SHELF %d: %s", G.shelf, boardTwistName(G.twist));
        setMsg(t, 150);
        queueMsg(s, 180);
    } else setMsg(s, 180);
}

static int todaySeed(void)
{
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    if (!tm) return 20260101;
    return (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
}

static void newGame(int mode)
{
    G.mode = mode;
    G.shelf = 1;
    G.score = 0;
    G.hintShown = 0;
    G.lives = 3;
    G.twist = 0; G.lastTwist = 0;
    memset(G.recentPass, 0, sizeof(G.recentPass)); G.recentHead = 0;
    if (mode == MODE_DAILY) { G.dailySeed = todaySeed(); srand(G.dailySeed); }
    startShelf();
    G.state = ST_PLAY;
}

static void recordScore(void)
{
    bool changed = false;
    if (G.mode == MODE_DAILY) {
        if (G.dailySeed != S.bestDailyDate || G.score > S.bestDaily) { S.bestDaily = G.score; S.bestDailyDate = G.dailySeed; changed = true; }
    } else {
        if (G.score > S.bestClassic) { S.bestClassic = G.score; changed = true; }
        if (G.state == ST_PASSED && G.shelf > S.bestClassicShelf) { S.bestClassicShelf = G.shelf; changed = true; }
    }
    if (changed) { G.newBest = 1; settingsSave(); }
}

static void cycleOption(int row, int dir)
{
    switch (row) {
        case 0: S.theme = (S.theme + THEME_COUNT + dir) % THEME_COUNT; break;
        case 1: S.frame = (S.frame + FRAME_COUNT + dir) % FRAME_COUNT; break;
        case 2: S.accent = (S.accent + ACCENT_COUNT + dir) % ACCENT_COUNT; break;
        case 3: S.hand = !S.hand; break;
        case 4: S.fire = (S.fire + FIRE_COUNT + dir) % FIRE_COUNT; break;
        case 5: S.music = !S.music; break;
        case 6: S.library = (S.library + LIB_COUNT + dir) % LIB_COUNT; break;
    }
    gfxApplySettings();
}

static void quitToTitle(void)
{
    G.mode = MODE_CLASSIC;
    G.shelf = 1;
    G.score = 0;
    G.twist = 0;
    boardNewShelf(&G);
    G.state = ST_TITLE;
    G.msg[0] = 0; G.msgTimer = 0; pendingMsg[0] = 0;
    gfxFadeIn();
    musicNewShelf(1, false);
}

static void enterFail(void)
{
    G.state = ST_FAIL;
    boardClearChain(&G);
    if (G.mode == MODE_CLASSIC && G.lives > 0) G.lives--;   // a failed shelf costs a bookmark
    recordScore();
    gfxShake(14);
    gfxFlash();
    G.peekHeld = 0;
    musicPeek(false);
    G.stateTimer = 0;
    musicFail();
}

// Sparks on the gem(s) that caught fire this turn (spawnFire leaves them in fireC/fireR).
static void burstNewFlames(void)
{
    for (int i = 0; i < G.fireN; i++) gfxBurst(G.fireC[i], G.fireR[i], BURST_FIRE, G.cell[G.fireC[i]][G.fireR[i]].letter);
    G.fireN = 0;
}

// Submit the current chain: score it, shatter the gems (in the colours they had), callouts.
static void submitChain(void)
{
    u8 cc[MAX_CHAIN], cr[MAX_CHAIN], cl[MAX_CHAIN];
    int cn = G.chainLen;
    for (int i = 0; i < cn; i++) {
        cc[i] = G.chainC[i]; cr[i] = G.chainR[i];
        cl[i] = G.cell[cc[i]][cr[i]].letter;     // snapshot: gravity moves the letters
    }
    int gained = boardSubmit(&G);
    if (gained > 0) {
        for (int i = 0; i < cn; i++) gfxBurst(cc[i], cr[i], BURST_SHATTER, cl[i]);
        char ps[12]; siprintf(ps, "+%d", gained);
        gfxPopup(cc[cn / 2], cr[cn / 2], ps, 1);
        musicWord(G.lastWordLen, gained);
        // callout, one per word, in priority order
        if (G.lastWordLen >= 8) setMsg("SPECTACULAR!", 60);
        else if (G.lastWordDiamonds >= 1) {
            // the "+N" popup sits on the middle gem: put PASS LINK on the diamond (or the
            // chain's first gem when the diamond is the middle one) so the two never stack
            int d = cn / 2;
            for (int i = 0; i < cn; i++) if (G.cell[cc[i]][cr[i]].state == CELL_SECRET) { d = i; break; }
            if (d == cn / 2) d = 0;
            gfxPopup(cc[d], cr[d], "PASS LINK", 1);
            setMsg("PASS LINK!", 60);
        }
        else if (G.lastWordLen >= 7) setMsg("EXCELLENT!", 60);
        else if (G.lastWordLen >= 5) setMsg("GOOD!", 60);
        else if (G.lastWordRepeat) setMsg("AGAIN", 45);
        else if (G.lastWordTier == 2) setMsg("RARE WORD", 60);
        burstNewFlames();
        if (G.fail) enterFail();
    } else if (G.chainLen >= 3) {
        char w[24]; boardChainWord(&G, w);
        bool obscure = S.library == LIB_COMMON && boardWordTier(w, strlen(w)) == 2;
        setMsg(obscure ? "TOO OBSCURE" : "NOT IN THE LIBRARY", 45);
        gfxInvalidWord();
    }
}

static void handlePlay(u32 down, u32 held, touchPosition *t)
{
    bool peekNow = (held & (KEY_L | KEY_R)) != 0;
    if (peekNow != G.peekHeld) { G.peekHeld = peekNow; musicPeek(peekNow); }

    if (down & KEY_START) { G.state = ST_PAUSE; return; }   // a held chain survives the pause
    if (down & KEY_B) boardClearChain(&G);

    int c, r;
    bool touching = (held & KEY_TOUCH) != 0;
    if (touching && t->px == 0 && t->py == 0) touching = false;

    if (G.peekHeld) {
        if ((down & KEY_TOUCH) && gfxCellAt(t->px, t->py, &c, &r)) {
            int letter = G.cell[c][r].letter;
            int res = boardPeel(&G, c, r);
            if (res == 2) {
                gfxBurst(c, r, BURST_REVEAL, letter);
                int reward = 100 * G.shelf;              // every pass letter pays
                G.score += reward;
                char ps[12]; siprintf(ps, "+%d", reward);
                gfxPopup(c, r, ps, 1);
                musicReveal(true);
                if (boardPassComplete(&G)) {
                    char s[32]; siprintf(s, "SHELF %d CLEARED", G.shelf); setMsg(s, 400);
                } else setMsg("PASS LETTER FOUND", 90);
            } else if (res == 1) musicReveal(false);
            else if (res == 3 || res == -1) { setMsg("SCORCHED", 60); gfxBurst(c, r, BURST_SCORCH, letter); gfxShake(6); }
            burstNewFlames();
            // the peel that completes the pass wins the shelf even if its turn ended badly
            if (G.fail && !boardPassComplete(&G)) { enterFail(); return; }
            if (boardPassComplete(&G)) {
                G.state = ST_PASSED; G.stateTimer = 0; G.fail = FAIL_NONE;
                boardClearChain(&G);
                G.peekHeld = 0; musicPeek(false);
                // shelf bonus, a speed bonus for a quick pass, and a bonus for a tidy peel count
                int shelfBonus = 1000 * G.shelf;
                int left = 40 - G.turn;
                G.speedBonus = 25 * G.shelf * (left > 0 ? left : 0);
                if (G.peels == G.passLen) { G.passBonusKind = 2; G.peelBonus = 400 * G.shelf; }
                else if (G.peels <= G.passLen + 2) { G.passBonusKind = 1; G.peelBonus = 150 * G.shelf; }
                else { G.passBonusKind = 0; G.peelBonus = 0; }
                G.score += shelfBonus + G.speedBonus + G.peelBonus;
                if (G.lockdown && G.mode == MODE_CLASSIC && G.lives < 3) G.lives++;   // a Lockdown shelf earns a bookmark back
                if (G.passBonusKind == 2)                // a perfect pass: every diamond sparkles
                    for (int dc = 0; dc < COLS; dc++)
                        for (int dr = 0; dr < ROWS; dr++)
                            if (G.cell[dc][dr].state == CELL_SECRET) gfxBurst(dc, dr, BURST_REVEAL, G.cell[dc][dr].letter);
                musicPassComplete();
                recordScore();
            }
        }
        return;
    }

    if (touching) {
        if (gfxCellAt(t->px, t->py, &c, &r)) {
            boardTryAddToChain(&G, c, r);
        } else if (down & KEY_TOUCH) {
            if (gfxInSubmit(t->px, t->py)) submitChain();
            else if (gfxInClear(t->px, t->py)) boardClearChain(&G);
        }
    }
    if (down & KEY_A) submitChain();
    // first-shelf nudge: three words in and no peek yet
    if (G.shelf == 1 && G.mode == MODE_CLASSIC && !G.hintShown && G.peels == 0 && G.wordsThisShelf >= 3) {
        G.hintShown = 1;
        setMsg("HOLD L + TAP A GEM TO PEEK", 300);
    }
}

int main(void)
{
    settingsInit();
    gfxInit();
    dictInit();
    poolsInit();
    musicInit();
    memset(&G, 0, sizeof(G));
    G.state = ST_TITLE;
    G.shelf = 1;
    boardNewShelf(&G);
    unsigned seedCounter = 0;

    while (1) {
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        touchPosition t; touchRead(&t);
        seedCounter++;

        switch (G.state) {
            case ST_TITLE:
                if (down & KEY_TOUCH) {
                    // only the drawn buttons do anything; a stray tap elsewhere is ignored
                    srand(seedCounter ^ (t.px << 8) ^ t.py);
                    if (gfxInTitleOptions(t.px, t.py)) { G.state = ST_OPTIONS; G.optRow = 0; }
                    else if (gfxInTitleDaily(t.px, t.py)) newGame(MODE_DAILY);
                    else if (gfxInTitleClassic(t.px, t.py)) newGame(MODE_CLASSIC);
                } else if (down & (KEY_START | KEY_A)) { srand(seedCounter); newGame(MODE_CLASSIC); }
                else if (down & KEY_SELECT) newGame(MODE_DAILY);
                else if (down & KEY_Y) { G.state = ST_OPTIONS; G.optRow = 0; }
                break;
            case ST_OPTIONS: {
                int row = (down & KEY_TOUCH) ? gfxOptionRowAt(t.px, t.py) : -1;
                if (row == OPT_ROWS || (down & KEY_B)) { settingsSave(); G.state = ST_TITLE; break; }
                if (row >= 0) { G.optRow = row; cycleOption(row, 1); }
                if (down & KEY_UP)    G.optRow = (G.optRow + OPT_ROWS - 1) % OPT_ROWS;
                if (down & KEY_DOWN)  G.optRow = (G.optRow + 1) % OPT_ROWS;
                if (down & (KEY_RIGHT | KEY_A)) cycleOption(G.optRow, 1);
                if (down & KEY_LEFT)  cycleOption(G.optRow, -1);
                break;
            }
            case ST_PLAY:
                handlePlay(down, held, &t);
                break;
            case ST_PASSED:
                G.stateTimer++;
                if (G.stateTimer > 30 && (down & (KEY_TOUCH | KEY_START | KEY_A))) {
                    if (G.mode == MODE_DAILY) { quitToTitle(); break; }
                    G.shelf++;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_FAIL:
                G.stateTimer++;
                if (G.stateTimer > 45 && (down & (KEY_TOUCH | KEY_START | KEY_A))) {
                    if (G.mode == MODE_DAILY || G.lives <= 0) { quitToTitle(); break; }   // no bookmark left: the run is over
                    G.score = G.shelfStartScore;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_PAUSE:
                if (down & KEY_START) G.state = ST_PLAY;
                else if (down & KEY_SELECT) {
                    // a restart costs a bookmark in Classic and is refused on the last one
                    if (G.mode == MODE_CLASSIC && G.lives <= 1) { setMsg("LAST BOOKMARK", 60); break; }
                    if (G.mode == MODE_CLASSIC) G.lives--;
                    G.score = G.shelfStartScore; startShelf(); G.state = ST_PLAY;
                }
                else if (down & KEY_B) quitToTitle();
                break;
        }
        if (G.msgTimer > 0 && --G.msgTimer == 0) {
            G.msg[0] = 0;
            if (pendingMsg[0]) { setMsg(pendingMsg, pendingFrames); pendingMsg[0] = 0; }
        }
        boardAnimate(&G);
        musicTick(&G);
        debugSync(&G);
        gfxFrame(&G);
    }
    return 0;
}
