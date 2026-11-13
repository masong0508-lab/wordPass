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

// Fills `s` from a WPP1 image of `bytes` bytes (IMAGE_TRUSTED: the compiled-in one). Returns false, with no themes, when
// the magic is wrong, the header or any word run would reach past the end of the image, or a theme has no word.
static bool poolsParse(PoolSet *s, const u8 *p, size_t bytes)
{
    s->n = 0;
    if (!p || memcmp(p, "WPP1", 4) != 0) return false;
    const bool trusted = bytes == IMAGE_TRUSTED;
    if (!trusted && bytes < 8) return false;
    const u32 fileThemes = (u32)p[4] | ((u32)p[5] << 8) | ((u32)p[6] << 16) | ((u32)p[7] << 24);
    if (!trusted && (fileThemes == 0 || fileThemes > 4096 || 8 + 20 * (size_t)fileThemes > bytes)) return false;
    const int n = fileThemes > MAX_THEMES ? MAX_THEMES : (int)fileThemes;
    const u8 *hdr = p + 8;
    const u8 *body = p + 8 + 20 * (size_t)fileThemes;       // word data follows every theme's header
    unsigned long long used = 8 + 20 * (unsigned long long)fileThemes;
    for (int t = 0; t < n; t++) {
        s->name[t] = (const char *)hdr;                      // 8 bytes, NUL padded, 7 letters at most
        unsigned words = 0;
        for (int k = 0; k < NLEN; k++) { s->count[t][k] = hdr[8 + 2 * k] | (hdr[9 + 2 * k] << 8); words += s->count[t][k]; }
        if (!trusted && words == 0) return false;            // pickPass needs a word in every theme
        hdr += 20;
    }
    for (int t = 0; t < n; t++)
        for (int k = 0; k < NLEN; k++) {
            s->base[t][k] = body;
            body += (size_t)s->count[t][k] * (POOL_MIN_LEN + k);
            used += (unsigned long long)s->count[t][k] * (POOL_MIN_LEN + k);
        }
    if (!trusted && used > bytes) return false;
    s->n = n;
    return true;
}

void poolsInit(void) { poolsParse(&enSet, pools_bin, IMAGE_TRUSTED); P = &enSet; }

// Make a language's pools (a WPP1 image of `bytes` bytes) the active ones; NULL or the English image selects English.
// A damaged image is refused (false) and English stays.
bool poolsSet(const u8 *p, size_t bytes)
{
    if (!p || p == pools_bin) { P = &enSet; return true; }
    if (!poolsParse(&altSet, p, bytes)) { altSet.n = 0; P = &enSet; return false; }
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
