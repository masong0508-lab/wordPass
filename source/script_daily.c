#include "script_daily.h"

// Explicitly validate actual calendar dates; do not seed a bogus DS RTC
// (such as 2000-01-01) or collapse Feb 29 / Feb 30 into the same puzzle.
bool scriptDailyValidDate(int ymd)
{
    int year = ymd / 10000, month = (ymd / 100) % 100, day = ymd % 100;
    if (year < 2025 || year > 2099 || month < 1 || month > 12 || day < 1)
        return false;
    static const uint8_t days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    int last = days[month];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
        last = 29;
    return day <= last;
}

uint32_t scriptDailySeed(int ymd, int language)
{
    if (!scriptDailyValidDate(ymd) || (language != 0 && language != 1))
        return 0;
    // Mix a calendar key with a per-language salt. Stable across platforms
    // and launches; never uses locale-sensitive text or the random seedCounter.
    uint32_t h = (uint32_t)ymd ^ (language ? 0x554b4459u : 0x52554459u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h ? h : 1u;
}
