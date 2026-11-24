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
static uint32_t readLE32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void fixChecksum(uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 20; i < n; i++) h = (h ^ p[i]) * 16777619u;
    for (int i = 0; i < 4; i++) p[16 + i] = (uint8_t)(h >> (i * 8));
}
static void writeLE32(uint8_t *p, uint32_t value)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}
// An independent bit-at-a-time encoder checks every supported bit width and
// word length, including byte crossings and the last 20-symbol key window.
static void referencePack(uint8_t *out, const uint8_t *ids, int length,
                          unsigned bits, int tier)
{
    unsigned stride = ((unsigned)length * bits + 8u) / 8u;
    memset(out, 0, stride);
    for (unsigned at = 0; at < (unsigned)length * bits; at++) {
        unsigned bit = (ids[at / bits] >> (bits - 1u - at % bits)) & 1u;
        out[at / 8u] |= (uint8_t)(bit << (7u - at % 8u));
    }
    out[stride - 1u] |= (uint8_t)tier;
}
static void testPackedWidths(void)
{
    for (unsigned bits = 1; bits <= 6; bits++) {
        unsigned symbols = 1u << bits;
        for (int length = WPD4_MIN_WORD; length <= WPD4_MAX_WORD; length++) {
            uint8_t image[512] = {0}, ids[4][WPD4_MAX_WORD] = {{0}};
            unsigned counts = 20u + 4u * symbols, start = counts + 18u * 4u;
            unsigned stride = ((unsigned)length * bits + 8u) / 8u;
            size_t bytes = start + 4u * stride;
            memcpy(image, "WPD4", 4);
            image[4] = 1; image[6] = (uint8_t)symbols; image[7] = (uint8_t)bits;
            image[8] = WPD4_MIN_WORD; image[9] = WPD4_MAX_WORD;
            writeLE32(image + 12, (uint32_t)bytes);
            for (unsigned i = 0; i < symbols; i++) writeLE32(image + 20 + i * 4u, 0x21u + i);
            writeLE32(image + counts + (unsigned)(length - WPD4_MIN_WORD) * 4u, 4);
            ids[1][length - 1] = (uint8_t)(symbols - 1u);
            ids[2][0] = (uint8_t)(symbols - 1u);
            memset(ids[3], (int)symbols - 1, (size_t)length);
            for (int word = 0; word < 4; word++)
                referencePack(image + start + (unsigned)word * stride,
                              ids[word], length, bits, word & 1);
            fixChecksum(image, bytes);
            V4Dict d;
            bool opened = v4DictOpen(&d, image, bytes);
            CHECK(opened, "reference dictionary bits=%u length=%d", bits, length);
            if (!opened) continue;
            for (int word = 0; word < 4; word++) {
                CHECK(v4DictLookupIds(&d, ids[word], length) == 1 + (word & 1),
                      "packed lookup bits=%u length=%d word=%d", bits, length, word);
                for (int prefix = 1; prefix <= length; prefix++) {
                    CHECK(v4DictHasPrefixIds(&d, ids[word], prefix) &&
                          v4DictHasPrefixLengthIds(&d, ids[word], prefix, length),
                          "prefix bits=%u length=%d word=%d prefix=%d", bits, length, word, prefix);
                }
            }
            uint8_t missing[WPD4_MAX_WORD] = {0};
            missing[1] = (uint8_t)(symbols - 1u);
            CHECK(!v4DictLookupIds(&d, missing, length) &&
                  !v4DictHasPrefixIds(&d, missing, 2) &&
                  !v4DictHasPrefixLengthIds(&d, missing, 2, length),
                  "absent prefix bits=%u length=%d", bits, length);
            int otherLength = length == WPD4_MAX_WORD ? length - 1 : length + 1;
            CHECK(!v4DictHasPrefixLengthIds(&d, ids[0], 1, otherLength),
                  "prefix excludes other length table bits=%u length=%d", bits, length);
            CHECK(!v4DictHasPrefixLengthIds(&d, ids[0], length + 1, length) &&
                  !v4DictHasPrefixLengthIds(&d, ids[0], 0, length) &&
                  !v4DictHasPrefixLengthIds(&d, ids[0], 1, 2) &&
                  !v4DictHasPrefixLengthIds(&d, ids[0], 1, WPD4_MAX_WORD + 1) &&
                  !v4DictHasPrefixLengthIds(&d, NULL, 1, length) &&
                  !v4DictHasPrefixLengthIds(NULL, ids[0], 1, length),
                  "bounded prefix rejects invalid lengths/pointers");
            missing[0] = (uint8_t)symbols;
            CHECK(!v4DictLookupIds(&d, missing, length) &&
                  !v4DictHasPrefixLengthIds(&d, missing, 1, length),
                  "bounded packing rejects ID past alphabet");
            // Differential proof: the optimized narrowing API must match
            // the original packed-prefix binary search for all four fixture
            // words at every symbol depth, for each 1..6-bit packing width.
            for (int word = 0; word < 4; word++) {
                uint32_t first = 0, past = d.count[length];
                for (int pos = 0; pos < length; pos++) {
                    CHECK(v4DictNarrowPrefixLengthId(&d, length, pos,
                              ids[word][pos], &first, &past) && first < past &&
                          past <= d.count[length] &&
                          v4DictHasPrefixLengthIds(&d, ids[word], pos+1, length),
                          "span narrows real word bits=%u length=%d word=%d position=%d",
                          bits, length, word, pos);
                }
                CHECK(past == first + 1u &&
                      v4DictLookupIds(&d, ids[word], length) == 1 + (word & 1),
                      "full-length prefix interval holds exactly one validated word");
            }
            // Exhaust every one- and two-symbol prefix of this 4-word group
            // for small and full 64-symbol alphabets: old and new APIs must
            // agree on empty ranges, including byte-crossing symbol indexes.
            for (unsigned a = 0; a < symbols; a++) {
                uint8_t trial[2] = {(uint8_t)a, 0};
                uint32_t lo = 0, hi = d.count[length];
                bool found = v4DictNarrowPrefixLengthId(&d, length, 0,
                                      trial[0], &lo, &hi);
                CHECK(found == v4DictHasPrefixLengthIds(&d, trial, 1, length),
                      "span first letter matches packed prefix bits=%u length=%d id=%u",
                      bits, length, a);
                if (!found) continue;
                for (unsigned b = 0; b < symbols; b++) {
                    trial[1] = (uint8_t)b;
                    uint32_t first = lo, past = hi;
                    bool pair = v4DictNarrowPrefixLengthId(&d, length, 1,
                                                 trial[1], &first, &past);
                    CHECK(pair == v4DictHasPrefixLengthIds(&d, trial, 2, length),
                          "span two letters matches packed prefix bits=%u length=%d a=%u b=%u",
                          bits, length, a, b);
                    if (pair) CHECK(first >= lo && past <= hi && first < past,
                                    "narrowed span remains inside its parent");
                    else CHECK(first == lo && past == hi,
                               "an absent next letter must leave interval unchanged");
                }
            }
            uint32_t lo = 0, hi = d.count[length];
            CHECK(!v4DictNarrowPrefixLengthId(&d, length, 0,
                                              (uint8_t)symbols, &lo, &hi) &&
                  !v4DictNarrowPrefixLengthId(&d, length, -1,
                                              0, &lo, &hi) &&
                  !v4DictNarrowPrefixLengthId(&d, length, length,
                                              0, &lo, &hi) &&
                  !v4DictNarrowPrefixLengthId(NULL, length, 0,
                                              0, &lo, &hi) &&
                  !v4DictNarrowPrefixLengthId(&d, length, 0,
                                              0, NULL, &hi) &&
                  lo == 0 && hi == d.count[length],
                  "bad span inputs fail closed and never mutate caller interval");
            lo = 1; hi = d.count[length] + 1u;
            CHECK(!v4DictNarrowPrefixLengthId(&d, length, 0, 0, &lo, &hi) &&
                  lo == 1 && hi == d.count[length] + 1u,
                  "out-of-bounds prefix span rejected without mutation");
        }
    }
}
static void testRechecks(const uint8_t *p, size_t n, const V4Dict *dict)
{
    uint8_t *badData = malloc(n);
    CHECK(badData != NULL, "checksum-preserving mutation buffer");
    if (!badData) return;
    V4Dict bad;
    size_t countOffset = 20u + 4u * dict->symbols;
    size_t off = countOffset + 18u * 4u;
    bool foundGroup = false;
    for (int len = WPD4_MIN_WORD; len <= WPD4_MAX_WORD; len++) {
        uint32_t count = readLE32(p + countOffset + (len - WPD4_MIN_WORD) * 4u);
        unsigned rec = ((unsigned)len * dict->bits + 1u + 7u) / 8u;
        if (count >= 2 && !foundGroup) {
            foundGroup = true;
            // All tests recompute a matching checksum. The structural reader
            // must independently refuse broken bit packing and binary order.
            memcpy(badData, p, n);
            badData[off + rec - 1] |= 2u; // nonzero padding above tier bit
            fixChecksum(badData, n);
            CHECK(!v4DictOpen(&bad, badData, n) && !bad.image,
                  "checksum-valid nonzero record padding rejected");

            memcpy(badData, p, n);
            badData[off] = (uint8_t)((badData[off] & 3u) | 0xfcu); // 63 >= 33 letters
            fixChecksum(badData, n);
            CHECK(!v4DictOpen(&bad, badData, n) && !bad.image,
                  "checksum-valid unrepresentable symbol ID rejected");

            memcpy(badData, p, n);
            memcpy(badData + off + rec, badData + off, rec);
            fixChecksum(badData, n);
            CHECK(!v4DictOpen(&bad, badData, n) && !bad.image,
                  "checksum-valid duplicate word rejected");

            memcpy(badData, p, n);
            uint8_t tmp[16];
            CHECK(rec <= sizeof tmp, "maximum dictionary record width");
            if (rec <= sizeof tmp) {
                memcpy(tmp, badData + off, rec);
                memcpy(badData + off, badData + off + rec, rec);
                memcpy(badData + off + rec, tmp, rec);
                fixChecksum(badData, n);
                CHECK(!v4DictOpen(&bad, badData, n) && !bad.image,
                      "checksum-valid unsorted word records rejected");
            }
        }
        off += (size_t)count * rec;
    }
    CHECK(foundGroup, "test fixture has a multiword length group");
    memcpy(badData, p, n);
    for (int byte = 0; byte < 4; byte++) {
        uint8_t tmp = badData[20 + byte];
        badData[20 + byte] = badData[24 + byte];
        badData[24 + byte] = tmp;
    }
    fixChecksum(badData, n);
    CHECK(!v4DictOpen(&bad, badData, n) && !bad.image,
          "checksum-valid unsorted Unicode alphabet rejected");
    free(badData);
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
    CHECK(v4DictHasPrefixIds(&d, ids, 1) &&
          v4DictHasPrefixIds(&d, ids, 2) &&
          v4DictHasPrefixIds(&d, ids, 3) &&
          v4DictHasPrefixIds(&d, ids, n),
          "Unicode Cyrillic prefix lookup works for short and whole words");
    uint8_t badPrefix[] = { 255, 0, 0 };
    CHECK(!v4DictHasPrefixIds(&d, badPrefix, 3) &&
          !v4DictHasPrefixIds(&d, NULL, 2) &&
          !v4DictHasPrefixIds(&d, ids, 0),
          "Unicode prefix lookup rejects invalid lengths, symbols and pointers");
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
    testRechecks(p, bytes, &d);
    CHECK(!v4DictOpen(NULL, p, bytes), "NULL destination view");
    free(p);
}
static void testApostrophe(const char *file)
{
    size_t bytes = 0;
    uint8_t *p = readFile(file, &bytes);
    CHECK(p != NULL, "read Ukrainian orthographic fixture");
    if (!p) return;
    V4Dict d;
    bool opened = v4DictOpen(&d, p, bytes);
    CHECK(opened && d.symbols == 33 && d.apostrophe[WPD4_MIN_WORD],
          "flagged Ukrainian pack opens without a punctuation tile");
    if (!opened) { free(p); return; }
    CHECK(v4DictSymbol(&d, 0x02bc) == -1 &&
          v4DictLookup(&d, "п'ять") == 2 &&
          v4DictLookup(&d, "п’ять") == 2 &&
          v4DictLookup(&d, "пʼять") == 2,
          "all three apostrophe forms resolve to the attested spelling");
    CHECK(v4DictLookup(&d, "мʼясо") == 2 &&
          v4DictLookup(&d, "зв’язок") == 2 &&
          v4DictLookup(&d, "ПРИВІТ") == 1,
          "extended dictionary retains normal letters, tiers and spelling");
    CHECK(!v4DictLookup(&d, "ПЯТЬ") &&
          !v4DictLookup(&d, "ПʼʼЯТЬ") &&
          !v4DictLookup(&d, "ʼПЯТЬ") &&
          !v4DictLookup(&d, "ПЯТЬʼ") &&
          !v4DictLookup(&d, "ПЯʼТЬ"),
          "strict UTF-8 lookup never accepts missing, repeated or misplaced apostrophes");
    const uint32_t cps[] = {0x041f, 0x042f, 0x0422, 0x042c};
    uint8_t ids[4];
    for (int i = 0; i < 4; i++) ids[i] = (uint8_t)v4DictSymbol(&d, cps[i]);
    uint32_t mask = 0;
    CHECK(v4DictLookupSpellingIds(&d, ids, 4, &mask) == 2 && mask == 2 &&
          v4DictLookupIds(&d, ids, 4) == 2 &&
          v4DictHasPrefixLengthIds(&d, ids, 2, 4),
          "four physical letters score the complete stored apostrophe spelling");
    uint32_t first = 0, past = d.count[4];
    for (int i = 0; i < 4; i++)
        CHECK(v4DictNarrowPrefixLengthId(&d, 4, i, ids[i], &first, &past),
              "hint prefix intervals still work with appended masks");
    CHECK(past == first + 1u, "extended prefix identifies exactly one spelling");
    size_t trailer = (size_t)(d.apostrophe[WPD4_MIN_WORD] - p);
    p[trailer] |= 1;
    fixChecksum(p, bytes);
    V4Dict bad;
    CHECK(!v4DictOpen(&bad, p, bytes) && !bad.image,
          "checksum-valid leading apostrophe in metadata rejected");
    free(p);
}
int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s ru.bin uk.bin uk_apostrophe.bin\n", argv[0]); return 2; }
    testPackedWidths();
    exercise(argv[1], false);
    exercise(argv[2], true);
    testApostrophe(argv[3]);
    printf(fails ? "WPD4 NATIVE FAILED: %d failures\n" : "WPD4 NATIVE OK: 1..6-bit packing, bounded prefixes, Cyrillic words and strict records\n", fails);
    return fails ? 1 : 0;
}
