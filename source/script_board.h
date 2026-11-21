// Experimental symbol-ID game board for WPD4/WPL2 dictionaries.
// This model deliberately stays separate from the original 26-letter board
// until Cyrillic pass pools, UI and save/score integration are implemented.
// All memory is fixed at compile time; no heap or framebuffer allocations.
#ifndef WORDPASS_SCRIPT_BOARD_H
#define WORDPASS_SCRIPT_BOARD_H
#include "dict_v4.h"
#include "tiles_v2.h"
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
    uint8_t bag[WPL2_BAG_SIZE];
    uint8_t bagPos;
    uint32_t seed;
} ScriptBoard;
bool scriptBoardStart(ScriptBoard *board, const V4Tiles *tiles, uint32_t seed);
bool scriptBoardAdd(ScriptBoard *board, int col, int row); // append, undo, or truncate
void scriptBoardClearChain(ScriptBoard *board);
int scriptBoardPreview(const ScriptBoard *board, const V4Dict *dict,
                       const V4Tiles *tiles, int *tier);
int scriptBoardSubmit(ScriptBoard *board, const V4Dict *dict,
                      const V4Tiles *tiles, int *tier);
bool scriptBoardChainUtf8(const ScriptBoard *board, const V4Dict *dict,
                          char *out, size_t capacity);
#endif
