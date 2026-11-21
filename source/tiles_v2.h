// WPL2: variable-alphabet tile metadata and deterministic 100-tile bag.
// Parallel groundwork for WPD4; 26-letter board mechanics remain unchanged.
#ifndef WORDPASS_TILES_V2_H
#define WORDPASS_TILES_V2_H
#include "dict_v4.h"
#define WPL2_BAG_SIZE 100
typedef struct {
    uint32_t alphabet[WPD4_MAX_SYMBOLS];
    uint64_t vowels;                    // symbol-ID bitmask
    uint8_t weight[WPD4_MAX_SYMBOLS];
    uint8_t value[WPD4_MAX_SYMBOLS];
    uint8_t rare[4];
    uint8_t symbols, rareCount;
    bool valid;
} V4Tiles;
bool v4TilesOpen(V4Tiles *tiles, const uint8_t *data, size_t bytes,
                 const V4Dict *dictionary);
bool v4TilesVowel(const V4Tiles *tiles, int id);
bool v4TilesFill(const V4Tiles *tiles, uint8_t out[WPL2_BAG_SIZE],
                 uint32_t seed);          // zero heap; deterministic per seed
#endif
