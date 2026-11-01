// Native tests for source/settings.c: the pure encode/decode (WPS2 with its CRC, the WPS1
// migration, clamping), the streak arithmetic, and the save path against a scratch file.
// Built by `make check` with a stub <fat.h>; CFG_PATH points at build/settings_test.cfg.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool stubFatOk = true;

static int fails;
#define CHECK(name, cond) do { if (cond) printf("PASS %s\n", name); else { printf("FAIL %s (%s) at %s:%d\n", name, #cond, __FILE__, __LINE__); fails++; } } while (0)

static void put32(unsigned char *b, unsigned v) { b[0] = v & 255; b[1] = (v >> 8) & 255; b[2] = (v >> 16) & 255; b[3] = (v >> 24) & 255; }

// a WPS1 file as the old builds wrote it: magic, eight option bytes (the later ones zero in
// the oldest files), then best classic score and shelf, best daily score and date
static size_t legacyFile(unsigned char *b, size_t n, int withLibrary)
{
    memset(b, 0, n);
    put32(b, SETTINGS_V1_MAGIC);
    b[4] = THEME_CANDY; b[5] = FRAME_SILVER; b[6] = 5; b[7] = HAND_LEFT; b[8] = FIRE_FAST; b[9] = 0;
    if (withLibrary) { b[10] = LIB_COMMON; b[11] = SPELL_UK; }
    put32(b + 12, 41250); put32(b + 16, 7); put32(b + 20, 9100); put32(b + 24, 20260214);
    return n;
}

#define CRC_BAD_BYTE(buf) ((buf)[sizeof(Settings) - 5] ^= 0x40)

