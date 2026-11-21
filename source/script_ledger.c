#include "script_ledger.h"
#include <string.h>

static bool valid(const ScriptLedger *ledger, const uint8_t *word, int length)
{
    return ledger && word && length >= WPD4_MIN_WORD &&
           length <= WPD4_MAX_WORD;
}

void scriptLedgerReset(ScriptLedger *ledger)
{
    if (ledger) memset(ledger, 0, sizeof(*ledger));
}

void scriptLedgerPeel(ScriptLedger *ledger)
{
    if (ledger) ledger->combo = 0;
}

bool scriptLedgerHas(const ScriptLedger *ledger, const uint8_t *word, int length)
{
    if (!valid(ledger, word, length)) return false;
    for (unsigned i = 0; i < ledger->count; i++) {
        if (ledger->length[i] == length &&
            memcmp(ledger->word[i], word, (size_t)length) == 0)
            return true;
    }
    return false;
}

int scriptLedgerQuote(const ScriptLedger *ledger, const uint8_t *word,
                      int length, int rawPoints)
{
    if (!valid(ledger, word, length) || rawPoints <= 0) return 0;
    int gained = scriptLedgerHas(ledger, word, length) ?
                 rawPoints / 2 : rawPoints;
    int comboPct = ledger->combo >= 5 ? 50 : (int)ledger->combo * 10;
    return gained + gained * comboPct / 100;
}

int scriptLedgerCommit(ScriptLedger *ledger, const uint8_t *word,
                       int length, int rawPoints)
{
    int gained = scriptLedgerQuote(ledger, word, length, rawPoints);
    if (gained <= 0) return 0; // a rejected word changes no ledger state
    bool repeat = scriptLedgerHas(ledger, word, length);
    unsigned at = ledger->head;
    memcpy(ledger->word[at], word, (size_t)length);
    ledger->length[at] = (uint8_t)length;
    // The ledger is informational; scores may exceed UINT16_MAX on very long
    // words, but clipping its display history must not alter total scoring.
    ledger->points[at] = (uint16_t)(gained > 65535 ? 65535 : gained);
    ledger->head = (uint8_t)((at + 1) % SCRIPT_LEDGER_N);
    if (ledger->count < SCRIPT_LEDGER_N) ledger->count++;
    if (!repeat && ledger->uniqueWords < 65535) ledger->uniqueWords++;
    if (ledger->combo < 65535) ledger->combo++;
    if (ledger->bestCombo < ledger->combo) ledger->bestCombo = ledger->combo;
    ledger->lastRepeat = repeat;
    return gained;
}
