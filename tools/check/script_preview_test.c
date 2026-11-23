// Exercise the actual preview action bridge with the 40-theme draft packs.
// Lower-level board/rules tests cannot catch the ordering between them.
#include "script_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(expr, ...) do { if (!(expr)) { failures++; printf("FAIL "); \
    printf(__VA_ARGS__); putchar('\n'); } } while (0)

static uint8_t *readFile(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) {
        if (file) fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    uint8_t *data = malloc((size_t)length);
    if (!data) { fclose(file); return NULL; }
    bool read = fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!read) { free(data); return NULL; }
    *size = (size_t)length;
    return data;
}

static void start(unsigned seed, unsigned turns, unsigned tick)
{
    CHECK(scriptPreviewNewPass(seed), "start real preview shelf seed %u", seed);
    CHECK(scriptPreviewOpeningWords >= 1 && scriptPreviewOpeningWords <= 2,
          "new Russian/Ukrainian shelf plants one or two real words");
    scriptPreviewRules.turns = (uint16_t)turns;
    scriptPreviewRules.burnTick = (uint8_t)tick;
    scriptPreviewTurns = (int)turns;
    scriptPreviewScore = 900;
    scriptPreviewHints = 1;
    scriptPreviewLastScore = 120;
    scriptPreviewLastTier = 1;
    uint8_t ids[3] = {0, 1, 2};
    (void)scriptLedgerCommit(&scriptPreviewLedger, ids, 3, 120);
}

static int hintable(const ScriptBoard *board)
{
    uint8_t columns[SCRIPT_HINT_MAX], rows[SCRIPT_HINT_MAX];
    int length = scriptBoardHint(board, &scriptPreviewDict, &scriptPreviewTiles,
                                 columns, rows);
    if (!length) return 0;
    ScriptBoard selected = *board;
    memcpy(selected.chainC, columns, (size_t)length);
    memcpy(selected.chainR, rows, (size_t)length);
    selected.chainLen = (uint8_t)length;
    CHECK(scriptBoardPreview(&selected, &scriptPreviewDict,
                              &scriptPreviewTiles, NULL) > 0,
          "returned rescue hint is a real scoreable hex chain");
    return length;
}

static void refused(int error, const char *why)
{
    ScriptBoard board = scriptPreviewBoard;
    ScriptRules rules = scriptPreviewRules;
    ScriptLedger ledger = scriptPreviewLedger;
    int score = scriptPreviewScore, turns = scriptPreviewTurns;
    int hints = scriptPreviewHints, rescues = scriptPreviewRescues;
    int lastScore = scriptPreviewLastScore, lastTier = scriptPreviewLastTier;
    CHECK(!scriptPreviewRescue(), "%s refuses the recovery", why);
    CHECK(!memcmp(&board, &scriptPreviewBoard, sizeof board) &&
          !memcmp(&rules, &scriptPreviewRules, sizeof rules) &&
          !memcmp(&ledger, &scriptPreviewLedger, sizeof ledger) &&
          score == scriptPreviewScore && turns == scriptPreviewTurns &&
          hints == scriptPreviewHints && rescues == scriptPreviewRescues &&
          lastScore == scriptPreviewLastScore && lastTier == scriptPreviewLastTier,
          "%s leaves gameplay, score, RNG and allowances unchanged", why);
    CHECK(scriptPreviewRescueError == error, "%s reports expected error", why);
}

