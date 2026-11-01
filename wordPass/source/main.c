// wordPass: Bookworm chaining on a hex grid, a hidden pass behind the board,
// and fire that punishes lazy words and careless peeling.
#include "game.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

Game G;

static void setMsg(const char *m, int frames)      // frames -1: sticky, shown until something replaces it
{
    strncpy(G.msg, m, MSG_LEN - 1); G.msg[MSG_LEN - 1] = 0;
    G.msgTimer = frames;
}

// a message queued behind the current one: shown when msgTimer runs out
static char pendingMsg[MSG_LEN];
static int pendingFrames;

static void queueMsg(const char *m, int frames)
{
    strncpy(pendingMsg, m, MSG_LEN - 1); pendingMsg[MSG_LEN - 1] = 0;
    pendingFrames = frames;
}

// ---- the guided first shelf: sticky prompts on the top screen while Classic shelf 1 is new to the player.
// TUTOR_DRAG: drag a word. TUTOR_PEEK: peek. TUTOR_FIND: (after the first peel) find a pass letter.
// TUTOR_REST: the closing tip, whose end ends the guide. A gameplay message replaces a prompt for its
// duration and the prompt comes back when it is over (tutorAfterMsg).
static bool tutorOn(void) { return G.mode == MODE_CLASSIC && G.shelf == 1 && !S.tutorDone && G.tutorStep < TUTOR_DONE; }

static int foundCount(void)
{
    int n = 0;
    for (int i = 0; i < G.passLen; i++) n += G.found[i];
    return n;
}

static void tutorPrompt(void)
{
    if (!tutorOn()) return;
    if (G.tutorStep == TUTOR_DRAG) setMsg("DRAG 3 TOUCHING GEMS, TAP THE LAST", -1);
    else if (G.tutorStep < TUTOR_REST) setMsg(S.hand == HAND_LEFT ? "HOLD R OR TAP PEEK, THEN TAP A GEM" : "HOLD L OR TAP PEEK, THEN TAP A GEM", -1);
}

static void tutorFinish(void)
{
    if (G.tutorStep < TUTOR_DONE && !S.tutorDone && G.mode == MODE_CLASSIC) { S.tutorDone = 1; settingsSave(); }
    G.tutorStep = TUTOR_DONE;
}

// the message line just ran out: the guide's next prompt, or its end
static void tutorAfterMsg(void)
{
    if (!tutorOn()) return;
    if (G.tutorStep == TUTOR_REST) { tutorFinish(); return; }
    if (G.tutorStep == TUTOR_FIND && foundCount() >= 1) { G.tutorStep = TUTOR_REST; setMsg("FIND THE REST. DIAMONDS NEVER BURN", 240); return; }
    tutorPrompt();
}

static void tutorWord(void)            // a word scored
{
    if (!tutorOn() || G.tutorStep != TUTOR_DRAG) return;
    G.tutorStep = TUTOR_PEEK;
    if (G.msgTimer < 0) tutorPrompt();   // swap the sticky prompt at once; a callout that is up keeps the line for its moment
}

static void tutorPeel(int res, int c, int r)   // a peel resolved: res as boardPeel returns it
{
    if (!tutorOn() || G.tutorStep == TUTOR_DRAG) return;
    if (G.tutorStep == TUTOR_PEEK) {     // the first peel gets a message about what it found
        G.tutorStep = TUTOR_FIND;
        if (res == 2) setMsg("A PASS LETTER! PEEL AROUND IT", 240);
        else if (res == 1) {
            char t[MSG_LEN]; int n = boardBackNumber(&G, c, r);
            if (n == 0) strcpy(t, "0: NO PASS LETTERS TOUCH THIS CELL");
            else if (n == 1) strcpy(t, "1: A PASS LETTER TOUCHES THIS CELL");
            else siprintf(t, "%d: PASS LETTERS TOUCH THIS CELL", n);
            setMsg(t, 240);
        }
    } else if (G.tutorStep == TUTOR_FIND && res == 2) {
        G.tutorStep = TUTOR_REST;
        setMsg("FIND THE REST. DIAMONDS NEVER BURN", 240);
    }
}

