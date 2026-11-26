// wordPass: Bookworm chaining on a hex grid, a hidden pass behind the board,
// and fire that punishes lazy words and careless peeling.
#include "game.h"
#include "script_preview.h"
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
static char pendingMsg[2][MSG_LEN];          // up to two: a banner, its one line explanation, then the find-the-pass line
static int pendingFrames[2], pendingN;

static void queueMsg(const char *m, int frames)
{
    if (pendingN >= 2) return;
    size_t n = strlen(m); if (n > MSG_LEN - 1) n = MSG_LEN - 1;
    memcpy(pendingMsg[pendingN], m, n); pendingMsg[pendingN][n] = 0;
    pendingFrames[pendingN++] = frames;
}

// ---- the guided first shelf: sticky prompts on the top screen while Classic shelf 1 is new to the player.
// TUTOR_DRAG: drag a word. TUTOR_PEEK: peek. TUTOR_FIND: (after the first peel) find a pass letter.
// TUTOR_REST: keep finding letters until the whole password is revealed. Only a cleared
// first shelf finishes the guide. Gameplay messages temporarily replace its next action.
static bool tutorOn(void) { return !G.training && G.mode == MODE_CLASSIC && G.shelf == 1 && !S.tutorDone && G.tutorStep < TUTOR_DONE; }

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
    if (G.tutorStep == TUTOR_PEEK || (G.tutorStep == TUTOR_DRAG && G.peekHeld)) { uiFmt(t, sizeof t, UI_TU_PEEK, handKey()); setMsg(t, -1); }
    else if (G.tutorStep == TUTOR_DRAG) setMsg(ui(UI_TU_DRAG), -1);
    else if (G.tutorStep == TUTOR_FIND) setMsg(ui(UI_TU_FIND), -1);
    else { uiFmt(t, sizeof t, UI_TU_REST, foundCount(), G.passLen); setMsg(t, -1); }
}

static void tutorFinish(void)
{
    if (G.tutorStep < TUTOR_DONE && !S.tutorDone && G.mode == MODE_CLASSIC) { S.tutorDone = 1; settingsSave(); }
    G.tutorStep = TUTOR_DONE;
}

// The message line just ran out: restore the next useful action. A single found
// letter is progress, not a completed lesson or a completed password.
static void tutorAfterMsg(void)
{
    if (!tutorOn()) return;
    if (G.tutorStep == TUTOR_FIND && foundCount() >= 1) G.tutorStep = TUTOR_REST;
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
        tutorPrompt();
    } else if (G.tutorStep == TUTOR_REST && res == 2) {
        tutorPrompt();
    }
}

// a sticky prompt must not outlive the moment it was written for
static void clearSticky(void)
{
    if (G.msgTimer < 0) { G.msg[0] = 0; G.msgTimer = 0; }
}

// The stylus stroke tracker of the play screen. A stroke that was down when play was left (pause, fail, pass, a hint, a
// clear, a peek) must not carry over: the first play frame that finds the stylus still down without a fresh press
// reads it as a stylus that came down elsewhere and ignores it until it lifts.
static bool wasTouching, ignoreStroke;
static void dropStroke(void) { wasTouching = false; ignoreStroke = false; }

