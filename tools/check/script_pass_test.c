// Cyrillic password mechanics, tested on the independent 7x8 game model.
// All fixtures are tiny local test data, not a playable DS language pack.
#include "script_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(ok, ...) do { if (!(ok)) { failures++; printf("FAIL "); printf(__VA_ARGS__); putchar('\n'); } } while (0)
static uint8_t *readFile(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f);
    *length = (size_t)n;
    return p;
}
// Evidence on a Cyrillic shelf must behave like the real Minesweeper back
// layer: no spoiler on an unopened cell, a fixed clue from a scored word,
// opened holes with numbers, and already-found diamonds still counted.
static void clueTests(void)
{
    ScriptBoard b;
    memset(&b, 0, sizeof b);
    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = 0; r < SCRIPT_ROWS; r++)
            b.state[c][r] = SCRIPT_TILE;
    b.passLen = 4;
    b.back[2][4] = 1; // neighbor below (2,3)
    b.back[3][3] = 2; // diagonal neighbor of the even column
    b.back[2][3] = 3; // the center is explicitly NOT a neighbor
    int remaining = 99, covered = 99;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == -1 &&
          scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_UNSEEN &&
          remaining == 0 && covered == 0,
          "an unopened Cyrillic password cell has no visible clue or inference");
    CHECK(scriptBoardVisibleClue(&b, -1, 3) == -1 &&
          scriptBoardVisibleClue(&b, 2, SCRIPT_ROWS) == -1,
          "invalid coordinates cannot expose password information");
    b.surveyed[2] |= 1u << 3;
    ScriptBoard before = b;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == 2,
          "survey counts two hex neighbors but excludes its own hidden password");
    CHECK(scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_UNKNOWN &&
          remaining == 2 && covered == 6,
          "uncertain clue reports remaining password neighbors without guessing");
    CHECK(!memcmp(&b, &before, sizeof b),
          "inspecting Cyrillic clue does not alter board, RNG, or hidden password");
    b.state[2][4] = SCRIPT_DIAMOND;
    b.back[2][4] = 0;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == 2 &&
          scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_UNKNOWN &&
          remaining == 1 && covered == 5,
          "revealed diamond still contributes to the fixed clue after its back index clears");
    b.back[3][3] = 0;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == 1 &&
          scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_SAFE &&
          remaining == 0 && covered == 5,
          "satisfied word clue proves other covered neighbors lack password letters");
    b.surveyed[2] = 0;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == -1,
          "removing a survey never leaves hidden pass counts visible");
    b.state[2][3] = SCRIPT_HOLE;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == 1,
          "opened hole publishes its same nearby password count");

    memset(&b, 0, sizeof b);
    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = 0; r < SCRIPT_ROWS; r++) b.state[c][r] = SCRIPT_HOLE;
    b.state[2][3] = SCRIPT_TILE;
    b.state[2][4] = SCRIPT_TILE;
    b.back[2][4] = 1;
    b.surveyed[2] = 1u << 3;
    CHECK(scriptBoardVisibleClue(&b, 2, 3) == 1 &&
          scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_PASS &&
          remaining == 1 && covered == 1,
          "one covered neighbor and one remaining letter forces the pass location");
    b.state[2][4] = SCRIPT_BURN;
    CHECK(scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_PASS &&
          covered == 1,
          "a burning tile is still an unknown cover; the renderer must warn of fire");
    b.state[2][4] = SCRIPT_HOLE;
    b.back[2][4] = 0;
    CHECK(scriptBoardClueInference(&b, 2, 3, &remaining, &covered) == SCRIPT_CLUE_UNKNOWN &&
          remaining == 0 && covered == 0,
          "already explored ring never offers a phantom free peel");
    printf("SCRIPT CLUES OK: visible-only evidence, diamonds, holes and forced deduction\n");
}

