// Themed pass-word pools (format WPP1, built by tools/gen_pools.py into data/pools.bin).
// Words of one length are stored back to back, so word idx of length len is len bytes at
// base + idx * len; nothing is copied until a pass is picked.
#include "game.h"
#include <string.h>
#include "pools_bin.h"

#define MAX_THEMES 48
#define NLEN (POOL_MAX_LEN - POOL_MIN_LEN + 1)

static int nThemes;
static const char *themeName[MAX_THEMES];
static u16 themeCount[MAX_THEMES][NLEN];
static const u8 *themeBase[MAX_THEMES][NLEN];

void poolsInit(void)
{
    const u8 *p = pools_bin;
    nThemes = 0;
    if (memcmp(p, "WPP1", 4) != 0) return;
    const int fileThemes = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
    const int n = fileThemes > MAX_THEMES ? MAX_THEMES : fileThemes;
    const u8 *hdr = p + 8;
    const u8 *body = p + 8 + 20 * fileThemes;               // word data follows every theme's header
    for (int t = 0; t < n; t++) {
        themeName[t] = (const char *)hdr;                    // 8 bytes, NUL padded, 7 letters at most
        for (int k = 0; k < NLEN; k++) themeCount[t][k] = hdr[8 + 2 * k] | (hdr[9 + 2 * k] << 8);
        hdr += 20;
    }
    for (int t = 0; t < n; t++)
        for (int k = 0; k < NLEN; k++) {
            themeBase[t][k] = body;
            body += (size_t)themeCount[t][k] * (POOL_MIN_LEN + k);
        }
    nThemes = n;
}

int poolThemeCount(void) { return nThemes; }

const char *poolName(int theme) { return theme >= 0 && theme < nThemes ? themeName[theme] : ""; }

int poolCount(int theme, int len)
{
    if (theme < 0 || theme >= nThemes || len < POOL_MIN_LEN || len > POOL_MAX_LEN) return 0;
    return themeCount[theme][len - POOL_MIN_LEN];
}

void poolWord(int theme, int len, int idx, char *out)
{
    out[0] = 0;
    if (idx < 0 || idx >= poolCount(theme, len)) return;
    memcpy(out, themeBase[theme][len - POOL_MIN_LEN] + (size_t)idx * len, len);
    out[len] = 0;
}
