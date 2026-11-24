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
int scriptPreviewRescues, scriptPreviewRescueError;
int scriptPreviewShelf;
ScriptRules scriptPreviewRules;
ScriptLedger scriptPreviewLedger;
int scriptPreviewDaily, scriptPreviewDailyDate, scriptPreviewDailyClockError;
int scriptPreviewHelp;
int scriptPreviewLastTier, scriptPreviewLastScore;
int scriptPreviewCheckpointStatus;
uint32_t scriptPreviewSeed;
uint32_t scriptPreviewPassApostrophe;
int scriptPreviewOpeningWords;
uint32_t scriptPreviewGlyphEpoch;
int scriptPreviewOpenError;
int scriptPreviewCursorActive, scriptPreviewCursorCol, scriptPreviewCursorRow;

bool scriptPreviewFocusClue(int c, int r)
{
    // The gate checks the visible state/survey before reading a count.
    // A covered, unsurveyed gem cannot become a deduction focus.
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING ||
        scriptBoardVisibleClue(&scriptPreviewBoard, c, r) < 0) return false;
    scriptPreviewBoard.clueC = (uint8_t)c;
    scriptPreviewBoard.clueR = (uint8_t)r;
    return true;
}

void scriptPreviewCursorHide(void)
{
    scriptPreviewCursorActive = 0;
}
void scriptPreviewCursorMove(int dc, int dr)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return;
    if (!scriptPreviewCursorActive) {
        // The first controller input focuses the end of an existing hint
        // or chain, otherwise the central board tile. No hidden back data
        // influences navigation. Only a published clue can change focus.
        if (scriptPreviewBoard.chainLen) {
            int tail = scriptPreviewBoard.chainLen - 1;
            scriptPreviewCursorCol = scriptPreviewBoard.chainC[tail];
            scriptPreviewCursorRow = scriptPreviewBoard.chainR[tail];
        } else {
            scriptPreviewCursorCol = SCRIPT_COLS / 2;
            scriptPreviewCursorRow = SCRIPT_ROWS / 2;
        }
        scriptPreviewCursorActive = 1;
    }
    // One bounded step per button edge, keeping the same simple axis rules
    // as the production D-pad. Positions are allowed over opened holes.
    int c = scriptPreviewCursorCol + (dc > 0) - (dc < 0);
    int r = scriptPreviewCursorRow + (dr > 0) - (dr < 0);
    if (c < 0) c = 0;
    if (c >= SCRIPT_COLS) c = SCRIPT_COLS - 1;
    if (r < 0) r = 0;
    if (r >= SCRIPT_ROWS) r = SCRIPT_ROWS - 1;
    scriptPreviewCursorCol = c;
    scriptPreviewCursorRow = r;
    (void)scriptPreviewFocusClue(c, r);
}
bool scriptPreviewCursorSelect(void)
{
    if (!scriptPreviewCursorActive || !scriptPreviewDict.image ||
        scriptPreviewRules.status != SCRIPT_RUNNING) return false;
    // Both cursor A and a second fresh stylus tap use the exact same
    // dictionary-validated submit operation, so one input cannot score a
    // rejected word or spend turns that the other input would refuse.
    if (scriptPreviewTrySubmitTail(scriptPreviewCursorCol,
                                   scriptPreviewCursorRow)) return true;
    return scriptPreviewChain(scriptPreviewCursorCol, scriptPreviewCursorRow);
}
int scriptPreviewCursorPeel(void)
{
    if (!scriptPreviewCursorActive) return -1;
    return scriptPreviewPeel(scriptPreviewCursorCol, scriptPreviewCursorRow);
}
bool scriptPreviewCursorBacktrack(void)
{
    if (!scriptPreviewBoard.chainLen ||
        scriptPreviewRules.status != SCRIPT_RUNNING) return false;
    // B undoes a front-letter chain regardless of whether the last cell was
    // chosen by a cursor or by the stylus. Switching input methods should
    // never make B unexpectedly quit an unfinished Russian/Ukrainian word.
    scriptPreviewBoard.chainLen--;
    return true;
}

static unsigned char *images[3];
// Only an advance banks a completed shelf. Retrying/re-dealing rolls this
// shelf back without losing points earned on earlier completed shelves.
static int shelfStartScore;

