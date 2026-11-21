// Native WPD4 smoke. The Russian/Ukrainian fixture is generated locally from
// synthetic, tiny word sets; it is NEVER offered as a selectable language.
#include "dict_v4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(ok, ...) do { if (!(ok)) { fails++; printf("FAIL "); printf(__VA_ARGS__); putchar('\n'); } } while (0)

static uint8_t *readFile(const char *name, size_t *length)
{
    FILE *f = fopen(name, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f);
    *length = (size_t)n;
    return p;
}
static void exercise(const char *file, bool uk)
{
    size_t bytes = 0;
    uint8_t *p = readFile(file, &bytes);
    CHECK(p != NULL, "open fixture %s", file);
    if (!p) return;
    V4Dict d;
    CHECK(v4DictOpen(&d, p, bytes), "open WPD4 %s", file);
    CHECK(d.symbols == 33 && d.bits == 6, "33-letter Cyrillic alphabet uses six-bit indexes");
    const char *known = uk ? "ПРИВІТ" : "ПРИВЕТ";
    const char *rare = uk ? "ҐАНОК" : "БИБЛИОТЕКА";
    const char *extra = uk ? "ЇЖАК" : "ЁЖИК";
    const char *low = uk ? "привіт" : "привет";
    CHECK(v4DictLookup(&d, known) == 1, "%s should be common", known);
    CHECK(v4DictLookup(&d, low) == 1, "case-insensitive Cyrillic lookup");
    CHECK(v4DictLookup(&d, rare) == 2, "%s should be rare", rare);
    CHECK(v4DictLookup(&d, extra) == 1, "%s extra alphabet letter", extra);
    CHECK(v4DictLookup(&d, "НЕТТАКОГОСЛОВА") == 0, "reject missing word");
    CHECK(v4DictLookup(&d, "HELLO") == 0, "reject foreign alphabet");
    CHECK(v4DictLookup(&d, "\xD0") == 0, "reject truncated UTF-8");
    CHECK(v4DictLookup(&d, "\xC0\xAF") == 0, "reject overlong UTF-8");
    CHECK(v4DictLookup(&d, NULL) == 0, "NULL lookup");
    CHECK(v4DictLookup(&d, "") == 0, "empty word");
    CHECK(v4DictLookupIds(&d, NULL, 4) == 0, "NULL tile path");
    uint8_t invalid[] = { 1, 2, 255 };
    CHECK(v4DictLookupIds(&d, invalid, 3) == 0, "tile index outside alphabet");
    uint8_t ids[WPD4_MAX_WORD];
    int n = 0;
    // Build a symbol-ID chain for ПРИВІТ / ПРИВЕТ without relying on bytes
    // in the source word. The tile engine can eventually call this fast path.
    const uint32_t cps[] = { 0x041f, 0x0420, 0x0418, 0x0412, uk ? 0x0406 : 0x0415, 0x0422 };
    for (unsigned i = 0; i < sizeof cps / sizeof cps[0]; i++) {
        int id = v4DictSymbol(&d, cps[i]);
        CHECK(id >= 0, "alphabet codepoint %x", (unsigned)cps[i]);
        ids[n++] = (uint8_t)(id < 0 ? 0 : id);
    }
    CHECK(v4DictLookupIds(&d, ids, n) == 1, "fast symbol-ID lookup matches UTF-8 lookup");
    CHECK(v4DictSymbol(&d, 0x1f600) == -1, "not an alphabet character");
    for (size_t cut = 0; cut < bytes; cut++) {
        V4Dict broken;
        CHECK(!v4DictOpen(&broken, p, cut) && !broken.image, "reject truncated WPD4 image (%zu)", cut);
    }
    uint8_t *copy = malloc(bytes);
    CHECK(copy != NULL, "buffer for corrupted dictionary");
    if (copy) {
        memcpy(copy, p, bytes);
        copy[bytes - 1] ^= 1;
        V4Dict bad;
        CHECK(!v4DictOpen(&bad, copy, bytes) && !bad.image, "refuse data with a flipped tier bit");
        memcpy(copy, p, bytes);
        copy[6] = 65;
        CHECK(!v4DictOpen(&bad, copy, bytes), "refuse oversized alphabet");
        memcpy(copy, p, bytes);
        copy[12] ^= 1;
        CHECK(!v4DictOpen(&bad, copy, bytes), "refuse lying file size");
        memcpy(copy, p, bytes);
        copy[7] = 5;
        CHECK(!v4DictOpen(&bad, copy, bytes), "refuse undersized bits per tile");
        free(copy);
    }
    CHECK(!v4DictOpen(NULL, p, bytes), "NULL destination view");
    free(p);
}
int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s build/wpd4_ru.bin build/wpd4_uk.bin\n", argv[0]); return 2; }
    exercise(argv[1], false);
    exercise(argv[2], true);
    printf(fails ? "WPD4 NATIVE FAILED: %d failures\n" : "WPD4 NATIVE OK: Cyrillic words, alphabet IDs, corruption and truncation\n", fails);
    return fails ? 1 : 0;
}
