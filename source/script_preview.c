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
int scriptPreviewScore, scriptPreviewTurns, scriptPreviewPeels, scriptPreviewHints;
int scriptPreviewThemeCount;
int scriptPreviewShelf;
ScriptRules scriptPreviewRules;
ScriptLedger scriptPreviewLedger;
int scriptPreviewDaily, scriptPreviewDailyDate, scriptPreviewDailyClockError;
int scriptPreviewHelp;
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
    scriptPreviewScore = scriptPreviewTurns = scriptPreviewPeels = scriptPreviewHints = 0;
    scriptPreviewThemeCount = 0;
    scriptPreviewShelf = 0;
    memset(&scriptPreviewRules, 0, sizeof scriptPreviewRules);
    scriptLedgerReset(&scriptPreviewLedger);
    scriptPreviewDaily = scriptPreviewDailyDate = scriptPreviewDailyClockError = 0;
    scriptPreviewHelp = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
}
bool scriptPreviewNewPass(uint32_t seed)
{
    if (!scriptPreviewPools.image || !scriptPreviewDict.image ||
        !scriptPreviewTiles.valid) return false;
    if (scriptPreviewDaily) {
        seed = scriptDailySeed(scriptPreviewDailyDate, scriptPreviewLanguage);
        if (!seed) return false;
        scriptPreviewScore = 0; // retries must not farm score
    }
    scriptPreviewSeed = seed ? seed : 0x87654321u;
    scriptRulesStart(&scriptPreviewRules,
                     scriptPreviewShelf ? (unsigned)scriptPreviewShelf : 1u,
                     scriptPreviewSeed);
    // 5 and 6 are both represented in the tiny fixtures; if a sampled theme
    // lacks that length, the prototype picks its nearest valid word length.
    if (!scriptBoardStartPass(&scriptPreviewBoard, &scriptPreviewDict,
                              &scriptPreviewTiles, &scriptPreviewPools,
                              scriptPreviewSeed, 5)) return false;
    scriptPreviewTurns = scriptPreviewPeels = scriptPreviewHints = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
    scriptLedgerReset(&scriptPreviewLedger);
    // Each generated shelf now guarantees a valid dictionary word from its
    // own theme when possible, instead of always spelling ПРИВЕТ/ПРИВІТ.
    // This includes Daily Practice: identical date + language => same word.
    return scriptBoardPlantThemeWord(&scriptPreviewBoard, &scriptPreviewDict,
                                    &scriptPreviewTiles, &scriptPreviewPools,
                                    scriptPreviewSeed);
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
bool scriptPreviewToggleDaily(int date, uint32_t freeSeed)
{
    if (!scriptPreviewDict.image) return false;
    int next = !scriptPreviewDaily;
    if (next && !scriptDailyValidDate(date)) {
        scriptPreviewDailyClockError = 1;
        return false;
    }
    scriptPreviewDailyClockError = 0;
    scriptPreviewDaily = next;
    scriptPreviewDailyDate = next ? date : 0;
    scriptPreviewShelf = 1;
    scriptPreviewScore = 0;
    return scriptPreviewNewPass(freeSeed);
}
bool scriptPreviewAdvance(uint32_t seed)
{
    if (scriptPreviewDaily) return false; // one unranked repeatable daily shelf
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
        scriptLedgerPeel(&scriptPreviewLedger); // a peel breaks the retail combo
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
bool scriptPreviewHint(void)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING ||
        scriptPreviewHints) return false;
    uint8_t c[SCRIPT_HINT_MAX], r[SCRIPT_HINT_MAX];
    int n = scriptBoardHint(&scriptPreviewBoard, &scriptPreviewDict,
                             &scriptPreviewTiles, c, r);
    if (n < WPD4_MIN_WORD) return false;
    memcpy(scriptPreviewBoard.chainC, c, (size_t)n);
    memcpy(scriptPreviewBoard.chainR, r, (size_t)n);
    scriptPreviewBoard.chainLen = (uint8_t)n;
    scriptPreviewHints++;
    return true;
}
// Words live as compact alphabet IDs, never as variable-length UTF-8 bytes.
// This read-only quote is used only to show the score of a selected chain.
int scriptPreviewQuote(void)
{
    int raw = scriptBoardPreview(&scriptPreviewBoard, &scriptPreviewDict,
                                 &scriptPreviewTiles, NULL);
    int length = scriptPreviewBoard.chainLen;
    if (raw <= 0 || length < WPD4_MIN_WORD || length > SCRIPT_MAX_CHAIN)
        return 0;
    uint8_t ids[SCRIPT_MAX_CHAIN];
    for (int i = 0; i < length; i++) {
        int col = scriptPreviewBoard.chainC[i], row = scriptPreviewBoard.chainR[i];
        if (col >= SCRIPT_COLS || row >= SCRIPT_ROWS) return 0;
        ids[i] = scriptPreviewBoard.tile[col][row];
    }
    return scriptLedgerQuote(&scriptPreviewLedger, ids, length, raw);
}

int scriptPreviewSubmit(void)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return 0;
    int length = scriptPreviewBoard.chainLen;
    uint8_t ids[SCRIPT_MAX_CHAIN] = {0};
    // Capture the current word before Submit removes and refills tiles.
    // The board scorer has authority to reject malformed/invalid chains.
    if (length >= WPD4_MIN_WORD && length <= SCRIPT_MAX_CHAIN)
        for (int i = 0; i < length; i++) {
            int col = scriptPreviewBoard.chainC[i], row = scriptPreviewBoard.chainR[i];
            if (col >= SCRIPT_COLS || row >= SCRIPT_ROWS) return 0;
            ids[i] = scriptPreviewBoard.tile[col][row];
        }
    int tier = 0;
    int points = scriptBoardSubmit(&scriptPreviewBoard, &scriptPreviewDict,
                                   &scriptPreviewTiles, &tier);
    scriptPreviewLastTier = tier;
    if (points > 0) {
        points = scriptLedgerCommit(&scriptPreviewLedger, ids, length, points);
        scriptPreviewScore += points;
        scriptPreviewScore += scriptRulesTurn(&scriptPreviewRules,
                                &scriptPreviewBoard, &scriptPreviewTiles,
                                scriptRulesWordHeat(length), false, false);
        scriptPreviewTurns = scriptPreviewRules.turns;
    }
    scriptPreviewLastScore = points;
    return points;
}
#endif
