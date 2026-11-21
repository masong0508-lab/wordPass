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
    for (int row=0;row<4;row++) {
        int id=v4DictSymbol(&d,four[row]);
        CHECK(id>=0,"four-gem word's letter exists");
        b.tile[1][row]=(uint8_t)(id<0?0:id);
        b.state[1][row]=SCRIPT_TILE;
        CHECK(scriptBoardAdd(&b,1,row), "script chain goes down adjacent hex cells");
    }
    int tier=0;
    int points=scriptBoardPreview(&b,&d,&t,&tier);
    CHECK(points>0 && tier==1,"Cyrillic word scored on pass puzzle board");
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
    suite(argv[1],argv[3],argv[5],false);
    suite(argv[2],argv[4],argv[6],true);
    printf(failures?"SCRIPT PASS FAILED: %d failure(s)\n":
                    "SCRIPT PASS OK: Cyrillic passwords, secrets, peels, firebreaks, gravity\n",failures);
    return failures?1:0;
}
