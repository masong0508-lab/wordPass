// Experimental symbol-ID game board for WPD4/WPL2 dictionaries.
// This model deliberately stays separate from the original 26-letter board
// until Cyrillic pass pools, UI and save/score integration are implemented.
// All memory is fixed at compile time; no heap or framebuffer allocations.
#ifndef WORDPASS_SCRIPT_BOARD_H
#define WORDPASS_SCRIPT_BOARD_H
#include "dict_v4.h"
#include "tiles_v2.h"
#include "pools_v2.h"
#define SCRIPT_COLS 7
#define SCRIPT_ROWS 8
#define SCRIPT_MAX_CHAIN 20
enum { SCRIPT_HOLE = 0, SCRIPT_TILE = 1, SCRIPT_BURN = 2, SCRIPT_DIAMOND = 3 };
typedef struct {
    uint8_t tile[SCRIPT_COLS][SCRIPT_ROWS];    // 0..63, dictionary alphabet index
    uint8_t state[SCRIPT_COLS][SCRIPT_ROWS];
    uint8_t chainC[SCRIPT_MAX_CHAIN];
    uint8_t chainR[SCRIPT_MAX_CHAIN];
    uint8_t chainLen;
    uint8_t back[SCRIPT_COLS][SCRIPT_ROWS]; // 0 hidden/no pass, 1..passLen for hidden secrets
    uint8_t pass[WPP2_MAX_LEN];            // symbol IDs, never ASCII bytes
    uint8_t found[WPP2_MAX_LEN];           // revealed pass symbols
    uint8_t passLen, passTheme;
    uint8_t bag[WPL2_BAG_SIZE];
    uint8_t bagPos;
    uint32_t seed;
} ScriptBoard;
bool scriptBoardStart(ScriptBoard *board, const V4Tiles *tiles, uint32_t seed);
// Build a deterministic test-only password shelf from validated WPP2 pools.
// Requested length 4..9 uses nearest length available within the chosen theme.
bool scriptBoardStartPass(ScriptBoard *board, const V4Dict *dictionary,
                          const V4Tiles *tiles, const V4Pools *pools,
                          uint32_t seed, int requestedLength);
// A peel returns -1 for invalid action, 0 for empty hole, or 1..passLen for
// the hidden symbol it revealed. Revealed diamonds are permanent firebreaks.
int scriptBoardPeel(ScriptBoard *board, int col, int row);
bool scriptBoardPassComplete(const ScriptBoard *board);
bool scriptBoardPassUtf8(const ScriptBoard *board, const V4Dict *dictionary,
                         char *out, size_t capacity);
bool scriptBoardAdd(ScriptBoard *board, int col, int row); // append, undo, or truncate
void scriptBoardClearChain(ScriptBoard *board);
int scriptBoardPreview(const ScriptBoard *board, const V4Dict *dict,
                       const V4Tiles *tiles, int *tier);
int scriptBoardSubmit(ScriptBoard *board, const V4Dict *dict,
                      const V4Tiles *tiles, int *tier);
bool scriptBoardChainUtf8(const ScriptBoard *board, const V4Dict *dict,
                          char *out, size_t capacity);
#endif
