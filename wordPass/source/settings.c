// Player settings, records and lifetime stats, saved to the card's FAT filesystem when
// one is available (flashcarts via DLDI, DSi SD). Without one they live for the session.
//
// The file is the Settings struct as a WPS2 image: magic, version, a CRC-16 over the rest,
// then the fields. Everything that decides what a file means (decode, encode, clamping, the
// CRC, the WPS1 migration, the streak arithmetic) is a pure function over a buffer, so
// tools/check/settings_test.c exercises it natively with crafted buffers; settingsInit and
// settingsSave are the thin FAT wrappers around them.
#include "game.h"
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

Settings S;
static bool fatReady;
static bool lastSaveFailed;
static int clockOverride;           // settingsDebugClock: 0 = the real clock
#ifndef CFG_PATH
#define CFG_PATH "/wordPass.cfg"
#endif
#define CFG_TMP CFG_PATH ".tmp"
#define V1_BYTES_A 28               // every WPS1 file ever written: magic, up to eight option bytes, four records
#define V1_BYTES_B 32               // the same layout with a trailing word of padding
#define DATE_MIN 20250101
#define DATE_MAX 20991231

void settingsDefaults(void)
{
    memset(&S, 0, sizeof(S));
    S.magic = SETTINGS_MAGIC;
    S.version = SETTINGS_VERSION;
    S.theme = THEME_JEWEL;
    S.frame = FRAME_GOLD;
    S.accent = 0;           // auto per shelf
    S.hand = HAND_RIGHT;
    S.fire = FIRE_NORMAL;
    S.music = 1;
    S.library = LIB_FULL;
    S.spelling = SPELL_BOTH;
}

// ---- validation

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampDate(int v) { return (v == 0 || settingsClockOk(v)) ? v : 0; }

// A stored word: letters only, NUL-terminated inside the buffer (anything else ends it).
static void cleanWord(char *w)
{
    int i = 0;
    while (i < SETTINGS_WORD_BUF - 1 && ((w[i] >= 'a' && w[i] <= 'z') || (w[i] >= 'A' && w[i] <= 'Z'))) i++;
    memset(w + i, 0, SETTINGS_WORD_BUF - i);
}

// Clamp every field to its legal range: a file that passed the CRC may still come from a
// build with other option counts, and a migrated one carries whatever was in its padding.
static void settingsValid(void)
{
    if (S.theme >= THEME_COUNT) S.theme = THEME_JEWEL;
    if (S.frame >= FRAME_COUNT) S.frame = FRAME_GOLD;
    if (S.accent >= ACCENT_COUNT) S.accent = 0;
    if (S.hand > 1) S.hand = HAND_RIGHT;
    if (S.fire >= FIRE_COUNT) S.fire = FIRE_NORMAL;
    if (S.music > 1) S.music = 1;
    if (S.library >= LIB_COUNT) S.library = LIB_FULL;
    if (S.spelling >= SPELL_COUNT) S.spelling = SPELL_BOTH;
    if (S.tutorDone > 1) S.tutorDone = 1;
    memset(S.reserved, 0, sizeof(S.reserved));
    S.bestClassic = clampInt(S.bestClassic, 0, 999999999);
    S.bestClassicShelf = clampInt(S.bestClassicShelf, 0, 9999);
    S.bestDaily = clampInt(S.bestDaily, 0, 999999999);
    S.bestDailyDate = clampDate(S.bestDailyDate);
    S.dailyPlayedDate = clampDate(S.dailyPlayedDate);
    S.dailyLastDate = clampDate(S.dailyLastDate);
    S.dailyLastScore = clampInt(S.dailyLastScore, 0, 999999999);
    S.dailyStreak = clampInt(S.dailyStreak, 0, 9999);
    if (!S.dailyLastDate) S.dailyStreak = 0;
    S.gamesPlayed = clampInt(S.gamesPlayed, 0, 999999999);
    S.shelvesCleared = clampInt(S.shelvesCleared, 0, 999999999);
    S.wordsSpelled = clampInt(S.wordsSpelled, 0, 999999999);
    S.longestLen = clampInt(S.longestLen, 0, MAX_WORD);
    S.bestWordPts = clampInt(S.bestWordPts, 0, 999999999);
    S.perfectPasses = clampInt(S.perfectPasses, 0, 999999999);
    S.rareWords = clampInt(S.rareWords, 0, 999999999);
    S.peels = clampInt(S.peels, 0, 999999999);
    S.peelsHit = clampInt(S.peelsHit, 0, S.peels);
    cleanWord(S.longestWord);
    cleanWord(S.bestWordTxt);
    if (!S.longestWord[0]) S.longestLen = 0;
    if (!S.bestWordTxt[0]) S.bestWordPts = 0;
}

// ---- the file image

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
static u16 crc16(const u8 *p, size_t n)
{
    u16 crc = 0xFFFF;
    while (n--) {
        crc ^= (u16)(*p++ << 8);
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
    }
    return crc;
}