// Audit actual shelf-length selection against the real source-derived
// WPP2 theme counts rather than just testing the pure requested target.
// A theme may lack a 4/9-letter option, so the existing nearest legal word
// fallback (shorter before longer) must remain deterministic.
static int nearestThemeLength(int theme, int requested)
{
    for (int d = 0; d <= WPP2_MAX_LEN - WPP2_MIN_LEN; d++) {
        int shorter = requested - d, longer = requested + d;
        if (shorter >= WPP2_MIN_LEN &&
            v4PoolsCount(&scriptPreviewPools, theme, shorter)) return shorter;
        if (longer <= WPP2_MAX_LEN &&
            v4PoolsCount(&scriptPreviewPools, theme, longer)) return longer;
    }
    return 0;
}
static void passLengthProgression(void)
{
    const unsigned shelves[] = {1, 3, 4, 9, 10, 19, 20, 34, 35, 55};
    for (unsigned i = 0; i < sizeof shelves / sizeof shelves[0]; i++) {
        scriptPreviewShelf = (int)shelves[i];
        for (unsigned variant = 0; variant < 16; variant++) {
            uint32_t seed = 81u + variant * 7919u + shelves[i] * 37u;
            CHECK(scriptPreviewNewPass(seed),
                  "shelf %u seed %u starts a complete Cyrillic password",
                  shelves[i], seed);
            int requested = scriptRulesPassTarget(shelves[i], scriptPreviewSeed);
            int actual = nearestThemeLength(scriptPreviewBoard.passTheme,
                                            requested);
            CHECK(scriptPreviewBoard.passLen == actual &&
                  actual >= WPP2_MIN_LEN && actual <= WPP2_MAX_LEN &&
                  scriptPreviewRules.shelf == shelves[i],
                  "shelf %u selects nearest source-backed pass length %d",
                  shelves[i], actual);
            CHECK(scriptPreviewOpeningWords >= 1 &&
                  scriptPreviewOpeningWords <= 2 &&
                  hintable(&scriptPreviewBoard) >= WPD4_MIN_WORD,
                  "staged length keeps at least one playable real word");
        }
    }
    scriptPreviewShelf = 1;
    CHECK(scriptPreviewNewPass(617), "reset test to first shelf");
}

static void retryScores(void)
{
    CHECK(scriptPreviewNewPass(314), "start a fresh score/retry scenario");
    CHECK(scriptPreviewHint(), "score/retry scenario has a real hinted word");
    ScriptBoard beforeQuote = scriptPreviewBoard;
    ScriptRules rulesBeforeQuote = scriptPreviewRules;
    ScriptLedger ledgerBeforeQuote = scriptPreviewLedger;
    int tier = -1;
    int quoted = scriptPreviewQuote(&tier);
    CHECK(quoted > 0 && (tier == 1 || tier == 2) &&
          !memcmp(&beforeQuote, &scriptPreviewBoard, sizeof beforeQuote) &&
          !memcmp(&rulesBeforeQuote, &scriptPreviewRules, sizeof rulesBeforeQuote) &&
          !memcmp(&ledgerBeforeQuote, &scriptPreviewLedger, sizeof ledgerBeforeQuote),
          "score and tier quote is read-only for a real hinted word");
    int points = scriptPreviewSubmit();
    CHECK(points == quoted && scriptPreviewScore == points &&
          scriptPreviewLastTier == tier,
          "a first-shelf word awards the exact quoted points and tier");
    tier = -1;
    CHECK(scriptPreviewQuote(&tier) == 0 && tier == 0,
          "an empty chain clears its score and tier quote");
    CHECK(scriptPreviewNewPass(315) && scriptPreviewScore == 0 &&
          scriptPreviewLedger.count == 0 && scriptPreviewHints == 0,
          "re-dealing shelf one discards its attempt score and history");

    // Complete an actual password, preserving earned points only when A
    // advances to the next shelf. Replay must not bank the same pass bonus.
    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = 0; r < SCRIPT_ROWS; r++)
            if (scriptPreviewBoard.back[c][r])
                CHECK(scriptPreviewPeel(c, r) > 0, "reveal an actual password tile");
    int banked = scriptPreviewScore;
    CHECK(scriptPreviewRules.status == SCRIPT_PASSED && banked > 0,
          "finishing a password earns a real shelf bonus");
    CHECK(scriptPreviewAdvance(316) && scriptPreviewShelf == 2 &&
          scriptPreviewScore == banked, "advancing banks the completed shelf score");
    CHECK(scriptPreviewHint() && scriptPreviewSubmit() > 0 &&
          scriptPreviewScore > banked, "next-shelf word adds temporary attempt points");
    CHECK(scriptPreviewNewPass(317) && scriptPreviewScore == banked,
          "re-dealing keeps completed shelves but discards this attempt's points");
    CHECK(scriptPreviewHint() && scriptPreviewSubmit() > 0,
          "failed retry scenario starts with actual word points");
    scriptPreviewRules.status = SCRIPT_FAILED;
    CHECK(scriptPreviewNewPass(318) && scriptPreviewScore == banked &&
          scriptPreviewRules.status == SCRIPT_RUNNING,
          "retrying a failed shelf cannot farm its word points");

    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = 0; r < SCRIPT_ROWS; r++)
            if (scriptPreviewBoard.back[c][r])
                CHECK(scriptPreviewPeel(c, r) > 0, "finish second real password");
    CHECK(scriptPreviewRules.status == SCRIPT_PASSED && scriptPreviewScore > banked,
          "completed second shelf displays its new bonus");
    CHECK(scriptPreviewNewPass(319) && scriptPreviewScore == banked,
          "replaying a completed shelf discards its unbanked bonus");

    CHECK(scriptPreviewToggleDaily(20261121, 320) && scriptPreviewScore == 0 &&
          scriptPreviewShelf == 1 && scriptPreviewDaily,
          "Daily practice starts independent of banked free-play score");
    ScriptBoard daily = scriptPreviewBoard;
    CHECK(scriptPreviewHint() && scriptPreviewSubmit() > 0,
          "Daily practice has a scoreable real word");
    CHECK(scriptPreviewNewPass(321) && scriptPreviewScore == 0 &&
          !memcmp(&daily, &scriptPreviewBoard, sizeof daily),
          "Daily retry restores exactly the same board and zero score");
    CHECK(scriptPreviewToggleDaily(20261121, 322) && !scriptPreviewDaily &&
          scriptPreviewShelf == 1 && scriptPreviewScore == 0,
          "leaving Daily starts a fresh free-play score bank");
}

