// Native Cyrillic fire/shelf playthrough tests, using tiny test-only
// RU/UK WPD4 + WPL2 + WPP2 resources. The retail 26-letter game is unchanged.
#include "script_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(expr, ...) do { if (!(expr)) { failures++; printf("FAIL "); printf(__VA_ARGS__); putchar('\n'); } } while (0)

static uint8_t *readFile(const char *name, size_t *size)
{
    FILE *f = fopen(name,"rb");
    if (!f || fseek(f,0,SEEK_END)) { if(f) fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f,0,SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf,1,(size_t)n,f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f); *size=(size_t)n; return buf;
}
static int countState(const ScriptBoard *b, int kind)
{
    int n=0;
    for (int c=0;c<SCRIPT_COLS;c++)
        for (int r=0;r<SCRIPT_ROWS;r++)
            n += b->state[c][r] == kind;
    return n;
}
static void exercise(const char *dbFile,const char *tbFile,const char *pbFile, bool uk)
{
    size_t ds=0,ts=0,ps=0;
    uint8_t *db=readFile(dbFile,&ds),*tb=readFile(tbFile,&ts),*pb=readFile(pbFile,&ps);
    CHECK(db && tb && pb,"RU/UK test resources readable");
    if(!db || !tb || !pb) { free(db); free(tb); free(pb); return; }
    V4Dict dict;
    V4Tiles tiles;
    V4Pools pools;
    bool valid=v4DictOpen(&dict,db,ds) &&
        v4TilesOpen(&tiles,tb,ts,&dict) &&
        v4PoolsOpen(&pools,pb,ps,&dict);
    CHECK(valid,"validate WPD4/WPL2/WPP2 before native fire test");
    if(!valid) { free(db); free(tb); free(pb); return; }

    ScriptBoard b, repeat;
    CHECK(scriptBoardStart(&b,&tiles,2718),"create Cyrillic fire test board");
    // Keep the seeded fire fixture pristine: hint assertions mutate only
    // a disposable copy, so fire determinism still compares equal seeds.
    ScriptBoard hintBoard = b;
    // The intentionally small lexicon is still hintable because one real
    // six-symbol word is planted on adjacent touchscreen gems.
    const uint32_t greeting[] = {
        0x041f, 0x0420, 0x0418, 0x0412, uk ? 0x0406u : 0x0415u, 0x0422
    };
    for (int row=0; row<6; row++) {
        int symbol = v4DictSymbol(&dict, greeting[row]);
        CHECK(symbol >= 0, "seeded hint symbol in alphabet");
        hintBoard.tile[0][row] = (uint8_t)(symbol < 0 ? 0 : symbol);
    }
    uint8_t hintC[SCRIPT_HINT_MAX], hintR[SCRIPT_HINT_MAX];
    int hintLen = scriptBoardHint(&hintBoard, &dict, &tiles, hintC, hintR);
    CHECK(hintLen >= 3 && hintLen <= SCRIPT_HINT_MAX,
          "bounded solver discovers at least one Cyrillic word path");
    hintBoard.chainLen = (uint8_t)hintLen;
    for (int i=0; i<hintLen; i++) {
        hintBoard.chainC[i] = hintC[i];
        hintBoard.chainR[i] = hintR[i];
    }
    CHECK(scriptBoardPreview(&hintBoard,&dict,&tiles,NULL) > 0,
          "hint path is geometrically adjacent and a valid real word");
    scriptBoardClearChain(&hintBoard);
    for (int c=0;c<SCRIPT_COLS;c++)
        for(int row=0;row<SCRIPT_ROWS;row++)
            hintBoard.state[c][row] = SCRIPT_HOLE;
    CHECK(scriptBoardHint(&hintBoard,&dict,&tiles,hintC,hintR)==0,
          "bounded hint solver refuses a board with no chainable tiles");
    CHECK(scriptBoardStart(&repeat,&tiles,2718) &&
          !memcmp(&b,&repeat,sizeof b),"seeded fire boards reproducible");
    CHECK(scriptBoardIgnite(&b,1784) && scriptBoardIgnite(&repeat,1784) &&
          !memcmp(&b,&repeat,sizeof b),"deterministic fire ignition");
    CHECK(countState(&b,SCRIPT_BURN)==1,"one tile ignited");
    int fc=-1,fr=-1;
    for(int c=0;c<SCRIPT_COLS;c++)
        for(int r=0;r<SCRIPT_ROWS;r++)
            if(b.state[c][r]==SCRIPT_BURN) { fc=c;fr=r; }
    CHECK(fc>=0 && fr==0,"flame starts on topmost plain gem");
    CHECK(!scriptBoardFireStep(&b,&tiles) &&
          !scriptBoardFireStep(&repeat,&tiles) &&
          !memcmp(&b,&repeat,sizeof b),"same seeded flame movement and bag refill");
    CHECK(b.state[fc][1]==SCRIPT_BURN && b.state[fc][0]==SCRIPT_TILE,
          "flame descends one tile, vacated slot is refilled");
    CHECK(countState(&b,SCRIPT_EMPTY)==0,"no unfilled temporary cells after movement");

    CHECK(scriptBoardStart(&b,&tiles,18),"reset for diamond firebreak");
    b.state[3][0]=SCRIPT_BURN;
    b.state[3][1]=SCRIPT_DIAMOND;
    CHECK(!scriptBoardFireStep(&b,&tiles) &&
          b.state[3][0]==SCRIPT_BURN &&
          b.state[3][1]==SCRIPT_DIAMOND,
          "revealed password diamond is an immutable firebreak");
    CHECK(scriptBoardStart(&b,&tiles,18),"reset for hole chimney");
    b.state[3][0]=SCRIPT_BURN;
    b.state[3][1]=SCRIPT_HOLE;
    CHECK(!scriptBoardFireStep(&b,&tiles) &&
          b.state[3][1]==SCRIPT_HOLE &&
          b.state[3][2]==SCRIPT_BURN,
          "fire falls through holes without erasing them");
    CHECK(scriptBoardStart(&b,&tiles,18),"reset for bottom escape");
    b.state[0][SCRIPT_ROWS-1]=SCRIPT_BURN;
    CHECK(scriptBoardFireStep(&b,&tiles),
          "burning bottom tile ends shelf immediately");

    CHECK(scriptBoardStartPass(&b,&dict,&tiles,&pools,0x12345678,5),
          "create a themed Cyrillic pass");
    int secretC=-1,secretR=-1,index=-1;
    for(int c=0;c<SCRIPT_COLS;c++)
        for(int r=0;r<SCRIPT_ROWS;r++)
            if(b.back[c][r] && secretC<0) {
                secretC=c;secretR=r;index=b.back[c][r];
            }
    CHECK(secretC>=0 && index>=1,"locate one hidden password letter");
    if(secretC>=0) {
        b.state[secretC][secretR]=SCRIPT_BURN;
        CHECK(scriptBoardPeel(&b,secretC,secretR)==-2 &&
              b.state[secretC][secretR]==SCRIPT_HOLE &&
              !b.found[index-1],
              "scorched Cyrillic password ends as a fatal peel");
    }

    // Every tier is deterministic across seeds but actually exercises its
    // requested lengths, instead of silently keeping ALL passwords at 5.
    // These targets are editorial/engineering balance scaffolding and the
    // WPP2 picker still chooses only a real source-backed password.
    const struct { unsigned shelf; int low, high; } windows[] = {
        {1,4,5}, {3,4,5}, {4,5,6}, {9,5,6}, {10,5,7},
        {19,5,7}, {20,6,8}, {34,6,8}, {35,6,9}, {99,6,9}
    };
    CHECK(scriptRulesPassTarget(1,0x1234u)==
          scriptRulesPassTarget(1,0x1234u),
          "same shelf and seed reproduce password length");
    for (unsigned w=0; w<sizeof windows/sizeof windows[0]; w++) {
        unsigned seen=0;
        for (uint32_t seed=1;seed<=256;seed++) {
            int length=scriptRulesPassTarget(windows[w].shelf,seed);
            CHECK(length>=windows[w].low && length<=windows[w].high,
                  "shelf %u password length stays inside its difficulty band",
                  windows[w].shelf);
            seen |= 1u << length;
        }
        unsigned expected=0;
        for(int len=windows[w].low;len<=windows[w].high;len++)
            expected |= 1u << len;
        CHECK((seen & expected)==expected,
              "every configured target length appears across deterministic seeds");
    }
    ScriptRules rules;
    scriptRulesStart(&rules,1,730);
    CHECK(rules.shelf==1 && rules.turns==0 && rules.heat==0 &&
          rules.status==SCRIPT_RUNNING && rules.heatThreshold==8 &&
          rules.burnEvery==4, "first shelf uses gentle, bounded heat and fire cadence");
    CHECK(scriptRulesWordHeat(3)==3 && scriptRulesWordHeat(4)==2 &&
          scriptRulesWordHeat(5)==1 && scriptRulesWordHeat(6)==1 &&
          scriptRulesWordHeat(8)==0, "length-based heating honors longer words");
    scriptBoardStart(&b,&tiles,730);
    for(int k=0;k<4;k++) {
        CHECK(scriptRulesTurn(&rules,&b,&tiles,2,false,false)==0,
              "no arbitrary bonus before pass completion");
        CHECK(rules.status==SCRIPT_RUNNING,
              "valid Cyrillic moves do not prematurely lose the shelf");
    }
    CHECK(rules.turns==4 && rules.words==4 && rules.ignitions==1 &&
          rules.heat==0 && countState(&b,SCRIPT_BURN)==1,
          "four short words fill the heat meter and ignite a flame");
    scriptRulesStart(&rules,25,730);
    CHECK(rules.heatThreshold==5 && rules.burnEvery==2,
          "later shelf increases pressure without unbounded heat");
    scriptBoardStart(&b,&tiles,730);
    for(int k=0;k<3;k++) (void)scriptRulesTurn(&rules,&b,&tiles,2,true,false);
    CHECK(rules.peels==3 && rules.ignitions>=1,
          "peels cost turns and add heat to the harder shelf");
    scriptRulesStart(&rules,1,730);
    scriptBoardStart(&b,&tiles,730);
    b.state[0][SCRIPT_ROWS-1]=SCRIPT_BURN;
    rules.burnTick=(uint8_t)(rules.burnEvery-1);
    (void)scriptRulesTurn(&rules,&b,&tiles,1,false,false);
    CHECK(rules.status==SCRIPT_FAILED && rules.failReason==SCRIPT_LOSS_FIRE,
          "fire escaping at bottom loses the shelf with the correct reason");
    CHECK(!scriptRulesTurn(&rules,&b,&tiles,1,false,false) &&
          rules.turns==1 && rules.failReason==SCRIPT_LOSS_FIRE,
          "lost shelves preserve their loss reason and cannot accumulate turns");

    // A recovery word can be placed around holes/flames without touching
    // passwords. Spending it is a real heat/fire turn but not a word or peel.
    scriptRulesStart(&rules,1,217);
    CHECK(scriptBoardStartPass(&b,&dict,&tiles,&pools,217,5),
          "create a real draft themed shelf for rescue");
    for (int col=0;col<SCRIPT_COLS;col+=2)
        b.state[col][0]=SCRIPT_HOLE; // obstruct preferred paths
    b.state[1][0]=SCRIPT_BURN;
    ScriptBoard obstructed = b;
    CHECK(scriptBoardPlantThemeWord(&b,&dict,&tiles,&pools,217^0x31415926u),
          "rescue reroutes around obstructed top-row columns");
    CHECK(!memcmp(b.back,obstructed.back,sizeof b.back) &&
          !memcmp(b.found,obstructed.found,sizeof b.found) &&
          !memcmp(b.bag,obstructed.bag,sizeof b.bag) &&
          b.seed==obstructed.seed && b.bagPos==obstructed.bagPos,
          "rescue does not alter hidden password, bag or RNG state");
    for (int col=0;col<SCRIPT_COLS;col++)
        if (obstructed.state[col][0]!=SCRIPT_TILE)
            CHECK(b.state[col][0]==obstructed.state[col][0] &&
                  b.tile[col][0]==obstructed.tile[col][0],
                  "rescue cannot overwrite burns or holes");
    uint8_t rescueC[SCRIPT_HINT_MAX],rescueR[SCRIPT_HINT_MAX];
    CHECK(scriptBoardHint(&b,&dict,&tiles,rescueC,rescueR)>=3,
          "rerouted rescue keeps a scoreable valid Cyrillic chain");
    CHECK(scriptRulesRescueTurn(&rules,&b,&tiles)==0 &&
          rules.turns==1 && rules.words==0 && rules.peels==0 &&
          rules.heat==2 && rules.status==SCRIPT_RUNNING,
          "recovery costs a real turn and two heat, no word/peel statistics");
    // No placement possible: must fail atomically, not remove obstacles.
    CHECK(scriptBoardStartPass(&b,&dict,&tiles,&pools,218,5),
          "create fully blocked recovery fixture");
    for (int col=0;col<SCRIPT_COLS;col++)
        b.state[col][0]=SCRIPT_HOLE;
    obstructed=b;
    CHECK(!scriptBoardPlantThemeWord(&b,&dict,&tiles,&pools,218^0x31415926u) &&
          !memcmp(&b,&obstructed,sizeof b),
          "impossible rescue preserves blocked board byte for byte");

    CHECK(scriptBoardStartPass(&b,&dict,&tiles,&pools,77,5),
          "start pass for completion and bonus");
    // Calling rescue directly cannot claim the completion bonus from an
    // already revealed pass even if caller's status has not caught up.
    scriptRulesStart(&rules,2,77);
    for(int i=0;i<b.passLen;i++) b.found[i]=1;
    CHECK(scriptRulesRescueTurn(&rules,&b,&tiles)==0 &&
          rules.turns==0 && rules.status==SCRIPT_RUNNING &&
          rules.lastBonus==0, "rescue cannot complete or award a finished pass");
    scriptRulesStart(&rules,2,77);
    for(int i=0;i<b.passLen;i++) b.found[i]=1;
    int bonus=scriptRulesTurn(&rules,&b,&tiles,1,true,false);
    CHECK(rules.status==SCRIPT_PASSED && bonus>0 && bonus==rules.lastBonus &&
          rules.turns==1 && rules.peels==1 && rules.failReason==SCRIPT_LOSS_NONE,
          "final Cyrillic letter awards exactly one shelf bonus");
    CHECK(!scriptRulesTurn(&rules,&b,&tiles,2,false,false) &&
          rules.turns==1 && rules.lastBonus==bonus,
          "completed shelf freezes its score and turn count");

    scriptRulesStart(&rules,3,10);
    scriptBoardStart(&b,&tiles,10);
    (void)scriptRulesTurn(&rules,&b,&tiles,1,true,true);
    CHECK(rules.status==SCRIPT_FAILED && rules.peels==1 &&
          rules.failReason==SCRIPT_LOSS_SCORCH && !rules.ignitions,
          "fatal scorched password immediately loses with distinct reason");
    CHECK(!scriptRulesTurn(&rules,&b,&tiles,1,false,false) &&
          rules.failReason==SCRIPT_LOSS_SCORCH,
          "further actions cannot overwrite terminal scorch reason");
    scriptRulesStart(&rules,3,11);
    CHECK(rules.status==SCRIPT_RUNNING && rules.failReason==SCRIPT_LOSS_NONE,
          "a fresh shelf clears the previous loss reason");
    free(db); free(tb); free(pb);
    (void)uk;
}
int main(int argc,char **argv)
{
    if(argc!=7) { fprintf(stderr,"usage: %s ru.dict uk.dict ru.tiles uk.tiles ru.pools uk.pools\n",argv[0]);return 2; }
    exercise(argv[1],argv[3],argv[5],false);
    exercise(argv[2],argv[4],argv[6],true);
    printf(failures?"SCRIPT FIRE FAILED: %d failure(s)\n":
                   "SCRIPT FIRE OK: Cyrillic flames, rescue, heat, loss and shelf completion\n",failures);
    return failures?1:0;
}
