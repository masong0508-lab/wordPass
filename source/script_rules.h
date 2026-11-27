// Cyrillic wordPass engineering-preview shelf rules.
// No save migration, heap allocations or 26-letter gameplay changes.
// The structure is independent of DS platform headers for native tests.
#ifndef WORDPASS_SCRIPT_RULES_H
#define WORDPASS_SCRIPT_RULES_H
#include "script_board.h"

enum { SCRIPT_RUNNING = 0, SCRIPT_PASSED = 1, SCRIPT_FAILED = 2 };
enum { SCRIPT_LOSS_NONE = 0, SCRIPT_LOSS_SCORCH = 1, SCRIPT_LOSS_FIRE = 2 };
typedef struct {
    uint16_t shelf, turns, peels, words;
    uint16_t heat, ignitions;
    uint8_t heatThreshold, burnEvery, burnTick, status;
    int lastBonus;
    uint32_t seed;
    // Runtime-only explanation of a terminal loss. Keep earlier fields and
    // their offsets stable; no settings, ROM data or save-format change.
    uint8_t failReason;
} ScriptRules;

// A new shelf resets the heat and turn clock; score is kept separately.
void scriptRulesStart(ScriptRules *rules, unsigned shelf, uint32_t seed);
// 0 relaxed, 1 normal, 2 fast. Apply once at a normal shelf's start.
void scriptRulesApplyFireMode(ScriptRules *rules, int mode);
// Deterministic engineering-preview password-length target (4..9).
// The board chooses the nearest real word length within its actual theme
// when the requested length is not in that language's WPP2 source pool.
// No additional ROM data, save format changes or per-frame work.
int scriptRulesPassTarget(unsigned shelf, uint32_t seed);
// Call once after a successful word or an accepted peel (including a fatal
// scorch). Returns a pass bonus, otherwise 0. Handles heat, flames, overtime,
// failure and completion in one place, allowing deterministic replays.
int scriptRulesTurn(ScriptRules *rules, ScriptBoard *board,
                    const V4Tiles *tiles, int heatBase, bool peel, bool fatal);
// Practice uses the same score/completion logic, but cannot ignite fire.
int scriptRulesTrainingTurn(ScriptRules *rules, ScriptBoard *board,
                            const V4Tiles *tiles, int heatBase, bool peel);
// A one-time rescue costs a real heat/fire turn but is neither a word nor
// a peel, so it cannot inflate scoring stats or distort pass efficiency.
int scriptRulesRescueTurn(ScriptRules *rules, ScriptBoard *board,
                          const V4Tiles *tiles);
// Derive heat for a valid word; longer words feed the fire less.
int scriptRulesWordHeat(int letters);
// Shared calculation for the actual turn and the live HUD heat quote.
// turns is the count AFTER the proposed action.
static inline int scriptRulesOvertime(const ScriptRules *rules, int turns)
{
    return turns - (12 + rules->shelf / 3);
}
static inline int scriptRulesOvertimeHeat(int overtime)
{
    return overtime > 0 ? 1 + overtime / 4 : 0;
}
#endif