// a sticky prompt must not outlive the moment it was written for
static void clearSticky(void)
{
    if (G.msgTimer < 0) { G.msg[0] = 0; G.msgTimer = 0; }
}

static void startShelf(void)
{
    G.armed = 0; G.peekLatch = 0;
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

// Today's date from the DS clock. The title refreshes it once a second, and starting a Daily
// reads it again, so a shelf is always seeded from the date it was started on.
static void refreshClock(void)
{
    int today = settingsToday();
    G.clockOk = settingsClockOk(today);
    G.dailySeed = G.clockOk ? today : 0;
    G.dailyMod = G.clockOk ? boardDailyMod(today) : 0;
}

static bool dailyRecorded;      // this Daily attempt's result is already in the records

static bool newGame(int mode)
{
    G.practice = false;
    if (mode == MODE_DAILY) {
        refreshClock();
        if (!G.clockOk) { setMsg("SET THE DS CLOCK FOR DAILY", 120); return false; }
        // one scored attempt a day: the second and later runs are practice (nothing recorded)
        G.practice = (S.dailyPlayedDate == G.dailySeed);
        if (!G.practice) { S.dailyPlayedDate = G.dailySeed; settingsSave(); }
        dailyRecorded = false;
        srand(G.dailySeed);
    }
    G.mode = mode;
    G.shelf = 1;
    G.score = 0;
    G.hintShown = 0;
    G.curC = 3; G.curR = 3; G.curOn = 0;
    G.lives = 3;
    G.tutorStep = (mode == MODE_CLASSIC && !S.tutorDone) ? TUTOR_DRAG : TUTOR_DONE;
    G.twist = 0; G.lastTwist = 0;
    memset(G.recentPass, 0, sizeof(G.recentPass)); G.recentHead = 0;
    startShelf();
    if (mode == MODE_DAILY) {
        char t[32];
        if (G.practice) strcpy(t, "PRACTICE RUN: NOT SCORED");
        else siprintf(t, "DAILY: %s", boardDailyModName(G.dailyMod));
        queueMsg(G.msg, G.msgTimer);       // the shelf intro follows
        setMsg(t, 150);
    }
    G.state = ST_PLAY;
    return true;
}

// Fold a finished run into the saved records. A Daily replay (practice) or a Daily without a
// readable clock records nothing; a scored Daily updates the last score, the best, the
// streak and the games played exactly once.
static void recordScore(void)
{
    bool best = false;
    if (G.mode == MODE_DAILY) {
        if (G.practice || !G.clockOk || dailyRecorded) return;
        dailyRecorded = true;
        S.dailyLastScore = G.score;
        if (G.dailySeed != S.bestDailyDate || G.score > S.bestDaily) { S.bestDaily = G.score; S.bestDailyDate = G.dailySeed; best = true; }
        S.dailyStreak = settingsNextStreak(S.dailyLastDate, S.dailyStreak, G.dailySeed);
        S.dailyLastDate = G.dailySeed;
        S.gamesPlayed++;
        if (G.state == ST_PASSED) S.shelvesCleared++;
        if (G.state == ST_PASSED && G.passBonusKind == 2) S.perfectPasses++;
        if (best) G.newBest = 1;
        settingsSave();
        return;
    }
    if (G.score > S.bestClassic) { S.bestClassic = G.score; best = true; }
    if (G.state == ST_PASSED) {
        S.shelvesCleared++;
        if (G.passBonusKind == 2) S.perfectPasses++;
        if (G.shelf > S.bestClassicShelf) { S.bestClassicShelf = G.shelf; best = true; }
    }
    if (best) { G.newBest = 1; settingsSave(); }
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
        case 7: S.spelling = (S.spelling + SPELL_COUNT + dir) % SPELL_COUNT; break;
    }
    gfxApplySettings();
    musicSfx(SFX_MENU, 0);
}