// Also used by the 100k-word sourced RU/UK CI build: verify the actual
// imported tile weights, not only the small built-in language fixtures.
static void checkSourcedVowelFloor(const V4Tiles *tiles, bool uk)
{
    unsigned total = 0;
    for (unsigned i = 0; i < tiles->symbols; i++)
        if ((tiles->vowels >> i) & UINT64_C(1)) total += tiles->weight[i];
    CHECK(total >= 10, "sourced %s weighted bag retains WPL2 vowel minimum",
          uk ? "Ukrainian" : "Russian");
    for (uint32_t seed = 1; seed <= 128; seed++) {
        ScriptBoard b, copy;
        uint32_t actual = seed * 127u + 0x20261122u;
        CHECK(scriptBoardStart(&b, tiles, actual) &&
              scriptBoardStart(&copy, tiles, actual) &&
              !memcmp(&b, &copy, sizeof b),
              "sourced Cyrillic initial vowel distribution is deterministic");
        unsigned front = 0;
        for (int c = 0; c < SCRIPT_COLS; c++) {
            unsigned inColumn = 0;
            for (int r = 0; r < SCRIPT_ROWS; r++) {
                unsigned id = b.tile[c][r];
                inColumn += (unsigned)((tiles->vowels >> id) & UINT64_C(1));
            }
            front += inColumn;
            CHECK(inColumn >= (total >= 14 ? 2u : 1u),
                  "sourced Cyrillic seed %u column %d has playable vowels",
                  seed, c);
        }
        CHECK(front >= (total >= 14 ? 14u : total),
              "sourced Cyrillic seed %u opening grid meets total vowel floor",
              seed);
    }
}

