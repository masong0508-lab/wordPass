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

static const char *handKey(void) { return S.hand == HAND_LEFT ? "R" : "L"; }   // the shoulder under the idle hand

static void tutorPrompt(void)
{
    if (!tutorOn()) return;
    char t[MSG_LEN];
    if (G.tutorStep == TUTOR_DRAG) setMsg(ui(UI_TU_DRAG), -1);
    else if (G.tutorStep < TUTOR_REST) { uiFmt(t, sizeof t, UI_TU_PEEK, handKey()); setMsg(t, -1); }
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
    if (G.tutorStep == TUTOR_FIND && foundCount() >= 1) { G.tutorStep = TUTOR_REST; setMsg(ui(UI_TU_REST), 240); return; }
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
        if (res == 2) setMsg(ui(UI_TU_APASS), 240);
        else if (res == 1) {
            char t[MSG_LEN]; int n = boardBackNumber(&G, c, r);
            if (n == 0) strcpy(t, ui(UI_TU_N0));
            else if (n == 1) strcpy(t, ui(UI_TU_N1));
            else uiFmt(t, sizeof t, UI_TU_NN, n);
            setMsg(t, 240);
        }
    } else if (G.tutorStep == TUTOR_FIND && res == 2) {
        G.tutorStep = TUTOR_REST;
        setMsg(ui(UI_TU_REST), 240);
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
    G.cue = CUE_NONE; G.cueAge = 0; G.ghostDone = 0; G.idleFrames = 0; G.hintLen = 0;
    boardNewShelf(&G);
    G.newBest = 0;
    gfxFadeIn();
    musicPlayShelf(&G);
    char s[MSG_LEN];
    uiFmt(s, sizeof s, UI_MS_FIND, boardPassTheme(&G), G.passLen);   // 31 chars at most in English
    pendingMsg[0] = 0;
    if (G.twist) {
        char t[MSG_LEN];
        uiFmt(t, sizeof t, UI_MS_SHELFTWIST, G.shelf, boardTwistName(G.twist));
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
        if (!G.clockOk) { setMsg(ui(UI_MS_CLOCK), 120); return false; }
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
    G.lives = BOOKMARKS_MAX;
    G.tutorStep = (mode == MODE_CLASSIC && !S.tutorDone) ? TUTOR_DRAG : TUTOR_DONE;
    G.twist = 0; G.lastTwist = 0;
    memset(G.recentPass, 0, sizeof(G.recentPass)); G.recentHead = 0;
    startShelf();
    if (mode == MODE_DAILY) {
        char t[MSG_LEN];
        if (G.practice) strcpy(t, ui(UI_MS_PRACTICE));
        else uiFmt(t, sizeof t, UI_MS_DAILYMOD, boardDailyModName(G.dailyMod));
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

// Switch language (title and options only: a shelf in play keeps the letters it was dealt). An unavailable language
// is English. The title's board is rebuilt so the pass and the debug mirror belong to the new language.
void gameSetLanguage(int idx)
{
    if (G.state != ST_TITLE && G.state != ST_OPTIONS) return;
    if (idx < 0 || idx >= LANG_COUNT || !langAvailable(idx)) idx = LANG_EN;
    if (!langLoad(idx)) idx = LANG_EN;
    S.language = (u8)langCur();
    S.languageChosen = 1;
    boardNewShelf(&G);
}

// the next available language in a direction, wrapping (English is always there)
static void cycleLanguage(int dir)
{
    int idx = langCur();
    for (int i = 0; i < LANG_COUNT; i++) {
        idx = (idx + LANG_COUNT + dir) % LANG_COUNT;
        if (langAvailable(idx)) break;
    }
    gameSetLanguage(idx);
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
        case 7: if (langCur() == LANG_EN) S.spelling = (S.spelling + SPELL_COUNT + dir) % SPELL_COUNT; break;   // ENGLISH ONLY: other languages ignore it
        case 8: cycleLanguage(dir); break;
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
    ids[n++] = PAUSE_GUIDE;
    ids[n++] = PAUSE_QUIT;
    return n;
}

// HOW TO PLAY: from the title or the pause menu, back to wherever it was opened from
static void openGuide(int from)
{
    G.guideFrom = from;
    G.guidePage = 0;
    G.state = ST_GUIDE;
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
    musicMenu();
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
    musicFail(G.fail);
}

// Sparks on the gem(s) that caught fire this turn (spawnFire leaves them in fireC/fireR).
static void burstNewFlames(void)
{
    for (int i = 0; i < G.fireN; i++) gfxBurst(G.fireC[i], G.fireR[i], BURST_FIRE, G.cell[G.fireC[i]][G.fireR[i]].letter);
    G.fireN = 0;
}

// The board was just re-dealt because no word was left (board.c reshuffle): say so on the message line and the board
static void announceShuffle(void)
{
    if (!G.shuffled) return;
    G.shuffled = 0;
    char m[MSG_LEN]; uiFmt(m, sizeof m, UI_MS_SHUFFLE, SHUFFLE_HEAT);
    setMsg(m, 120);
    gfxCallout(ui(UI_CO_SHUFFLE), CALLOUT_ACCENT);
    gfxShake(6);
}

// SELECT in play: the ghost drag traces a common word on the real board (a hint). Each shelf has HINTS_PER_SHELF; the
// hint prefers a word through a flame when one burns. It stops at the first touch of the board.
static void useHint(void)
{
    if (G.hints <= 0) { setMsg(ui(UI_MS_NOHINT), 60); return; }
    boardClearChain(&G);
    G.hintLen = boardHintChain(&G, G.hintC, G.hintR, 5);
    if (!G.hintLen) { setMsg(ui(UI_MS_NOHINTWORD), 60); return; }
    G.hints--;
    G.hintReq = 1; G.hintIdle = 0;
    char m[MSG_LEN]; uiFmt(m, sizeof m, UI_MS_HINT, G.hints);
    setMsg(m, 90);
    musicSfx(SFX_MENU, 0);
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
        musicWord(G.lastWordLen, gained, G.lastWordBurn != 0);
        gfxSubmitFlash();
        tutorWord();
        // callout, one per word, in priority order: the top-screen message and the word over the board
        if (G.lastWordLen >= 8) { setMsg(ui(UI_CO_SPECT), 60); gfxCallout(ui(UI_CO_SPECT), CALLOUT_GOLD); }
        else if (G.lastWordDiamonds >= 1) {
            // the "+N" popup sits on the middle gem: put PASS LINK on the diamond (or the
            // chain's first gem when the diamond is the middle one) so the two never stack
            int d = cn / 2;
            for (int i = 0; i < cn; i++) if (G.cell[cc[i]][cr[i]].state == CELL_SECRET) { d = i; break; }
            if (d == cn / 2) d = 0;
            gfxPopup(cc[d], cr[d], ui(UI_POP_LINK), 1);
            setMsg(ui(UI_CO_LINK), 60); gfxCallout(ui(UI_CO_LINK), CALLOUT_ACCENT);
        }
        else if (G.combo >= 3) {            // three words in a row without a peel: the streak is worth a callout of its own
            char cb[MSG_LEN]; uiFmt(cb, sizeof cb, UI_CO_COMBO, G.combo);
            setMsg(cb, 60); gfxCallout(cb, CALLOUT_ACCENT);
        }
        else if (G.lastWordLen >= 7) { setMsg(ui(UI_CO_EXCELLENT), 60); gfxCallout(ui(UI_CO_EXCELLENT), CALLOUT_GOLD); }
        else if (G.lastWordLen >= 5) { setMsg(ui(UI_CO_GOOD), 60); gfxCallout(ui(UI_CO_GOOD), CALLOUT_GOLD); }
        else if (G.lastWordRepeat) { setMsg(ui(UI_CO_AGAIN), 45); gfxCallout(ui(UI_CO_AGAIN), CALLOUT_ACCENT); }
        else if (G.lastWordTier == 2) { setMsg(ui(UI_CO_RARE), 60); gfxCallout(ui(UI_CO_RARE), CALLOUT_GOLD); }
        burstNewFlames();
        if (!G.fail) announceShuffle();
        if (G.fail) enterFail();
    } else if (G.chainLen >= 3) {
        char w[WORD_BUF]; boardChainWord(&G, w);
        int dialect = boardWordSpelling(w, strlen(w));
        bool obscure = S.library == LIB_COMMON && boardWordTier(w, strlen(w)) == 2;
        setMsg(ui(dialect == 2 ? UI_MS_BRIT : dialect == 1 ? UI_MS_AMER : obscure ? UI_CO_OBSCURE : UI_MS_NOTLIB), 45);
        // the board callout stays short enough to pop at 2x (the top-screen message carries the full reason)
        gfxCallout(ui(dialect == 2 ? UI_CO_UK : dialect == 1 ? UI_CO_US : obscure ? UI_CO_OBSCURE : UI_CO_NOTWORD), CALLOUT_FIRE);
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
            if (G.mode == MODE_CLASSIC && G.lives <= 1) { setMsg(ui(UI_MS_LASTBOOK), 60); G.pauseArm = 0; break; }   // the last one cannot be spent
            if (!armed) { G.pauseArm = 90; G.pauseArmId = id; break; }
            G.pauseArm = 0;
            if (G.mode == MODE_CLASSIC) G.lives--;
            G.score = G.shelfStartScore; startShelf(); G.state = ST_PLAY;
            break;
        case PAUSE_GUIDE:
            G.pauseArm = 0;
            openGuide(ST_PAUSE);
            break;
        case PAUSE_QUIT:
            if (!armed) { G.pauseArm = 90; G.pauseArmId = id; break; }
            G.pauseArm = 0;
            quitToTitle();
            break;
    }
}

// The guided first shelf also points at things in the world: a breathing ring and a bobbing pointer on the tail
// gem of a valid chain, on the PEEK button, on the armed cell, and once after a few idle seconds a ghost drag that
// traces a real word on the real board. A cue lasts only until the player does the thing, and never shows while the
// stylus is down.
static void updateCue(u32 held)
{
    int want = CUE_NONE;
    bool touching = (held & KEY_TOUCH) != 0;
    if (G.state == ST_PLAY && tutorOn() && !G.fail) {
        if (held & (KEY_TOUCH | KEY_A | KEY_B | KEY_X | KEY_Y | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) G.idleFrames = 0;
        else G.idleFrames++;
        if (G.tutorStep == TUTOR_DRAG && !touching) {
            char w[WORD_BUF];
            if (G.chainLen >= 3) { boardChainWord(&G, w); if (boardWordOk(w, (int)strlen(w))) want = CUE_TAIL; }
            else if (G.chainLen == 0 && (!G.ghostDone || G.cue == CUE_GHOST) && G.idleFrames >= GHOST_IDLE_FRAMES) want = CUE_GHOST;   // once: it keeps going until its own end
        } else if (G.tutorStep == TUTOR_PEEK && !touching && G.chainLen == 0) {
            if (G.armed) want = CUE_ARMED;
            else if (!G.peekHeld) want = CUE_PEEKBTN;
        } else if (G.tutorStep == TUTOR_PEEK && G.armed) want = CUE_ARMED;
    } else G.idleFrames = 0;
    if (G.hintReq) {             // a hint: the same ghost drag, on request, until the player touches the board
        if (G.state != ST_PLAY || touching || G.chainLen > 0 || G.fail) G.hintReq = 0;
        else want = CUE_GHOST;
    }
    if (want == CUE_GHOST && G.cue != CUE_GHOST && !G.hintReq) {
        G.hintLen = boardHintChain(&G, G.hintC, G.hintR, 5);
        G.ghostDone = 1;                         // shown once: whatever happens next
        if (!G.hintLen) want = CUE_NONE;
    }
    if (want != G.cue) { G.cue = want; G.cueAge = 0; }
    else G.cueAge++;
    if (G.cue == CUE_GHOST && G.cueAge > G.hintLen * GHOST_GEM_FRAMES + 30) { G.cue = CUE_NONE; G.cueAge = 0; G.hintReq = 0; }
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
    if (down & KEY_SELECT) useHint();
    // the idle offer: ten seconds of nothing with hints in hand, once a shelf (the guided shelf has its own ghost)
    if ((held & (KEY_TOUCH | KEY_A | KEY_B | KEY_X | KEY_Y | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) || G.chainLen > 0) G.hintIdle = 0;
    else if (++G.hintIdle == HINT_IDLE_FRAMES && G.hints > 0 && !G.hintOffered && !tutorOn()) { G.hintOffered = 1; setMsg(ui(UI_MS_HINTIDLE), 150); }
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
                int reward = boardPassLetterReward(G.shelf);   // every pass letter pays
                G.score += reward;
                char ps[12]; siprintf(ps, "+%d", reward);
                gfxPopup(c, r, ps, 1);
                musicReveal(true);
                if (boardPassComplete(&G)) {
                    char s[MSG_LEN]; uiFmt(s, sizeof s, UI_MS_SHELFCLEARED, G.shelf); setMsg(s, 400);
                } else { setMsg(ui(UI_MS_PASSFOUND), 90); gfxCallout(ui(UI_CO_PASSLETTER), CALLOUT_ACCENT); }
            } else if (res == 1) { musicSfx(SFX_PEEL, 0); musicReveal(false); }
            else if (res == 3 || res == -1) { musicSfx(SFX_SCORCH, 0); setMsg(ui(UI_CO_SCORCHED), 60); gfxCallout(ui(UI_CO_SCORCHED), CALLOUT_FIRE); gfxBurst(c, r, BURST_SCORCH, letter); gfxShake(6); }
            burstNewFlames();
            if (!G.fail) announceShuffle();
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
                int shelfBonus = boardShelfBonus(G.shelf);
                G.speedBonus = boardSpeedBonus(G.shelf, G.turn);
                G.peelBonus = boardPeelBonus(G.shelf, G.peels, G.passLen, &G.passBonusKind);
                G.score += shelfBonus + G.speedBonus + G.peelBonus;
                if (G.lockdown && G.mode == MODE_CLASSIC && G.lives < BOOKMARKS_MAX) G.lives++;   // a Lockdown shelf earns a bookmark back
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
        { char hm[MSG_LEN]; uiFmt(hm, sizeof hm, UI_TU_PEELHINT, handKey()); setMsg(hm, 300); }
    }
}

// The results and fail cards ignore taps for their first frames (the stylus that made the last peel is still landing);
// a tap after CARD_QUEUE_FROM is remembered and applied when the gate opens.
#define CARD_QUEUE_FROM 10
static bool cardTapQueued;

int main(void)
{
    settingsInit();
    gfxInit();
    dictInit();
    poolsInit();
    langInit();                       // NitroFS; without it only English is available
    musicInit();
    memset(&G, 0, sizeof(G));
    G.state = ST_TITLE;
    G.shelf = 1;
    if (S.language != LANG_EN) {     // the saved language, if its files are on this ROM; English otherwise
        int want = S.language;
        S.language = LANG_EN;
        if (langAvailable(want) && langLoad(want)) S.language = (u8)langCur();
    }
    boardNewShelf(&G);
    refreshClock();
    if (!S.languageChosen) {         // first boot: choose a language before the title (only when there is a choice)
        bool other = false;
        for (int i = 1; i < LANG_COUNT; i++) other = other || langAvailable(i);
        if (other) { G.langPick = langCur(); G.langFrom = ST_TITLE; G.state = ST_LANGPICK; }
    }
    unsigned seedCounter = 0;

    while (1) {
        gfxLateSample();
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
                    else if (gfxInTitleGuide(t.px, t.py)) openGuide(ST_TITLE);
                    else if (gfxInTitleLanguage(t.px, t.py)) { G.langPick = langCur(); G.langFrom = ST_TITLE; G.state = ST_LANGPICK; }
                } else if (down & (KEY_START | KEY_A)) { srand(seedCounter); newGame(MODE_CLASSIC); }
                else if (down & (KEY_LEFT | KEY_L)) { gfxTipFlip(-1); musicSfx(SFX_MENU, 0); }
                else if (down & (KEY_RIGHT | KEY_R)) { gfxTipFlip(1); musicSfx(SFX_MENU, 0); }
                else if (down & KEY_SELECT) newGame(MODE_DAILY);
                else if (down & KEY_Y) { G.state = ST_OPTIONS; G.optRow = 0; musicSfx(SFX_MENU, 0); }
                else if (down & KEY_X) { G.state = ST_STATS; musicSfx(SFX_MENU, 0); }
                else if (down & KEY_B) openGuide(ST_TITLE);
                break;
            case ST_GUIDE: {     // eight pages: LEFT/RIGHT or L/R or the buttons or a page dot flip them; B, START or BACK leaves
                int hit = (down & KEY_TOUCH) ? gfxGuideHit(t.px, t.py) : GH_NONE, dir = 0;
                if (down & (KEY_TOUCH | KEY_A | KEY_B | KEY_X | KEY_Y | KEY_START | KEY_SELECT | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) gfxGuideSkip();   // any press finishes the typing (a page flip then starts the next page's)
                if ((down & (KEY_B | KEY_START)) || hit == GH_BACK) { G.state = G.guideFrom; break; }
                if (down & (KEY_RIGHT | KEY_R)) dir = 1;
                else if (down & (KEY_LEFT | KEY_L)) dir = -1;
                else if (down & KEY_A) { if (G.guidePage == GUIDE_PAGES - 1) { G.state = G.guideFrom; break; } dir = 1; }   // A reads on, and closes after the last page
                else if (hit == GH_NEXT) dir = 1;
                else if (hit == GH_PREV) dir = -1;
                else if (hit >= GH_DOT && hit < GH_DOT + GUIDE_PAGES) G.guidePage = hit - GH_DOT;
                if (dir) G.guidePage = (G.guidePage + GUIDE_PAGES + dir) % GUIDE_PAGES;
                break;
            }
            case ST_LANGPICK: {  // tap a language, or UP/DOWN and A; B backs out once a language has been chosen
                int hit = (down & KEY_TOUCH) ? gfxLangPickRowAt(t.px, t.py) : -1;
                if (down & KEY_UP)   G.langPick = (G.langPick + LANG_COUNT - 1) % LANG_COUNT;
                if (down & KEY_DOWN) G.langPick = (G.langPick + 1) % LANG_COUNT;
                bool go = (down & (KEY_A | KEY_START)) != 0;
                if (hit >= 0) { G.langPick = hit; go = true; }
                if ((down & KEY_B) && S.languageChosen) { G.state = G.langFrom; break; }
                if (go) { G.state = G.langFrom; gameSetLanguage(G.langPick); settingsSave(); }
                break;
            }
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
                if ((down & KEY_A) && G.optRow == 8) { G.langPick = langCur(); G.langFrom = ST_OPTIONS; G.state = ST_LANGPICK; break; }   // A on LANGUAGE opens the chooser
                if (down & (KEY_RIGHT | KEY_A)) { cycleOption(G.optRow, 1); gfxOptionNudge(1); }
                if (down & KEY_LEFT)  { cycleOption(G.optRow, -1); gfxOptionNudge(-1); }
                break;
            }
            case ST_PLAY:
                handlePlay(down, held, &t);
                updateCue(held);
                break;
            case ST_PASSED:
                G.stateTimer++;
                if (G.stateTimer == 1) cardTapQueued = false;
                // a tap once the card is up but before it takes input is kept and applied the moment it does (never dropped)
                if (G.stateTimer > CARD_QUEUE_FROM && G.stateTimer <= CARD_GATE_PASSED && (down & (KEY_TOUCH | KEY_START | KEY_A))) cardTapQueued = true;
                if (G.stateTimer > CARD_GATE_PASSED && ((down & (KEY_TOUCH | KEY_START | KEY_A)) || cardTapQueued)) {
                    cardTapQueued = false;
                    settingsSave();
                    if (G.mode == MODE_DAILY) { quitToTitle(); break; }
                    G.shelf++;
                    startShelf();
                    G.state = ST_PLAY;
                }
                break;
            case ST_FAIL:
                G.stateTimer++;
                if (G.stateTimer == 1) cardTapQueued = false;
                if (G.stateTimer > CARD_QUEUE_FROM && G.stateTimer <= CARD_GATE_FAIL && (down & (KEY_TOUCH | KEY_START | KEY_A))) cardTapQueued = true;
                if (G.stateTimer > CARD_GATE_FAIL && ((down & (KEY_TOUCH | KEY_START | KEY_A)) || cardTapQueued)) {
                    cardTapQueued = false;
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
        if (G.state != ST_PLAY) { G.cue = CUE_NONE; G.cueAge = 0; }
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
