// Exercise the actual preview action bridge with the 40-theme draft packs.
// Lower-level board/rules tests cannot catch the ordering between them.
#include "script_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

// Controller-only Cyrillic gameplay must share the exact same validated
// chain/peel/score functions as the stylus, without new board state or
// hidden-position information in its focus system.
static void controllerOnly(void)
{
    CHECK(scriptPreviewNewPass(551) && !scriptPreviewCursorActive,
          "fresh Cyrillic shelf has no inherited controller cursor");
    ScriptBoard board = scriptPreviewBoard;
    ScriptRules rules = scriptPreviewRules;
    ScriptLedger ledger = scriptPreviewLedger;
    CHECK(!scriptPreviewCursorSelect() && scriptPreviewCursorPeel() == -1 &&
          !scriptPreviewCursorBacktrack(),
          "controller actions without a focused tile are harmless");
    scriptPreviewCursorMove(1, 0);
    CHECK(scriptPreviewCursorActive && scriptPreviewCursorCol == 4 &&
          scriptPreviewCursorRow == 4,
          "first D-pad step starts at board center and moves right");
    for (int i = 0; i < 20; i++) scriptPreviewCursorMove(-1, -1);
    CHECK(scriptPreviewCursorCol == 0 && scriptPreviewCursorRow == 0,
          "controller cursor clamps safely at upper left");
    for (int i = 0; i < 20; i++) scriptPreviewCursorMove(1, 1);
    CHECK(scriptPreviewCursorCol == SCRIPT_COLS - 1 &&
          scriptPreviewCursorRow == SCRIPT_ROWS - 1,
          "controller cursor clamps safely at lower right");
    CHECK(!memcmp(&board, &scriptPreviewBoard, sizeof board) &&
          !memcmp(&rules, &scriptPreviewRules, sizeof rules) &&
          !memcmp(&ledger, &scriptPreviewLedger, sizeof ledger) &&
          scriptPreviewTurns == 0 && scriptPreviewScore == 0,
          "D-pad navigation is read-only and does not consume turns");
    // Choose the first unobstructed top tile with cursor-only input and
    // undo it with B; no accidental back-to-title action.
    for (int i = 0; i < 20; i++) scriptPreviewCursorMove(-1, -1);
    CHECK(scriptPreviewCursorSelect() && scriptPreviewBoard.chainLen == 1 &&
          scriptPreviewCursorBacktrack() && !scriptPreviewBoard.chainLen &&
          !scriptPreviewCursorBacktrack() && !scriptPreviewTurns,
          "A selects a letter and B undoes it without leaving the shelf");
    // B is a word-undo command for either input method. A chain started
    // with the stylus no longer turns B into an unexpected quit-to-title.
    scriptPreviewCursorHide();
    CHECK(scriptPreviewChain(0, 0) && !scriptPreviewCursorActive &&
          scriptPreviewBoard.chainLen == 1 &&
          scriptPreviewCursorBacktrack() && !scriptPreviewBoard.chainLen &&
          !scriptPreviewCursorBacktrack() && scriptPreviewTurns == 0,
          "B backtracks stylus-created words without needing D-pad focus");
    CHECK(scriptPreviewNewPass(552) && !scriptPreviewCursorActive &&
          scriptPreviewHint() && !scriptPreviewCursorActive,
          "a real hint selects a word without activating a stale D-pad cursor");
    int len = scriptPreviewBoard.chainLen;
    int tailC = scriptPreviewBoard.chainC[len - 1];
    int tailR = scriptPreviewBoard.chainR[len - 1];
    scriptPreviewCursorMove(0, 0);
    CHECK(scriptPreviewCursorActive && scriptPreviewCursorCol == tailC &&
          scriptPreviewCursorRow == tailR,
          "D-pad after a hint focuses the last selected letter");
    CHECK(scriptPreviewCursorSelect() && scriptPreviewScore > 0 &&
          scriptPreviewTurns == 1 && !scriptPreviewBoard.chainLen,
          "A on selected word tail submits a real word, scores and costs one turn");
    CHECK(!scriptPreviewCursorBacktrack(), "no empty post-submit chain to undo");
    // L+A on a discovered cursor coordinate peels exactly once; revealing a
    // secret is still governed by the real fire/turn/password mechanics.
    CHECK(scriptPreviewNewPass(553), "reset for controller-only secret peel");
    int secretC = -1, secretR = -1, secretIndex = 0;
    for (int c = 0; c < SCRIPT_COLS && secretIndex == 0; c++)
        for (int rr = 0; rr < SCRIPT_ROWS; rr++)
            if (scriptPreviewBoard.back[c][rr]) {
                secretC = c; secretR = rr;
                secretIndex = scriptPreviewBoard.back[c][rr];
                break;
            }
    CHECK(secretIndex > 0 && !scriptPreviewCursorActive,
          "real Cyrillic pass has a covered controller-peel target");
    scriptPreviewCursorMove(0, 0);
    while (scriptPreviewCursorCol != secretC || scriptPreviewCursorRow != secretR)
        scriptPreviewCursorMove((secretC > scriptPreviewCursorCol) -
                                (secretC < scriptPreviewCursorCol),
                                (secretR > scriptPreviewCursorRow) -
                                (secretR < scriptPreviewCursorRow));
    CHECK(scriptPreviewCursorPeel() == secretIndex &&
          scriptPreviewBoard.found[secretIndex - 1] == 1 &&
          scriptPreviewPeels == 1 && scriptPreviewTurns == 1,
          "controller L+A uses the identical real peel and turn rules");
    scriptPreviewCursorHide();
    CHECK(!scriptPreviewCursorActive && scriptPreviewCursorPeel() == -1,
          "touch handoff clears D-pad focus and cannot cause extra peels");
    CHECK(scriptPreviewNewPass(554) && !scriptPreviewCursorActive,
          "shelf replay clears controller focus without changing save format");
}

