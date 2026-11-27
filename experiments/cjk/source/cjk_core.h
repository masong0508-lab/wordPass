#ifndef WORDPASS_CJK_CORE_H
#define WORDPASS_CJK_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CJ_COLS 6
#define CJ_ROWS 6
#define CJ_CELLS (CJ_COLS*CJ_ROWS)
#define CJ_MAX 6
enum { CJ_FIRE_OFF=-1,CJ_FIRE_RELAXED=0,CJ_FIRE_NORMAL=1,CJ_FIRE_FAST=2 };
enum { CJ_LOSS_NONE=0,CJ_LOSS_FIRE=1 };
typedef struct {
    uint16_t text[CJ_MAX];
    uint8_t len;
    const char *reading, *meaning;
} CjWord;
typedef struct { const CjWord *words; uint16_t count; } CjLexicon;
typedef struct {
    const CjLexicon *lex;
    uint16_t tile[CJ_CELLS];
    uint16_t pass[CJ_MAX];
    uint8_t back[CJ_CELLS], opened[CJ_CELLS], surveyed[CJ_CELLS];
    uint8_t chain[CJ_MAX], len, passLen, foundMask;
    int8_t focus;
    uint16_t lastWord;
    uint32_t rng, score, turns;
    uint32_t dealSeed, bankedScore;
    uint16_t shelf;
    int8_t fireMode;
    uint8_t heat, heatLimit, burnEvery, burnTick, loss;
    uint8_t flame[CJ_CELLS];
} CjGame;
bool cjAdjacent(int a,int b);
bool cjStart(CjGame *g,const CjLexicon *lex,uint32_t seed);
bool cjStartMode(CjGame *g,const CjLexicon *lex,uint32_t seed,int mode,unsigned shelf,uint32_t score);
bool cjNextShelf(CjGame *g);
bool cjRetryShelf(CjGame *g);
int cjWordHeat(int len);
int cjHeatCost(const CjGame *g,int base);
bool cjPeelBlocked(const CjGame *g,int cell);
bool cjEnded(const CjGame *g);
int cjLookup(const CjLexicon *lex,const uint16_t *text,int len);
int cjSelected(const CjGame *g);
bool cjAdd(CjGame *g,int cell);
bool cjSubmit(CjGame *g);
int cjClue(const CjGame *g,int cell);
// Read only an already-public clue; do not count uncovered diamonds twice.
bool cjEvidence(const CjGame *g,int cell,int *remaining,int *covered);
int cjPeel(CjGame *g,int cell);
bool cjHint(CjGame *g);
// Paid rescue only when no bounded hint exists; leaves password evidence intact.
bool cjRecover(CjGame *g);
bool cjComplete(const CjGame *g);
#endif
