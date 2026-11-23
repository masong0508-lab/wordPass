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
enum { SCRIPT_HOLE = 0, SCRIPT_TILE = 1, SCRIPT_BURN = 2, SCRIPT_DIAMOND = 3,
       SCRIPT_EMPTY = 4 };  // temporary vacated cell, distinct from a permanent peeled hole
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
    // Fixed back-layer surveys: each bit records a coordinate whose nearby
    // password count has been earned by submitting a valid word. Seven bytes,
    // no heap; the published number itself is derived from back + diamonds.
    // Added at the end to preserve the debug mirror's legacy field offsets.
    uint8_t surveyed[SCRIPT_COLS];  // one bit per row; reset each shelf
    uint8_t clueC, clueR;          // last earned clue for a compact HUD focus
} ScriptBoard;
bool scriptBoardStart(ScriptBoard *board, const V4Tiles *tiles, uint32_t seed);
// Build a deterministic test-only password shelf from validated WPP2 pools.
// Requested length 4..9 uses nearest length available within the chosen theme.
bool scriptBoardStartPass(ScriptBoard *board, const V4Dict *dictionary,
                          const V4Tiles *tiles, const V4Pools *pools,
                          uint32_t seed, int requestedLength);
// Plant one valid, theme-aware 4..6-symbol word on a deterministic adjacent
// path in the top five rows, never over hidden password tiles. Prefer this
// shelf's actual theme; fall back to a valid nearby theme for sparse pools.
// Does not modify password placement, board RNG, saves or the symbol bag.
// Called once on a new experimental shelf (never per frame).
bool scriptBoardPlantThemeWord(ScriptBoard *board, const V4Dict *dictionary,
                               const V4Tiles *tiles, const V4Pools *pools,
                               uint32_t seed);
// On a NEW Russian/Ukrainian shelf only, plant up to two DISTINCT valid
// themed word chains using disjoint columns. They survive one another's
// submit/refill, never touch the hidden pass, and never consume the tile bag.
// Returns 0 on failure, 1 for sparse packs, or 2 when both are available.
// Rescue still calls the single-word API above and keeps its heat cost.
int scriptBoardPlantOpeningWords(ScriptBoard *board, const V4Dict *dictionary,
                                 const V4Tiles *tiles, const V4Pools *pools,
                                 uint32_t seed);
// A peel returns -1 for invalid action, 0 for empty hole, or 1..passLen for
// the hidden symbol it revealed. Revealed diamonds are permanent firebreaks.
int scriptBoardPeel(ScriptBoard *board, int col, int row);
// Exactly the same shifted-hex neighbor rule as the retail board. Only a
// previously opened HOLE or surveyed word endpoint publishes a number.
// -1 means no visible clue, 0..6 is the number of adjacent pass letters,
// including already revealed diamonds; the center is NEVER counted.
int scriptBoardVisibleClue(const ScriptBoard *board, int col, int row);
// Logical deductions from a *visible* clue; counts only revealed diamonds
// and still-covered cells. No clue, contradictory data, and zero unknowns
// must never advertise a forced peel or expose unopened back locations.
enum { SCRIPT_CLUE_UNSEEN = -1, SCRIPT_CLUE_UNKNOWN = 0,
       SCRIPT_CLUE_SAFE = 1, SCRIPT_CLUE_PASS = 2 };
int scriptBoardClueInference(const ScriptBoard *board, int col, int row,
                             int *remaining, int *covered);
bool scriptBoardPassComplete(const ScriptBoard *board);
bool scriptBoardPassUtf8(const ScriptBoard *board, const V4Dict *dictionary,
                         char *out, size_t capacity);
bool scriptBoardAdd(ScriptBoard *board, int col, int row); // append, undo, or truncate
void scriptBoardClearChain(ScriptBoard *board);
bool scriptBoardIgnite(ScriptBoard *board, uint32_t turnSeed);
// Returns true when fire escapes the board and the shelf is lost. Flame
// movement and a tile gravity/refill pass are deterministic, heap-free.
bool scriptBoardFireStep(ScriptBoard *board, const V4Tiles *tiles);
// Find an existing valid 3..6-symbol path with bounded prefix-pruned DFS.
// Returns path length, or zero when none is found within the search budget.
// Outputs are caller-owned arrays with at least SCRIPT_HINT_MAX glyphs.
#define SCRIPT_HINT_MAX 6
int scriptBoardHint(const ScriptBoard *board, const V4Dict *dict,
                    const V4Tiles *tiles, uint8_t outC[SCRIPT_HINT_MAX],
                    uint8_t outR[SCRIPT_HINT_MAX]);
int scriptBoardPreview(const ScriptBoard *board, const V4Dict *dict,
                       const V4Tiles *tiles, int *tier);
int scriptBoardSubmit(ScriptBoard *board, const V4Dict *dict,
                      const V4Tiles *tiles, int *tier);
bool scriptBoardChainUtf8(const ScriptBoard *board, const V4Dict *dict,
                          char *out, size_t capacity);
#endif