// Two genuine blank peels publish independent clues. Revisiting either
// clue can change only its two public focus coordinates, not the game.
static void revisitClues(void)
{
    CHECK(scriptPreviewNewPass(712), "start public clue inspection scenario");
    ScriptBoard before = scriptPreviewBoard;
    CHECK(!scriptPreviewFocusClue(0, 0) &&
          !scriptPreviewFocusClue(-1, 0) &&
          !scriptPreviewFocusClue(SCRIPT_COLS, SCRIPT_ROWS) &&
          !memcmp(&before, &scriptPreviewBoard, sizeof before),
          "unearned and out-of-bounds clues must never set focus");
    CHECK(scriptPreviewPeel(0, 0) == 0 &&
          scriptPreviewBoard.clueC == 0 && scriptPreviewBoard.clueR == 0,
          "new blank peel focuses its public number");
    CHECK(scriptPreviewPeel(6, 0) == 0 &&
          scriptPreviewBoard.clueC == 6 && scriptPreviewBoard.clueR == 0,
          "another blank peel focuses the new hole");
    CHECK(scriptPreviewChain(2, 0) && scriptPreviewChain(2, 1),
          "a selected word can be retained during clue inspection");
    ScriptBoard expected = scriptPreviewBoard;
    ScriptRules rules = scriptPreviewRules;
    ScriptLedger ledger = scriptPreviewLedger;
    int score = scriptPreviewScore, turns = scriptPreviewTurns;
    int peels = scriptPreviewPeels, hints = scriptPreviewHints;
    expected.clueC = 0;
    expected.clueR = 0;
    CHECK(scriptPreviewFocusClue(0, 0) &&
          !memcmp(&expected, &scriptPreviewBoard, sizeof expected),
          "revisit earlier hole with no mutation except public focus");
    CHECK(!scriptPreviewFocusClue(1, 1) &&
          !memcmp(&expected, &scriptPreviewBoard, sizeof expected),
          "unearned neighbor cannot displace an earlier clue");
    // Cursor movement over another published number is inspection-only.
    // Move from a selected chain tail to the upper-right corner.
    scriptPreviewCursorHide();
    scriptPreviewCursorMove(0, 0);
    for (int i = 0; i < SCRIPT_COLS; ++i)
        scriptPreviewCursorMove(1, -1);
    expected.clueC = 6;
    CHECK(scriptPreviewCursorCol == 6 && scriptPreviewCursorRow == 0 &&
          !memcmp(&expected, &scriptPreviewBoard, sizeof expected),
          "controller can refocus another numbered hole");
    CHECK(scriptPreviewScore == score && scriptPreviewTurns == turns &&
          scriptPreviewPeels == peels && scriptPreviewHints == hints &&
          !memcmp(&rules, &scriptPreviewRules, sizeof rules) &&
          !memcmp(&ledger, &scriptPreviewLedger, sizeof ledger),
          "inspection preserves score, turn, heat, combo, peels and hints");
    CHECK(scriptPreviewNewPass(713) &&
          !scriptPreviewFocusClue(6, 0) && !scriptPreviewFocusClue(0, 0),
          "new attempt has no stale clue to inspect");
}

