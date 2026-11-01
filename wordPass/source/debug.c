// Fixed-layout mirror of game state for the emulator test harness, plus a poke channel.
// Tests read DBG via its symbol address, so changes to Game never break them. Every
// documented offset is pinned with _Static_assert so it cannot drift; tools/emu/wp.py
// carries the same numbers in its OFF table.
//
// Poke channel: a test writes cmd/a0..a3, the next debugSync applies the command and
// clears cmd. The writes land in main RAM that the ARM9 would normally cache: fine in
// DeSmuME (no cache emulation) and the channel is test-only, so hardware builds may
// keep it. DBG stays volatile so the reads are never hoisted.
#include "game.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    u32 magic;              // 0x44424731 "DBG1"
    u32 frame;
    s32 state, mode, shelf, score, fail, turn, heat, chainLen, peels, lockdown;
    s32 bestWordScore, dailySeed, passLen;
    char pass[16];
    u8  found[16];
    u8  cellLetter[COLS * ROWS];
    u8  cellState[COLS * ROWS];
    u8  back[COLS * ROWS];
    char msg[32];
    char lastWord[16];
    char bestWord[16];
    s32 lastWordTier, rareWords, library;   // offsets 324, 328, 332
    // ---- poke channel (test -> game)
    u32 cmd, a0, a1, a2, a3;                // 336..355
    // ---- more mirrored state
    s32 heatThreshold, burnEvery, burnTimer; // 356, 360, 364
    u8  scorched[COLS * ROWS];              // 368..423
    u8  theme, hand, fire, music;           // 424..427
    s32 optRow;                             // 428
    u8  chainC[10], chainR[10]; // 432.., 442..
    s32 pulseEvery, lockTier, overtime;     // 452, 456, 460
    s32 chainScore;                         // 464: boardChainScore(g), the live preview
    s32 speedBonus, peelBonus, passBonusKind, ledgerN, newBest;   // 468, 472, 476, 480, 484
    s32 lives, passTheme, twist;            // 488, 492, 496 (lockTier is at 456)
    s32 spelling;                           // 500: S.spelling (0 both, 1 American, 2 British)
    s32 practice, dailyMod, clockOk;        // 504, 508, 512: Daily replay, DAILYMOD_*, the clock reads a real date
    s32 statWords, statGames, streak;       // 516, 520, 524: S.wordsSpelled, S.gamesPlayed, S.dailyStreak
    s32 dailyPlayed, dailyLast, dailyLastScore;   // 528, 532, 536: S.dailyPlayedDate, S.dailyLastDate, S.dailyLastScore
    s32 peek, armed, armC, armR;            // 540, 544, 548, 552: peekHeld || peekLatch, the armed peel and its cell
    s32 curC, curR, curOn;                  // 556, 560, 564: the d-pad cursor
} DebugView;                                // total 568

#define PIN(field, off) _Static_assert(offsetof(DebugView, field) == (off), "DebugView." #field " moved: update tools/emu/wp.py OFF")
PIN(magic, 0); PIN(frame, 4); PIN(state, 8); PIN(mode, 12); PIN(shelf, 16); PIN(score, 20);
PIN(fail, 24); PIN(turn, 28); PIN(heat, 32); PIN(chainLen, 36); PIN(peels, 40); PIN(lockdown, 44);
PIN(bestWordScore, 48); PIN(dailySeed, 52); PIN(passLen, 56);
PIN(pass, 60); PIN(found, 76); PIN(cellLetter, 92); PIN(cellState, 148); PIN(back, 204);
PIN(msg, 260); PIN(lastWord, 292); PIN(bestWord, 308);
PIN(lastWordTier, 324); PIN(rareWords, 328); PIN(library, 332);
PIN(cmd, 336); PIN(a0, 340); PIN(a1, 344); PIN(a2, 348); PIN(a3, 352);
PIN(heatThreshold, 356); PIN(burnEvery, 360); PIN(burnTimer, 364);
PIN(scorched, 368); PIN(theme, 424); PIN(hand, 425); PIN(fire, 426); PIN(music, 427);
PIN(optRow, 428); PIN(chainC, 432); PIN(chainR, 442);
PIN(pulseEvery, 452); PIN(lockTier, 456); PIN(overtime, 460);
PIN(chainScore, 464); PIN(speedBonus, 468); PIN(peelBonus, 472); PIN(passBonusKind, 476); PIN(ledgerN, 480); PIN(newBest, 484);
PIN(lives, 488); PIN(passTheme, 492); PIN(twist, 496); PIN(spelling, 500);
PIN(practice, 504); PIN(dailyMod, 508); PIN(clockOk, 512); PIN(statWords, 516); PIN(statGames, 520); PIN(streak, 524);
PIN(dailyPlayed, 528); PIN(dailyLast, 532); PIN(dailyLastScore, 536);
PIN(curC, 556); PIN(curR, 560); PIN(curOn, 564);
PIN(peek, 540); PIN(armed, 544); PIN(armC, 548); PIN(armR, 552);
_Static_assert(sizeof(DebugView) == 568, "DebugView size changed: update tools/emu/wp.py");

volatile DebugView DBG;

