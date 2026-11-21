// Actual DS runtime bridge for the WPD4/WPL2/WPP2 symbol-ID board.
// This code only compiles in a separate opt-in cyrillic preview ROM.
// The normal wordPass language menu, saves and gameplay remain unchanged.
#include "script_preview.h"
#ifdef WP_CYR_PREVIEW
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ScriptBoard scriptPreviewBoard;
V4Dict scriptPreviewDict;
V4Tiles scriptPreviewTiles;
V4Pools scriptPreviewPools;
int scriptPreviewLanguage;
int scriptPreviewScore, scriptPreviewTurns, scriptPreviewPeels;
int scriptPreviewThemeCount;
int scriptPreviewLastTier, scriptPreviewLastScore;
uint32_t scriptPreviewSeed;

static unsigned char *images[3];

static unsigned char *readPreview(const char *code, const char *file,
                                  size_t maximum, size_t *size)
{
    char path[70];
    snprintf(path, sizeof path, "nitro:/cyrpreview/%s/%s", code, file);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long length = ftell(fp);
    if (length <= 0 || (unsigned long)length > maximum ||
        fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    unsigned char *data = malloc((size_t)length);
    if (!data) { fclose(fp); return NULL; }
    if (fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data); fclose(fp); return NULL;
    }
    fclose(fp);
    *size = (size_t)length;
    return data;
}
void scriptPreviewClose(void)
{
    for (unsigned i = 0; i < 3; i++) {
        free(images[i]);
        images[i] = NULL;
    }
    memset(&scriptPreviewDict, 0, sizeof scriptPreviewDict);
    memset(&scriptPreviewTiles, 0, sizeof scriptPreviewTiles);
    memset(&scriptPreviewPools, 0, sizeof scriptPreviewPools);
    memset(&scriptPreviewBoard, 0, sizeof scriptPreviewBoard);
    scriptPreviewScore = scriptPreviewTurns = scriptPreviewPeels = 0;
    scriptPreviewThemeCount = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
}
bool scriptPreviewNewPass(uint32_t seed)
{
    if (!scriptPreviewPools.image || !scriptPreviewDict.image ||
        !scriptPreviewTiles.valid) return false;
    scriptPreviewSeed = seed ? seed : 0x87654321u;
    // 5 and 6 are both represented in the tiny fixtures; if a sampled theme
    // lacks that length, the prototype picks its nearest valid word length.
    if (!scriptBoardStartPass(&scriptPreviewBoard, &scriptPreviewDict,
                              &scriptPreviewTiles, &scriptPreviewPools,
                              scriptPreviewSeed, 5)) return false;
    scriptPreviewTurns = scriptPreviewPeels = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
    // A real Russian/Ukrainian common word is laid vertically along the
    // first column so this tiny engineering lexicon is actually playable
    // by stylus, rather than relying on a chance arrangement of 16 words.
    static const uint32_t ru[] = {0x041f,0x0420,0x0418,0x0412,0x0415,0x0422};
    static const uint32_t uk[] = {0x041f,0x0420,0x0418,0x0412,0x0406,0x0422};
    const uint32_t *word = scriptPreviewLanguage ? uk : ru;
    for (unsigned r = 0; r < 6; r++) {
        int id = v4DictSymbol(&scriptPreviewDict, word[r]);
        if (id < 0) return false;
        scriptPreviewBoard.tile[0][r] = (uint8_t)id;
        scriptPreviewBoard.state[0][r] = SCRIPT_TILE;
    }
    return true;
}
bool scriptPreviewOpen(int language, uint32_t seed)
{
    scriptPreviewClose();
    if (language != 0 && language != 1) return false;
    const char *code = language ? "uk" : "ru";
    size_t sizes[3] = {0};
    images[0] = readPreview(code, "dict.bin", 2u << 20, &sizes[0]);
    images[1] = readPreview(code, "tiles.bin", 512u, &sizes[1]);
    images[2] = readPreview(code, "pools.bin", 256u << 10, &sizes[2]);
    if (!images[0] || !images[1] || !images[2] ||
        !v4DictOpen(&scriptPreviewDict, images[0], sizes[0]) ||
        !v4TilesOpen(&scriptPreviewTiles, images[1], sizes[1],
                     &scriptPreviewDict) ||
        !v4PoolsOpen(&scriptPreviewPools, images[2], sizes[2],
                     &scriptPreviewDict) ||
        !v4PoolsMatches(&scriptPreviewPools, &scriptPreviewDict)) {
        scriptPreviewClose();
        return false;
    }
    scriptPreviewLanguage = language;
    scriptPreviewThemeCount = scriptPreviewPools.themes;
    scriptPreviewScore = 0;
    if (!scriptPreviewNewPass(seed)) { scriptPreviewClose(); return false; }
    return true;
}
int scriptPreviewPeel(int c, int r)
{
    if (!scriptPreviewDict.image) return -1;
    int result = scriptBoardPeel(&scriptPreviewBoard, c, r);
    if (result >= 0) {
        scriptPreviewPeels++;
        scriptPreviewTurns++;
        scriptPreviewLastTier = 0;
        scriptPreviewLastScore = 0;
    }
    return result;
}
bool scriptPreviewChain(int c, int r)
{
    if (!scriptPreviewDict.image) return false;
    return scriptBoardAdd(&scriptPreviewBoard, c, r);
}
int scriptPreviewSubmit(void)
{
    if (!scriptPreviewDict.image) return 0;
    int tier = 0;
    int points = scriptBoardSubmit(&scriptPreviewBoard, &scriptPreviewDict,
                                   &scriptPreviewTiles, &tier);
    scriptPreviewLastTier = tier;
    scriptPreviewLastScore = points;
    if (points > 0) {
        scriptPreviewScore += points;
        scriptPreviewTurns++;
    }
    return points;
}
#endif