static bool checkpointRecordEqual(const ScriptCheckpoint *a,
                                  const ScriptCheckpoint *b)
{
    return a->language == b->language && a->pack == b->pack &&
           a->serial == b->serial && a->shelf == b->shelf &&
           a->seed == b->seed && a->score == b->score;
}

static void checkpointTest(void)
{
    char dir[] = "/tmp/wordpass-checkpoint-XXXXXX";
    if (!mkdtemp(dir)) {
        CHECK(false, "create isolated native checkpoint directory");
        return;
    }
    char cfg[SCRIPT_CHECKPOINT_PATH], slot0[SCRIPT_CHECKPOINT_PATH];
    char slot1[SCRIPT_CHECKPOINT_PATH], other[SCRIPT_CHECKPOINT_PATH];
    snprintf(cfg, sizeof cfg, "%s/wordPass.cfg", dir);
    uint32_t tag = scriptCheckpointTag(&scriptPreviewDict,
                                       &scriptPreviewTiles, &scriptPreviewPools);
    unsigned language = (unsigned)scriptPreviewLanguage;
    CHECK(scriptCheckpointPath(cfg, language, tag, 0, slot0) &&
          scriptCheckpointPath(cfg, language, tag, 1, slot1) &&
          scriptCheckpointPath(cfg, language ^ 1u, tag, 0, other) &&
          strcmp(slot0, slot1) && strcmp(slot0, other) && strcmp(slot0, cfg),
          "checkpoint filenames isolate both languages, slots and retail save");
    CHECK(scriptCheckpointPath("fat:/wordPass.cfg", 0, 0x89abcdefu, 0, other) &&
          !strcmp(other, "fat:/R9ABCDEF.C80"),
          "checkpoint uses the explicit FAT device and complete 32-bit pack tag");

    scriptPreviewShelf = 1;
    CHECK(scriptPreviewNewPass(812), "start fresh checkpoint test shelf");
    CHECK(scriptPreviewCheckpoint(true, NULL) == SCRIPT_CHECKPOINT_NO_CARD &&
          scriptPreviewCheckpoint(false, cfg) == SCRIPT_CHECKPOINT_NO_RECORD,
          "missing card/record is safe and explicit");
    // Complete a real first shelf and bank the bonus on entering the next.
    for (int c = 0; c < SCRIPT_COLS; ++c)
        for (int rr = 0; rr < SCRIPT_ROWS; ++rr)
            if (scriptPreviewBoard.back[c][rr])
                CHECK(scriptPreviewPeel(c, rr) > 0, "complete password for checkpoint");
    int banked = scriptPreviewScore;
    CHECK(banked > 0 && scriptPreviewAdvance(813) &&
          scriptPreviewShelf == 2 && scriptPreviewScore == banked,
          "only completed shelves enter checkpoint score");
    ScriptBoard first = scriptPreviewBoard;
    uint32_t firstSeed = scriptPreviewSeed;
    CHECK(scriptPreviewCheckpoint(true, cfg) == SCRIPT_CHECKPOINT_SAVED,
          "save first banked shelf to a separate verified checkpoint");
    ScriptCheckpoint record = {0};
    CHECK(scriptCheckpointLoad(cfg, language, tag, &record) &&
          record.serial == 1 && record.shelf == 2 &&
          record.score == (unsigned)banked && record.seed == firstSeed,
          "first slot contains exact banked state and deterministic seed");
    CHECK(!scriptCheckpointLoad(cfg, language ^ 1u, tag, &record) &&
          !scriptCheckpointLoad(cfg, language, tag ^ 1u, &record),
          "different language/pack cannot read this checkpoint");

    // Strict, endian-independent codec validation: no raw structs written.
    uint8_t image[SCRIPT_CHECKPOINT_BYTES] = {0};
    ScriptCheckpoint decoded = {0};
    CHECK(scriptCheckpointEncode(&record, image) &&
          scriptCheckpointDecode(image, sizeof image, language, tag, &decoded) &&
          checkpointRecordEqual(&record, &decoded),
          "checkpoint encoding is a bounded 32-byte record");
    CHECK(!scriptCheckpointDecode(image, sizeof image - 1, language, tag, &decoded) &&
          !scriptCheckpointDecode(image, sizeof image, language ^ 1u, tag, &decoded) &&
          !scriptCheckpointDecode(image, sizeof image, language, tag ^ 1u, &decoded),
          "truncated, cross-language or cross-pack records are refused");
    image[20] ^= 1u;
    CHECK(!scriptCheckpointDecode(image, sizeof image, language, tag, &decoded),
          "checksum detects damaged seed");
    ScriptCheckpoint invalid = record;
    invalid.shelf = 1000;
    CHECK(!scriptCheckpointEncode(&invalid, image), "invalid shelf cannot be stored");
    invalid = record;
    invalid.score = SCRIPT_CHECKPOINT_MAX_SCORE + 1u;
    CHECK(!scriptCheckpointEncode(&invalid, image), "unbounded score cannot be stored");

    // A real word earns unbanked points; do not serialize partial attempts.
    CHECK(scriptPreviewHint() && scriptPreviewSubmit() > 0 &&
          scriptPreviewCheckpoint(true, cfg) == SCRIPT_CHECKPOINT_NOT_START,
          "partial shelf cannot overwrite its valid checkpoint");
    CHECK(scriptPreviewCheckpoint(false, cfg) == SCRIPT_CHECKPOINT_LOADED &&
          scriptPreviewShelf == 2 && scriptPreviewTurns == 0 &&
          scriptPreviewScore == banked && scriptPreviewSeed == firstSeed &&
          !memcmp(&first, &scriptPreviewBoard, sizeof first),
          "load atomically regenerates exact shelf-start board and score");

    // A second save writes the OTHER slot. Simulate an interrupted write to
    // that newest record, then fall back to the still-complete older slot.
    CHECK(scriptPreviewNewPass(814) &&
          scriptPreviewCheckpoint(true, cfg) == SCRIPT_CHECKPOINT_SAVED &&
          scriptCheckpointLoad(cfg, language, tag, &decoded) &&
          decoded.serial == 2 && decoded.seed != firstSeed,
          "second checkpoint alternates the active slot");
    FILE *f = fopen(slot1, "wb");
    CHECK(f != NULL, "simulate torn newest checkpoint");
    if (f) {
        CHECK(fwrite("WPC1", 1, 4, f) == 4, "write truncated record");
        fclose(f);
    }
    CHECK(scriptPreviewCheckpoint(false, cfg) == SCRIPT_CHECKPOINT_LOADED &&
          scriptPreviewSeed == firstSeed && scriptPreviewScore == banked &&
          !memcmp(&first, &scriptPreviewBoard, sizeof first),
          "torn newer record restores older verified slot");

    CHECK(scriptPreviewToggleDaily(20261124, 815), "enter unranked daily practice");
    ScriptBoard daily = scriptPreviewBoard;
    CHECK(scriptPreviewCheckpoint(true, cfg) == SCRIPT_CHECKPOINT_DAILY &&
          scriptPreviewCheckpoint(false, cfg) == SCRIPT_CHECKPOINT_DAILY &&
          !memcmp(&daily, &scriptPreviewBoard, sizeof daily),
          "daily practice cannot save or import free-play state");
    CHECK(scriptPreviewToggleDaily(20261124, 816), "leave daily practice");
    CHECK(scriptPreviewCheckpoint(false, cfg) == SCRIPT_CHECKPOINT_LOADED &&
          scriptPreviewScore == banked && scriptPreviewShelf == 2,
          "daily practice has not changed the previous free-play checkpoint");
    CHECK(access(cfg, F_OK) != 0, "preview never writes the retail settings file");
    unlink(slot0);
    unlink(slot1);
    rmdir(dir);
    scriptPreviewShelf = 1;
    // Reinitialize banked score as a new free-play run through the public mode
    // bridge, rather than leaving a test's saved score in subsequent tests.
    CHECK(scriptPreviewToggleDaily(20261124, 817) &&
          scriptPreviewToggleDaily(20261124, 818),
          "reset independent free-play run after checkpoint regression");
}