enum {
    POKE_NONE = 0,
    POKE_CELL,      // a0=c a1=r a2=state a3=letter; motion and flash cleared
    POKE_BACK,      // back[a0][a1] = a2
    POKE_HEAT,      // heat = a0
    POKE_BURN,      // burnTimer = a0, burnEvery = a1
    POKE_FIRESTEP,  // one fire advance plus gravity
    POKE_TURN,      // turn = a0
    POKE_LIVES,     // lives = a0 (bookmarks)
    POKE_SCORE,     // score = a0 (panel layout checks)
    POKE_SPELL,     // S.spelling = a0 (0 both, 1 American, 2 British)
    POKE_CLOCK,     // the DS clock reads YYYYMMDD a0 (0 = the real clock)
    POKE_DAILY      // S.dailyPlayedDate = a0, S.dailyLastDate = a1, S.dailyStreak = a2 (records only; nothing is saved)
};

static void applyPoke(Game *g)
{
    u32 cmd = DBG.cmd;
    if (!cmd) return;
    int a0 = (int)DBG.a0, a1 = (int)DBG.a1, a2 = (int)DBG.a2, a3 = (int)DBG.a3;
    bool inBoard = a0 >= 0 && a0 < COLS && a1 >= 0 && a1 < ROWS;
    switch (cmd) {
        case POKE_CELL:
            if (inBoard) {
                Cell *cl = &g->cell[a0][a1];
                cl->state = (u8)a2; cl->letter = (u8)(a3 % 26);
                cl->yoff = 0; cl->yv = 0; cl->squash = 0; cl->flash = 0;
            }
            break;
        case POKE_BACK:     if (inBoard) g->back[a0][a1] = (u8)a2; break;
        case POKE_HEAT:     g->heat = a0; break;
        case POKE_BURN:     g->burnTimer = a0; g->burnEvery = a1 > 0 ? a1 : 1; break;
        case POKE_FIRESTEP: boardDebugFireStep(g); break;
        case POKE_TURN:     g->turn = a0; break;
        case POKE_LIVES:    g->lives = a0; break;
        case POKE_SCORE:    g->score = a0; break;
        case POKE_SPELL:    if (a0 >= 0 && a0 < SPELL_COUNT) S.spelling = (u8)a0; break;
        case POKE_CLOCK:    settingsDebugClock(a0); break;
        case POKE_DAILY:    S.dailyPlayedDate = a0; S.dailyLastDate = a1; S.dailyStreak = a2; break;
    }
    DBG.cmd = 0;
}

void debugSync(Game *g)
{
    applyPoke(g);
    DBG.magic = 0x44424731;
    DBG.frame++;
    DBG.state = g->state; DBG.mode = g->mode; DBG.shelf = g->shelf; DBG.score = g->score;
    DBG.fail = g->fail; DBG.turn = g->turn; DBG.heat = g->heat; DBG.chainLen = g->chainLen;
    DBG.peels = g->peels; DBG.lockdown = g->lockdown; DBG.bestWordScore = g->bestWordScore;
    DBG.dailySeed = g->dailySeed; DBG.passLen = g->passLen;
    memcpy((void *)DBG.pass, g->pass, 12); DBG.pass[12] = 0;
    memcpy((void *)DBG.found, g->found, 12);
    for (int c = 0; c < COLS; c++)
        for (int r = 0; r < ROWS; r++) {
            DBG.cellLetter[c * ROWS + r] = g->cell[c][r].letter;
            DBG.cellState[c * ROWS + r] = g->cell[c][r].state;
            DBG.back[c * ROWS + r] = g->back[c][r];
            DBG.scorched[c * ROWS + r] = g->scorched[c][r];
        }
    memcpy((void *)DBG.msg, g->msg, 32);
    memcpy((void *)DBG.lastWord, g->lastWord, 15); DBG.lastWord[15] = 0;   // 15 letters at most are mirrored
    memcpy((void *)DBG.bestWord, g->bestWord, 15); DBG.bestWord[15] = 0;
    DBG.lastWordTier = g->lastWordTier; DBG.rareWords = g->rareWords; DBG.library = S.library; DBG.spelling = S.spelling;
    DBG.heatThreshold = g->heatThreshold; DBG.burnEvery = g->burnEvery; DBG.burnTimer = g->burnTimer;
    DBG.theme = S.theme; DBG.hand = S.hand; DBG.fire = S.fire; DBG.music = S.music;
    DBG.optRow = g->optRow;
    memcpy((void *)DBG.chainC, g->chainC, 10);       // the mirror shows the first ten gems of a chain
    memcpy((void *)DBG.chainR, g->chainR, 10);
    DBG.pulseEvery = g->pulseEvery; DBG.lockTier = g->lockTier; DBG.overtime = g->overtime;
    DBG.chainScore = boardChainScore(g);
    DBG.speedBonus = g->speedBonus; DBG.peelBonus = g->peelBonus; DBG.passBonusKind = g->passBonusKind;
    DBG.ledgerN = g->ledgerN; DBG.newBest = g->newBest;
    DBG.lives = g->lives; DBG.passTheme = g->passTheme; DBG.twist = g->twist;
    DBG.practice = g->practice; DBG.dailyMod = g->dailyMod; DBG.clockOk = g->clockOk;
    DBG.peek = g->peekHeld; DBG.armed = g->armed; DBG.armC = g->armC; DBG.armR = g->armR;
    DBG.curC = g->curC; DBG.curR = g->curR; DBG.curOn = g->curOn;
    DBG.statWords = S.wordsSpelled; DBG.statGames = S.gamesPlayed; DBG.streak = S.dailyStreak;
    DBG.dailyPlayed = S.dailyPlayedDate; DBG.dailyLast = S.dailyLastDate; DBG.dailyLastScore = S.dailyLastScore;
}