static unsigned char *readPreview(const char *code, const char *file,
                                  size_t maximum, size_t *size)
{
    char path[70];
    static const char prefix[] = "nitro:/cyrpreview/";
    size_t prefixLen = sizeof prefix - 1;
    size_t codeLen = strlen(code), fileLen = strlen(file);
    if (prefixLen + codeLen + 1 + fileLen + 1 > sizeof path) return NULL;
    memcpy(path, prefix, prefixLen);
    memcpy(path + prefixLen, code, codeLen);
    path[prefixLen + codeLen] = '/';
    memcpy(path + prefixLen + codeLen + 1, file, fileLen + 1);
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
    shelfStartScore = 0;
    scriptPreviewRescues = scriptPreviewRescueError = 0;
    scriptPreviewThemeCount = 0;
    scriptPreviewShelf = 0;
    memset(&scriptPreviewRules, 0, sizeof scriptPreviewRules);
    scriptLedgerReset(&scriptPreviewLedger);
    scriptPreviewDaily = scriptPreviewDailyDate = scriptPreviewDailyClockError = 0;
    scriptPreviewHelp = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
    scriptPreviewOpeningWords = 0;
    scriptPreviewPassApostrophe = 0;
    scriptPreviewCheckpointStatus = SCRIPT_CHECKPOINT_NONE;
    scriptPreviewCursorActive = scriptPreviewCursorCol = scriptPreviewCursorRow = 0;
}
static bool startPass(unsigned shelf, uint32_t seed, int bankedScore)
{
    if (!scriptPreviewPools.image || !scriptPreviewDict.image ||
        !scriptPreviewTiles.valid) return false;
    if (scriptPreviewDaily) {
        seed = scriptDailySeed(scriptPreviewDailyDate, scriptPreviewLanguage);
        if (!seed) return false;
        bankedScore = 0;
    }
    seed = seed ? seed : 0x87654321u;
    ScriptBoard nextBoard;
    ScriptRules nextRules;
    scriptRulesStart(&nextRules, shelf, seed);
    // Scale password length across shelves using only attested words from
    // the selected theme. Small draft pools fall back to their nearest
    // available length; sourced RU/UK packs can exercise 4..9-letter passes.
    int target = scriptRulesPassTarget(shelf, seed);
    if (!scriptBoardStartPass(&nextBoard, &scriptPreviewDict,
                              &scriptPreviewTiles, &scriptPreviewPools,
                              seed, target)) return false;
    uint32_t passMask;
    if (!v4DictLookupSpellingIds(&scriptPreviewDict, nextBoard.pass,
                                nextBoard.passLen, &passMask)) return false;
    // Generate up to two distinct legal opening words; explicit L+R rescue
    // still plants only one word after charging its normal turn and heat.
    int openingWords = scriptBoardPlantOpeningWords(
        &nextBoard, &scriptPreviewDict, &scriptPreviewTiles,
        &scriptPreviewPools, seed, NULL, NULL, NULL);
    if (!openingWords) return false;
    // Generate both the hidden password and a playable word before replacing
    // a live shelf. A refused generation must not erase its score or state.
    scriptPreviewBoard = nextBoard;
    scriptPreviewPassApostrophe = passMask;
    scriptPreviewOpeningWords = openingWords;
    scriptPreviewRules = nextRules;
    scriptPreviewShelf = nextRules.shelf;
    scriptPreviewSeed = seed;
    shelfStartScore = scriptPreviewScore = bankedScore;
    scriptPreviewTurns = scriptPreviewPeels = scriptPreviewHints = 0;
    scriptPreviewRescues = scriptPreviewRescueError = 0;
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
    scriptPreviewCheckpointStatus = SCRIPT_CHECKPOINT_NONE;
    scriptLedgerReset(&scriptPreviewLedger);
    scriptPreviewCursorActive = scriptPreviewCursorCol = scriptPreviewCursorRow = 0;
    // Each generated shelf now guarantees a valid dictionary word from its
    // own theme when possible, instead of always spelling ПРИВЕТ/ПРИВІТ.
    // This includes Daily Practice: identical date + language => same word.
    return true;
}
bool scriptPreviewNewPass(uint32_t seed)
{
    return startPass((unsigned)scriptPreviewShelf, seed, shelfStartScore);
}
int scriptPreviewCheckpoint(bool save, const char *settingsPath)
{
    int result;
    if (scriptPreviewDaily)
        result = SCRIPT_CHECKPOINT_DAILY;
    else if (!settingsPath)
        result = SCRIPT_CHECKPOINT_NO_CARD;
    else if (!scriptPreviewDict.image || !scriptPreviewPools.image ||
             !scriptPreviewTiles.valid || scriptPreviewShelf < 1)
        result = SCRIPT_CHECKPOINT_IO_ERROR;
    else {
        uint32_t tag = scriptCheckpointTag(&scriptPreviewDict,
                                           &scriptPreviewTiles, &scriptPreviewPools);
        if (save) {
            // No partially played shelf or unbanked pass bonus is saved.
            // Finish a shelf, press A to advance, and then checkpoint.
            if (scriptPreviewRules.status != SCRIPT_RUNNING ||
                scriptPreviewTurns || scriptPreviewScore != shelfStartScore)
                result = SCRIPT_CHECKPOINT_NOT_START;
            else {
                ScriptCheckpoint record = {
                    .language = (unsigned)scriptPreviewLanguage, .pack = tag,
                    .shelf = (unsigned)scriptPreviewShelf, .seed = scriptPreviewSeed,
                    .score = (unsigned)shelfStartScore
                };
                result = scriptCheckpointStore(settingsPath, &record) ?
                    SCRIPT_CHECKPOINT_SAVED : SCRIPT_CHECKPOINT_IO_ERROR;
            }
        } else {
            ScriptCheckpoint record;
            if (!scriptCheckpointLoad(settingsPath, (unsigned)scriptPreviewLanguage,
                                      tag, &record))
                result = SCRIPT_CHECKPOINT_NO_RECORD;
            else {
                result = startPass(record.shelf, record.seed, (int)record.score) ?
                    SCRIPT_CHECKPOINT_LOADED : SCRIPT_CHECKPOINT_IO_ERROR;
            }
        }
    }
    scriptPreviewCheckpointStatus = result;
    return result;
}