static void tailTapSubmission(void)
{
    CHECK(scriptPreviewNewPass(666), "prepare a Cyrillic tail-tap scenario");
    CHECK(!scriptPreviewTrySubmitTail(0, 0) &&
          scriptPreviewScore == 0 && scriptPreviewTurns == 0,
          "tap without a selected word cannot submit or spend a turn");
    CHECK(scriptPreviewChain(0, 0) && scriptPreviewChain(0, 1) &&
          !scriptPreviewTrySubmitTail(0, 1) &&
          scriptPreviewBoard.chainLen == 2 &&
          scriptPreviewTurns == 0 && scriptPreviewScore == 0,
          "double-tapping a too-short two-symbol chain never auto-submits");
    scriptBoardClearChain(&scriptPreviewBoard);
    CHECK(scriptPreviewHint(), "valid dictionary word available for tail-tap test");
    int n = scriptPreviewBoard.chainLen;
    int c = scriptPreviewBoard.chainC[n - 1];
    int rr = scriptPreviewBoard.chainR[n - 1];
    ScriptBoard selected = scriptPreviewBoard;
    ScriptRules rules = scriptPreviewRules;
    CHECK(!scriptPreviewTrySubmitTail((c + 1) % SCRIPT_COLS, rr) &&
          !memcmp(&scriptPreviewBoard, &selected, sizeof selected) &&
          !memcmp(&scriptPreviewRules, &rules, sizeof rules),
          "non-tail tap cannot submit, reveal clue or consume selected word");
    CHECK(scriptPreviewTrySubmitTail(c, rr) &&
          scriptPreviewScore > 0 && scriptPreviewTurns == 1 &&
          scriptPreviewBoard.chainLen == 0,
          "second fresh tail tap scores real word through normal submit path");
    int scored = scriptPreviewScore, turns = scriptPreviewTurns;
    CHECK(!scriptPreviewTrySubmitTail(c, rr) &&
          scriptPreviewScore == scored && scriptPreviewTurns == turns,
          "repeated tail taps after a submit cannot score the word twice");
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

static void guidedTraining(void)
{
    scriptPreviewShelf = 1;
    CHECK(scriptPreviewNewPass(301) &&
          scriptPreviewToggleTraining(302) &&
          scriptPreviewTrainingStage() == 1 &&
          scriptPreviewTrainingLen >= 4 && scriptPreviewTrainingLen <= 6,
          "real Cyrillic training starts with a numbered accepted word");
    ScriptBoard initial = scriptPreviewBoard;
    CHECK(scriptPreviewNewPass(303) &&
          !memcmp(&initial, &scriptPreviewBoard, sizeof initial),
          "replay generates the exact same language-specific tutorial");
    CHECK(scriptPreviewCheckpoint(true, "/tmp/wordPass.cfg") == SCRIPT_CHECKPOINT_TRAINING &&
          scriptPreviewCheckpoint(false, "/tmp/wordPass.cfg") == SCRIPT_CHECKPOINT_TRAINING &&
          !scriptPreviewToggleDaily(20261126, 303) &&
          !scriptPreviewRescue(),
          "training cannot overwrite real checkpoints, start Daily or spend rescue");
    CHECK(scriptPreviewBoard.state[5][0] == SCRIPT_HOLE && !scriptPreviewPeels &&
          scriptPreviewPeel(6,0) == -1 && scriptPreviewTurns == 0 &&
          scriptPreviewTrainingNotice == SCRIPT_TRAIN_NOTICE_WORD_FIRST,
          "early peel explains that a word must be spelled first");
    // Hinting must reinforce the numbered path, even after a partial
    // touch/controller chain, and must never spend the normal hint budget.
    CHECK(scriptPreviewChain(scriptPreviewTrainingC[0], scriptPreviewTrainingR[0]),
          "start a partial numbered chain");
    for (int attempt=0; attempt<2; ++attempt) {
        CHECK(scriptPreviewHint() &&
              scriptPreviewBoard.chainLen == scriptPreviewTrainingLen &&
              !memcmp(scriptPreviewBoard.chainC, scriptPreviewTrainingC,
                      (size_t)scriptPreviewTrainingLen) &&
              !memcmp(scriptPreviewBoard.chainR, scriptPreviewTrainingR,
                      (size_t)scriptPreviewTrainingLen) &&
              scriptPreviewQuote(NULL) > 0 &&
              scriptPreviewTrainingStage() == 1 && !scriptPreviewTurns &&
              !scriptPreviewScore && !scriptPreviewPeels && !scriptPreviewHints,
              "repeatable free tutorial hint selects the exact accepted numbered word");
    }
    scriptBoardClearChain(&scriptPreviewBoard);
    for (int i=0; i<scriptPreviewTrainingLen; ++i)
        CHECK(scriptPreviewChain(scriptPreviewTrainingC[i],scriptPreviewTrainingR[i]),
              "numbered tutorial route follows actual adjacent gems");
    CHECK(scriptPreviewQuote(NULL) > 0 && scriptPreviewSubmit() > 0 &&
          scriptPreviewTrainingStage() == 2 && scriptPreviewTurns == 1 &&
          scriptPreviewTrainingNotice == SCRIPT_TRAIN_NOTICE_NONE,
          "first real word advances and dismisses the correction");
    CHECK(scriptPreviewPeel(0,7) == -1 && scriptPreviewTurns == 1 &&
          scriptPreviewTrainingNotice == SCRIPT_TRAIN_NOTICE_TARGET,
          "wrong marked target explains the error without a turn");
    CHECK(scriptPreviewPeel(6,0) == 0 &&
          scriptBoardVisibleClue(&scriptPreviewBoard,6,0) == 1 &&
          scriptPreviewTrainingStage() == 3 &&
          scriptPreviewTrainingNotice == SCRIPT_TRAIN_NOTICE_NONE,
          "correct peel gives a clue and dismisses the correction");
    int left, covered;
    CHECK(scriptBoardClueInference(&scriptPreviewBoard,6,0,&left,&covered) ==
          SCRIPT_CLUE_PASS && left == 1 && covered == 1 &&
          scriptPreviewPeel(0,7) == -1 && scriptPreviewTurns == 2,
          "public corner clue proves its sole covered neighbor");
    CHECK(scriptPreviewPeel(6,1) == 1 &&
          scriptPreviewTrainingStage() == 4,
          "find the indicated actual neighboring letter");
    int len = scriptPreviewBoard.passLen;
    for (int i=1; i<len; ++i) {
        int c, r;
        CHECK(scriptPreviewTrainingTarget(&c,&r) &&
              scriptPreviewPeel(c,r) > 0,
              "complete the remaining real password letters");
    }
    CHECK(scriptPreviewTrainingStage() == 5 &&
          scriptPreviewRules.status == SCRIPT_PASSED &&
          scriptPreviewTurns == len + 2 && !scriptPreviewRules.ignitions,
          "complete actual Cyrillic tutorial without fire");
    CHECK(scriptPreviewAdvance(304) && !scriptPreviewTraining &&
          scriptPreviewShelf == 1 && !scriptPreviewScore && !scriptPreviewTurns &&
          scriptPreviewTrainingStage() == 0,
          "lesson completion starts a fresh real game, not an inflated score");
    CHECK(scriptPreviewHint() && scriptPreviewSubmit() > 0,
          "play a real normal word");
    int score = scriptPreviewScore, turns = scriptPreviewTurns;
    CHECK(!scriptPreviewToggleTraining(305) && !scriptPreviewTraining &&
          scriptPreviewScore == score && scriptPreviewTurns == turns,
          "opening a tutorial cannot discard an active run");
    CHECK(scriptPreviewNewPass(306), "reset test to untouched free-play shelf");
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
    guidedTraining();
    passLengthProgression();
    controllerOnly();
    revisitClues();
    tailTapSubmission();

    checkpointTest();
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
