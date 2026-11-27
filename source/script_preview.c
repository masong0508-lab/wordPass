// Actual DS runtime bridge for the WPD4/WPL2/WPP2 symbol-ID board.
// This code only compiles in a separate opt-in cyrillic preview ROM.
// The normal wordPass language menu, saves and gameplay remain unchanged.
#include "script_preview.h"
#ifdef WP_CYR_PREVIEW
#ifdef WP_CYR_MAIN
#include "game.h"
#include "script_career.h"
#endif
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
#ifdef WP_CYR_MAIN
int scriptPreviewDailyRanked; // 0 practice, 1 scored attempt, 2 recorded/closed
int scriptPreviewTouchPeek;
static void finishRankedDaily(void)
{
    if(scriptPreviewDailyRanked!=1) return;
    scriptPreviewDailyRanked=2; // close the result exactly once
    (void)scriptDailyCareerFinish(settingsPersistent()?settingsCfgPath():NULL,
        (unsigned)scriptPreviewLanguage,scriptPreviewDailyDate,
        scriptPreviewScore>0?(unsigned)scriptPreviewScore:0);
}
#endif
int scriptPreviewHelp;
int scriptPreviewTraining, scriptPreviewTrainingNotice, scriptPreviewTrainingLen;
uint8_t scriptPreviewTrainingC[6], scriptPreviewTrainingR[6];
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
#ifdef WP_CYR_MAIN
    finishRankedDaily();
    scriptPreviewDailyRanked=0;
    scriptPreviewTouchPeek=0;
#endif
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
    scriptPreviewTraining = scriptPreviewTrainingNotice = scriptPreviewTrainingLen = 0;
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
    if (scriptPreviewTraining) {
        seed = 0x7475746fu ^ (unsigned)scriptPreviewLanguage;
        bankedScore = 0;
    }
    seed = seed ? seed : 0x87654321u;
    ScriptBoard nextBoard;
    ScriptRules nextRules;
    scriptRulesStart(&nextRules, shelf, seed);
#ifdef WP_CYR_MAIN
    if (!scriptPreviewDaily && !scriptPreviewTraining)
        scriptRulesApplyFireMode(&nextRules,S.fire);
#endif
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
    if (scriptPreviewTraining) {
        // Same solvable corner as retail: (6,0) has one open and one
        // covered neighbor. Set this before the planter so no numbered
        // teaching word crosses an already opened hole or hidden letter.
        static const uint8_t cols[9] = {6,3,4,5,6,3,4,6,5};
        static const uint8_t rows[9] = {1,4,5,6,7,6,7,4,4};
        memset(nextBoard.back, 0, sizeof nextBoard.back);
        for (unsigned i = 0; i < nextBoard.passLen; ++i)
            nextBoard.back[cols[i]][rows[i]] = (uint8_t)(i + 1);
        static const uint8_t hc[7] = {3,4,6,6,5,2,2}, hr[7] = {5,6,5,6,5,5,6};
        nextBoard.state[5][0] = SCRIPT_HOLE;
        for (int i=0; i<7; ++i) nextBoard.state[hc[i]][hr[i]]=SCRIPT_HOLE;
        for (unsigned i=nextBoard.passLen; i<9; ++i)
            nextBoard.state[cols[i]][rows[i]]=SCRIPT_HOLE;
    }
    // Generate up to two distinct legal opening words; explicit L+R rescue
    // still plants only one word after charging its normal turn and heat.
    uint8_t pathsC[2][6], pathsR[2][6], lengths[2];
    int openingWords = scriptBoardPlantOpeningWords(
        &nextBoard, &scriptPreviewDict, &scriptPreviewTiles,
        &scriptPreviewPools, seed, pathsC, pathsR, lengths);
    if (!openingWords) return false;
    if (scriptPreviewTraining) {
        memcpy(scriptPreviewTrainingC, pathsC[0], lengths[0]);
        memcpy(scriptPreviewTrainingR, pathsR[0], lengths[0]);
        scriptPreviewTrainingLen = lengths[0];
        int first = 0;
        for (int i = 1; i < lengths[0]; ++i)
            if (pathsR[0][i] < pathsR[0][first]) first = i;
        nextBoard.state[pathsC[0][first]][pathsR[0][first]] = SCRIPT_BURN;
    } else scriptPreviewTrainingLen = 0;
    // Generate both the hidden password and a playable word before replacing
    // a live shelf. A refused generation must not erase its score or state.
    scriptPreviewBoard = nextBoard;
