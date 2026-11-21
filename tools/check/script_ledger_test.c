// Native, allocation-free tests for the experimental Russian/Ukrainian
// eight-word score ledger. No DS SDK, filesystem or production save involved.
#include "script_ledger.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(ok, msg) do { if (!(ok)) { fails++; printf("FAIL: %s\n", msg); } } while (0)

int main(void)
{
    ScriptLedger l;
    uint8_t a[4] = {1,2,3,4}, b[4] = {1,2,3,5};
    scriptLedgerReset(&l);
    CHECK(!scriptLedgerHas(&l, a, 4) && l.combo == 0 && l.count == 0,
          "new shelf has empty independent word ledger");
    CHECK(scriptLedgerQuote(&l, a, 4, 100) == 100,
          "new word previews its original points");
    CHECK(scriptLedgerCommit(&l, a, 4, 100) == 100,
          "first unique word gives base score");
    CHECK(scriptLedgerHas(&l, a, 4) && l.uniqueWords == 1 && l.combo == 1,
          "first word recorded in eight-entry history");
    CHECK(scriptLedgerQuote(&l, b, 4, 100) == 110,
          "second unique word previews ten percent combo");
    CHECK(scriptLedgerCommit(&l, b, 4, 100) == 110,
          "combo actually awarded at submission");
    CHECK(scriptLedgerQuote(&l, a, 4, 100) == 60,
          "repeat halves raw score then adds twenty percent combo");
    CHECK(scriptLedgerCommit(&l, a, 4, 100) == 60 &&
          l.lastRepeat && l.uniqueWords == 2 && l.combo == 3,
          "repeat tracked but not counted as a distinct word");
    scriptLedgerPeel(&l);
    CHECK(l.combo == 0 && scriptLedgerQuote(&l, a, 4, 100) == 50,
          "peeling breaks combo but preserves repeat history");
    CHECK(scriptLedgerCommit(&l, a, 4, 100) == 50 &&
          l.lastRepeat && l.bestCombo == 3,
          "repeat remains penalized after peeling");
    CHECK(!scriptLedgerQuote(&l, NULL, 4, 100) &&
          !scriptLedgerQuote(&l, a, 2, 100) &&
          !scriptLedgerQuote(&l, a, 4, 0),
          "invalid or unvalidated words do not earn points");
    unsigned oldCount = l.count;
    CHECK(scriptLedgerCommit(&l, a, 4, 0) == 0 && l.count == oldCount,
          "rejected word never enters history");

    scriptLedgerReset(&l);
    uint8_t words[10][3];
    for (int k = 0; k < 10; k++) {
        words[k][0] = 1; words[k][1] = 2; words[k][2] = (uint8_t)k;
        CHECK(scriptLedgerCommit(&l, words[k], 3, 100) > 0,
              "ten distinct generated words accept normally");
    }
    CHECK(l.count == SCRIPT_LEDGER_N && l.combo == 10 &&
          l.bestCombo == 10 && l.uniqueWords == 10,
          "ledger rolls over after eight, combo remains capped");
    CHECK(!scriptLedgerHas(&l, words[0], 3) &&
          !scriptLedgerHas(&l, words[1], 3) &&
          scriptLedgerHas(&l, words[2], 3) &&
          scriptLedgerHas(&l, words[9], 3),
          "oldest two words evict exactly on ring wrap");
    CHECK(scriptLedgerQuote(&l, words[0], 3, 100) == 150 &&
          scriptLedgerQuote(&l, words[9], 3, 100) == 75,
          "combo capped at +50% whether first-time or repeated");
    scriptLedgerReset(&l);
    CHECK(l.count == 0 && l.combo == 0 &&
          l.uniqueWords == 0 && !l.lastRepeat,
          "next shelf resets all history and combos");
    printf(fails ? "SCRIPT LEDGER FAILED: %d errors\n" :
                   "SCRIPT LEDGER OK: unique words, repeat halving, combo cap, peel reset and 8-entry rollover\n", fails);
    return fails ? 1 : 0;
}
