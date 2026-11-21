// Optional on-hardware Cyrillic engineering preview. NOT a selectable
// language: tiny test-only lexicon and themes, no saves, no production UI.
// Only a separate build with WP_CYR_PREVIEW includes this feature.
#ifndef WORDPASS_SCRIPT_PREVIEW_H
#define WORDPASS_SCRIPT_PREVIEW_H
#ifdef WP_CYR_PREVIEW
#include "script_board.h"
#include "script_rules.h"
#include "script_ledger.h"
#include "script_daily.h"
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
extern int scriptPreviewRescues;      // 0 or 1 successful word recovery per shelf
extern int scriptPreviewRescueError;  // 0 none, 1 too early, 2 used, 3 blocked
extern int scriptPreviewShelf;
extern ScriptRules scriptPreviewRules;
extern ScriptLedger scriptPreviewLedger;
extern int scriptPreviewDaily;     // 0 classic-style demo; 1 unranked Daily practice
extern int scriptPreviewDailyDate; // YYYYMMDD, independent of retail Daily records
extern int scriptPreviewDailyClockError; // unset RTC feedback, no save writes
extern int scriptPreviewHelp; // localized instructions overlay (top LCD only)
extern int scriptPreviewLastTier;
extern int scriptPreviewLastScore;
extern uint32_t scriptPreviewSeed;

bool scriptPreviewOpen(int language, uint32_t seed);
void scriptPreviewClose(void);
bool scriptPreviewNewPass(uint32_t seed);
// L+SELECT switches between free shelves and reproducible single-shelf
// Daily practice; never touches the production score and save format.
bool scriptPreviewToggleDaily(int date, uint32_t freeSeed);
// Advance only after a completed password. No production save changes.
bool scriptPreviewAdvance(uint32_t seed);
int scriptPreviewPeel(int c, int r);
bool scriptPreviewChain(int c, int r);
// One free, bounded path hint per engineering shelf (retail hint economy TBD).
bool scriptPreviewHint(void);
// L+R after a game turn plants a fresh valid word around existing hazards,
// charges heat/time and breaks the combo. Never affects retail saves.
bool scriptPreviewRescue(void);
// Score the current valid word with repeat penalty and combo before submit.
int scriptPreviewQuote(void);
int scriptPreviewSubmit(void);
#endif
#endif
