// Host-only search parity/timing harness; benchmark_search.py compiles it
// against a baseline and current search implementation with identical data.
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "dict_bin.h"
#include "pools_bin.h"
#include "variants_bin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned long lookups, prefixes;
int __real_dictLookup(const char *word, int length);
int __wrap_dictLookup(const char *word, int length)
{
    lookups++;
    return __real_dictLookup(word, length);
}
#ifdef SEARCH_PREFIX
bool __real_dictHasPrefix(const char *word, int length, int low, int high);
bool __wrap_dictHasPrefix(const char *word, int length, int low, int high)
{
    prefixes++;
    return __real_dictHasPrefix(word, length, low, high);
}
#endif
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}
static const u8 *readImage(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) exit(2);
    long length = ftell(f);
    if (length <= 0 || fseek(f, 0, SEEK_SET)) exit(2);
    u8 *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, f) != (size_t)length || fclose(f)) exit(2);
    return bytes;
}
static void measure(int language, const char *label, int count, int constant)
{
    double elapsed = 0;
    unsigned long full = 0, partial = 0;
    for (int sample = 0; sample < count; sample++) {
        Game board;
        memset(&board, 0, sizeof board);
        if (constant >= 0) {
            for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) {
                board.cell[c][r].state = CELL_TILE;
                board.cell[c][r].letter = constant;
            }
        } else {
            board.mode = MODE_CLASSIC; board.shelf = 12 + sample % 6;
            board.state = ST_PLAY; board.lives = BOOKMARKS_MAX;
            srand(1000u + (unsigned)sample * 7919u);
            boardNewShelf(&board);
            // Include fire, holes and pass diamonds without consuming RNG.
            for (int c = 0; c < COLS; c++) for (int r = 0; r < ROWS; r++) {
                int at = c * ROWS + r;
                if (sample % 3 == 1 && at % 17 == 0) board.cell[c][r].state = CELL_BURN;
                if (sample % 3 == 2 && at % 13 == 0) board.cell[c][r].state = CELL_HOLE;
                if (sample % 3 == 2 && at % 19 == 0) board.cell[c][r].state = CELL_SECRET;
            }
            if (sample % 4 == 3) {
                u8 c[5], r[5];
                int n = boardHintChain(&board, c, r, 5);
                if (n) {
                    board.chainLen = n;
                    memcpy(board.chainC, c, (size_t)n); memcpy(board.chainR, r, (size_t)n);
                    boardChainWord(&board, board.ledger[0]);
                    board.ledgerN = board.ledgerHead = 1;
                    board.chainLen = 0;
                }
            }
        }
        Game before = board;
        u8 c[5], r[5];
        srand(0x1234u + (unsigned)sample);
        int expectedRandom = rand();
        srand(0x1234u + (unsigned)sample);
        unsigned long fullBefore = lookups, prefixBefore = prefixes;
        double start = now();
        bool hasWord = boardHasWords(&board);
        int length = boardHintChain(&board, c, r, 5);
        elapsed += now() - start;
        full += lookups - fullBefore; partial += prefixes - prefixBefore;
        if (memcmp(&board, &before, sizeof board) || rand() != expectedRandom) {
            fprintf(stderr, "search changed board/RNG: %s %s %d\n", langCode(language), label, sample);
            exit(1);
        }
        // Compare complete result lines, not only a collision-prone checksum.
        printf("RESULT %s %s %d %d %d", langCode(language), label, sample, hasWord, length);
        for (int i = 0; i < length; i++) printf(" %u:%u", c[i], r[i]);
        putchar('\n');
    }
    printf("MEASURE %s %s %d %.6f %lu %lu\n", langCode(language), label,
           count, elapsed * 1000, full, partial);
}
int main(int argc, char **argv)
{
    int count = argc == 2 ? atoi(argv[1]) : 100;
    if (count < 1 || count > 1000) return 2;
    dict_bin = readImage("data/dict.bin"); pools_bin = readImage("data/pools.bin");
    variants_bin = readImage("data/variants.bin");
    dictInit(); poolsInit(); langInit(); S.fire = FIRE_NORMAL;
    for (int language = 0; language < LANG_COUNT; language++) {
        if (!langLoad(language)) return 2;
        S.library = LIB_FULL; S.spelling = SPELL_BOTH;
        measure(language, "seeded-full", count, -1);
        S.library = LIB_COMMON;
        measure(language, "seeded-common", count, -1);
        S.library = LIB_FULL;
        if (language == LANG_EN) {
            S.spelling = SPELL_US; measure(language, "seeded-us", count, -1);
            S.spelling = SPELL_UK; measure(language, "seeded-uk", count, -1);
        }
        S.spelling = SPELL_BOTH;
        measure(language, "all-x", 1, 'x' - 'a');
        measure(language, "all-q", 1, 'q' - 'a');
    }
    return 0;
}