static void suite(const char *dictPath, const char *tilePath, const char *poolPath, bool uk)
{
    size_t dl=0, tl=0, pl=0;
    uint8_t *db=readFile(dictPath,&dl);
    uint8_t *tb=readFile(tilePath,&tl);
    uint8_t *pb=readFile(poolPath,&pl);
    CHECK(db && tb && pb, "all script resources readable");
    if (!db || !tb || !pb) { free(db); free(tb); free(pb); return; }
    V4Dict d;
    V4Tiles t;
    V4Pools p;
    CHECK(v4DictOpen(&d,db,dl) && v4TilesOpen(&t,tb,tl,&d) &&
          v4PoolsOpen(&p,pb,pl,&d), "validate complete Cyrillic resources");
    CHECK(v4PoolsMatches(&p, &d), "pool matches active dictionary exactly");
    CHECK(sizeof(ScriptBoard) <= 384, "prototype stays under 384 bytes of board RAM");
    checkSourcedVowelFloor(&t, uk);
    ScriptBoard b, repeat;
    CHECK(scriptBoardStart(&b,&t,17), "basic board start");
    CHECK(scriptBoardPeel(&b,0,0) == -1 &&
          !scriptBoardPassComplete(&b), "no secret peels without a pass");
    CHECK(!scriptBoardStartPass(&b,&d,&t,&p,17,3) &&
          !scriptBoardStartPass(&b,&d,&t,&p,17,10), "reject impossible pass lengths");
    for (unsigned length = 4; length <= 9; length++)
        for (uint32_t seed = 0; seed < 32; seed += 7) {
            CHECK(scriptBoardStartPass(&b,&d,&t,&p,seed,length),
                  "start pass seed=%u length=%u", seed, length);
            CHECK(scriptBoardStartPass(&repeat,&d,&t,&p,seed,length) &&
                  memcmp(&b,&repeat,sizeof b)==0, "pass, positions, bag deterministic");
            CHECK(b.passLen >= 4 && b.passLen <= 9 &&
                  b.passTheme < p.themes, "pass word length and theme valid");
            CHECK(v4PoolsCount(&p,b.passTheme,b.passLen)>0 &&
                  v4DictLookupIds(&d,b.pass,b.passLen)>0,
                  "selected password comes from a real WPP2 theme");
            unsigned seen[WPP2_MAX_LEN]={0}, assigned=0;
            for (int col=0; col<SCRIPT_COLS; col++)
                for (int row=0; row<SCRIPT_ROWS; row++) {
                    unsigned id=b.back[col][row];
                    if (!id) continue;
                    CHECK(row >= SCRIPT_ROWS-3 && id <= b.passLen,
                          "pass secret in bottom three rows with valid index");
                    if (id>=1 && id<=WPP2_MAX_LEN) seen[id-1]++;
                    assigned++;
                }
            CHECK(assigned==b.passLen, "one secret location per pass symbol");
            for (unsigned i=0; i<b.passLen; i++)
                CHECK(seen[i]==1 && !b.found[i], "each hidden symbol has one location");
            CHECK(!scriptBoardPassComplete(&b), "unrevealed pass incomplete");
            char word[80];
            CHECK(scriptBoardPassUtf8(&b,&d,word,sizeof word) &&
                  v4DictLookup(&d,word)>0, "pass displays in Cyrillic and is in dictionary");
            CHECK(!scriptBoardPassUtf8(&b,&d,word,2) && !word[0],
                  "pass text buffer bounds enforced");
        }

    // Every draft theme must yield a deterministic, adjacent, scoreable
    // word without the fixed ПРИВЕТ / ПРИВІТ demonstration. The password
    // lives in the lower 3 rows; planting is confined to the upper 5.
    // This also exercises the sparse-theme fallback with real RU/UK packs.
    int seeded = 0, changedColumns = 0, lastColumn = -1;
    CHECK(!scriptBoardPlantThemeWord(NULL,&d,&t,&p,1),
          "null board rejected for theme seed");
    for (unsigned theme = 0; theme < p.themes; theme++)
        for (unsigned variation = 0; variation < 3; variation++) {
            uint32_t seed = (theme << 8) | (variation * 37u + 3u);
            CHECK(scriptBoardStartPass(&b,&d,&t,&p,seed,5),
                  "theme=%u variant=%u password generated", theme, variation);
            ScriptBoard before = b;
            CHECK(scriptBoardPlantThemeWord(&b,&d,&t,&p,seed),
                  "theme=%u variant=%u must have guaranteed playable word",
                  theme, variation);
            CHECK(scriptBoardStartPass(&repeat,&d,&t,&p,seed,5) &&
                  scriptBoardPlantThemeWord(&repeat,&d,&t,&p,seed) &&
                  !memcmp(&b,&repeat,sizeof b),
                  "theme=%u variant=%u seeded playable word reproducible",
                  theme, variation);
            CHECK(!memcmp(b.back,before.back,sizeof b.back) &&
                  !memcmp(b.pass,before.pass,sizeof b.pass) &&
                  !memcmp(b.found,before.found,sizeof b.found) &&
                  !memcmp(b.bag,before.bag,sizeof b.bag) &&
                  b.seed==before.seed && b.bagPos==before.bagPos,
                  "playable word cannot change password locations or RNG");
            // The new shelf-only pathway places a second distinct word in
            // fully independent columns when this dictionary permits it.
            // Both source-backed and compact 40-theme pools must remain
            // reproducible and preserve hidden pass locations and bag state.
            ScriptBoard opening, openingCopy;
            CHECK(scriptBoardStartPass(&opening,&d,&t,&p,seed,5),
                  "start separate two-word opening");
            uint8_t openC[2][6] = {{0}}, openR[2][6] = {{0}};
            uint8_t openLen[2] = {0};
            int openings=scriptBoardPlantOpeningWords(&opening,&d,&t,&p,seed,
                                                       openC,openR,openLen);
            CHECK(openings>=1 && openings<=2,
                  "theme=%u variant=%u has bounded legal opening words",theme,variation);
            CHECK(scriptBoardStartPass(&openingCopy,&d,&t,&p,seed,5) &&
                  scriptBoardPlantOpeningWords(&openingCopy,&d,&t,&p,seed,
                                                NULL,NULL,NULL)==openings &&
                  !memcmp(&opening,&openingCopy,sizeof opening),
                  "two-opening word placement must remain deterministic");
            CHECK(!memcmp(opening.back,before.back,sizeof before.back) &&
                  !memcmp(opening.pass,before.pass,sizeof before.pass) &&
                  !memcmp(opening.bag,before.bag,sizeof before.bag) &&
                  opening.seed==before.seed && opening.bagPos==before.bagPos,
                  "two starting words must preserve all password cells and the tile bag");
            // Check the exact two separately seeded words, not an arbitrary
            // hint, which could legitimately cross both column groups.
            // Following the FIRST seeded path must leave the SECOND word
            // intact and scoring the same after one gravity/refill pass.
            int expected[2]={0};
            uint8_t colsUsed[2][SCRIPT_COLS] = {{0}};
            for(int word=0;word<openings;word++) {
                CHECK(openLen[word]>=4 && openLen[word]<=6,
                      "opening word has a bounded 4..6-symbol path");
                ScriptBoard selected = opening;
                for(int j=0;j<openLen[word];j++) {
                    int col=openC[word][j],row=openR[word][j];
                    CHECK(col>=0 && col<SCRIPT_COLS &&
                          row>=0 && row<SCRIPT_ROWS-3,
                          "opening stays above secret password rows");
                    if(col>=0 && col<SCRIPT_COLS)
                        colsUsed[word][col]=1;
                    CHECK(scriptBoardAdd(&selected,col,row),
                          "every opening tile is adjacent and chainable");
                }
                expected[word]=scriptBoardPreview(&selected,&d,&t,NULL);
                CHECK(expected[word]>0,
                      "each seeded RU/UK opening is an accepted real word");
            }
            if(openings==2 && expected[0]>0 && expected[1]>0) {
                for(int col=0;col<SCRIPT_COLS;col++)
                    CHECK(!colsUsed[0][col] || !colsUsed[1][col],
                          "opening words reserve separate whole columns");
                bool different=openLen[0]!=openLen[1];
                for(int j=0;j<openLen[0] && !different;j++)
                    if(opening.tile[openC[0][j]][openR[0][j]] !=
                       opening.tile[openC[1][j]][openR[1][j]])
                        different=true;
                CHECK(different,"two opening words must have different spellings");
                ScriptBoard played=opening;
                for(int j=0;j<openLen[0];j++)
                    CHECK(scriptBoardAdd(&played,openC[0][j],openR[0][j]),
                          "trace first actual planted word for submit");
                CHECK(scriptBoardSubmit(&played,&d,&t,NULL)==expected[0],
                      "first planted word submits for its previewed score");
                for(int j=0;j<openLen[1];j++)
                    CHECK(scriptBoardAdd(&played,openC[1][j],openR[1][j]),
                          "second opening remains on the board after refill");
                CHECK(scriptBoardPreview(&played,&d,&t,NULL)==expected[1],
                      "second independent word survives first seeded word refill");
            }
            uint8_t hc[SCRIPT_HINT_MAX], hr[SCRIPT_HINT_MAX];
            int hn = scriptBoardHint(&b,&d,&t,hc,hr);
            CHECK(hn>=WPD4_MIN_WORD && hn<=SCRIPT_HINT_MAX,
                  "theme=%u variant=%u exposes a playable hint path",theme,variation);
            if (hn > 0) {
                for (int k=0;k<hn;k++)
                    CHECK(scriptBoardAdd(&b,hc[k],hr[k]),
                          "hint path stays adjacent for theme=%u",theme);
                CHECK(scriptBoardPreview(&b,&d,&t,NULL)>0,
                      "hinted themed word must score for theme=%u",theme);
                scriptBoardClearChain(&b);
            }
            // Planting never alters the bottom three rows (even if an
            // example word is six symbols long). Each one remains peelable.
            for (int col=0;col<SCRIPT_COLS;col++)
                for (int row=SCRIPT_ROWS-3;row<SCRIPT_ROWS;row++)
                    CHECK(b.tile[col][row]==before.tile[col][row] &&
                          b.state[col][row]==before.state[col][row],
                          "seeded word must not overwrite a hidden secret");
            int changedCol = -1;
            for (int col=0;col<SCRIPT_COLS;col++)
                for (int row=0;row<SCRIPT_ROWS-3;row++)
                    if (b.tile[col][row]!=before.tile[col][row])
                        changedCol=col;
            if (changedCol>=0) {
                if (lastColumn>=0 && changedCol!=lastColumn) changedColumns++;
                lastColumn=changedCol;
            }
            seeded++;
        }
    CHECK(seeded==(int)p.themes*3 && changedColumns>5,
          "both languages should generate varied themed board layouts");

    CHECK(scriptBoardStartPass(&b,&d,&t,&p,0x12345678,5), "start full password test");
    CHECK(scriptBoardPeel(&b,-1,0)==-1 && scriptBoardPeel(&b,0,8)==-1,
          "invalid peel coordinates rejected");
    CHECK(scriptBoardPeel(&b,0,0)==0 &&
          b.state[0][0]==SCRIPT_HOLE && scriptBoardPeel(&b,0,0)==-1,
          "miss becomes persistent hole, never reveals twice");
    CHECK(scriptBoardAdd(&b,0,1) && b.chainLen==1, "temporary chain before reveal");
    for (unsigned i=0; i<b.passLen; i++) {
        int foundC=-1, foundR=-1;
        for (int col=0; col<SCRIPT_COLS; col++)
            for (int row=0; row<SCRIPT_ROWS; row++)
                if (b.back[col][row]==i+1) { foundC=col; foundR=row; }
        CHECK(foundC>=0, "secret position exists");
        if(foundC<0) continue;
        CHECK(scriptBoardPeel(&b,foundC,foundR)==(int)(i+1),
              "correct pass letter revealed");
        CHECK(b.state[foundC][foundR]==SCRIPT_DIAMOND &&
              b.tile[foundC][foundR]==b.pass[i] &&
              b.found[i] && !b.back[foundC][foundR] && !b.chainLen,
              "peel leaves permanent chainable diamond, clears chain");
        CHECK(scriptBoardPeel(&b,foundC,foundR)==-1,
              "revealed diamond cannot be peeled twice");
        CHECK(scriptBoardPassComplete(&b)==(i+1==b.passLen),
              "pass completes only after every letter has been found");
    }
    // Spell a real four-gem Cyrillic word strictly in the top four rows,
    // outside all hidden pass locations. A submit must not erase diamonds
    // or move tiles through the already-open hole.
    const uint32_t four[] = {uk?0x0407u:0x0401u,0x0416u,
                             uk?0x0410u:0x0418u,0x041au}; // ЇЖАК / ЁЖИК
    uint8_t fourIds[4] = {0};
    for (int row=0;row<4;row++) {
        int id=v4DictSymbol(&d,four[row]);
        CHECK(id>=0,"four-gem word's letter exists");
        fourIds[row] = (uint8_t)(id < 0 ? 0 : id);
        b.tile[1][row]=(uint8_t)(id<0?0:id);
        b.state[1][row]=SCRIPT_TILE;
        CHECK(scriptBoardAdd(&b,1,row), "script chain goes down adjacent hex cells");
    }
    int tier=0;
    int points=scriptBoardPreview(&b,&d,&t,&tier);
    // The small built-in fixtures mark this word COMMON, but a licensed
    // source pack can legitimately classify it RARE. Require the exact
    // validated dictionary tier rather than assuming a particular editor's
    // frequency classification.
    int actualTier = v4DictLookupIds(&d, fourIds, 4);
    CHECK(points>0 && actualTier>=1 && tier==actualTier,
          "Cyrillic word scored on pass puzzle board at source dictionary tier");
    uint8_t secretC[WPP2_MAX_LEN],secretR[WPP2_MAX_LEN];
    int k=0;
    for(int col=0;col<SCRIPT_COLS;col++)
        for(int row=0;row<SCRIPT_ROWS;row++)
            if(b.state[col][row]==SCRIPT_DIAMOND) {
                secretC[k]=(uint8_t)col;secretR[k]=(uint8_t)row;k++;
            }
    CHECK(k==b.passLen,"all secrets become visible diamonds");
    CHECK(scriptBoardSubmit(&b,&d,&t,&tier)==points && b.chainLen==0,
          "accepted word clears tiles and refills within segments");
    CHECK((b.surveyed[1] & (1u << 3)) && b.clueC==1 && b.clueR==3 &&
          scriptBoardVisibleClue(&b,1,3)==0,
          "accepted Cyrillic word surveys its final fixed coordinate after gravity");
    CHECK(b.state[0][0]==SCRIPT_HOLE, "submitted word cannot fill past a hole");
    for (int i=0;i<k;i++)
        CHECK(b.state[secretC[i]][secretR[i]]==SCRIPT_DIAMOND,
              "revealed password diamond survives unrelated word submission");
    CHECK(scriptBoardPassComplete(&b), "pass completion survives word submission");
    free(db);free(tb);free(pb);
}
int main(int argc,char **argv)
{
    if (argc!=7) {
        fprintf(stderr,"usage: %s ru.dict uk.dict ru.tiles uk.tiles ru.pools uk.pools\n",argv[0]);
        return 2;
    }
    clueTests();
    suite(argv[1],argv[3],argv[5],false);
    suite(argv[2],argv[4],argv[6],true);
    printf(failures?"SCRIPT PASS FAILED: %d failure(s)\n":
                    "SCRIPT PASS OK: Cyrillic passwords, varied theme words, hints, secrets and gravity\n",failures);
    return failures?1:0;
}
