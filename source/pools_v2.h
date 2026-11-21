// WPP2: versioned Cyrillic themed pass-word pools, backed by a WPD4 alphabet.
// Safe parallel format; the 11 shipped languages still use WPP1.
#ifndef WORDPASS_POOLS_V2_H
#define WORDPASS_POOLS_V2_H
#include "dict_v4.h"

#define WPP2_MAX_THEMES 48
#define WPP2_NAME_BYTES 24
#define WPP2_MIN_LEN 4
#define WPP2_MAX_LEN 9
#define WPP2_LENGTHS (WPP2_MAX_LEN - WPP2_MIN_LEN + 1)

typedef struct {
    const uint8_t *image;                        // bytes owned by caller
    const char *name[WPP2_MAX_THEMES];           // NUL-terminated UTF-8
    const uint8_t *base[WPP2_MAX_THEMES][WPP2_LENGTHS];
    uint16_t count[WPP2_MAX_THEMES][WPP2_LENGTHS];
    uint8_t themes, symbols;
} V4Pools;

// On failure image stays NULL and no half-validated pool is selectable.
// Validation includes checksum, exact size, UTF-8 theme names, alphabet parity,
// sorted unique pass words, symbol bounds and WPD4 dictionary membership.
bool v4PoolsOpen(V4Pools *view, const uint8_t *data, size_t size,
                 const V4Dict *dict);
bool v4PoolsMatches(const V4Pools *view, const V4Dict *dictionary);
int v4PoolsCount(const V4Pools *view, int theme, int len);
const char *v4PoolsName(const V4Pools *view, int theme);
bool v4PoolsWord(const V4Pools *view, int theme, int len, int index,
                 uint8_t out[WPP2_MAX_LEN]);
#endif
