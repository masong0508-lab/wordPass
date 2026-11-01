// Dictionary: sorted fixed-width packed records, binary search per length.
#include "game.h"
#include <string.h>
#include "dict_bin.h"

#define MIN_LEN 3
#define MAX_LEN 10
#define REC 7

static const u8 *groupBase[MAX_LEN + 1];
static u32 groupCount[MAX_LEN + 1];

void dictInit(void)
{
    const u8 *p = dict_bin;
    if (memcmp(p, "WPD1", 4) != 0) return;
    p += 4;
    u32 counts[MAX_LEN + 1];
    for (int n = MIN_LEN; n <= MAX_LEN; n++) {
        counts[n] = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
        p += 4;
    }
    for (int n = MIN_LEN; n <= MAX_LEN; n++) {
        groupBase[n] = p;
        groupCount[n] = counts[n];
        p += (size_t)counts[n] * REC;
    }
}

static void packWord(const char *w, int len, u8 out[REC])
{
    // 5 bits per letter, big-endian, left aligned in 56 bits
    u64 bits = 0;
    for (int i = 0; i < len; i++) bits = (bits << 5) | (u64)((w[i] | 0x20) - 'a');
    bits <<= (REC * 8 - 5 * len);
    for (int i = 0; i < REC; i++) out[i] = (u8)(bits >> (8 * (REC - 1 - i)));
}

bool dictLookup(const char *word, int len)
{
    if (len < MIN_LEN || len > MAX_LEN || !groupBase[len]) return false;
    u8 key[REC];
    packWord(word, len, key);
    const u8 *base = groupBase[len];
    int lo = 0, hi = (int)groupCount[len] - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int cmp = memcmp(base + (size_t)mid * REC, key, REC);
        if (cmp == 0) return true;
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}
