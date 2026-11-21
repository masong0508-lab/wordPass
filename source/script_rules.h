// Cyrillic wordPass engineering-preview shelf rules.
// No save migration, heap allocations or 26-letter gameplay changes.
// The structure is independent of DS platform headers for native tests.
#ifndef WORDPASS_SCRIPT_RULES_H
#define WORDPASS_SCRIPT_RULES_H
#include "script_board.h"

enum { SCRIPT_RUNNING = 0, SCRIPT_PASSED = 1, SCRIPT_FAILED = 2 };
typedef struct {
    uint16_t shelf, turns, peels, words;
    uint16_t heat, ignitions;
    uint8_t heatThreshold, burnEvery, burnTick, status;
    int lastBonus;
    uint32_t seed;
} ScriptRules;

// A new shelf resets the heat and turn clock; score is kept separately.
void scriptRulesStart(ScriptRules *rules, unsigned shelf, uint32_t seed);
// Call once after a successful word or an accepted peel (including a fatal
// scorch). Returns a pass bonus, otherwise 0. Handles heat, flames, overtime,
// failure and completion in one place, allowing deterministic replays.
int scriptRulesTurn(ScriptRules *rules, ScriptBoard *board,
                    const V4Tiles *tiles, int heatBase, bool peel, bool fatal);
// Derive heat for a valid word; longer words feed the fire less.
int scriptRulesWordHeat(int letters);
#endif
