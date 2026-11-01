// wordPass: Bookworm chaining on a hex grid, a hidden pass behind the board,
// and fire that punishes lazy words and careless peeling.
#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Game G;

static void setMsg(const char *m, int frames)
{
    strncpy(G.msg, m, 31); G.msg[31] = 0;
    G.msgTimer = frames;
}

static void startShelf(void)
{
    boardNewShelf(&G);
    gfxFadeIn();
    musicNewShelf(G.shelf, G.lockdown);
    char s[32];
    sprintf(s, "FIND THE %d LETTER PASS", G.passLen);
    setMsg(s, 180);
}

static void newGame(void)
{
    G.shelf = 1;
    G.score = 0;
    startShelf();
    G.state = ST_PLAY;
}

static void enterFail(void)
{
    G.state = ST_FAIL;
    gfxShake(14);
    gfxFlash();
    G.peekHeld = 0;
    musicPeek(false);
    G.stateTimer = 0;
    musicFail();
}

static void handlePlay(u32 down, u32 held, touchPosition *t)
{
    bool peekNow = (held & (KEY_L | KEY_R)) != 0;
    if (peekNow != G.peekHeld) { G.peekHeld = peekNow; musicPeek(peekNow); }

    if (down & KEY_START) { G.prevState = G.state; G.state = ST_PAUSE; return; }
    if (down & KEY_B) boardClearChain(&G);

    int c, r;
    bool touching = (held & KEY_TOUCH) != 0;
    if (touching && t->px == 0 && t->py == 0) touching = false;

    if (G.peekHeld) {
        if ((down & KEY_TOUCH) && gfxCellAt(t->px, t->py, &c, &r)) {
            int res = boardPeel(&G, c, r);
            if (res == 2) {
                gfxBurst(c, r, BURST_REVEAL);
                gfxPopup(c, r, "FOUND", 1);
                musicReveal(true);
                if (boardPassComplete(&G)) {
                    char s[32]; sprintf(s, "SHELF %d CLEARED", G.shelf); setMsg(s, 400);
                } else setMsg("PASS LETTER FOUND", 90);
            } else if (res == 1) musicReveal(false);
            else if (res == 3) { setMsg("SCORCHED", 60); gfxBurst(c, r, BURST_SCORCH); gfxShake(6); }
            if (G.fail) { enterFail(); return; }
            if (boardPassComplete(&G)) {
                G.state = ST_PASSED; G.stateTimer = 0;
                G.peekHeld = 0; musicPeek(false);
                G.score += 500 * G.shelf;
                musicPassComplete();
            }
        }
        return;
    }

    if (touching) {
        if (gfxCellAt(t->px, t->py, &c, &r)) {
            boardTryAddToChain(&G, c, r);
        } else if (down & KEY_TOUCH) {
            if (gfxInSubmit(t->px, t->py)) goto submit;
            if (gfxInClear(t->px, t->py)) boardClearChain(&G);
        }
    }
    if (down & KEY_A) {
submit:;
        u8 cc[MAX_CHAIN], cr[MAX_CHAIN]; int cn = G.chainLen;
        memcpy(cc, G.chainC, cn); memcpy(cr, G.chainR, cn);
        int gained = boardSubmit(&G);
        if (gained > 0) {
            for (int i = 0; i < cn; i++) gfxBurst(cc[i], cr[i], BURST_SHATTER);
            char ps[12]; sprintf(ps, "+%d", gained);
            gfxPopup(cc[cn / 2], cr[cn / 2], ps, 1);
            musicWord(G.lastWordLen, gained);
            char s[32];
            const char *tag = G.lastWordLen >= 8 ? "SPECTACULAR" : G.lastWordLen >= 7 ? "EXCELLENT" : G.lastWordLen >= 5 ? "GOOD" : "";
            if (tag[0]) { sprintf(s, "%s!", tag); setMsg(s, 60); }
            if (G.fail) enterFail();
        } else if (G.chainLen >= 3) {
            setMsg("NOT IN THE LIBRARY", 45);
        }
    }
}

int main(void)
{
    gfxInit();
    dictInit();
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
                if (down & (KEY_TOUCH | KEY_START | KEY_A)) { srand(seedCounter ^ (t.px << 8) ^ t.py); newGame(); }
                break;
            case ST_PLAY:
                handlePlay(down, held, &t);
                break;
            case ST_PASSED:
                G.stateTimer++;
                if (G.stateTimer > 30 && (down & (KEY_TOUCH | KEY_START | KEY_A))) {
                    G.shelf++;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_FAIL:
                G.stateTimer++;
                if (G.stateTimer > 45 && (down & (KEY_TOUCH | KEY_START | KEY_A))) {
                    G.score = G.shelfStartScore;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_PAUSE:
                if (down & KEY_START) G.state = ST_PLAY;
                break;
        }
        if (G.msgTimer > 0 && --G.msgTimer == 0) G.msg[0] = 0;
        boardAnimate(&G);
        musicTick(&G);
        gfxFrame(&G);
    }
    return 0;
}