// The pause menu's rows: Daily has no RESTART (its one attempt cannot be retried)
int pauseRows(const Game *g, int ids[PAUSE_ROWS_MAX])
{
    int n = 0;
    ids[n++] = PAUSE_RESUME;
    if (g->mode != MODE_DAILY) ids[n++] = PAUSE_RESTART;
    ids[n++] = PAUSE_QUIT;
    return n;
}

static void quitToTitle(void)
{
    // walking away from a scored Daily forfeits it: the attempt is spent, so the score so far stands
    if (G.mode == MODE_DAILY && G.state == ST_PAUSE) recordScore();
    G.practice = false;
    G.mode = MODE_CLASSIC;
    G.shelf = 1;
    G.score = 0;
    G.twist = 0;
    G.armed = 0; G.peekLatch = 0;
    boardNewShelf(&G);
    G.state = ST_TITLE;
    G.msg[0] = 0; G.msgTimer = 0; pendingMsg[0] = 0;
    refreshClock();
    gfxFadeIn();
    musicNewShelf(1, false);
    settingsSave();
}

static void enterFail(void)
{
    clearSticky();
    G.state = ST_FAIL;
    boardClearChain(&G);
    if (G.mode == MODE_CLASSIC) {
        if (G.lives > 0) G.lives--;   // a failed shelf costs a bookmark
        if (G.lives == 0) S.gamesPlayed++;   // the last bookmark: the run is over
    }
    recordScore();
    gfxShake(14);
    gfxFlash();
    G.peekHeld = 0; G.armed = 0; G.peekLatch = 0;
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
        gfxSubmitFlash();
        tutorWord();
        // callout, one per word, in priority order: the top-screen message and the word over the board
        if (G.lastWordLen >= 8) { setMsg("SPECTACULAR!", 60); gfxCallout("SPECTACULAR!", CALLOUT_GOLD); }
        else if (G.lastWordDiamonds >= 1) {
            // the "+N" popup sits on the middle gem: put PASS LINK on the diamond (or the
            // chain's first gem when the diamond is the middle one) so the two never stack
            int d = cn / 2;
            for (int i = 0; i < cn; i++) if (G.cell[cc[i]][cr[i]].state == CELL_SECRET) { d = i; break; }
            if (d == cn / 2) d = 0;
            gfxPopup(cc[d], cr[d], "PASS LINK", 1);
            setMsg("PASS LINK!", 60); gfxCallout("PASS LINK!", CALLOUT_ACCENT);
        }
        else if (G.lastWordLen >= 7) { setMsg("EXCELLENT!", 60); gfxCallout("EXCELLENT!", CALLOUT_GOLD); }
        else if (G.lastWordLen >= 5) { setMsg("GOOD!", 60); gfxCallout("GOOD!", CALLOUT_GOLD); }
        else if (G.lastWordRepeat) { setMsg("AGAIN", 45); gfxCallout("AGAIN", CALLOUT_ACCENT); }
        else if (G.lastWordTier == 2) { setMsg("RARE WORD", 60); gfxCallout("RARE WORD", CALLOUT_GOLD); }
        burstNewFlames();
        if (G.fail) enterFail();
    } else if (G.chainLen >= 3) {
        char w[WORD_BUF]; boardChainWord(&G, w);
        int dialect = boardWordSpelling(w, strlen(w));
        bool obscure = S.library == LIB_COMMON && boardWordTier(w, strlen(w)) == 2;
        setMsg(dialect == 2 ? "BRITISH SPELLING" : dialect == 1 ? "AMERICAN SPELLING" : obscure ? "TOO OBSCURE" : "NOT IN THE LIBRARY", 45);
        // the board callout stays short enough to pop at 2x (the top-screen message carries the full reason)
        gfxCallout(dialect == 2 ? "UK SPELLING" : dialect == 1 ? "US SPELLING" : obscure ? "TOO OBSCURE" : "NOT A WORD", CALLOUT_FIRE);
        gfxInvalidWord();
        musicSfx(SFX_INVALID, 0);
    }
}