static u32 rd32(const u8 *b) { return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24); }

// A saved image into S. WPS2 must be exactly sizeof(Settings) with a matching CRC. A WPS1
// file (28 bytes, or 32 with a trailing pad word) keeps its option bytes at 4..11 (older
// builds left the later ones as zero padding, which reads as the defaults) and its four
// records at 12..27; every newer field starts at zero. Anything else falls back to the
// defaults. Returns whether the buffer was accepted.
bool settingsDecode(const void *buf, size_t n)
{
    const u8 *b = (const u8 *)buf;
    u32 magic = n >= 4 ? rd32(b) : 0;
    if (magic == SETTINGS_V1_MAGIC && (n == V1_BYTES_A || n == V1_BYTES_B)) {
        settingsDefaults();
        S.theme = b[4]; S.frame = b[5]; S.accent = b[6]; S.hand = b[7];
        S.fire = b[8]; S.music = b[9]; S.library = b[10]; S.spelling = b[11];
        S.bestClassic = (int)rd32(b + 12); S.bestClassicShelf = (int)rd32(b + 16);
        S.bestDaily = (int)rd32(b + 20); S.bestDailyDate = (int)rd32(b + 24);
        settingsValid();
        return true;
    }
    if (n == sizeof(Settings)) {
        Settings t;
        memcpy(&t, b, sizeof(t));
        if (t.magic == SETTINGS_MAGIC && t.version == SETTINGS_VERSION &&
            t.crc == crc16((const u8 *)&t + 8, sizeof(t) - 8)) {
            S = t;
            settingsValid();
            return true;
        }
    }
    settingsDefaults();
    return false;
}

size_t settingsEncode(void *buf)
{
    settingsValid();
    S.magic = SETTINGS_MAGIC;
    S.version = SETTINGS_VERSION;
    S.crc = crc16((const u8 *)&S + 8, sizeof(S) - 8);
    memcpy(buf, &S, sizeof(S));
    return sizeof(S);
}

// ---- the card

static size_t readFile(const char *path, void *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

void settingsInit(void)
{
    settingsDefaults();
    fatReady = fatInitDefault();
    if (!fatReady) return;
    u8 buf[256];                        // room to see a file that is too long, which is rejected
    size_t n = readFile(CFG_PATH, buf, sizeof(buf));
    if (n && settingsDecode(buf, n)) return;
    // an interrupted save may have left the finished image next to the (missing) old file
    n = readFile(CFG_TMP, buf, sizeof(buf));
    if (n) settingsDecode(buf, n);
    else settingsDefaults();
}

// Write the image to a temporary file and rename it over the real one, so a card pulled
// mid-write leaves the previous file intact. libfat's rename may refuse an existing target,
// in which case the old file is removed first (the temp file still holds the new image).
bool settingsSave(void)
{
    if (!fatReady) return false;
    u8 buf[sizeof(Settings)];
    size_t n = settingsEncode(buf);
    bool ok = false;
    FILE *f = fopen(CFG_TMP, "wb");
    if (f) {
        ok = fwrite(buf, 1, n, f) == n;
        if (fclose(f) != 0) ok = false;
    }
    if (ok && rename(CFG_TMP, CFG_PATH) != 0) {
        remove(CFG_PATH);
        ok = rename(CFG_TMP, CFG_PATH) == 0;
    }
    lastSaveFailed = !ok;
    return ok;
}

bool settingsPersistent(void) { return fatReady; }
bool settingsSaveFailed(void) { return lastSaveFailed; }

// ---- the clock and Daily arithmetic

void settingsDebugClock(int ymd) { clockOverride = ymd; }

int settingsToday(void)
{
    if (clockOverride) return clockOverride;
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    if (!tm) return 0;
    return (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
}

// An unset DS clock reads 2000-01-01; anything before 2025 is treated as unset.
bool settingsClockOk(int ymd)
{
    if (ymd < DATE_MIN || ymd > DATE_MAX) return false;
    int m = ymd / 100 % 100, d = ymd % 100;
    return m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// Howard Hinnant's days_from_civil: days since 1970-01-01
int settingsDayNumber(int ymd)
{
    int y = ymd / 10000, m = ymd / 100 % 100, d = ymd % 100;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

// Same day keeps the streak, the next day extends it, a gap (or a last date in the future,
// or no last date) starts over at 1.
int settingsNextStreak(int lastDate, int streak, int today)
{
    if (!settingsClockOk(lastDate)) return 1;
    int gap = settingsDayNumber(today) - settingsDayNumber(lastDate);
    if (gap == 0) return clampInt(streak, 1, 9999);
    if (gap == 1) return clampInt(streak + 1, 1, 9999);
    return 1;
}

int settingsLiveStreak(int today)
{
    if (!settingsClockOk(S.dailyLastDate) || !settingsClockOk(today)) return 0;
    int gap = settingsDayNumber(today) - settingsDayNumber(S.dailyLastDate);
    return (gap == 0 || gap == 1) ? S.dailyStreak : 0;
}
