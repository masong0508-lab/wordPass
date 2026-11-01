// Fixed-layout mirror of game state for the emulator test harness.
// Tests read DBG via its symbol address, so changes to Game never break them.
#include "game.h"
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
} DebugView;

volatile DebugView DBG;

void debugSync(const Game *g)
{
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
        }
    memcpy((void *)DBG.msg, g->msg, 32);
    memcpy((void *)DBG.lastWord, g->lastWord, 16);
    memcpy((void *)DBG.bestWord, g->bestWord, 16);
    DBG.lastWordTier = g->lastWordTier; DBG.rareWords = g->rareWords; DBG.library = S.library;
}