int main(void)
{
    unsigned char buf[sizeof(Settings)], big[256];

    CHECK("Settings is a whole number of words and the CRC starts at byte 8", sizeof(Settings) % 4 == 0 && offsetof(Settings, theme) == 8);

    // ---- defaults round trip
    settingsDefaults();
    size_t n = settingsEncode(buf);
    CHECK("encode writes sizeof(Settings) bytes starting WPS2", n == sizeof(Settings) && buf[0] == '2' && buf[1] == 'S' && buf[2] == 'P' && buf[3] == 'W');
    settingsDefaults(); S.theme = THEME_EMBER;
    CHECK("decode accepts an encoded image and replaces S", settingsDecode(buf, n) && S.theme == THEME_JEWEL && S.music == 1);

    // ---- every field survives encode -> decode
    settingsDefaults();
    S.theme = THEME_INK; S.frame = FRAME_ACCENT; S.accent = 4; S.hand = HAND_LEFT; S.fire = FIRE_FAST; S.music = 0;
    S.library = LIB_COMMON; S.spelling = SPELL_US; S.tutorDone = 1;
    S.bestClassic = 123456; S.bestClassicShelf = 11; S.bestDaily = 7777; S.bestDailyDate = 20260301;
    S.dailyPlayedDate = 20260302; S.dailyLastScore = 5150; S.dailyStreak = 9; S.dailyLastDate = 20260302;
    S.gamesPlayed = 31; S.shelvesCleared = 77; S.wordsSpelled = 1234; S.longestLen = 13; S.bestWordPts = 2840;
    S.perfectPasses = 5; S.rareWords = 66; S.peels = 300; S.peelsHit = 120;
    strcpy(S.longestWord, "quartermaster"); strcpy(S.bestWordTxt, "jazziness");
    Settings want = S;
    n = settingsEncode(buf);
    settingsDefaults();
    bool ok = settingsDecode(buf, n);
    want.magic = S.magic; want.version = S.version; want.crc = S.crc;     // the header is filled by encode
    CHECK("encode -> decode round-trips every field", ok && memcmp(&S, &want, sizeof(S)) == 0);
    unsigned char again[sizeof(Settings)];
    CHECK("encode is deterministic", settingsEncode(again) == n && memcmp(again, buf, n) == 0);

    // ---- corruption
    unsigned char bad[sizeof(Settings)];
    memcpy(bad, buf, n); CRC_BAD_BYTE(bad);
    settingsDefaults(); S.bestClassic = 5;
    CHECK("a WPS2 image with a bad CRC falls back to the defaults", !settingsDecode(bad, n) && S.bestClassic == 0 && S.music == 1 && S.theme == THEME_JEWEL);
    memcpy(bad, buf, n); bad[4] = 3;      // version 3
    CHECK("an unknown version is refused", !settingsDecode(bad, n));
    memcpy(bad, buf, n); bad[8] = 200;    // theme byte changed without fixing the CRC
    CHECK("a flipped option byte is caught by the CRC", !settingsDecode(bad, n));
    CHECK("a truncated image is refused", !settingsDecode(buf, n - 4) && S.bestClassic == 0);
    CHECK("an over-long image is refused", !settingsDecode(buf, n + 4));
    CHECK("an empty buffer is refused", !settingsDecode(buf, 0) && S.music == 1);
    memset(big, 0, sizeof(big));
    CHECK("a zeroed buffer of the right size is refused", !settingsDecode(big, sizeof(Settings)));

    // ---- clamping (a file whose CRC is right but whose values are out of range)
    settingsDefaults();
    S.theme = 200; S.frame = 9; S.accent = 77; S.hand = 5; S.fire = 8; S.music = 3; S.library = 4; S.spelling = 12; S.tutorDone = 8;
    S.bestClassic = -5; S.bestDaily = -1; S.bestDailyDate = 19990101; S.dailyPlayedDate = 21000101; S.dailyLastDate = 20261340;
    S.dailyStreak = 123456; S.wordsSpelled = -3; S.longestLen = 99; S.peels = 4; S.peelsHit = 40;
    memset(S.longestWord, 'q', sizeof(S.longestWord)); memcpy(S.bestWordTxt, "ab\x01zz", 6);
    S.bestWordPts = 10;
    n = settingsEncode(buf);                 // encode validates before it computes the CRC
    settingsDefaults();
    ok = settingsDecode(buf, n);
    CHECK("theme=200 and the other options clamp to their defaults",
          ok && S.theme == THEME_JEWEL && S.frame == FRAME_GOLD && S.accent == 0 && S.hand == HAND_RIGHT && S.fire == FIRE_NORMAL &&
          S.music == 1 && S.library == LIB_FULL && S.spelling == SPELL_BOTH && S.tutorDone == 1);
    CHECK("scores clamp to >= 0, bad dates to 0, streak to 0..9999, hits to peels",
          S.bestClassic == 0 && S.bestDaily == 0 && S.bestDailyDate == 0 && S.dailyPlayedDate == 0 && S.dailyLastDate == 0 &&
          S.dailyStreak == 0 && S.wordsSpelled == 0 && S.peelsHit == S.peels);
    CHECK("stored words are NUL-terminated letters only and length is bounded",
          S.longestWord[SETTINGS_WORD_BUF - 1] == 0 && strlen(S.longestWord) <= SETTINGS_WORD_BUF - 1 && S.longestLen <= MAX_WORD &&
          !strcmp(S.bestWordTxt, "ab"));
    settingsDefaults();
    S.dailyLastDate = 20260301; S.dailyStreak = 123456; S.longestLen = 5; strcpy(S.longestWord, "hello");
    n = settingsEncode(buf); settingsDefaults(); settingsDecode(buf, n);
    CHECK("a streak over 9999 clamps to 9999", S.dailyStreak == 9999 && S.longestLen == 5);

    // ---- WPS1 migration: options at 4..11, records at 12..27, everything newer starts at zero
    unsigned char old[40];
    size_t on = legacyFile(old, 28, 1);
    settingsDefaults(); S.wordsSpelled = 9;
    ok = settingsDecode(old, on);
    CHECK("a 28-byte WPS1 file migrates its options", ok && S.theme == THEME_CANDY && S.frame == FRAME_SILVER && S.accent == 5 &&
          S.hand == HAND_LEFT && S.fire == FIRE_FAST && S.music == 0 && S.library == LIB_COMMON && S.spelling == SPELL_UK);
    CHECK("... and its four records", S.bestClassic == 41250 && S.bestClassicShelf == 7 && S.bestDaily == 9100 && S.bestDailyDate == 20260214);
    CHECK("... with every new field zeroed and the header upgraded",
          S.wordsSpelled == 0 && S.gamesPlayed == 0 && S.dailyStreak == 0 && S.dailyPlayedDate == 0 && !S.longestWord[0] &&
          S.magic == SETTINGS_MAGIC && S.version == SETTINGS_VERSION);
    n = settingsEncode(buf); settingsDefaults();
    CHECK("a migrated file re-encodes as a valid WPS2", settingsDecode(buf, n) && S.bestClassic == 41250 && S.theme == THEME_CANDY);
    on = legacyFile(old, 28, 0);
    settingsDecode(old, on);
    CHECK("an oldest WPS1 file (padding zero) reads library FULL and spelling BOTH", S.library == LIB_FULL && S.spelling == SPELL_BOTH && S.bestClassic == 41250);
    on = legacyFile(old, 32, 1);
    CHECK("the 32-byte WPS1 form migrates the same way", settingsDecode(old, on) && S.spelling == SPELL_UK && S.bestDailyDate == 20260214);
    on = legacyFile(old, 30, 1);
    settingsDefaults();
    CHECK("a WPS1 file of any other length is refused", !settingsDecode(old, on) && S.bestClassic == 0);
    on = legacyFile(old, 28, 1); old[4] = 99; old[8] = 77; old[11] = 9; put32(old + 20, (unsigned)-40); put32(old + 24, 12345);
    settingsDecode(old, on);
    CHECK("a migrated file is clamped too", S.theme == THEME_JEWEL && S.fire == FIRE_NORMAL && S.spelling == SPELL_BOTH && S.bestDaily == 0 && S.bestDailyDate == 0);
    put32(old, 0x57505333u);
    CHECK("an unknown magic is refused", !settingsDecode(old, 28));

    // ---- the clock and the streak
    CHECK("the clock check accepts 2025-01-01 .. 2099-12-31 only",
          settingsClockOk(20250101) && settingsClockOk(20261101) && settingsClockOk(20991231) &&
          !settingsClockOk(20241231) && !settingsClockOk(20000101) && !settingsClockOk(21000101) && !settingsClockOk(0) &&
          !settingsClockOk(20261301) && !settingsClockOk(20260100) && !settingsClockOk(20260132));
    CHECK("day numbers: the epoch, a leap day and the year boundary",
          settingsDayNumber(19700101) == 0 && settingsDayNumber(20240301) - settingsDayNumber(20240228) == 2 &&
          settingsDayNumber(20260101) - settingsDayNumber(20251231) == 1 && settingsDayNumber(20000301) - settingsDayNumber(20000228) == 2 &&
          settingsDayNumber(21000301) - settingsDayNumber(21000228) == 1 && settingsDayNumber(20260301) - settingsDayNumber(20260201) == 28);
    CHECK("streak: the first scored Daily is 1", settingsNextStreak(0, 0, 20260301) == 1);
    CHECK("streak: the next day extends it, across a month and a year end",
          settingsNextStreak(20260228, 4, 20260301) == 5 && settingsNextStreak(20251231, 1, 20260101) == 2 && settingsNextStreak(20260430, 9, 20260501) == 10);
    CHECK("streak: the same day keeps it", settingsNextStreak(20260301, 6, 20260301) == 6);
    CHECK("streak: a missed day starts over", settingsNextStreak(20260301, 6, 20260303) == 1 && settingsNextStreak(20260301, 6, 20260401) == 1);
    CHECK("streak: a last date in the future counts as broken", settingsNextStreak(20260310, 6, 20260301) == 1);
    CHECK("streak is capped at 9999", settingsNextStreak(20260301, 9999, 20260302) == 9999);
    settingsDefaults(); S.dailyLastDate = 20260301; S.dailyStreak = 4;
    CHECK("the live streak survives today and tomorrow, then dies",
          settingsLiveStreak(20260301) == 4 && settingsLiveStreak(20260302) == 4 && settingsLiveStreak(20260303) == 0 && settingsLiveStreak(20260228) == 0);
    settingsDebugClock(20270315);
    CHECK("the debug clock overrides settingsToday and 0 restores the real one", settingsToday() == 20270315);
    settingsDebugClock(0);
    CHECK("the real clock reads a plausible date here", settingsClockOk(settingsToday()));

    // ---- the card: save, reload, no leftover temp file, fallbacks
    remove(CFG_PATH); remove(CFG_PATH ".tmp");
    stubFatOk = true;
    settingsInit();
    CHECK("no file: defaults, and the card counts as present", settingsPersistent() && S.music == 1 && S.bestClassic == 0 && !settingsSaveFailed());
    S.bestClassic = 4242; S.theme = THEME_EMBER; S.wordsSpelled = 12; strcpy(S.longestWord, "wordpass"); S.longestLen = 8;
    CHECK("settingsSave succeeds", settingsSave() && !settingsSaveFailed());
    FILE *f = fopen(CFG_PATH, "rb");
    long len = -1;
    if (f) { fseek(f, 0, SEEK_END); len = ftell(f); fclose(f); }
    CHECK("the file is exactly sizeof(Settings) bytes", len == (long)sizeof(Settings));
    f = fopen(CFG_PATH ".tmp", "rb");
    CHECK("no temp file is left behind", f == NULL);
    if (f) fclose(f);
    settingsInit();
    CHECK("settingsInit reads the saved file back", S.bestClassic == 4242 && S.theme == THEME_EMBER && S.wordsSpelled == 12 && !strcmp(S.longestWord, "wordpass"));
    S.bestClassic = 5000;
    CHECK("a second save replaces the first", settingsSave());
    settingsInit();
    CHECK("... and reads back the newer one", S.bestClassic == 5000);

    // a card pulled mid-save: the old file is gone but the finished temp image is there
    n = settingsEncode(buf);
    f = fopen(CFG_PATH ".tmp", "wb"); fwrite(buf, 1, n, f); fclose(f);
    remove(CFG_PATH);
    S.bestClassic = 1;
    settingsInit();
    CHECK("a missing file with a valid temp image loads the temp image", S.bestClassic == 5000);
    remove(CFG_PATH ".tmp");

    // a corrupt file: defaults, not garbage
    f = fopen(CFG_PATH, "wb"); memcpy(bad, buf, n); CRC_BAD_BYTE(bad); fwrite(bad, 1, n, f); fclose(f);
    settingsInit();
    CHECK("a corrupt saved file loads the defaults", S.bestClassic == 0 && S.music == 1 && S.theme == THEME_JEWEL);
    // a legacy file on the card is migrated and the next save upgrades it
    f = fopen(CFG_PATH, "wb"); on = legacyFile(old, 28, 1); fwrite(old, 1, on, f); fclose(f);
    settingsInit();
    CHECK("a WPS1 file on the card migrates", S.bestClassic == 41250 && S.theme == THEME_CANDY);
    CHECK("... and saving writes the WPS2 size", settingsSave());
    f = fopen(CFG_PATH, "rb"); len = -1; if (f) { fseek(f, 0, SEEK_END); len = ftell(f); fclose(f); }
    CHECK("... the file is now sizeof(Settings)", len == (long)sizeof(Settings));

    // a failing card: SAVE FAILED shows, and goes away after a good save
    char tmpPath[] = CFG_PATH ".tmp";
    CHECK("scratch setup: a directory squats on the temp name", system("mkdir -p " CFG_PATH ".tmp") == 0);
    CHECK("a save that cannot open its temp file returns false", !settingsSave() && settingsSaveFailed());
    CHECK("... the old file is untouched", (f = fopen(CFG_PATH, "rb")) != NULL);
    if (f) fclose(f);
    CHECK("scratch teardown", rmdir(tmpPath) == 0);
    CHECK("the next good save clears SAVE FAILED", settingsSave() && !settingsSaveFailed());

    // no card at all
    stubFatOk = false;
    settingsInit();
    CHECK("no card: defaults, not persistent, a save is false and is not a failure", !settingsPersistent() && S.bestClassic == 0 && !settingsSave() && !settingsSaveFailed());
    remove(CFG_PATH);

    printf("%s: %d failure(s)\n", fails ? "SETTINGS FAILED" : "SETTINGS OK", fails);
    return fails;
}
