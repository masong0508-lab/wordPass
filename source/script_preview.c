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
int scriptPreviewShelf;
ScriptRules scriptPreviewRules;
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
    scriptPreviewShelf = 0;
    memset(&scriptPreviewRules, 0, sizeof scriptPreviewRules);
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
}
bool scriptPreviewNewPass(uint32_t seed)
{
    if (!scriptPreviewPools.image || !scriptPreviewDict.image ||
        !scriptPreviewTiles.valid) return false;
    scriptPreviewSeed = seed ? seed : 0x87654321u;
    scriptRulesStart(&scriptPreviewRules,
                     scriptPreviewShelf ? (unsigned)scriptPreviewShelf : 1u,
                     scriptPreviewSeed);
    // 5 and 6 are both represented in the tiny fixtures; if a sampled theme
    // lacks that length, the prototype picks its nearest valid word length.
    if (!scriptBoardStartPass(&scriptPreviewBoard, &scriptPreviewDict,
                              &scriptPreviewTiles, &scriptPreviewPools,
                              scriptPreviewSeed, 5)) return false;
    scriptPreviewTurns = scriptPreviewPeels = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
    // Guarantee one real Cyrillic word that can be chained on the DS, even
    // when an external legally sourced Hunspell pack excludes the old
    // sample ПРИВЕТ / ПРИВІТ. The tiny built-in fixtures retain that exact
    // six-letter demonstration for deterministic emulator screenshots.
    static const uint32_t ru[] = {0x041f,0x0420,0x0418,0x0412,0x0415,0x0422};
    static const uint32_t uk[] = {0x041f,0x0420,0x0418,0x0412,0x0406,0x0422};
    const uint32_t *word = scriptPreviewLanguage ? uk : ru;
    uint8_t ids[WPP2_MAX_LEN] = {0};
    int len = 6;
    bool known = true;
    for (int i = 0; i < len; i++) {
        int id = v4DictSymbol(&scriptPreviewDict, word[i]);
        if (id < 0) { known = false; break; }
        ids[i] = (uint8_t)id;
    }
    if (known) known = v4DictLookupIds(&scriptPreviewDict, ids, len) != 0;
    if (!known) {
        static const uint8_t preferred[] = {6, 5, 4, 7, 8, 9};
        len = 0;
        for (int theme = 0; theme < scriptPreviewPools.themes && !len; theme++)
            for (unsigned k = 0; k < sizeof preferred; k++) {
                int n = preferred[k];
                if (v4PoolsCount(&scriptPreviewPools, theme, n) > 0 &&
                    v4PoolsWord(&scriptPreviewPools, theme, n, 0, ids)) {
                    len = n;
                    break;
                }
            }
        if (!len) return false;
    }
    for (int i = 0; i < len; i++) {
        // For a rare nine-symbol seed, the ninth gem sits on the hex
        // neighbor at (1,6), avoiding the clipped odd-column bottom row.
        int c = i < SCRIPT_ROWS ? 0 : 1;
        int r = i < SCRIPT_ROWS ? i : SCRIPT_ROWS - 2;
        scriptPreviewBoard.tile[c][r] = ids[i];
        scriptPreviewBoard.state[c][r] = SCRIPT_TILE;
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
    scriptPreviewShelf = 1;
    scriptPreviewThemeCount = scriptPreviewPools.themes;
    scriptPreviewScore = 0;
    if (!scriptPreviewNewPass(seed)) { scriptPreviewClose(); return false; }
    return true;
}
bool scriptPreviewAdvance(uint32_t seed)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_PASSED)
        return false;
    if (scriptPreviewShelf >= 999) return false;
    // Keep the cumulative score across shelves; each new pass resets
    // only heat, fire, peels and turn counters, never the release save.
    scriptPreviewShelf++;
    if (!scriptPreviewNewPass(seed ^ ((uint32_t)scriptPreviewShelf * 0x9e3779b9u))) {
        scriptPreviewShelf--;
        return false;
    }
    return true;
}
int scriptPreviewPeel(int c, int r)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return -1;
    int result = scriptBoardPeel(&scriptPreviewBoard, c, r);
    if (result >= 0 || result == -2) {
        scriptPreviewPeels++;
        scriptPreviewLastTier = scriptPreviewLastScore = 0;
        int bonus = scriptRulesTurn(&scriptPreviewRules, &scriptPreviewBoard,
                                    &scriptPreviewTiles, 1, true, result == -2);
        scriptPreviewTurns = scriptPreviewRules.turns;
        scriptPreviewScore += bonus;
    }
    return result;
}
bool scriptPreviewChain(int c, int r)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return false;
    return scriptBoardAdd(&scriptPreviewBoard, c, r);
}
int scriptPreviewSubmit(void)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return 0;
    int length = scriptPreviewBoard.chainLen;
    int tier = 0;
    int points = scriptBoardSubmit(&scriptPreviewBoard, &scriptPreviewDict,
                                   &scriptPreviewTiles, &tier);
    scriptPreviewLastTier = tier;
    scriptPreviewLastScore = points;
    if (points > 0) {
        scriptPreviewScore += points;
        scriptPreviewScore += scriptRulesTurn(&scriptPreviewRules,
                                &scriptPreviewBoard, &scriptPreviewTiles,
                                scriptRulesWordHeat(length), false, false);
        scriptPreviewTurns = scriptPreviewRules.turns;
    }
    return points;
}
#endif