static void startShelf(void)
{
    // A retry deals a new board with no earned clues. Restart the unfinished
    // lesson too, so its next action and automatic word hint remain useful.
    if (!G.training && G.mode == MODE_CLASSIC && G.shelf == 1 && !S.tutorDone) G.tutorStep = TUTOR_DRAG;
    dropStroke();
    G.armed = 0; G.peekLatch = 0;
    G.cue = CUE_NONE; G.cueAge = 0; G.ghostDone = 0; G.idleFrames = 0; G.hintLen = 0;
    boardNewShelf(&G);
    G.newBest = 0;
    gfxFadeIn();
    musicPlayShelf(&G);
    char s[MSG_LEN];
    uiFmt(s, sizeof s, UI_MS_FIND, boardPassTheme(&G), G.passLen);   // 31 chars at most in English
    pendingN = 0;
    if (G.twist) {
        char t[MSG_LEN];
        uiFmt(t, sizeof t, UI_MS_SHELFTWIST, G.shelf, boardTwistName(G.twist));
        setMsg(t, 150);
        queueMsg(ui(G.twist == TWIST_INK ? UI_MS_TW_INK : G.twist == TWIST_DAMP ? UI_MS_TW_DAMP : UI_MS_TW_BURIED), 150);
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

static bool beginGame(int mode, bool training)
{
    G.training = training;
    G.practice = training;
    if (mode == MODE_DAILY) {
        refreshClock();
        if (!G.clockOk) { setMsg(ui(UI_MS_CLOCK), 120); return false; }
        // one scored attempt a day: the second and later runs are practice (nothing recorded)
        G.practice = (G.dailySeed <= S.dailyPlayedDate || G.dailySeed <= S.dailyLastDate);   // also a clock set back: a date already behind the last scored one is never scored again
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
    G.tutorStep = (!training && mode == MODE_CLASSIC && !S.tutorDone) ? TUTOR_DRAG : TUTOR_DONE;
    G.twist = 0; G.lastTwist = 0;
    memset(G.recentPass, 0, sizeof(G.recentPass)); G.recentHead = 0;
    startShelf();
    if (mode == MODE_DAILY) {
        char t[MSG_LEN];
        if (G.practice) strcpy(t, ui(UI_MS_PRACTICE));
        else uiFmt(t, sizeof t, UI_MS_DAILYONE, boardDailyModName(G.dailyMod));   // a scored Daily: one try today
        char intro[MSG_LEN]; strcpy(intro, G.msg); int introFrames = G.msgTimer;
        pendingN = 0;
        if (!G.practice) {                 // what the modifier does, then the shelf intro follows
            char d[MSG_LEN];
            if (G.dailyMod == DAILYMOD_LONG) uiFmt(d, sizeof d, UI_MS_DM_LONG, DAILY_LONG_LEN);
            else strcpy(d, ui(G.dailyMod == DAILYMOD_EMBER ? UI_MS_DM_EMBER : G.dailyMod == DAILYMOD_DEEP ? UI_MS_DM_DEEP : UI_MS_DM_STANDARD));
            queueMsg(d, 150);
        }
        queueMsg(intro, introFrames);
        setMsg(t, 150);
    }
    G.state = ST_PLAY;
    return true;
}
static bool newGame(int mode) { return beginGame(mode, false); }
static void newTraining(void) { (void)beginGame(MODE_CLASSIC, true); }

// Fold a finished run into the saved records. A Daily replay (practice) or a Daily without a
// readable clock records nothing; a scored Daily updates the last score, the best (a higher score only; it keeps its own date), the
// streak and the games played exactly once.
static void recordScore(void)
{
    if (G.training) return;
    bool best = false;
    if (G.mode == MODE_DAILY) {
        if (G.practice || !G.clockOk || dailyRecorded) return;
        dailyRecorded = true;
        S.dailyLastScore = G.score;
        best = settingsRecordDaily(G.score, G.dailySeed);
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

// A Classic shelf or run ended (the fail on the last bookmark, a cleared shelf): recordScore saves only when a record
// changed, but the lifetime statistics (games, shelves, words, peels) changed in RAM all the same, and the RUN OVER card
// is where the DS gets switched off. One write at most.
static void recordAndSave(void)
{
    if (G.training) return;
    unsigned before = settingsSaveCount();
    recordScore();
    if (G.mode == MODE_CLASSIC && settingsSaveCount() == before) settingsSave();
}

// Switch language (title and options only: a shelf in play keeps the letters it was dealt). An unavailable language
// is English. The title's board is rebuilt so the pass and the debug mirror belong to the new language.
void gameSetLanguage(int idx)
{
    if (G.state != ST_TITLE && G.state != ST_OPTIONS) return;
    if (idx < 0 || idx >= LANG_COUNT || !langAvailable(idx)) idx = LANG_EN;
    if (idx == langCur()) {       // selecting the current entry should not reroll the waiting board
        (void)langLoad(idx);     // clear an old error if the player chose English
        S.language = (u8)langCur();
        S.languageChosen = 1;
        return;
    }
    bool loaded = langLoad(idx);
    S.language = (u8)langCur();   // English if a pack failed partway through loading
    if (loaded) S.languageChosen = 1;
    boardNewShelf(&G);
}

#ifdef WP_CYR_PREVIEW
// Retail packs can occupy most of the DS heap. The preview owns separate
// dictionaries, so release the active retail buffers before opening it and
// reload them only after every preview image is closed. S.language remains
// the player's saved preference throughout this temporary runtime switch.
static int scriptReturnLanguage = -1;
// Match the retail peek safety rule: touch arms, moving off cancels, and only
// lifting on that same gem peels it. No board fields, heap or frame searches.
// "scriptPeekStroke" keeps a cancelled peel from turning into a word stroke.
static bool scriptPeekStroke, scriptPeelArmed;
static int scriptPeelCol, scriptPeelRow;
// Ordinary Cyrillic word strokes must retain a previous TOUCH sample. Unlike
// retail's full input loop they previously only saw the final cell of a fast
// swipe, skipping one or more adjacent letters between frames. These scalars
// are preview-only, require no heap and survive no mode transitions.
static bool scriptWordStrokeActive, scriptWordStrokeSuppressed;
static int scriptWordLastTx, scriptWordLastTy;
static void scriptDropPeekStroke(void)
{
    scriptPeekStroke = scriptPeelArmed = false;
    scriptWordStrokeActive = scriptWordStrokeSuppressed = false;
}

static void leaveScriptPreview(void)
{
    scriptDropPeekStroke();
    scriptPreviewClose();
    int language = scriptReturnLanguage;
    scriptReturnLanguage = -1;
    G.state = G.langFrom == ST_OPTIONS ? ST_OPTIONS : ST_TITLE;
    if (language >= 0 && !langLoad(language)) {
        // A removed/damaged pack safely leaves English active. Keep the
        // existing load-failure banner and saved choice, but rebuild the
        // waiting board so it cannot contain a different language's pass.
        boardNewShelf(&G);
        setMsg(ui(UI_OP_LANGFAIL), 180);
    }
    musicMenu();
}

static bool enterScriptPreview(int language, u32 seed, int from)
{
    scriptDropPeekStroke();
    G.langFrom = from == ST_OPTIONS ? ST_OPTIONS : ST_TITLE;
    scriptReturnLanguage = langCur();
    (void)langLoad(LANG_EN); // built-in data; frees all three retail buffers
    if (scriptPreviewOpen(language, seed)) {
        G.state = ST_SCRIPT_PREVIEW;
        return true;
    }
    leaveScriptPreview(); // also restores the retail pack after a load failure
    return false;
}
#endif

// the next available language in a direction, wrapping (English is always there)
static void cycleLanguage(int dir)
{
    gameSetLanguage(langNextAvailable(langCur(), dir));
}

// Experimental ROM: two playable but INCOMPLETE script-ID engines appear as
// explicit Russian/Ukrainian choices. They do not participate in LANG_COUNT,
// WPD3 dictionaries, retail option cycling or persisted settings.
static bool pickerAvailable(int idx)
{
#ifdef WP_CYR_PREVIEW
    if (idx == LANG_COUNT || idx == LANG_COUNT + 1) return true;
#endif
    return langAvailable(idx);
}
static int pickerNextAvailable(int from, int dir)
{
#ifdef WP_CYR_PREVIEW
    enum { PICK_COUNT = LANG_COUNT + 2 };
    if (from < 0 || from >= PICK_COUNT) from = LANG_EN;
    int step = dir < 0 ? -1 : 1;
    for (int i = 0; i < PICK_COUNT; i++) {
        from = (from + PICK_COUNT + step) % PICK_COUNT;
        if (pickerAvailable(from)) return from;
    }
    return LANG_EN;
#else
    return langNextAvailable(from, dir);
#endif
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
    bool wasTraining = G.training;
    G.training = false;
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
    G.msg[0] = 0; G.msgTimer = 0; pendingN = 0;
    refreshClock();
    gfxFadeIn();
    musicMenu();
    if (!wasTraining) settingsSave();
}

static void enterFail(void)
{
    clearSticky();
    dropStroke();
    G.state = ST_FAIL;
    boardClearChain(&G);
    if (G.mode == MODE_CLASSIC && !G.training) {
        if (G.lives > 0) G.lives--;   // a failed shelf costs a bookmark
        if (G.lives == 0) S.gamesPlayed++;   // the last bookmark: the run is over
    }
    // a failed shelf with a bookmark left is retried from its start score: those points are not the player's, so they
    // record nothing (the run's score is recorded, and saved, when it ends)
    if (G.mode != MODE_CLASSIC || G.lives == 0) recordAndSave();
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
// hint prioritizes a flame, an unplayed word and lower-heat longer words. It stops at the first touch of the board.
static void useHint(void)
{
    if (G.hints <= 0 && !G.training) { setMsg(ui(UI_MS_NOHINT), 60); return; }
    boardClearChain(&G);
    dropStroke();                      // a stylus still down must not restart a chain and cancel the ghost
    G.hintLen = boardHintChain(&G, G.hintC, G.hintR, MAX_CHAIN);
    if (!G.hintLen) { setMsg(ui(UI_MS_NOHINTWORD), 60); return; }
    if (!G.training) G.hints--;
    // Following a word hint must chain gems. A one-shot PEEK latch otherwise
    // turns that first touch into a destructive peel instead of the shown word.
    G.peekLatch = 0; G.peekHeld = 0; G.armed = 0; musicPeek(false);
    G.hintReq = 1; G.hintIdle = 0; G.cueAge = 0;
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
    bool newClue = cn >= 3 && !G.wordClue[cc[cn - 1]][cr[cn - 1]];
    int gained = boardSubmit(&G);
    if (gained > 0) {
        for (int i = 0; i < cn; i++) gfxBurst(cc[i], cr[i], BURST_SHATTER, cl[i]);
        char ps[12]; siprintf(ps, "+%d", gained);
        gfxPopup(cc[cn / 2], cr[cn / 2], ps, 1);
        musicWord(G.lastWordLen, gained, G.lastWordBurn != 0);
        gfxSubmitFlash();
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
        if (newClue) {
            int c = cc[cn - 1], r = cr[cn - 1];
            char cm[MSG_LEN]; uiFmt(cm, sizeof cm, UI_MS_WORDCLUE, G.wordClue[c][r] - 1);
            setMsg(cm, 150);
            gfxPopup(c, r, ui(UI_POP_CLUE), 1);
        }
        tutorWord();
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
    dropStroke();
    boardClearChain(&G);
    G.armed = 0; G.peekLatch = 0; G.peekHeld = 0; musicPeek(false);
    if (tutorOn() && G.tutorStep == TUTOR_DRAG && G.msgTimer < 0) tutorPrompt();
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
            if (!G.training && G.mode == MODE_CLASSIC && G.lives <= 1) { setMsg(ui(UI_MS_LASTBOOK), 60); G.pauseArm = 0; break; }   // the last one cannot be spent
            if (!armed) { G.pauseArm = 90; G.pauseArmId = id; break; }
            G.pauseArm = 0;
            if (!G.training && G.mode == MODE_CLASSIC) G.lives--;
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
        // A word gesture is only safe in chaining mode. Inspecting clues must
        // not leave a ghost or submit ring that would instead peel on touch.
        if (G.peekHeld && G.cue == CUE_GHOST && !G.hintReq && G.tutorStep == TUTOR_DRAG) G.ghostDone = 0;
        if (G.peekHeld || (held & (KEY_TOUCH | KEY_A | KEY_B | KEY_X | KEY_Y | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))) G.idleFrames = 0;
        else G.idleFrames++;
        if (G.tutorStep == TUTOR_DRAG && !touching && !G.peekHeld) {
            char w[WORD_BUF];
            if (G.chainLen >= 3) { boardChainWord(&G, w); if (boardWordOk(w, (int)strlen(w))) want = CUE_TAIL; }
            else if (G.chainLen == 0 && (!G.ghostDone || G.cue == CUE_GHOST) && G.idleFrames >= GHOST_IDLE_FRAMES) want = CUE_GHOST;   // once: it keeps going until its own end
        } else if (G.tutorStep >= TUTOR_PEEK && !touching && G.chainLen == 0) {
            if (G.armed) want = CUE_ARMED;
            else if (!G.peekHeld) want = CUE_PEEKBTN;
        } else if (G.tutorStep >= TUTOR_PEEK && G.armed) want = CUE_ARMED;
    } else G.idleFrames = 0;
    if (G.hintReq) {             // a hint: the same ghost drag, on request, until the player touches the board
        if (G.state != ST_PLAY || G.peekHeld || touching || G.chainLen > 0 || G.fail) G.hintReq = 0;
        else want = CUE_GHOST;
    }
    if (want == CUE_GHOST && G.cue != CUE_GHOST && !G.hintReq) {
        G.hintLen = boardHintChain(&G, G.hintC, G.hintR, MAX_CHAIN);
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
    if ((down & KEY_TOUCH) && gfxInGoalButton(t->px, t->py)) { enterPause(); openGuide(ST_PAUSE); return; }
    bool peekNow = (held & (KEY_L | KEY_R)) != 0 || G.peekLatch;
    if (peekNow != G.peekHeld) {
        G.peekHeld = peekNow; musicPeek(peekNow);
        if (tutorOn() && G.tutorStep == TUTOR_DRAG && G.msgTimer < 0) tutorPrompt();
    }
    if (!peekNow) G.armed = 0;

    if (down & KEY_START) { enterPause(); return; }
    if ((down & KEY_SELECT) && !(held & KEY_TOUCH)) {
        // A physical shoulder still means PEEK. Instead of charging for a
        // word hint that cannot be followed, SELECT scans already-visible
        // numbered clues and moves the cursor to one worth inspecting.
        // A latched-only PEEK still uses the established SELECT word hint.
        if (held & (KEY_L | KEY_R)) {
            int c, r;
            int afterC = G.curOn ? G.curC : G.wordClueC;
            int afterR = G.curOn ? G.curR : G.wordClueR;
            if (boardNextUsefulClue(&G, afterC, afterR, &c, &r) >= CLUE_UNDECIDED) {
                G.curC = c; G.curR = r; G.curOn = 1; G.armed = 0;
                int total = G.cell[c][r].state == CELL_HOLE
                          ? boardBackNumber(&G, c, r) : G.wordClue[c][r] - 1;
                // This clue was already earned. Use the localized CLUE
                // label, not the NEW CLUE message reserved for word submits.
                char msg[MSG_LEN]; siprintf(msg, "%s: %d", ui(UI_POP_CLUE), total);
                setMsg(msg, 120); musicSfx(SFX_MENU, 0);
            } else setMsg(ui(UI_MS_PEEKRELEASE), 120);
        } else useHint();
    }
    // the idle offer: ten seconds of nothing with hints in hand, once a shelf (the guided shelf has its own ghost)
    if ((held & (KEY_TOUCH | KEY_A | KEY_B | KEY_X | KEY_Y | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) || G.chainLen > 0) G.hintIdle = 0;
    else if (++G.hintIdle == HINT_IDLE_FRAMES && G.hints > 0 && !G.hintOffered && !tutorOn()) { G.hintOffered = 1; setMsg(ui(UI_MS_HINTIDLE), 150); }
    int chainBefore = G.chainLen;
    // B steps the chain back one gem, held for 20 frames it clears it; Y clears at once
    static int bHeld;
    if (held & KEY_B) { if (++bHeld == 20) { boardClearChain(&G); dropStroke(); } } else bHeld = 0;
    if ((down & KEY_B) && G.chainLen > 0) G.chainLen--;
    if (down & KEY_Y) { boardClearChain(&G); dropStroke(); gfxPress(PRESS_CLEAR); }

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
        dropStroke();                          // a stylus used for peeling is not a chain stroke when the peek ends
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
            if (!res && G.training) {
                gfxNope(c, r);
                musicSfx(SFX_INVALID, 0);
            }
            if (res == 2) {
                gfxBurst(c, r, BURST_REVEAL, letter);
                int reward = boardPassLetterReward(G.shelf);   // every pass letter pays
                int streakBonus = G.lastStreakBonus;           // ... and a pass letter right after another pays a streak bonus on top
                G.score += reward + streakBonus;
                char ps[12]; siprintf(ps, "+%d", reward + streakBonus);
                gfxPopup(c, r, ps, 1);
                musicReveal(true);
                if (boardPassComplete(&G)) {
                    char s[MSG_LEN]; uiFmt(s, sizeof s, UI_MS_SHELFCLEARED, G.shelf); setMsg(s, 400);
                } else if (streakBonus > 0) {
                    char sm[MSG_LEN], sc[MSG_LEN];
                    uiFmt(sm, sizeof sm, UI_MS_STREAK, streakBonus, G.hitStreak); setMsg(sm, 90);
                    uiFmt(sc, sizeof sc, UI_CO_STREAK, G.hitStreak); gfxCallout(sc, CALLOUT_GOLD);
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
                dropStroke();
                if (!G.training && G.shelf == 1) tutorFinish();       // the first shelf is cleared: the guide has done its job
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
                recordAndSave();
            }
        }
        return;
    }

    // Chain input. Between two samples a fast stylus can skip gems: walk the segment and feed every
    // gem it crosses that touches the tail as a drag (never a restart), then the final point.
    static int lastTx, lastTy;
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
                enum ChainTouch ct = boardChainCursor(&G, G.curC, G.curR);
                if (ct == CT_NOT_ADJACENT || ct == CT_RESTARTED) { gfxNope(G.curC, G.curR); if (ct == CT_NOT_ADJACENT) musicSfx(SFX_INVALID, 0); }
            }
        } else { gfxPress(PRESS_SUBMIT); submitChain(); }
    }
    chainSfx(chainBefore);
    // first-shelf nudge: three words in and no peek yet (the guide says it better, so this is for players past it)
    if (!G.training && G.shelf == 1 && G.mode == MODE_CLASSIC && S.tutorDone && !G.hintShown && G.peels == 0 && G.wordsThisShelf >= 3) {
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
#ifdef WP_CYR_PREVIEW
                // Hold L+R, press Y: engineering-only Russian/Ukrainian DS
                // preview. Never enters the production language selector or
                // writes a partially translated locale into the save card.
                if ((down & KEY_Y) && (held & (KEY_L | KEY_R)) == (KEY_L | KEY_R)) {
                    if (!enterScriptPreview(0, seedCounter, ST_TITLE))
                        setMsg("CYRILLIC DEMO FILES MISSING", 110);
                    break;
                }
#endif
                if (seedCounter % 60 == 0) refreshClock();   // midnight passes under an idle title
                if (down & KEY_TOUCH) {
                    // only the drawn buttons do anything; a stray tap elsewhere is ignored
                    srand(seedCounter ^ (t.px << 8) ^ t.py);
                    if (gfxInTitleOptions(t.px, t.py)) { G.state = ST_OPTIONS; G.optRow = 0; musicSfx(SFX_MENU, 0); }
                    else if (gfxInTitleDaily(t.px, t.py)) newGame(MODE_DAILY);
                    else if (gfxInTitleClassic(t.px, t.py)) newGame(MODE_CLASSIC);
                    else if (gfxInTitleTraining(t.px, t.py)) newTraining();
                    else if (gfxInTitleLedger(t.px, t.py)) { G.state = ST_STATS; musicSfx(SFX_MENU, 0); }
                    else if (gfxInTitleGuide(t.px, t.py)) openGuide(ST_TITLE);
                    else if (gfxInTitleLanguage(t.px, t.py)) { G.langPick = langCur(); G.langFrom = ST_TITLE; G.state = ST_LANGPICK; }
                } else if ((down & KEY_A) && (held & KEY_L)) newTraining();
                else if (down & (KEY_START | KEY_A)) { srand(seedCounter); newGame(MODE_CLASSIC); }
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
                if (down & KEY_UP)   G.langPick = pickerNextAvailable(G.langPick, -1);
                if (down & KEY_DOWN) G.langPick = pickerNextAvailable(G.langPick, 1);
                bool go = (down & (KEY_A | KEY_START)) != 0;
                if (hit >= 0 && pickerAvailable(hit)) { G.langPick = hit; go = true; }
                if ((down & KEY_B) && S.languageChosen) { G.state = G.langFrom; break; }
                if (go && pickerAvailable(G.langPick)) {
#ifdef WP_CYR_PREVIEW
                    if (G.langPick >= LANG_COUNT) {
                        // A named beta board, not a release locale. Leave
                        // S.language and the save format completely untouched.
                        int which = G.langPick - LANG_COUNT;
                        if (enterScriptPreview(which, seedCounter, G.langFrom)) {
                            musicSfx(SFX_MENU, 0);
                        } else {
                            setMsg("RU/UK PREVIEW FILES MISSING", 110);
                        }
                        break;
                    }
#endif
                    G.state = G.langFrom;
                    gameSetLanguage(G.langPick);
                    if (langLoadFailed()) {       // a pack may disappear after initial probing
                        G.state = ST_LANGPICK;     // keep the load error visible; do not save a failed choice
                        G.langPick = langCur();
                    } else settingsSave();
                }
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

#ifdef WP_CYR_PREVIEW
            case ST_SCRIPT_PREVIEW: {
                // Non-stylus actions must cancel any pending peel before
                // changing the board, language, help card or current word.
                const bool scriptKeyAction =
                    (down & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_R |
                             KEY_SELECT | KEY_START)) != 0;
                if (scriptKeyAction || (down & KEY_TOUCH)) {
                    scriptPreviewCheckpointStatus = SCRIPT_CHECKPOINT_NONE;
                    scriptPreviewTrainingNotice = 0;
                }
                if (scriptKeyAction) {
                    scriptDropPeekStroke();
                    // A submitted word or Y clearing it while the stylus
                    // remains down must not start a second phantom chain.
                    if (held & KEY_TOUCH) scriptWordStrokeSuppressed = true;
                }
                // L+Y opens localized rules without losing a selected chain.
                // B closes the rules instead of unexpectedly quitting to title.
                if ((down & KEY_Y) && (held & KEY_L)) {
                    scriptPreviewHelp ^= 1;
                    break;
                }
                if (scriptPreviewHelp) {
                    if (down & (KEY_A | KEY_B | KEY_Y | KEY_TOUCH)) {
                        scriptPreviewHelp = 0;
                        break; // dismissal must not also submit or touch a tile
                    }
                    if (!(down & KEY_START)) break;
                }
                // Visible practice button and controller shortcut. A fresh
                // contact never leaks into the newly generated lesson.
                if (!scriptPreviewHelp &&
                    (((down & KEY_X) && (held & KEY_L)) ||
                     ((down & KEY_TOUCH) && gfxScriptTrainingHit(t.px, t.py)))) {
                    scriptDropPeekStroke();
                    scriptWordStrokeSuppressed = true;
                    (void)scriptPreviewToggleTraining(seedCounter);
                    break;
                }
                // Deliberate preview-only shelf checkpoint commands. The
                // retail settings path supplies the FAT device/directory,
                // never the actual retail filename to write.
                if (!scriptPreviewHelp && (held & KEY_L) &&
                    (down & (KEY_START | KEY_B))) {
                    (void)scriptPreviewCheckpoint((down & KEY_START) != 0,
                        settingsPersistent() ? settingsCfgPath() : NULL);
                    break;
                }
                // B erases one controller-selected letter first. With no
                // active chain it preserves the existing exit shortcut.
                if (down & KEY_B) {
                    if (scriptPreviewCursorBacktrack()) break;
                    leaveScriptPreview();
                    break;
                }
                if (down & KEY_START) {
                    leaveScriptPreview();
                    break;
                }
                if (down & KEY_X) {
                    int next = scriptPreviewLanguage ^ 1;
                    bool wasDaily = scriptPreviewDaily != 0;
                    int date = scriptPreviewDailyDate;
                    if (!scriptPreviewOpen(next, seedCounter)) {
                        leaveScriptPreview();
                        setMsg("RU/UK PREVIEW FILES MISSING", 110);
                    } else if (wasDaily) {
                        // Keep deterministic practice in the newly selected
                        // language, but never carry score/ledger across it.
                        (void)scriptPreviewToggleDaily(date, seedCounter);
                    }
                    break;
                }
                if (down & KEY_SELECT) {
                    if (held & KEY_L) {
                        // Deliberate two-key action, leaving normal SELECT
                        // re-deal and stylus-peel control unmodified.
                        (void)scriptPreviewToggleDaily(settingsToday(), seedCounter);
                    } else (void)scriptPreviewNewPass(seedCounter);
                    break;
                }
                if ((down & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN)) &&
                    !(held & KEY_TOUCH)) {
                    int dc = !!(down & KEY_RIGHT) - !!(down & KEY_LEFT);
                    int dr = !!(down & KEY_DOWN) - !!(down & KEY_UP);
                    scriptPreviewCursorMove(dc, dr);
                }
                if (down & KEY_Y) scriptBoardClearChain(&scriptPreviewBoard);
                if (down & KEY_R) {
                    if (held & KEY_L) (void)scriptPreviewRescue();
                    else (void)scriptPreviewHint();
                }
                if (down & KEY_A) {
                    if (scriptPreviewRules.status == SCRIPT_PASSED)
                        (void)scriptPreviewAdvance(seedCounter);
                    else if (scriptPreviewRules.status == SCRIPT_FAILED)
                        (void)scriptPreviewNewPass(seedCounter);
                    else if (scriptPreviewCursorActive) {
                        if (held & KEY_L) (void)scriptPreviewCursorPeel();
                        else (void)scriptPreviewCursorSelect();
                    } else if (!(held & KEY_L))
                        (void)scriptPreviewSubmit();
                }
                if (held & KEY_TOUCH) {
                    int col, row;
                    if (down & KEY_TOUCH) {
                        // A real stylus stroke takes focus away from D-pad
                        // selection without modifying the game board.
                        scriptPreviewCursorHide();
                        scriptWordStrokeActive = false;
                        // A physical touch that arrives together with a
                        // non-touch command cannot chain letters twice.
                        scriptWordStrokeSuppressed = scriptKeyAction;
                        // L at the beginning of the stroke determines the
                        // gesture: it must never turn into letter chaining
                        // after a cancelled peek or a released shoulder.
                        scriptPeekStroke = (held & KEY_L) != 0;
                        scriptPeelArmed = false;
                        if (scriptPeekStroke &&
                            gfxCellAt(t.px, t.py, &col, &row)) {
                            scriptPeelArmed = true;
                            scriptPeelCol = col;
                            scriptPeelRow = row;
                        } else if (!scriptPeekStroke &&
                                   !scriptWordStrokeSuppressed &&
                                   gfxCellAt(t.px, t.py, &col, &row)) {
                            // Inspect a previously earned number without
                            // selecting or submitting letters. Center taps
                            // on a word tail still retain normal submission.
                            if (gfxScriptClueHit(t.px, t.py, col, row) &&
                                scriptPreviewFocusClue(col, row)) {
                                scriptWordStrokeSuppressed = true;
                            } else if (scriptPreviewTrySubmitTail(col, row)) {
                                // Only fresh contact on a valid word tail
                                // submits. Never retrace the cleared word.
                                scriptWordStrokeSuppressed = true;
                            }
                        }
                    }
                    if (scriptPeekStroke) {
                        // Moving away (even to another gem) or releasing L
                        // disarms permanently until a fresh stylus stroke.
                        if (scriptPeelArmed &&
                            (!(held & KEY_L) ||
                             !gfxCellAt(t.px, t.py, &col, &row) ||
                             col != scriptPeelCol || row != scriptPeelRow))
                            scriptPeelArmed = false;
                    } else if (held & KEY_L) {
                        // Grabbing peek partway through a word stroke
                        // suppresses the REST of that stroke. Releasing L
                        // must not start an accidental second word gesture.
                        scriptWordStrokeActive = false;
                        scriptWordStrokeSuppressed = true;
                    } else if (!scriptWordStrokeSuppressed) {
                        int valid = gfxCellAt(t.px, t.py, &col, &row);
                        // Up to 32 bounded 4px samples bridge real adjacent
                        // tiles crossed between video frames. Both endpoints
                        // must be inside the public board: returning from a
                        // panel button must never trace a phantom word.
                        if (valid && scriptWordStrokeActive &&
                            scriptPreviewBoard.chainLen) {
                            int fromC, fromR;
                            if (gfxCellAt(scriptWordLastTx, scriptWordLastTy,
                                          &fromC, &fromR)) {
                                int dx = t.px - scriptWordLastTx;
                                int dy = t.py - scriptWordLastTy;
                                int adx = dx < 0 ? -dx : dx;
                                int ady = dy < 0 ? -dy : dy;
                                int span = adx > ady ? adx : ady;
                                if (span > 10) {
                                    int steps = span / 4;
                                    if (steps > 32) steps = 32;
                                    for (int i = 1; i < steps; i++) {
                                        int sx = scriptWordLastTx + dx * i / steps;
                                        int sy = scriptWordLastTy + dy * i / steps;
                                        int cc, rr;
                                        if (gfxCellAt(sx, sy, &cc, &rr))
                                            (void)scriptPreviewChain(cc, rr);
                                    }
                                }
                            }
                        }
                        if (valid) (void)scriptPreviewChain(col, row);
                        scriptWordStrokeActive = true;
                        scriptWordLastTx = t.px;
                        scriptWordLastTy = t.py;
                    }
                } else if (keysUp() & KEY_TOUCH) {
                    // A real peel is a single irreversible turn; canceling a
                    // drag costs zero turns, heat or clues, like retail.
                    if (scriptPeekStroke && scriptPeelArmed && (held & KEY_L))
                        (void)scriptPreviewPeel(scriptPeelCol, scriptPeelRow);
                    scriptDropPeekStroke();
                }
                break;
            }
#endif
            case ST_PLAY:
                handlePlay(down, held, &t);
                updateCue(held);
                break;
            case ST_PASSED:
                G.stateTimer++;
                if (G.stateTimer == 1) cardTapQueued = false;
                if (G.training) {
                    if (G.stateTimer > CARD_QUEUE_FROM &&
                        G.stateTimer <= CARD_GATE_PASSED &&
                        (down & (KEY_TOUCH | KEY_A | KEY_START))) cardTapQueued = true;
                    if (G.stateTimer > CARD_GATE_PASSED) {
                        if (down & KEY_B) quitToTitle();
                        else if ((down & (KEY_TOUCH | KEY_A | KEY_START)) || cardTapQueued) {
                            cardTapQueued = false;
                            srand(seedCounter);
                            newGame(MODE_CLASSIC);
                        }
                    }
                    break;
                }
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
            if (pendingN) { setMsg(pendingMsg[0], pendingFrames[0]); pendingN--; memcpy(pendingMsg[0], pendingMsg[1], MSG_LEN); pendingFrames[0] = pendingFrames[1]; }
            else if (G.state == ST_PLAY || G.state == ST_PAUSE) tutorAfterMsg();     // the guide's next prompt, or its end
        }
        boardAnimate(&G);
        musicTick(&G);
        debugSync(&G);
        gfxFrame(&G);
    }
    return 0;
}
