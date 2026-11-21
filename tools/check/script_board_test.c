// Script-game prototype: genuine 33-tile WPD4/WPL2 lookup through the same
// shifted hex grid geometry, score preview, submit and deterministic refill.
// Tiny synthetic fixture is NOT a full Russian/Ukrainian language pack.
#include "dict_v4.h"
#include "tiles_v2.h"
#include "script_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL "); printf(__VA_ARGS__); putchar('\n'); } } while (0)
static uint8_t *readFile(const char *name, size_t *size)
{
    FILE *f = fopen(name, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *p = malloc((size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); *size = (size_t)n; return p;
}
static void test(const char *dictFile, const char *tileFile, bool uk)
{
    size_t nd=0, nt=0;
    uint8_t *db=readFile(dictFile,&nd), *tb=readFile(tileFile,&nt);
    CHECK(db && tb, "fixtures readable");
    if (!db || !tb) { free(db); free(tb); return; }
    V4Dict d;
    V4Tiles t;
    CHECK(v4DictOpen(&d, db, nd) && v4TilesOpen(&t, tb, nt, &d), "script resources validate together");
    ScriptBoard b, copy;
    CHECK(scriptBoardStart(&b, &t, 0x12345678), "start 7x8 Cyrillic board");
    CHECK(scriptBoardStart(&copy, &t, 0x12345678) &&
          memcmp(&b, &copy, sizeof b) == 0, "seeded script deal reproducible");
    CHECK(sizeof b <= 384, "prototype state uses less than 384 bytes of RAM");
    const char *word=uk?"ПРИВІТ":"ПРИВЕТ";
    const uint32_t cps[]={0x041f,0x0420,0x0418,0x0412,uk?0x0406:0x0415,0x0422};
    for (int r=0; r<6; r++) {
        int id=v4DictSymbol(&d,cps[r]);
        CHECK(id >= 0, "Cyrillic codepoint belongs in alphabet");
        b.tile[0][r]=(uint8_t)id;
        b.state[0][r]=SCRIPT_TILE;
        copy.tile[0][r]=(uint8_t)id;
        copy.state[0][r]=SCRIPT_TILE;
    }
    CHECK(!scriptBoardAdd(&b,-1,0) && !scriptBoardAdd(&b,0,8), "reject out of bounds touch");
    CHECK(scriptBoardAdd(&b,0,0), "first stylus tile");
    CHECK(!scriptBoardAdd(&b,3,3), "no teleport between hex cells");
    CHECK(scriptBoardAdd(&b,0,1) && scriptBoardAdd(&b,0,2), "hex-neighbor chain");
    CHECK(!scriptBoardAdd(&b,0,2), "ignore duplicate tail");
    CHECK(scriptBoardAdd(&b,0,1) && b.chainLen == 2, "backtrack one gem");
    CHECK(scriptBoardAdd(&b,0,2) && scriptBoardAdd(&b,0,3) &&
          scriptBoardAdd(&b,0,4) && scriptBoardAdd(&b,0,5), "word chain of six gems");
    char out[80];
    CHECK(scriptBoardChainUtf8(&b,&d,out,sizeof out) && !strcmp(out,word),
          "symbol IDs reconstruct correct Cyrillic word");
    CHECK(!scriptBoardChainUtf8(&b,&d,out,4) && !out[0],
          "too-short UTF-8 output buffer is safe");
    int tier=0;
    int pts=scriptBoardPreview(&b,&d,&t,&tier);
    CHECK(pts > 0 && tier == 1, "common Cyrillic word accepted and scored");
    CHECK(!scriptBoardAdd(&b,0,3) || b.chainLen == 4, "truncate back through old gem");
    if (b.chainLen != 6) {
        scriptBoardClearChain(&b);
        for (int r=0; r<6; r++) CHECK(scriptBoardAdd(&b,0,r), "rechain after truncation");
    }
    b.state[0][0]=SCRIPT_BURN; b.state[0][1]=SCRIPT_DIAMOND;
    int boosted=scriptBoardPreview(&b,&d,&t,&tier);
    CHECK(boosted > pts && tier == 1, "burn and pass-diamond boost script score");
    b.state[0][0]=SCRIPT_TILE; b.state[0][1]=SCRIPT_TILE;
    // A legal chain with one invalid tile index must be refused cleanly.
    uint8_t save=b.tile[0][1];
    b.tile[0][1]=255;
    CHECK(scriptBoardPreview(&b,&d,&t,&tier)==0 && tier==0 &&
          scriptBoardSubmit(&b,&d,&t,&tier)==0 && b.chainLen==6,
          "corrupted tile refuses submit without losing chain");
    b.tile[0][1]=save;
    ScriptBoard before=b;
    CHECK(scriptBoardSubmit(&b,&d,&t,&tier)==pts && !b.chainLen, "submit refreshes consumed cells");
    CHECK(scriptBoardSubmit(&copy,&d,&t,&tier)==0, "unselected chain never submits");
    for (int r=0; r<6; r++)
        CHECK(b.state[0][r]==SCRIPT_TILE && b.tile[0][r]<t.symbols,
              "consumed Cyrillic tiles refilled with playable IDs");
    CHECK(scriptBoardStart(&copy,&t,0x12345678), "reset seeded game");
    for (int r=0; r<6; r++) {
        copy.tile[0][r]=before.tile[0][r];
        copy.state[0][r]=SCRIPT_TILE;
        CHECK(scriptBoardAdd(&copy,0,r), "reproduce Cyrillic chain");
    }
    CHECK(scriptBoardSubmit(&copy,&d,&t,&tier)==pts &&
          memcmp(&b,&copy,sizeof b)==0, "same seed and move sequence replay identically");
    CHECK(v4DictLookup(&d,word)==1, "dictionary UTF-8 and chained IDs agree");
    free(db);free(tb);
}
int main(int argc,char **argv)
{
    if(argc!=5) { fprintf(stderr,"usage: %s ru.wpd4 uk.wpd4 ru.wpl2 uk.wpl2\n",argv[0]);return 2; }
    test(argv[1],argv[3],false);
    test(argv[2],argv[4],true);
    printf(failures?"SCRIPT BOARD FAILED: %d failures\n":"SCRIPT BOARD OK: 33-letter hex chains, preview, submit, deterministic refill\n",failures);
    return failures?1:0;
}
