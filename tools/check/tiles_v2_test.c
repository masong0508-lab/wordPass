// Native WPL2 test: variable alphabet, matching Unicode dictionary, no heap
// in game runtime; deterministic bags and corruption fail closed.
#include "dict_v4.h"
#include "tiles_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(ok, ...) do { if (!(ok)) { failures++; printf("FAIL "); printf(__VA_ARGS__); putchar('\n'); } } while (0)
static uint8_t *fileRead(const char *name, size_t *size)
{
    FILE *f = fopen(name, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return b;
}
static void exercise(const char *dictFile, const char *tilesFile, bool uk)
{
    size_t ds = 0, ts = 0;
    uint8_t *db = fileRead(dictFile, &ds), *tb = fileRead(tilesFile, &ts);
    CHECK(db && tb, "open dictionary and tiles for %s", uk ? "uk" : "ru");
    if (!db || !tb) { free(db); free(tb); return; }
    V4Dict dict;
    V4Tiles t;
    CHECK(v4DictOpen(&dict, db, ds), "open paired WPD4 dictionary");
    CHECK(v4TilesOpen(&t, tb, ts, &dict), "open matching WPL2 tile table");
    CHECK(t.valid && t.symbols == 33 && t.rareCount <= 2, "33-letter valid alphabet and rare cap");
    const int vowel = v4DictSymbol(&dict, uk ? 0x0407 : 0x0410); // Ї or А
    CHECK(vowel >= 0 && v4TilesVowel(&t, vowel), "script-specific vowel is marked");
    CHECK(!v4TilesVowel(&t, -1) && !v4TilesVowel(&t, 64), "out-of-range vowels refused");
    uint8_t a[WPL2_BAG_SIZE], b[WPL2_BAG_SIZE], c[WPL2_BAG_SIZE];
    CHECK(v4TilesFill(&t, a, 12345) && v4TilesFill(&t, b, 12345) &&
          !memcmp(a, b, sizeof a), "100-tile bag is deterministic");
    CHECK(v4TilesFill(&t, c, 12346) && memcmp(a, c, sizeof a) != 0,
          "different seeds shuffle the same letters differently");
    unsigned counts[WPD4_MAX_SYMBOLS] = {0}, vowelCount = 0, rareCount = 0;
    for (unsigned i = 0; i < WPL2_BAG_SIZE; i++) {
        CHECK(a[i] < t.symbols, "bag symbol index inside alphabet");
        if (a[i] >= t.symbols) continue;
        counts[a[i]]++;
        vowelCount += v4TilesVowel(&t, a[i]);
        for (unsigned j = 0; j < t.rareCount; j++) rareCount += a[i] == t.rare[j];
    }
    CHECK(vowelCount >= 10 && rareCount <= 2, "vowels preserved and rares at most two");
    for (unsigned i = 0; i < t.symbols; i++) {
        CHECK(counts[i] == t.weight[i], "bag frequency for symbol %u", i);
        CHECK((t.weight[i] != 0) == (t.value[i] != 0), "tile values only for present letters");
    }
    CHECK(!v4TilesOpen(&t, tb, ts, NULL), "missing dictionary rejected");
    CHECK(!v4TilesFill(&t, a, 7), "invalidated tile table cannot deal a bag");
    CHECK(!v4TilesOpen(NULL, tb, ts, &dict), "NULL output rejected");
    for (size_t n = 0; n < ts; n++)
        CHECK(!v4TilesOpen(&t, tb, n, &dict) && !t.valid,
              "truncated WPL2 (%zu)", n);
    uint8_t *bad = malloc(ts);
    CHECK(bad != NULL, "corruption buffer");
    if (bad) {
        memcpy(bad, tb, ts); bad[ts-1] ^= 1;
        CHECK(!v4TilesOpen(&t, bad, ts, &dict), "corrupt value checksum refused");
        memcpy(bad, tb, ts); bad[7] = 5;
        CHECK(!v4TilesOpen(&t, bad, ts, &dict), "invalid rare count refused");
        memcpy(bad, tb, ts); bad[8] = 0;
        CHECK(!v4TilesOpen(&t, bad, ts, &dict), "wrong bag size refused");
        free(bad);
    }
    free(db); free(tb);
}
int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: %s ru.wpd4 uk.wpd4 ru.wpl2 uk.wpl2\n", argv[0]); return 2; }
    exercise(argv[1], argv[3], false);
    exercise(argv[2], argv[4], true);
    // Identical size is not interchangeable: a Russian table must not open
    // against the Ukrainian dictionary with a different alphabet sequence.
    size_t ds=0, ts=0;
    uint8_t *db=fileRead(argv[2],&ds), *tb=fileRead(argv[3],&ts);
    if (db && tb) {
        V4Dict d; V4Tiles t;
        CHECK(v4DictOpen(&d, db, ds) && !v4TilesOpen(&t, tb, ts, &d),
              "cross-language alphabet mismatch must be refused");
    } else CHECK(false, "cross-language fixture files");
    free(db); free(tb);
    printf(failures ? "WPL2 NATIVE FAILED: %d failures\n" : "WPL2 NATIVE OK: 33-symbol bags, vowels, rarity, corruption, compatibility\n", failures);
    return failures ? 1 : 0;
}
