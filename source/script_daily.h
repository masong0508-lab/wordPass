// Stable date/language seed for the Russian/Ukrainian engineering ROM.
// This is UNRANKED DAILY PRACTICE; it never alters retail Daily records.
#ifndef WORDPASS_SCRIPT_DAILY_H
#define WORDPASS_SCRIPT_DAILY_H
#include <stdint.h>
#include <stdbool.h>
bool scriptDailyValidDate(int ymd);
uint32_t scriptDailySeed(int ymd, int language);
#endif
