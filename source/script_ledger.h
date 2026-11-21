// Fixed-size word history for the separate Russian/Ukrainian DS preview.
// Mirrors retail's eight-word repeat penalty and 10%-per-word combo capped
// at +50%, without storing UTF-8 copies, allocating memory or touching saves.
#ifndef WORDPASS_SCRIPT_LEDGER_H
#define WORDPASS_SCRIPT_LEDGER_H
#include <stdbool.h>
#include <stdint.h>
#include "dict_v4.h"

#define SCRIPT_LEDGER_N 8

typedef struct {
    uint8_t word[SCRIPT_LEDGER_N][WPD4_MAX_WORD];
    uint8_t length[SCRIPT_LEDGER_N];
    uint16_t points[SCRIPT_LEDGER_N];
    uint8_t count, head;
    uint16_t combo, bestCombo, uniqueWords;
    bool lastRepeat;
} ScriptLedger;

void scriptLedgerReset(ScriptLedger *ledger);
void scriptLedgerPeel(ScriptLedger *ledger);
bool scriptLedgerHas(const ScriptLedger *ledger, const uint8_t *word, int length);
// Score preview without mutating state. rawPoints must already represent a
// dictionary-validated geometric chain, as returned by scriptBoardPreview().
int scriptLedgerQuote(const ScriptLedger *ledger, const uint8_t *word,
                      int length, int rawPoints);
// Called exactly once AFTER scriptBoardSubmit() returns positive points.
int scriptLedgerCommit(ScriptLedger *ledger, const uint8_t *word,
                       int length, int rawPoints);
#endif
