// Optional on-hardware Cyrillic engineering preview. NOT a selectable
// language: tiny test-only lexicon and themes, no saves, no production UI.
// Only a separate build with WP_CYR_PREVIEW includes this feature.
#ifndef WORDPASS_SCRIPT_PREVIEW_H
#define WORDPASS_SCRIPT_PREVIEW_H
#ifdef WP_CYR_PREVIEW
#include "script_board.h"
#include "script_rules.h"
#include "script_ledger.h"
#include <stdint.h>
#include <stdbool.h>
extern ScriptBoard scriptPreviewBoard;
extern V4Dict scriptPreviewDict;
extern V4Tiles scriptPreviewTiles;
extern V4Pools scriptPreviewPools;
extern int scriptPreviewLanguage;  // 0 Russian, 1 Ukrainian
extern int scriptPreviewScore;
extern int scriptPreviewThemeCount; // inspect 40 draft themes in emulator regression
extern int scriptPreviewTurns;
extern int scriptPreviewPeels;
extern int scriptPreviewHints;
extern int scriptPreviewShelf;
extern ScriptRules scriptPreviewRules;
extern ScriptLedger scriptPreviewLedger;
extern int scriptPreviewLastTier;
extern int scriptPreviewLastScore;
extern uint32_t scriptPreviewSeed;

bool scriptPreviewOpen(int language, uint32_t seed);
void scriptPreviewClose(void);
bool scriptPreviewNewPass(uint32_t seed);
// Advance only after a completed password. No production save changes.
bool scriptPreviewAdvance(uint32_t seed);
int scriptPreviewPeel(int c, int r);
bool scriptPreviewChain(int c, int r);
// One free, bounded path hint per engineering shelf (retail hint economy TBD).
bool scriptPreviewHint(void);
// Score the current valid word with repeat penalty and combo before submit.
int scriptPreviewQuote(void);
int scriptPreviewSubmit(void);
#endif
#endif