#ifdef WP_CYR_MAIN
    scriptPreviewTouchPeek=0;
#endif
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
    scriptPreviewTrainingNotice = 0;
    scriptLedgerReset(&scriptPreviewLedger);
    scriptPreviewCursorActive = scriptPreviewCursorCol = scriptPreviewCursorRow = 0;
    // Each generated shelf now guarantees a valid dictionary word from its
    // own theme when possible, instead of always spelling ПРИВЕТ/ПРИВІТ.
    // This includes Daily Practice: identical date + language => same word.
    return true;
}
int scriptPreviewTrainingStage(void)
{
    const ScriptBoard *b = &scriptPreviewBoard;
    if (!scriptPreviewTraining) return 0;
    if (scriptBoardPassComplete(b)) return 5;
    if (!scriptPreviewRules.words) return 1;
    if (b->state[6][0] != SCRIPT_HOLE) return 2;
    if (!b->found[0]) return 3;
    return 4;
}
bool scriptPreviewTrainingTarget(int *c, int *r)
{
    int stage = scriptPreviewTrainingStage();
    if (!c || !r) return false;
    if (stage == 2) { *c = 6; *r = 0; return true; }
    if (stage == 3) { *c = 6; *r = 1; return true; }
    return false;
}
static bool focusTrainingClue(void)
{
    if (scriptPreviewTrainingStage()!=4) return false;
    const ScriptBoard *b=&scriptPreviewBoard;
    static const uint8_t cc[4]={3,4,6,6}, rr[4]={5,6,5,6};
    int start=-1;
    for (int i=0;i<4;++i)
        if (cc[i]==b->clueC && rr[i]==b->clueR) start=i;
    for (int i=1;i<=4;++i) {
        int k=(start+i)%4;
        if (scriptBoardClueInference(b,cc[k],rr[k],NULL,NULL)==SCRIPT_CLUE_PASS)
            return scriptPreviewFocusClue(cc[k],rr[k]);
    }
    return false;
}
bool scriptPreviewToggleTraining(uint32_t seed)
{
    if (!scriptPreviewDict.image || scriptPreviewDaily ||
        (!scriptPreviewTraining &&
         (scriptPreviewShelf != 1 || scriptPreviewTurns || scriptPreviewScore))) {
        scriptPreviewTrainingNotice = SCRIPT_TRAIN_NOTICE_ENTRY;
        return false;
    }
    int old = scriptPreviewTraining;
    scriptPreviewTraining = !old;
    if (!startPass(1, seed, 0)) {
        scriptPreviewTraining = old;
        return false;
    }
    return true;
}
bool scriptPreviewNewPass(uint32_t seed)
{
#ifdef WP_CYR_MAIN
    // A live scored Daily is a single attempt. Replays after a terminal
    // result use the same puzzle but can never update the record.
    if(scriptPreviewDailyRanked==1) return false;
    bool ok=startPass((unsigned)scriptPreviewShelf,seed,shelfStartScore);
    if(ok) scriptPreviewDailyRanked=0;
    return ok;
#else
    return startPass((unsigned)scriptPreviewShelf, seed, shelfStartScore);
#endif
}
static int takeTurn(int heat, bool peel, bool fatal)
{
    if (scriptPreviewTraining && !fatal)
        return scriptRulesTrainingTurn(&scriptPreviewRules, &scriptPreviewBoard,
                                       &scriptPreviewTiles, heat, peel);
    return scriptRulesTurn(&scriptPreviewRules, &scriptPreviewBoard,
                           &scriptPreviewTiles, heat, peel, fatal);
}
int scriptPreviewCheckpoint(bool save, const char *settingsPath)
{
    int result;
    if (scriptPreviewTraining)
        result = SCRIPT_CHECKPOINT_TRAINING;
    else if (scriptPreviewDaily)
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
    if (scriptPreviewTraining) { scriptPreviewTrainingNotice = SCRIPT_TRAIN_NOTICE_DAILY; return false; }
    if (!scriptPreviewDict.image) return false;
    int next = !scriptPreviewDaily;
    if (next && !scriptDailyValidDate(date)) {
        scriptPreviewDailyClockError = 1;
        return false;
    }
    int previousDate = scriptPreviewDailyDate;
#ifdef WP_CYR_MAIN
    if(!next) finishRankedDaily(); // leaving forfeits the remaining attempt
#endif
    scriptPreviewDaily = next;
    scriptPreviewDailyDate = next ? date : 0;
    if (!startPass(1, freeSeed, 0)) {
        scriptPreviewDaily = !next;
        scriptPreviewDailyDate = previousDate;
        return false;
    }
    scriptPreviewDailyClockError = 0;
#ifdef WP_CYR_MAIN
    scriptPreviewDailyRanked=next && scriptDailyCareerClaim(
        settingsPersistent()?settingsCfgPath():NULL,
        (unsigned)scriptPreviewLanguage,date);
#endif
    return true;
}
bool scriptPreviewAdvance(uint32_t seed)
{
    if (scriptPreviewDaily) return false; // one unranked repeatable daily shelf
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_PASSED)
        return false;
    if (scriptPreviewTraining) return scriptPreviewToggleTraining(seed);
    if (scriptPreviewShelf >= 999) return false;
    // Bank this completed shelf only when its successor is ready. Replays
    // discard the current attempt's words/bonus, keeping earlier shelves.
    unsigned nextShelf = (unsigned)scriptPreviewShelf + 1;
    return startPass(nextShelf, seed ^ (nextShelf * 0x9e3779b9u),
                     scriptPreviewScore);
}
#ifdef WP_CYR_MAIN
// Each accepted action enters while RUNNING. Only its first terminal
// transition reaches this hook; retry/advance/exit cannot count twice.
static void finishCareer(void)
{
    if(scriptPreviewTraining || scriptPreviewRules.status==SCRIPT_RUNNING) return;
    if(scriptPreviewDaily) { finishRankedDaily(); return; }
    unsigned revealed=0;
    for(unsigned i=0;i<scriptPreviewBoard.passLen;++i)
        revealed+=scriptPreviewBoard.found[i]!=0;
    scriptCareerFinish(settingsPersistent()?settingsCfgPath():NULL,
        (unsigned)scriptPreviewLanguage,
        scriptPreviewScore>0?(unsigned)scriptPreviewScore:0,
        (unsigned)scriptPreviewShelf,scriptPreviewRules.words,
        scriptPreviewRules.peels,revealed,scriptPreviewRules.status==SCRIPT_PASSED);
}
#endif
int scriptPreviewPeel(int c, int r)
{
    if (!scriptPreviewDict.image || scriptPreviewRules.status != SCRIPT_RUNNING)
        return -1;
    int stage = scriptPreviewTrainingStage();
    if (stage==4 && c>=0 && c<SCRIPT_COLS && r>=0 && r<SCRIPT_ROWS &&
        scriptPreviewBoard.state[c][r]==SCRIPT_HOLE) {
        (void)scriptPreviewFocusClue(c,r);
        scriptPreviewTrainingNotice=SCRIPT_TRAIN_NOTICE_NONE;
        return -1; // inspection, not a scored or charged peel
    }
    if (scriptPreviewTraining && stage != 4) {
        int cc, rr;
        if (!scriptPreviewTrainingTarget(&cc, &rr) || c != cc || r != rr) {
            scriptPreviewTrainingNotice = scriptPreviewTrainingStage() == 1 ?
                SCRIPT_TRAIN_NOTICE_WORD_FIRST : SCRIPT_TRAIN_NOTICE_TARGET;
            return -1;
        }
    }
    int result = scriptBoardPeel(&scriptPreviewBoard, c, r);
    if (result >= 0 || result == -2) {
        if (scriptPreviewTraining) scriptPreviewTrainingNotice = SCRIPT_TRAIN_NOTICE_NONE;
        scriptPreviewRescueError = 0; // dismiss stale recovery warnings
        // A nonfatal blank peel creates a real numbered hole. Show its
        // neighbor evidence immediately, without waiting for another word.
        if (result == 0) (void)scriptPreviewFocusClue(c, r);
        scriptPreviewPeels++;
        scriptLedgerPeel(&scriptPreviewLedger); // a peel breaks the retail combo
        scriptPreviewLastTier = scriptPreviewLastScore = 0;
        int bonus = takeTurn(1, true, result == -2);
        scriptPreviewTurns = scriptPreviewRules.turns;
        scriptPreviewScore += bonus;
        if (stage==3 && scriptPreviewTrainingStage()==4)
            (void)focusTrainingClue();
#ifdef WP_CYR_MAIN
        finishCareer();
#endif
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
        (scriptPreviewHints && !scriptPreviewTraining)) return false;
    // In the final lesson R visits a public clue, leaving the chain,
    // score, turn count and hint allowance untouched.
    if (scriptPreviewTrainingStage()==4) return focusTrainingClue();
    uint8_t c[SCRIPT_HINT_MAX], r[SCRIPT_HINT_MAX];
    int n;
    if (scriptPreviewTrainingStage() == 1) {
        // The board cannot change before the first accepted tutorial word:
        // peels and rescue are blocked. R must select the same route the
        // numbered lesson is teaching, not an unrelated solver result.
        n = scriptPreviewTrainingLen;
        if (n < WPD4_MIN_WORD || n > SCRIPT_HINT_MAX) return false;
        memcpy(c, scriptPreviewTrainingC, (size_t)n);
        memcpy(r, scriptPreviewTrainingR, (size_t)n);
    } else {
        n = scriptBoardHint(&scriptPreviewBoard, &scriptPreviewDict,
                            &scriptPreviewTiles, c, r);
    }
    if (n < WPD4_MIN_WORD || n > SCRIPT_HINT_MAX) return false;
    memcpy(scriptPreviewBoard.chainC, c, (size_t)n);
    memcpy(scriptPreviewBoard.chainR, r, (size_t)n);
    scriptPreviewBoard.chainLen = (uint8_t)n;
    // The next plain A should submit the hinted word, not append an old
    // controller focus left over from a previous attempted chain.
    scriptPreviewCursorHide();
    scriptPreviewRescueError = 0;
    if (!scriptPreviewTraining) scriptPreviewHints++;
    return true;
}
bool scriptPreviewRescue(void)
{
    if (!scriptPreviewDict.image || scriptPreviewTraining ||
        scriptPreviewRules.status != SCRIPT_RUNNING ||
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
#ifdef WP_CYR_MAIN
    finishCareer();
#endif
    return true;
}
// The initial lesson must extinguish the planted stationary flame.
// Bounded to the current chain; ordinary preview play returns immediately.
bool scriptPreviewTrainingFireSelected(void)
{
    if (scriptPreviewTrainingStage() != 1) return true;
    const ScriptBoard *b = &scriptPreviewBoard;
    if (b->chainLen > SCRIPT_MAX_CHAIN) return false;
    for (unsigned i = 0; i < b->chainLen; ++i) {
        int c = b->chainC[i], r = b->chainR[i];
        if (c < SCRIPT_COLS && r < SCRIPT_ROWS && b->state[c][r] == SCRIPT_BURN)
            return true;
    }
    return false;
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
    if (raw <= 0 || length < WPD4_MIN_WORD || length > SCRIPT_MAX_CHAIN ||
        !scriptPreviewTrainingFireSelected())
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
    if (!scriptPreviewTrainingFireSelected()) {
        scriptPreviewTrainingNotice = SCRIPT_TRAIN_NOTICE_FIRE_WORD;
        return 0;
    }
    int tier = 0;
    int points = scriptBoardSubmit(&scriptPreviewBoard, &scriptPreviewDict,
                                   &scriptPreviewTiles, &tier);
    scriptPreviewLastTier = tier;
    if (points > 0) {
        if (scriptPreviewTraining) scriptPreviewTrainingNotice = SCRIPT_TRAIN_NOTICE_NONE;
        scriptPreviewRescueError = 0;
        points = scriptLedgerCommit(&scriptPreviewLedger, ids, length, points);
        scriptPreviewScore += points;
        scriptPreviewScore += takeTurn(scriptRulesWordHeat(length), false, false);
        scriptPreviewTurns = scriptPreviewRules.turns;
#ifdef WP_CYR_MAIN
        finishCareer();
#endif
    }
    scriptPreviewLastScore = points;
    return points;
}
#endif
