// Dictionary: sorted packed records, binary search per length (format WPD2).
// Record width depends on word length (5 bits per letter, left aligned,
// big-endian); bit 0 of the last byte is the tier: 0 common, 1 rare.
#include "game.h"
#include <string.h>
#include "dict_bin.h"

#define MIN_LEN 3
#define MAX_LEN 10
#define REC_MAX 7

static const u8 recLen[MAX_LEN + 1] = { 0, 0, 0, 4, 4, 4, 4, 5, 6, 7, 7 };

static const u8 *groupBase[MAX_LEN + 1];
static u32 groupCount[MAX_LEN + 1];

void dictInit(void)
{
    const u8 *p = dict_bin;
    if (memcmp(p, "WPD2", 4) != 0) return;
    p += 4;
    u32 counts[MAX_LEN + 1];
    for (int n = MIN_LEN; n <= MAX_LEN; n++) {
        counts[n] = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
        p += 4;
    }
    for (int n = MIN_LEN; n <= MAX_LEN; n++) {
        groupBase[n] = p;
        groupCount[n] = counts[n];
        p += (size_t)counts[n] * recLen[n];
    }
}

static void packWord(const char *w, int len, u8 *out, int rec)
{
    // 5 bits per letter, big-endian, left aligned in rec*8 bits; tier bit clear
    u64 bits = 0;
    for (int i = 0; i < len; i++) bits = (bits << 5) | (u64)((w[i] | 0x20) - 'a');
    bits <<= (rec * 8 - 5 * len);
    for (int i = 0; i < rec; i++) out[i] = (u8)(bits >> (8 * (rec - 1 - i)));
}

// 0 = not a word, 1 = common, 2 = rare
int dictLookup(const char *word, int len)
{
    if (len < MIN_LEN || len > MAX_LEN || !groupBase[len]) return 0;
    const int rec = recLen[len];
    u8 key[REC_MAX];
    packWord(word, len, key, rec);
    const u8 *base = groupBase[len];
    int lo = 0, hi = (int)groupCount[len] - 1;
    if (rec == 4) {
        // lengths 3..6: one big-endian word per record, compared as integers
        u32 k = ((u32)key[0] << 24) | ((u32)key[1] << 16) | ((u32)key[2] << 8) | key[3];
        while (lo <= hi) {
            int mid = (lo + hi) >> 1;
            const u8 *r = base + mid * 4;
            u32 v = ((u32)r[0] << 24) | ((u32)r[1] << 16) | ((u32)r[2] << 8) | r[3];
            u32 vc = v & ~1u;
            if (vc == k) return 1 + (int)(v & 1);
            if (vc < k) lo = mid + 1; else hi = mid - 1;
        }
        return 0;
    }
    const int last = rec - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const u8 *r = base + mid * rec;
        int cmp = memcmp(r, key, last);
        if (cmp == 0) cmp = (int)(r[last] & 0xFE) - (int)key[last];
        if (cmp == 0) return 1 + (r[last] & 1);
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
