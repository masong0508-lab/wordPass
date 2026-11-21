// Optional on-hardware Cyrillic engineering preview. NOT a selectable
// language: tiny test-only lexicon and themes, no saves, no production UI.
// Only a separate build with WP_CYR_PREVIEW includes this feature.
#ifndef WORDPASS_SCRIPT_PREVIEW_H
#define WORDPASS_SCRIPT_PREVIEW_H
#ifdef WP_CYR_PREVIEW
#include "script_board.h"
#include <stdint.h>
#include <stdbool.h>
extern ScriptBoard scriptPreviewBoard;
extern V4Dict scriptPreviewDict;
extern V4Tiles scriptPreviewTiles;
extern V4Pools scriptPreviewPools;
extern int scriptPreviewLanguage;  // 0 Russian, 1 Ukrainian
extern int scriptPreviewScore;
extern int scriptPreviewTurns;
extern int scriptPreviewPeels;
extern int scriptPreviewLastTier;
extern int scriptPreviewLastScore;
extern uint32_t scriptPreviewSeed;

bool scriptPreviewOpen(int language, uint32_t seed);
void scriptPreviewClose(void);
bool scriptPreviewNewPass(uint32_t seed);
int scriptPreviewPeel(int c, int r);
bool scriptPreviewChain(int c, int r);
int scriptPreviewSubmit(void);
#endif
#endif
