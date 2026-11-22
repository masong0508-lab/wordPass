// Optional host microbenchmark, never an FPS gate. `make check` creates the
// Russian WPD4 fixture. Compile with -O2 and source/dict_v4.c, then pass that
// fixture path. See docs/PERFORMANCE.md for before/after reproduction.
#define _POSIX_C_SOURCE 200809L
#include "dict_v4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 2; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return 2; }
    rewind(f);
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { free(data); fclose(f); return 2; }
    fclose(f);
    V4Dict d;
    if (!v4DictOpen(&d, data, (size_t)size)) { free(data); return 2; }
    const uint32_t word[6] = {0x041f,0x0420,0x0418,0x0412,0x0415,0x0422};
    uint8_t ids[2][6];
    for (int i = 0; i < 6; i++) {
        int id = v4DictSymbol(&d, word[i]);
        if (id < 0) { free(data); return 2; }
        ids[0][i] = (uint8_t)id;
    }
    memcpy(ids[1], ids[0], 6);
    ids[1][0] = (uint8_t)((ids[1][0] + 1) % d.symbols);
    const int iterations = 4000000;
    unsigned sum = 0;
    double start = now();
    for (int i = 0; i < iterations; i++) sum += v4DictLookupIds(&d, ids[i & 1], 6);
    printf("lookups=%d sum=%u elapsed_ms=%.3f\n", iterations, sum, (now() - start) * 1000);
    free(data);
    return sum == (unsigned)iterations / 2u ? 0 : 1;
}
