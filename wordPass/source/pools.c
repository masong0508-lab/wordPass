// Themed pass-word pools (format WPP1, built by tools/gen_pools.py into data/pools.bin).
// Words of one length are stored back to back, so word idx of length len is len bytes at
// base + idx * len; nothing is copied until a pass is picked.
#include "game.h"
#include <string.h>
#include "pools_bin.h"

#define MAX_THEMES 48
#define NLEN (POOL_MAX_LEN - POOL_MIN_LEN + 1)

// Two sets: the English pools compiled into the ROM (always kept: the in-game guide quotes English theme
// names whatever the language) and the active language's, parsed from a buffer by poolsSet.
typedef struct {
    int n;
    const char *name[MAX_THEMES];
    u16 count[MAX_THEMES][NLEN];
    const u8 *base[MAX_THEMES][NLEN];
} PoolSet;
static PoolSet enSet, altSet;
static const PoolSet *P = &enSet;

static void poolsParse(PoolSet *s, const u8 *p)
{
    s->n = 0;
    if (!p || memcmp(p, "WPP1", 4) != 0) return;
    const int fileThemes = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
    const int n = fileThemes > MAX_THEMES ? MAX_THEMES : fileThemes;
    const u8 *hdr = p + 8;
    const u8 *body = p + 8 + 20 * fileThemes;               // word data follows every theme's header
    for (int t = 0; t < n; t++) {
        s->name[t] = (const char *)hdr;                      // 8 bytes, NUL padded, 7 letters at most
        for (int k = 0; k < NLEN; k++) s->count[t][k] = hdr[8 + 2 * k] | (hdr[9 + 2 * k] << 8);
        hdr += 20;
    }
    for (int t = 0; t < n; t++)
        for (int k = 0; k < NLEN; k++) {
            s->base[t][k] = body;
            body += (size_t)s->count[t][k] * (POOL_MIN_LEN + k);
        }
    s->n = n;
}

void poolsInit(void) { poolsParse(&enSet, pools_bin); P = &enSet; }

// Make a language's pools (a WPP1 image) the active ones; NULL or the English image selects English.
bool poolsSet(const u8 *p)
{
    if (!p || p == pools_bin) { P = &enSet; return true; }
    poolsParse(&altSet, p);
    if (altSet.n == 0) { P = &enSet; return false; }
    P = &altSet;
    return true;
}

int poolThemeCount(void) { return P->n; }

const char *poolName(int theme) { return theme >= 0 && theme < P->n ? P->name[theme] : ""; }
const char *poolNameEn(int theme) { return theme >= 0 && theme < enSet.n ? enSet.name[theme] : ""; }

int poolCount(int theme, int len)
{
    if (theme < 0 || theme >= P->n || len < POOL_MIN_LEN || len > POOL_MAX_LEN) return 0;
    return P->count[theme][len - POOL_MIN_LEN];
}

void poolWord(int theme, int len, int idx, char *out)
{
    out[0] = 0;
    if (idx < 0 || idx >= poolCount(theme, len)) return;
    memcpy(out, P->base[theme][len - POOL_MIN_LEN] + (size_t)idx * len, len);
    out[len] = 0;
}

// the English word of a theme, for the in-game guide's examples (which stay English whatever the language)
void poolWordEn(int theme, int len, int idx, char *out)
{
    out[0] = 0;
    if (theme < 0 || theme >= enSet.n || len < POOL_MIN_LEN || len > POOL_MAX_LEN) return;
    if (idx < 0 || idx >= enSet.count[theme][len - POOL_MIN_LEN]) return;
    memcpy(out, enSet.base[theme][len - POOL_MIN_LEN] + (size_t)idx * len, len);
    out[len] = 0;
}