static void failedGeneration(void)
{
    CHECK(scriptPreviewNewPass(420) && scriptPreviewHint(),
          "generation failure starts on a live selected word");
    ScriptBoard board = scriptPreviewBoard;
    ScriptRules rules = scriptPreviewRules;
    ScriptLedger ledger = scriptPreviewLedger;
    int score = scriptPreviewScore, shelf = scriptPreviewShelf;
    int openingWords = scriptPreviewOpeningWords;
    uint32_t seed = scriptPreviewSeed;
    // Simulate an unavailable pool after play began. Starting a replacement
    // must fail without resetting the old clock/board/selection or score.
    unsigned themes = scriptPreviewPools.themes;
    scriptPreviewPools.themes = 0;
    CHECK(!scriptPreviewNewPass(421) && scriptPreviewSeed == seed &&
          scriptPreviewScore == score && scriptPreviewShelf == shelf &&
          scriptPreviewOpeningWords == openingWords &&
          !memcmp(&board, &scriptPreviewBoard, sizeof board) &&
          !memcmp(&rules, &scriptPreviewRules, sizeof rules) &&
          !memcmp(&ledger, &scriptPreviewLedger, sizeof ledger),
          "failed re-deal preserves the previous complete attempt");
    CHECK(!scriptPreviewToggleDaily(20261121, 422) && !scriptPreviewDaily &&
          scriptPreviewDailyDate == 0 && scriptPreviewScore == score &&
          scriptPreviewSeed == seed && scriptPreviewShelf == shelf &&
          !memcmp(&board, &scriptPreviewBoard, sizeof board),
          "failed Daily switch restores the previous mode and attempt");
    scriptPreviewRules.status = SCRIPT_PASSED;
    rules = scriptPreviewRules;
    CHECK(!scriptPreviewAdvance(423) && scriptPreviewSeed == seed &&
          scriptPreviewScore == score && scriptPreviewShelf == shelf &&
          !memcmp(&board, &scriptPreviewBoard, sizeof board) &&
          !memcmp(&rules, &scriptPreviewRules, sizeof rules),
          "failed advance leaves the completed shelf ready to retry");
    scriptPreviewPools.themes = (uint16_t)themes;
}

