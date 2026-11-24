// Smaller Classic-style progression loop for the opt-in Cyrillic DS preview.
// Source of truth for the retail English / ten Latin languages is board.c.
// Explicitly separate while full fire, ledger, hints and saving are ported.
#include "script_rules.h"
#include <string.h>

int scriptRulesPassTarget(unsigned shelf, uint32_t seed)
{
    // Small introduction, then gradually broaden password lengths. A
    // successful word or pass may still demand several turns of reasoning.
    // This chooses a TARGET, never invents a password missing from WPP2.
    unsigned low = 4, range = 2;
    if (shelf >= 35) { low = 6; range = 4; }
    else if (shelf >= 20) { low = 6; range = 3; }
    else if (shelf >= 10) { low = 5; range = 3; }
    else if (shelf >= 4) { low = 5; range = 2; }
    // Mix the shelf identity into the free-play seed without RNG state;
    // Daily Practice receives the same date/language target on every retry.
    uint32_t roll = seed ^ (shelf * 0x9e3779b9u);
    roll ^= roll >> 16;
    roll *= 0x7feb352du;
    roll ^= roll >> 15;
    return (int)(low + roll % range);
}

void scriptRulesStart(ScriptRules *rules, unsigned shelf, uint32_t seed)
{
    if (!rules) return;
    memset(rules, 0, sizeof(*rules));
    if (!shelf) shelf = 1;
    if (shelf > 999) shelf = 999;
    rules->shelf = (uint16_t)shelf;
    rules->seed = seed ? seed : 0x9e3779b9u;
    // The board's sampled burn cadence shortens gently as shelves advance.
    unsigned burnEvery = 4;
    if (shelf >= 8) burnEvery--;
    if (shelf >= 20) burnEvery--;
    rules->burnEvery = (uint8_t)burnEvery;
    unsigned heatThreshold = 8;
    if (shelf >= 6) heatThreshold--;
    if (shelf >= 13) heatThreshold--;
    if (shelf >= 24) heatThreshold--;
    rules->heatThreshold = (uint8_t)heatThreshold;
    rules->status = SCRIPT_RUNNING;
}
int scriptRulesWordHeat(int letters)
{
    if (letters <= 0) return 0;
    return letters <= 3 ? 3 : letters == 4 ? 2 : letters <= 6 ? 1 : 0;
}
static int rulesTurnCore(ScriptRules *rules, ScriptBoard *board,
                         const V4Tiles *tiles, int heatBase, bool peel,
                         bool rescue, bool fatal)
{
    if (!rules || !board || !tiles || !tiles->valid ||
        rules->status != SCRIPT_RUNNING) return 0;
    rules->turns++;
    rules->peels += peel ? 1u : 0u;
    rules->words += (peel || rescue) ? 0u : 1u;
    rules->lastBonus = 0;
    if (fatal) {
        rules->failReason = SCRIPT_LOSS_SCORCH;
        rules->status = SCRIPT_FAILED;
        return 0;
    }
    // The final password letter completes the shelf before another fire
    // step can destroy an already-recovered secret.
    if (scriptBoardPassComplete(board)) {
        int speed = 16 - rules->turns;
        int efficiency = (int)board->passLen + 3 - rules->peels;
        if (speed < 0) speed = 0;
        if (efficiency < 0) efficiency = 0;
        rules->lastBonus = 150 * rules->shelf + 20 * speed + 10 * efficiency;
        rules->status = SCRIPT_PASSED;
        return rules->lastBonus;
    }
    if (heatBase < 0) heatBase = 0;
    int budget = 12 + rules->shelf / 3;
    int overtime = (int)rules->turns - budget;
    if (overtime > 0) heatBase += 1 + overtime / 4;
    unsigned nextHeat = rules->heat + (unsigned)heatBase;
    if (nextHeat > 65535u) nextHeat = 65535u;
    rules->heat = (uint16_t)nextHeat;

    // Advance the old flames BEFORE igniting a new tile. The new flame stays
    // on its top row for one complete burn interval.
    if (++rules->burnTick >= rules->burnEvery) {
        rules->burnTick = 0;
        if (scriptBoardFireStep(board, tiles)) {
            rules->failReason = SCRIPT_LOSS_FIRE;
            rules->status = SCRIPT_FAILED;
            return 0;
        }
    }
    if (rules->heat >= rules->heatThreshold) {
        if (scriptBoardIgnite(board, rules->seed ^ (uint32_t)rules->turns ^
                                   ((uint32_t)rules->ignitions << 16))) {
            rules->ignitions++;
            // During overtime, excess heat is retained so endless stalling
            // eventually lights more flames. Before overtime it resets.
            rules->heat = overtime > 0 ?
                (uint16_t)(rules->heat - rules->heatThreshold) : 0;
        } else {
            rules->heat = rules->heatThreshold - 1;
        }
    }
    return 0;
}

int scriptRulesTurn(ScriptRules *rules, ScriptBoard *board,
                    const V4Tiles *tiles, int heatBase, bool peel, bool fatal)
{
    return rulesTurnCore(rules, board, tiles, heatBase, peel, false, fatal);
}
int scriptRulesRescueTurn(ScriptRules *rules, ScriptBoard *board,
                          const V4Tiles *tiles)
{
    // Reject an already recovered password even if a caller has not yet
    // advanced the status flag. A rescue must never award a pass bonus.
    if (!rules || !board || !tiles || !tiles->valid ||
        rules->status != SCRIPT_RUNNING || scriptBoardPassComplete(board))
        return 0;
    // Recovering a legal move is not a word or a peel and never awards
    // points; it still advances flame, heat and overtime once.
    return rulesTurnCore(rules, board, tiles, 2, false, true, false);
}