// Pause: the chain is dropped so the shelf comes back at rest, peek lets go, the menu starts on RESUME
static void enterPause(void)
{
    gfxPress(PRESS_PAUSE);
    boardClearChain(&G);
    G.armed = 0; G.peekLatch = 0; G.peekHeld = 0; musicPeek(false);
    G.pauseRow = 0; G.pauseArm = 0;
    G.state = ST_PAUSE;
}

// Activate a pause row. RESTART (a bookmark) and QUIT (the run) must be activated twice within 90 frames.
static void pauseActivate(int id)
{
    bool armed = G.pauseArm > 0 && G.pauseArmId == id;
    switch (id) {
        case PAUSE_RESUME:
            G.state = ST_PLAY;
            break;
        case PAUSE_RESTART:
            if (G.mode == MODE_CLASSIC && G.lives <= 1) { setMsg("LAST BOOKMARK", 60); G.pauseArm = 0; break; }   // the last one cannot be spent
            if (!armed) { G.pauseArm = 90; G.pauseArmId = id; break; }
            G.pauseArm = 0;
            if (G.mode == MODE_CLASSIC) G.lives--;
            G.score = G.shelfStartScore; startShelf(); G.state = ST_PLAY;
            break;
        case PAUSE_QUIT:
            if (!armed) { G.pauseArm = 90; G.pauseArmId = id; break; }
            G.pauseArm = 0;
            quitToTitle();
            break;
    }
}

// The chain grew or shrank this frame: a rising pentatonic tap or a short step back. A submit or a clear
// (the chain emptied from more than one gem) has its own sounds, so it is silent here.
static void chainSfx(int before)
{
    int now = G.chainLen;
    if (now > before) musicSfx(SFX_TAP, now);
    else if (now < before && !(now == 0 && before > 1)) musicSfx(SFX_UNTAP, now < 1 ? 1 : now);
}