static void exercise(const char *dictPath, const char *tilesPath,
                     const char *poolsPath, int language)
{
    size_t ds = 0, ts = 0, ps = 0;
    uint8_t *dict = readFile(dictPath, &ds), *tiles = readFile(tilesPath, &ts);
    uint8_t *pools = readFile(poolsPath, &ps);
    scriptPreviewClose();
    bool valid = dict && tiles && pools &&
        v4DictOpen(&scriptPreviewDict, dict, ds) &&
        v4TilesOpen(&scriptPreviewTiles, tiles, ts, &scriptPreviewDict) &&
        v4PoolsOpen(&scriptPreviewPools, pools, ps, &scriptPreviewDict);
    CHECK(valid, "load real draft pack for preview bridge language %d", language);
    if (!valid) { free(dict); free(tiles); free(pools); return; }
    scriptPreviewLanguage = language;
    scriptPreviewShelf = 1;
    passLengthProgression();

    retryScores();
    failedGeneration();

    start(66, 0, 0);
    refused(1, "first-turn guard");

    // Regression: seed 66 in the Russian pack plants a six-symbol bend.
    // A flame below it makes gravity shift just one arm of the chain.
    // Planting before this turn used to return success with no playable word.
    start(66, 3, 3);
    scriptPreviewBoard.state[1][5] = SCRIPT_BURN;
    ScriptBoard expected = scriptPreviewBoard;
    ScriptRules expectedRules = scriptPreviewRules;
    (void)scriptRulesRescueTurn(&expectedRules, &expected, &scriptPreviewTiles);
    ScriptLedger expectedLedger = scriptPreviewLedger;
    scriptLedgerPeel(&expectedLedger);
    CHECK(scriptPreviewRescue(), "accept rescue on a moving-fire turn");
    CHECK(scriptPreviewRules.status == SCRIPT_RUNNING &&
          scriptPreviewTurns == 4 && scriptPreviewRules.turns == 4 &&
          scriptPreviewRules.heat == 2 && scriptPreviewRules.burnTick == 0 &&
          scriptPreviewRules.words == 0 && scriptPreviewRules.peels == 0,
          "rescue advances fire and heat exactly once without word/peel stats");
    CHECK(hintable(&scriptPreviewBoard) >= WPD4_MIN_WORD,
          "a live rescue still has a playable word AFTER fire and gravity");
    CHECK(!memcmp(expected.back, scriptPreviewBoard.back, sizeof expected.back) &&
          !memcmp(expected.found, scriptPreviewBoard.found, sizeof expected.found) &&
          !memcmp(expected.bag, scriptPreviewBoard.bag, sizeof expected.bag) &&
          !memcmp(expected.state, scriptPreviewBoard.state, sizeof expected.state) &&
          expected.seed == scriptPreviewBoard.seed &&
          expected.bagPos == scriptPreviewBoard.bagPos,
          "rescue placement preserves secrets and the normal turn's RNG/hazards");
    CHECK(scriptPreviewScore == 900 && scriptPreviewHints == 1 &&
          scriptPreviewRescues == 1 && scriptPreviewRescueError == 0 &&
          scriptPreviewLastScore == 0 && scriptPreviewLastTier == 0 &&
          !memcmp(&expectedLedger, &scriptPreviewLedger, sizeof expectedLedger),
          "rescue keeps score, history and hints; resets only combo/score display");
    refused(2, "second-rescue guard");

    // The heat from recovery would ignite the only remaining top-row route.
    // It must fail atomically instead of charging for an unusable placement.
    start(217, 1, 0);
    for (int c = 0; c < SCRIPT_COLS; c++)
        if (c != 3)
            for (int r = 0; r < SCRIPT_ROWS; r++)
                scriptPreviewBoard.state[c][r] = SCRIPT_HOLE;
    scriptPreviewRules.heat = scriptPreviewRules.heatThreshold - 2;
    refused(3, "route blocked by this recovery's new flame");

    start(218, 3, 3);
    for (int c = 0; c < SCRIPT_COLS; c++)
        scriptPreviewBoard.state[c][0] = SCRIPT_HOLE;
    scriptPreviewBoard.state[0][SCRIPT_ROWS - 1] = SCRIPT_BURN;
    refused(3, "already blocked board even when fire is due");

    // Recovery remains a real turn: an eligible action cannot prevent an
    // existing bottom flame from escaping or resurrect the failed shelf.
    start(77, 3, 3);
    scriptPreviewBoard.state[0][SCRIPT_ROWS - 1] = SCRIPT_BURN;
    CHECK(scriptPreviewRescue() && scriptPreviewRules.status == SCRIPT_FAILED &&
          scriptPreviewTurns == 4 && scriptPreviewRescues == 1 &&
          scriptPreviewScore == 900,
          "an accepted rescue still commits a fatal fire turn without points");
    refused(0, "failed-shelf guard");

    start(77, 1, 0);
    for (int i = 0; i < scriptPreviewBoard.passLen; i++)
        scriptPreviewBoard.found[i] = 1;
    refused(0, "already-complete password guard");
    scriptPreviewRules.status = SCRIPT_PASSED;
    refused(0, "passed-shelf guard");

    scriptPreviewClose();
    free(dict); free(tiles); free(pools);
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: %s ru.dict uk.dict ru.tiles uk.tiles ru.pools uk.pools\n", argv[0]);
        return 2;
    }
    exercise(argv[1], argv[3], argv[5], 0);
    exercise(argv[2], argv[4], argv[6], 1);
    printf(failures ? "SCRIPT PREVIEW FAILED: %d failure(s)\n" :
                     "SCRIPT PREVIEW OK: retries, score quote, generation, rescue and fire\n",
           failures);
    return failures ? 1 : 0;
}
