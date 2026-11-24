// Optional on-hardware Cyrillic engineering preview. NOT a selectable
// language: separate preview lexicons/themes and independent checkpoints,
// not the production language UI, statistics or save format.
// Only a separate build with WP_CYR_PREVIEW includes this feature.
#ifndef WORDPASS_SCRIPT_PREVIEW_H
#define WORDPASS_SCRIPT_PREVIEW_H
#ifdef WP_CYR_PREVIEW
#include "script_board.h"
#include "script_rules.h"
#include "script_ledger.h"
#include "script_daily.h"
#include "script_checkpoint.h"
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
extern int scriptPreviewCheckpointStatus;
extern uint32_t scriptPreviewSeed;
// Public non-tile spelling metadata cached once on successful shelf start.
extern uint32_t scriptPreviewPassApostrophe;
// 1..2 guaranteed legal opening words for the current demo shelf.
extern int scriptPreviewOpeningWords;
// Incremented ONLY when a full Russian/Ukrainian dictionary image is loaded.
// Forces font-cache renewal even when malloc reuses the previous file address.
extern uint32_t scriptPreviewGlyphEpoch;
// Diagnostic stage for a failed source-backed preview launch (0 = success).
// Exposed to the test ELF; never persisted in normal saves.
extern int scriptPreviewOpenError;
// Optional demo's controller-only focus, independent of the saved Game and
// board binary format. Zero means touch/normal hint selection is active.
extern int scriptPreviewCursorActive, scriptPreviewCursorCol, scriptPreviewCursorRow;
// D-pad can focus any of the 7x8 slots, including clues/holes. A chains or
// submits when pressed on the selected word's tail; L+A peels a selected
// covered gem. B steps back one letter, or exits if no chain remains.
// Invalid controller actions never consume a turn/heat or alter the board.
void scriptPreviewCursorMove(int dc, int dr);
void scriptPreviewCursorHide(void);
bool scriptPreviewCursorSelect(void);
int scriptPreviewCursorPeel(void);
bool scriptPreviewCursorBacktrack(void);

bool scriptPreviewOpen(int language, uint32_t seed);
void scriptPreviewClose(void);
// Replay/re-deal the current shelf, discarding this attempt's points while
// retaining completed earlier shelves. Failed generation leaves it intact.
bool scriptPreviewNewPass(uint32_t seed);
// Explicit free-play shelf-start checkpoints. Pass NULL when FAT is absent.
// Saving is permitted only at the beginning of a fresh shelf; loading
// atomically replaces the current attempt only after regeneration succeeds.
int scriptPreviewCheckpoint(bool save, const char *settingsPath);
// L+SELECT switches between free shelves and reproducible single-shelf
// Daily practice; never touches the production score and save format.
bool scriptPreviewToggleDaily(int date, uint32_t freeSeed);
// Advance only after a completed password. No production save changes.
bool scriptPreviewAdvance(uint32_t seed);
int scriptPreviewPeel(int c, int r);
bool scriptPreviewChain(int c, int r);
// Revisit ONLY a previously published clue (numbered hole or surveyed
// endpoint). Changes the public focus coordinate, never scores or peels.
bool scriptPreviewFocusClue(int c, int r);
// A fresh touch on the current selected word's last gem submits only if
// the word is dictionary-valid. A/normal controller submit still works.
bool scriptPreviewTrySubmitTail(int c, int r);
// One free, bounded path hint per engineering shelf (retail hint economy TBD).
bool scriptPreviewHint(void);
// L+R after a game turn plants a fresh valid word around existing hazards,
// charges heat/time and breaks the combo. Never affects retail saves.
bool scriptPreviewRescue(void);
// Score the current valid word with repeat penalty and combo before submit.
// Optionally return its dictionary tier from the same single lookup.
int scriptPreviewQuote(int *tier);
int scriptPreviewQuoteSpelling(int *tier, uint32_t *mask);
int scriptPreviewSubmit(void);
#endif
#endif
