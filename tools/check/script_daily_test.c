// Deterministic Russian/Ukrainian practice Daily seed tests, no DS SDK.
#include "script_daily.h"
#include <stdio.h>
static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { failures++; printf("FAIL: %s\n", msg); } } while (0)
int main(void)
{
    CHECK(scriptDailyValidDate(20261121), "valid November calendar date");
    CHECK(scriptDailyValidDate(20280229), "leap-year February 29");
    CHECK(!scriptDailyValidDate(20270229), "reject non-leap February 29");
    CHECK(!scriptDailyValidDate(20260230), "reject February 30");
    CHECK(!scriptDailyValidDate(20261301) && !scriptDailyValidDate(20260012),
          "reject out-of-range month");
    CHECK(!scriptDailyValidDate(20261131) && !scriptDailyValidDate(20261100),
          "reject out-of-range November day");
    CHECK(!scriptDailyValidDate(20000101) && !scriptDailyValidDate(21000101),
          "reject unset RTC and out-of-range future clock");
    CHECK(scriptDailySeed(20261121, 0) &&
          scriptDailySeed(20261121, 0) == scriptDailySeed(20261121, 0),
          "Russian Daily seed stable between sessions");
    CHECK(scriptDailySeed(20261121, 1) &&
          scriptDailySeed(20261121, 1) != scriptDailySeed(20261121, 0),
          "Ukrainian and Russian get distinct daily boards");
    CHECK(scriptDailySeed(20261122, 0) != scriptDailySeed(20261121, 0) &&
          scriptDailySeed(20261122, 1) != scriptDailySeed(20261121, 1),
          "a new date generates distinct boards in each locale");
    CHECK(!scriptDailySeed(20000101, 0) && !scriptDailySeed(20270229, 1) &&
          !scriptDailySeed(20261121, 2),
          "invalid RTC and script selections cannot seed a score");
    printf(failures ? "SCRIPT DAILY FAILED: %d failures\n" :
                      "SCRIPT DAILY OK: stable bilingual dates, leap rules and clock guard\n",
           failures);
    return failures ? 1 : 0;
}