bool scriptPreviewOpen(int language, uint32_t seed)
{
    scriptPreviewClose();
    scriptPreviewOpenError = 0;
    if (language != 0 && language != 1) {
        scriptPreviewOpenError = 1;
        return false;
    }
    const char *code = language ? "uk" : "ru";
    size_t sizes[3] = {0};
    images[0] = readPreview(code, "dict.bin", 2u << 20, &sizes[0]);
    images[1] = readPreview(code, "tiles.bin", 512u, &sizes[1]);
    images[2] = readPreview(code, "pools.bin", 256u << 10, &sizes[2]);
    // Diagnostic stages separate missing/FAT/malloc reads from validation
    // and gameplay generation. These values are never rendered as secrets.
    int error = !images[0] ? 2 : !images[1] ? 3 : !images[2] ? 4 : 0;
    if (!error && !v4DictOpen(&scriptPreviewDict, images[0], sizes[0])) error = 5;
    if (!error && !v4TilesOpen(&scriptPreviewTiles, images[1], sizes[1],
                                &scriptPreviewDict)) error = 6;
    if (!error && !v4PoolsOpen(&scriptPreviewPools, images[2], sizes[2],
                                &scriptPreviewDict)) error = 7;
    if (!error && !v4PoolsMatches(&scriptPreviewPools, &scriptPreviewDict))
        error = 8;
    if (error) {
        scriptPreviewClose();
        scriptPreviewOpenError = error;
        return false;
    }
    scriptPreviewLanguage = language;
    scriptPreviewShelf = 1;
    scriptPreviewThemeCount = scriptPreviewPools.themes;
    scriptPreviewScore = 0;
    if (!scriptPreviewNewPass(seed)) {
        scriptPreviewClose();
        scriptPreviewOpenError = 9;
        return false;
    }
    // Invalidate cached glyphs on each *successful* dictionary load, even
    // when malloc reuses an earlier image address.
    scriptPreviewGlyphEpoch++;
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
    int previousDate = scriptPreviewDailyDate;
    scriptPreviewDaily = next;
    scriptPreviewDailyDate = next ? date : 0;
    if (!startPass(1, freeSeed, 0)) {
        scriptPreviewDaily = !next;
        scriptPreviewDailyDate = previousDate;
        return false;
    }
    scriptPreviewDailyClockError = 0;
    return true;
}
bool scriptPreviewAdvance(uint32_t seed)
{
    if (scriptPreviewDaily) return false; // one unranked repeatable daily shelf
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_PASSED)
        return false;
    if (scriptPreviewShelf >= 999) return false;
    // Bank this completed shelf only when its successor is ready. Replays
    // discard the current attempt's words/bonus, keeping earlier shelves.
    unsigned nextShelf = (unsigned)scriptPreviewShelf + 1;
    return startPass(nextShelf, seed ^ (nextShelf * 0x9e3779b9u),
                     scriptPreviewScore);
}
int scriptPreviewPeel(int c, int r)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return -1;
    int result = scriptBoardPeel(&scriptPreviewBoard, c, r);
    if (result >= 0 || result == -2) {
        scriptPreviewRescueError = 0; // dismiss stale recovery warnings
        // A nonfatal blank peel creates a real numbered hole. Show its
        // neighbor evidence immediately, without waiting for another word.
        if (result == 0) (void)scriptPreviewFocusClue(c, r);
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
    bool added = scriptBoardAdd(&scriptPreviewBoard, c, r);
    if (added) scriptPreviewRescueError = 0;
    return added;
}
bool scriptPreviewTrySubmitTail(int c, int r)
{
    const ScriptBoard *b = &scriptPreviewBoard;
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING ||
        b->chainLen < WPD4_MIN_WORD || b->chainLen > SCRIPT_MAX_CHAIN)
        return false;
    int tail = b->chainLen - 1;
    if (b->chainC[tail] != c || b->chainR[tail] != r) return false;
    // Only accepted words count. Invalid/short/non-tail taps never consume a
    // turn, heat, clue or score, and keep the chain for correction.
    return scriptPreviewSubmit() > 0;
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
    // The next plain A should submit the hinted word, not append an old
    // controller focus left over from a previous attempted chain.
    scriptPreviewCursorHide();
    scriptPreviewRescueError = 0;
    scriptPreviewHints++;
    return true;
}
bool scriptPreviewRescue(void)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING ||
        scriptBoardPassComplete(&scriptPreviewBoard))
        return false;
    if (!scriptPreviewRules.turns) {
        scriptPreviewRescueError = 1; // use a normal word/peel first
        return false;
    }
    if (scriptPreviewRescues) {
        scriptPreviewRescueError = 2; // exactly one per shelf
        return false;
    }
    uint32_t seed = scriptPreviewSeed ^
                    ((uint32_t)(scriptPreviewRules.turns + 1u) * 0x9e3779b9u);
    // Preflight the current board so an already blocked rescue never spends
    // time, even on a fatal fire tick. Both copies are small fixed-size structs.
    ScriptBoard nextBoard = scriptPreviewBoard;
    if (!scriptBoardPlantThemeWord(&nextBoard, &scriptPreviewDict,
                                   &scriptPreviewTiles, &scriptPreviewPools, seed)) {
        scriptPreviewRescueError = 3; // board too obstructed, spend no turn
        return false;
    }
    // Let this action's normal fire/gravity settle BEFORE planting. Otherwise
    // gravity can split the bend of a six-symbol word in the very same action
    // that promises to recover a playable chain. Stage the turn as well: if a
    // new ignition blocks the final route, refuse without charging anything.
    nextBoard = scriptPreviewBoard;
    ScriptRules nextRules = scriptPreviewRules;
    (void)scriptRulesRescueTurn(&nextRules, &nextBoard, &scriptPreviewTiles);
    if (nextRules.status == SCRIPT_RUNNING &&
        !scriptBoardPlantThemeWord(&nextBoard, &scriptPreviewDict,
                                   &scriptPreviewTiles, &scriptPreviewPools, seed)) {
        scriptPreviewRescueError = 3;
        return false;
    }
    // A permitted recovery is still a real turn. Commit fatal fire as failed;
    // it must not become an escape from the shelf's normal loss condition.
    scriptPreviewBoard = nextBoard;
    scriptPreviewRules = nextRules;
    scriptBoardClearChain(&scriptPreviewBoard);
    scriptPreviewRescues = 1;
    scriptPreviewRescueError = 0;
    scriptLedgerPeel(&scriptPreviewLedger); // any recovery breaks the combo
    scriptPreviewLastTier = scriptPreviewLastScore = 0;
    scriptPreviewTurns = scriptPreviewRules.turns;
    return true;
}
// Words live as compact alphabet IDs, never as variable-length UTF-8 bytes.
// This read-only quote is used only to show the score of a selected chain.
int scriptPreviewQuoteSpelling(int *tier, uint32_t *mask)
{
    if (tier) *tier = 0;
    if (mask) *mask = 0;
    int wordTier = 0;
    int raw = scriptBoardPreviewSpelling(&scriptPreviewBoard, &scriptPreviewDict,
                                 &scriptPreviewTiles, &wordTier, mask);
    int length = scriptPreviewBoard.chainLen;
    if (raw <= 0 || length < WPD4_MIN_WORD || length > SCRIPT_MAX_CHAIN)
        return 0;
    uint8_t ids[SCRIPT_MAX_CHAIN];
    for (int i = 0; i < length; i++) {
        int col = scriptPreviewBoard.chainC[i], row = scriptPreviewBoard.chainR[i];
        if (col >= SCRIPT_COLS || row >= SCRIPT_ROWS) return 0;
        ids[i] = scriptPreviewBoard.tile[col][row];
    }
    int points = scriptLedgerQuote(&scriptPreviewLedger, ids, length, raw);
    if (points > 0 && tier) *tier = wordTier;
    return points;
}
int scriptPreviewQuote(int *tier)
{
    return scriptPreviewQuoteSpelling(tier, NULL);
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
        scriptPreviewRescueError = 0;
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