static void handlePlay(u32 down, u32 held, touchPosition *t)
{
    if (down & KEY_X) { G.peekLatch = !G.peekLatch; gfxPress(PRESS_PEEK); }
    if ((down & KEY_TOUCH) && gfxInPeekButton(t->px, t->py)) { G.peekLatch = !G.peekLatch; gfxPress(PRESS_PEEK); }
    if ((down & KEY_TOUCH) && gfxInPauseButton(t->px, t->py)) { enterPause(); return; }
    bool peekNow = (held & (KEY_L | KEY_R)) != 0 || G.peekLatch;
    if (peekNow != G.peekHeld) { G.peekHeld = peekNow; musicPeek(peekNow); }
    if (!peekNow) G.armed = 0;

    if (down & KEY_START) { enterPause(); return; }
    int chainBefore = G.chainLen;
    // B steps the chain back one gem, held for 20 frames it clears it; Y clears at once
    static int bHeld;
    if (held & KEY_B) { if (++bHeld == 20) boardClearChain(&G); } else bHeld = 0;
    if ((down & KEY_B) && G.chainLen > 0) G.chainLen--;
    if (down & KEY_Y) { boardClearChain(&G); gfxPress(PRESS_CLEAR); }

    // the d-pad moves a hex cursor; a real touch hands control back to the stylus
    if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
        if (down & KEY_UP)    { if (G.curR > 0) G.curR--; }
        if (down & KEY_DOWN)  { if (G.curR < ROWS - 1) G.curR++; }
        if (down & KEY_LEFT)  { if (G.curC > 0) G.curC--; }
        if (down & KEY_RIGHT) { if (G.curC < COLS - 1) G.curC++; }
        G.curOn = 1;
    }
    if (down & KEY_TOUCH) G.curOn = 0;

    int c, r;
    bool touching = (held & KEY_TOUCH) != 0;
    if (touching && t->px == 0 && t->py == 0) touching = false;

    if (G.peekHeld) {
        // peel on release: a touch arms the gem under it, sliding off (or onto another gem) disarms,
        // lifting the stylus on the armed gem peels it. A latched peek is one-shot.
        bool peel = false;
        if ((down & KEY_TOUCH) && gfxCellAt(t->px, t->py, &c, &r)) { G.armed = 1; G.armC = c; G.armR = r; }
        else if (G.armed && touching && !(down & KEY_TOUCH)) {
            if (!gfxCellAt(t->px, t->py, &c, &r) || c != G.armC || r != G.armR) G.armed = 0;
        }
        if ((down & KEY_A) && G.curOn) { peel = true; c = G.curC; r = G.curR; G.armed = 0; G.peekLatch = 0; }   // A on the cursor arms and peels at once
        else if ((keysUp() & KEY_TOUCH) && G.armed) { peel = true; c = G.armC; r = G.armR; G.armed = 0; G.peekLatch = 0; }
        if (peel) {
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
                } else { setMsg("PASS LETTER FOUND", 90); gfxCallout("PASS LETTER!", CALLOUT_ACCENT); }
            } else if (res == 1) { musicSfx(SFX_PEEL, 0); musicReveal(false); }
            else if (res == 3 || res == -1) { musicSfx(SFX_SCORCH, 0); setMsg("SCORCHED", 60); gfxCallout("SCORCHED", CALLOUT_FIRE); gfxBurst(c, r, BURST_SCORCH, letter); gfxShake(6); }
            burstNewFlames();
            if (res >= 1 && !G.fail && !boardPassComplete(&G)) tutorPeel(res, c, r);   // after the gameplay messages, so the guide's line wins
            // the peel that completes the pass wins the shelf even if its turn ended badly
            if (G.fail && !boardPassComplete(&G)) { enterFail(); return; }
            if (boardPassComplete(&G)) {
                G.state = ST_PASSED; G.stateTimer = 0; G.fail = FAIL_NONE;
                if (G.shelf == 1) tutorFinish();       // the first shelf is cleared: the guide has done its job
                clearSticky();
                boardClearChain(&G);
                G.peekHeld = 0; G.armed = 0; G.peekLatch = 0; musicPeek(false);
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

    // Chain input. Between two samples a fast stylus can skip gems: walk the segment and feed every
    // gem it crosses that touches the tail as a drag (never a restart), then the final point.
    static int lastTx, lastTy;
    static bool wasTouching, ignoreStroke;
    if (!touching) { wasTouching = false; ignoreStroke = false; }
    else if (!wasTouching && !(down & KEY_TOUCH)) ignoreStroke = true;   // the stylus came down on another screen (a title button): not a chain
    else if (ignoreStroke) { lastTx = t->px; lastTy = t->py; }      // a submit tap's stylus is still down
    else {
        bool fresh = (down & KEY_TOUCH) != 0;
        if (fresh) wasTouching = false;       // a new stroke never interpolates from an old one
        if (wasTouching && G.chainLen > 0) {
            int dx = t->px - lastTx, dy = t->py - lastTy;
            int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            int mx = adx > ady ? adx : ady;
            if (mx > 8) {
                int steps = mx / 4;
                for (int i = 1; i < steps; i++) {
                    int sx = lastTx + dx * i / steps, sy = lastTy + dy * i / steps;
                    if (!gfxCellAt(sx, sy, &c, &r)) continue;
                    int tc = G.chainC[G.chainLen - 1], tr = G.chainR[G.chainLen - 1];
                    int nc[6], nr[6], n = boardNeighbors(tc, tr, nc, nr);
                    for (int k = 0; k < n; k++)
                        if (nc[k] == c && nr[k] == r) { boardChainTouch(&G, c, r, false); break; }
                }
            }
        }
        if (gfxCellAt(t->px, t->py, &c, &r)) {
            enum ChainTouch ct = boardChainTouch(&G, c, r, fresh);
            if (ct == CT_SUBMIT_TAP) { submitChain(); ignoreStroke = true; }
            else if (ct == CT_NOT_ADJACENT) { gfxNope(c, r); if (fresh) musicSfx(SFX_INVALID, 0); }
        } else if (fresh) {
            if (gfxInSubmit(t->px, t->py)) { gfxPress(PRESS_SUBMIT); submitChain(); }
            else if (gfxInClear(t->px, t->py)) { gfxPress(PRESS_CLEAR); boardClearChain(&G); }
        }
        lastTx = t->px; lastTy = t->py; wasTouching = true;
    }
    if (down & KEY_A) {
        if (G.curOn) {
            int tc = G.chainLen ? G.chainC[G.chainLen - 1] : -1, tr = G.chainLen ? G.chainR[G.chainLen - 1] : -1;
            if (G.chainLen >= 3 && tc == G.curC && tr == G.curR) { gfxPress(PRESS_SUBMIT); submitChain(); }
            else {
                enum ChainTouch ct = boardChainTouch(&G, G.curC, G.curR, G.chainLen == 0);
                if (ct == CT_NOT_ADJACENT || ct == CT_RESTARTED) { gfxNope(G.curC, G.curR); if (ct == CT_NOT_ADJACENT) musicSfx(SFX_INVALID, 0); }
            }
        } else { gfxPress(PRESS_SUBMIT); submitChain(); }
    }
    chainSfx(chainBefore);
    // first-shelf nudge: three words in and no peek yet (the guide says it better, so this is for players past it)
    if (G.shelf == 1 && G.mode == MODE_CLASSIC && S.tutorDone && !G.hintShown && G.peels == 0 && G.wordsThisShelf >= 3) {
        G.hintShown = 1;
        setMsg(S.hand == HAND_LEFT ? "HOLD R OR X, TAP A GEM TO PEEL" : "HOLD L OR X, TAP A GEM TO PEEL", 300);
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
    refreshClock();
    unsigned seedCounter = 0;

    while (1) {
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        touchPosition t; touchRead(&t);
        seedCounter++;

        switch (G.state) {
            case ST_TITLE:
                if (seedCounter % 60 == 0) refreshClock();   // midnight passes under an idle title
                if (down & KEY_TOUCH) {
                    // only the drawn buttons do anything; a stray tap elsewhere is ignored
                    srand(seedCounter ^ (t.px << 8) ^ t.py);
                    if (gfxInTitleOptions(t.px, t.py)) { G.state = ST_OPTIONS; G.optRow = 0; musicSfx(SFX_MENU, 0); }
                    else if (gfxInTitleDaily(t.px, t.py)) newGame(MODE_DAILY);
                    else if (gfxInTitleClassic(t.px, t.py)) newGame(MODE_CLASSIC);
                    else if (gfxInTitleLedger(t.px, t.py)) { G.state = ST_STATS; musicSfx(SFX_MENU, 0); }
                } else if (down & (KEY_START | KEY_A)) { srand(seedCounter); newGame(MODE_CLASSIC); }
                else if (down & (KEY_LEFT | KEY_L)) { gfxTipFlip(-1); musicSfx(SFX_MENU, 0); }
                else if (down & (KEY_RIGHT | KEY_R)) { gfxTipFlip(1); musicSfx(SFX_MENU, 0); }
                else if (down & KEY_SELECT) newGame(MODE_DAILY);
                else if (down & KEY_Y) { G.state = ST_OPTIONS; G.optRow = 0; musicSfx(SFX_MENU, 0); }
                else if (down & KEY_X) { G.state = ST_STATS; musicSfx(SFX_MENU, 0); }
                break;
            case ST_STATS:       // THE LEDGER: B, X, A, START or any tap leaves
                if (down & (KEY_B | KEY_X | KEY_A | KEY_START | KEY_TOUCH)) { G.state = ST_TITLE; musicSfx(SFX_MENU, 0); }
                break;
            case ST_OPTIONS: {
                // a touch gives row * 2 + (the right half): the left half steps a value back, the right half forwards
                int hit = (down & KEY_TOUCH) ? gfxOptionRowAt(t.px, t.py) : -1;
                if (hit == OPT_ROWS * 2 || (down & KEY_B)) { settingsSave(); G.state = ST_TITLE; musicSfx(SFX_MENU, 0); break; }
                if (hit >= 0) { int dir = (hit & 1) ? 1 : -1; G.optRow = hit >> 1; cycleOption(G.optRow, dir); gfxOptionNudge(dir); }
                if (down & KEY_UP)    { G.optRow = (G.optRow + OPT_ROWS - 1) % OPT_ROWS; musicSfx(SFX_MENU, 0); }
                if (down & KEY_DOWN)  { G.optRow = (G.optRow + 1) % OPT_ROWS; musicSfx(SFX_MENU, 0); }
                if (down & (KEY_RIGHT | KEY_A)) { cycleOption(G.optRow, 1); gfxOptionNudge(1); }
                if (down & KEY_LEFT)  { cycleOption(G.optRow, -1); gfxOptionNudge(-1); }
                break;
            }
            case ST_PLAY:
                handlePlay(down, held, &t);
                break;
            case ST_PASSED:
                G.stateTimer++;
                if (G.stateTimer > 30 && (down & (KEY_TOUCH | KEY_START | KEY_A))) {
                    settingsSave();
                    if (G.mode == MODE_DAILY) { quitToTitle(); break; }
                    G.shelf++;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_FAIL:
                G.stateTimer++;
                if (G.stateTimer > 45 && (down & (KEY_TOUCH | KEY_START | KEY_A))) {
                    settingsSave();
                    if (G.mode == MODE_DAILY || G.lives <= 0) { quitToTitle(); break; }   // no bookmark left: the run is over
                    G.score = G.shelfStartScore;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_PAUSE: {
                int ids[PAUSE_ROWS_MAX], n = pauseRows(&G, ids);
                if (G.pauseArm > 0) G.pauseArm--;
                if (down & (KEY_START | KEY_B)) { G.state = ST_PLAY; musicSfx(SFX_MENU, 0); break; }
                if (G.pauseRow >= n) G.pauseRow = n - 1;
                bool activate = (down & KEY_A) != 0;
                if (down & KEY_UP)   { G.pauseRow = (G.pauseRow + n - 1) % n; G.pauseArm = 0; musicSfx(SFX_MENU, 0); }
                if (down & KEY_DOWN) { G.pauseRow = (G.pauseRow + 1) % n; G.pauseArm = 0; musicSfx(SFX_MENU, 0); }
                int tapRow = (down & KEY_TOUCH) ? gfxPauseRowAt(t.px, t.py, n) : -1;
                if (tapRow >= 0) {      // a tap on the highlighted row activates it; on another row it only highlights it
                    if (tapRow == G.pauseRow) activate = true;
                    else { G.pauseRow = tapRow; G.pauseArm = 0; musicSfx(SFX_MENU, 0); }
                }
                if (activate) { musicSfx(SFX_MENU, 0); pauseActivate(ids[G.pauseRow]); }
                break;
            }
        }
        if (G.msgTimer > 0 && --G.msgTimer == 0) {
            G.msg[0] = 0;
            if (pendingMsg[0]) { setMsg(pendingMsg, pendingFrames); pendingMsg[0] = 0; }
            else if (G.state == ST_PLAY || G.state == ST_PAUSE) tutorAfterMsg();     // the guide's next prompt, or its end
        }
        boardAnimate(&G);
        musicTick(&G);
        debugSync(&G);
        gfxFrame(&G);
    }
    return 0;
}
