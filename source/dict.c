// Dictionary: sorted packed records, binary search per length (format WPD3, lengths 3..20).
// Record width depends on word length (5 bits per letter, left aligned, big-endian); bit 0
// of the last byte is the tier: 0 common, 1 rare.
#include "game.h"
#include <string.h>
#include "dict_bin.h"
#include "variants_bin.h"

#define MIN_LEN 3
#define MAX_LEN MAX_WORD
#define REC_MAX 13

// bytes per record for words of length 0..20; every width leaves at least two spare bits
static const u8 recLen[MAX_LEN + 1] = { 0, 0, 0, 4, 4, 4, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13 };

// One sorted table per file: the dictionary (WPD3) and the spelling variants (WPV1) share a layout.
typedef struct { const u8 *base[MAX_LEN + 1]; u32 count[MAX_LEN + 1]; } Table;
static Table dict, variants;

static void tableInit(Table *t, const u8 *p, const char *magic)
{
    memset(t, 0, sizeof(*t));
    if (!p || memcmp(p, magic, 4) != 0) return;
    p += 4;
    u32 counts[MAX_LEN + 1];
    for (int n = MIN_LEN; n <= MAX_LEN; n++) {
        counts[n] = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
        p += 4;
    }
    for (int n = MIN_LEN; n <= MAX_LEN; n++) {
        t->base[n] = p;
        t->count[n] = counts[n];
        p += (size_t)counts[n] * recLen[n];
    }
}

void dictInit(void)
{
    tableInit(&dict, dict_bin, "WPD3");
    tableInit(&variants, variants_bin, "WPV1");
}

// Point the validation table at another WPD3 image (a language loaded from the filesystem); the
// spelling variants stay as dictInit left them (they only apply to English).
void dictSet(const u8 *p) { tableInit(&dict, p, "WPD3"); }

// 5 bits per letter, big-endian, left aligned in rec bytes, tier bit clear. A letter at bit
// offset b lands in the 16-bit window starting at byte b / 8 (5 + 7 bits never overflow it).
static void packWord(const char *w, int len, u8 *out, int rec)
{
    memset(out, 0, rec);
    for (int i = 0; i < len; i++) {
        u32 x = (u32)((w[i] | 0x20) - 'a') << (11 - ((5 * i) & 7));
        int at = (5 * i) >> 3;
        out[at] |= (u8)(x >> 8);
        if (at + 1 < rec) out[at + 1] |= (u8)x;
    }
}

// Binary search of one table: 0 = absent, else 1 + the tier bit (bit 0 of the last byte).
static int tableLookup(const Table *t, const char *word, int len)
{
    if (len < MIN_LEN || len > MAX_LEN || !t->base[len]) return 0;
    const int rec = recLen[len];
    u8 key[REC_MAX];
    packWord(word, len, key, rec);
    const u8 *base = t->base[len];
    int lo = 0, hi = (int)t->count[len] - 1;
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
        const u8 *r = base + (size_t)mid * rec;
        int cmp = memcmp(r, key, last);
        if (cmp == 0) cmp = (int)(r[last] & 0xFE) - (int)key[last];
        if (cmp == 0) return 1 + (r[last] & 1);
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

// 0 = not a word, 1 = common, 2 = rare
int dictLookup(const char *word, int len) { return tableLookup(&dict, word, len); }

// 0 = spelled the same everywhere (or unknown), 1 = American spelling only, 2 = British only
int dictSpelling(const char *word, int len) { return tableLookup(&variants, word, len); }
