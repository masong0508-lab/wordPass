// Native WPP2 Cyrillic pass-pool regression. Tiny synthetic themes are built
// by tools/lang/pools_v2.py, not shipped as a selectable locale.
#include "dict_v4.h"
#include "pools_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL "); printf(__VA_ARGS__); putchar('\n'); } } while (0)
static uint8_t *readImage(const char *path, size_t *out)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *out = (size_t)n; return p;
}
static void fixChecksum(uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 20; i < n; i++) h = (h ^ p[i]) * 16777619u;
    for (int i = 0; i < 4; i++) p[16+i] = (uint8_t)(h >> (8 * i));
}
static void exercise(const char *dictPath, const char *poolPath, bool uk)
{
    size_t dbn = 0, pn = 0;
    uint8_t *db = readImage(dictPath, &dbn);
    uint8_t *pb = readImage(poolPath, &pn);
    CHECK(db && pb, "read dictionary and theme fixture");
    if (!db || !pb) { free(db); free(pb); return; }
    V4Dict dict;
    V4Pools pools;
    CHECK(v4DictOpen(&dict, db, dbn), "WPD4 fixture opens");
    CHECK(v4PoolsOpen(&pools, pb, pn, &dict), "WPP2 fixture opens with same alphabet");
    CHECK(pools.themes == 3 && pools.symbols == 33, "three valid 33-symbol themes");
    CHECK(sizeof pools <= 256, "WPP2 view fits the compact 256-byte index budget");
    const size_t header = 20 + 4u * pools.symbols;
    const size_t stride = WPP2_NAME_BYTES + 2u * WPP2_LENGTHS;
    size_t expected = header + (size_t)pools.themes * stride;
    int total = 0;
    for (int t = 0; t < pools.themes; t++) {
        const char *name = v4PoolsName(&pools, t);
        CHECK(name[0] && strlen(name) < WPP2_NAME_BYTES, "localized UTF-8 theme name");
        const uint8_t *record = pb + header + (size_t)t * stride;
        CHECK(!memcmp(name, record, strlen(name)+1), "exact theme name from validated image");
        int inTheme = 0;
        for (int len = WPP2_MIN_LEN; len <= WPP2_MAX_LEN; len++) {
            int n = v4PoolsCount(&pools, t, len);
            const uint8_t *count = record + WPP2_NAME_BYTES + 2u * (len - WPP2_MIN_LEN);
            CHECK(n == (int)(count[0] | (count[1] << 8)), "exact length count");
            for (int i = 0; i < n; i++) {
                uint8_t ids[WPP2_MAX_LEN] = {0};
                CHECK(v4PoolsWord(&pools, t, len, i, ids), "lookup pass by theme, length and index");
                CHECK(v4DictLookupIds(&dict, ids, len) != 0, "every pass is in the matching dictionary");
                CHECK(expected <= pn && (size_t)len <= pn - expected &&
                      !memcmp(ids, pb + expected, (size_t)len),
                      "exact word at theme/length/index, without cross-theme offset drift");
                expected += (size_t)len;
                inTheme++;
            }
        }
        CHECK(inTheme > 0, "every named theme contains a real word");
        total += inTheme;
    }
    CHECK(total == 8, "all eight synthetic pass words are reachable");
    CHECK(expected == pn, "all validated word bytes are covered");
    CHECK(!v4PoolsCount(&pools, -1, 4) && !v4PoolsCount(&pools, 0, 3) &&
          !v4PoolsCount(&pools, 50, 6), "bad indices are harmless");
    CHECK(!*v4PoolsName(&pools, 50), "out-of-bounds theme has empty name");
    uint8_t out[WPP2_MAX_LEN] = {0};
    CHECK(!v4PoolsWord(&pools, 0, 6, 9999, out), "invalid word index is rejected");
    CHECK(!v4PoolsWord(&pools, 0, 4, 0, NULL), "NULL pass destination rejected");
    CHECK(!v4PoolsOpen(NULL, pb, pn, &dict), "NULL pool view rejected");
    for (size_t cut = 0; cut < pn; cut++) {
        V4Pools bad;
        CHECK(!v4PoolsOpen(&bad, pb, cut, &dict) && !bad.image, "truncated pool image %zu", cut);
    }
    uint8_t *mut = malloc(pn);
    CHECK(mut != NULL, "mutation buffer");
    if (mut) {
        V4Pools bad;
        memcpy(mut, pb, pn);
        mut[pn - 1] ^= 1;
        CHECK(!v4PoolsOpen(&bad, mut, pn, &dict) && !bad.image,
              "payload checksum corruption fails");
        memcpy(mut, pb, pn);
        mut[6] = 0;
        CHECK(!v4PoolsOpen(&bad, mut, pn, &dict), "zero themes refused");
        memcpy(mut, pb, pn);
        // Rename the first theme to an invalid UTF-8 byte while keeping checksum valid.
        mut[20 + 33 * 4] = 0xc0;
        fixChecksum(mut, pn);
        CHECK(!v4PoolsOpen(&bad, mut, pn, &dict), "malformed UTF-8 theme refused");
        memcpy(mut, pb, pn);
        mut[20 + 33 * 4] = 0;
        fixChecksum(mut, pn);
        CHECK(!v4PoolsOpen(&bad, mut, pn, &dict), "empty theme name refused");
        memcpy(mut, pb, pn);
        // Corrupt the first pass tile index, recompute checksum to test range checks.
        size_t body = 20 + 33 * 4 + 3 * (WPP2_NAME_BYTES + WPP2_LENGTHS * 2);
        mut[body] = 254;
        fixChecksum(mut, pn);
        CHECK(!v4PoolsOpen(&bad, mut, pn, &dict), "out-of-range symbol index refused");
        memcpy(mut, pb, pn);
        // Make a dictionary-compatible but unlisted word by mutating one symbol.
        mut[body] = 0;
        fixChecksum(mut, pn);
        CHECK(!v4PoolsOpen(&bad, mut, pn, &dict), "pass absent from dictionary refused");
        free(mut);
    }
    free(pb); free(db);
    (void)uk;
}
int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: %s ru.dict uk.dict ru.pools uk.pools\n", argv[0]); return 2; }
    exercise(argv[1], argv[3], false);
    exercise(argv[2], argv[4], true);
    // A valid file from another script must never validate against this dictionary.
    size_t dlen = 0, plen = 0;
    uint8_t *db = readImage(argv[2], &dlen), *pb = readImage(argv[3], &plen);
    if (db && pb) {
        V4Dict dict; V4Pools pools;
        CHECK(v4DictOpen(&dict, db, dlen) &&
              !v4PoolsOpen(&pools, pb, plen, &dict), "cross-language alphabet mismatch refused");
    } else CHECK(0, "cross-language fixtures readable");
    free(db); free(pb);
    printf(fails ? "WPP2 NATIVE FAILED: %d failures\n" :
                   "WPP2 NATIVE OK: Unicode themes, dictionary-linked passwords, corruption\n", fails);
    return fails != 0;
}
