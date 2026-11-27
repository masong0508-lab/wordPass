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
// Every fresh script-ID board receives the same fixed 100-letter weighted
// bag; reordering it for vowel coverage must never alter the distribution,
// RNG, hidden-password positions, or the deterministic Daily practice seed.
static void vowelFloorRegression(const V4Tiles *tiles, const char *tag)
{
    unsigned total = 0;
    for (unsigned id = 0; id < tiles->symbols; id++)
        if ((tiles->vowels >> id) & UINT64_C(1)) total += tiles->weight[id];
    CHECK(total >= 10, "%s WPL2 bag has at least ten vowels", tag);
    const unsigned target = total >= 14 ? 14 : total;
    for (uint32_t n = 1; n <= 256; n++) {
        uint32_t seed = n * 7919u + 0x9e3779b9u;
        ScriptBoard board, repeat;
        CHECK(scriptBoardStart(&board, tiles, seed) &&
              scriptBoardStart(&repeat, tiles, seed) &&
              !memcmp(&board, &repeat, sizeof board),
              "%s seed %u opens deterministically", tag, n);
        CHECK(board.bagPos == SCRIPT_COLS * SCRIPT_ROWS,
              "%s seed %u consumes exactly the 56 opening tiles", tag, n);
        unsigned histogram[WPD4_MAX_SYMBOLS] = {0};
        unsigned frontVowels = 0;
        for (int i = 0; i < WPL2_BAG_SIZE; i++) {
            unsigned id = board.bag[i];
            CHECK(id < tiles->symbols, "%s seed %u bag contains valid ID", tag, n);
            if (id >= tiles->symbols) continue;
            histogram[id]++;
            if (i < SCRIPT_COLS * SCRIPT_ROWS) {
                frontVowels += (unsigned)((tiles->vowels >> id) & UINT64_C(1));
                CHECK(board.tile[i / SCRIPT_ROWS][i % SCRIPT_ROWS] == id &&
                      board.state[i / SCRIPT_ROWS][i % SCRIPT_ROWS] == SCRIPT_TILE,
                      "%s seed %u board matches weighted bag", tag, n);
            }
        }
        CHECK(frontVowels >= target,
              "%s seed %u opening board has at least %u vowels (got %u)",
              tag, n, target, frontVowels);
        for (unsigned id = 0; id < tiles->symbols; id++)
            CHECK(histogram[id] == tiles->weight[id],
                  "%s seed %u keeps original frequency/rarity of symbol %u",
                  tag, n, id);
        for (int c = 0; c < SCRIPT_COLS; c++) {
            unsigned vowels = 0;
            for (int r = 0; r < SCRIPT_ROWS; r++) {
                unsigned id = board.tile[c][r];
                vowels += (unsigned)((tiles->vowels >> id) & UINT64_C(1));
            }
            CHECK(vowels >= (total >= 14 ? 2u : 1u),
                  "%s seed %u column %d retains enough vowels (got %u)",
                  tag, n, c, vowels);
        }
    }
}
static void sparseVowelFloorRegression(void)
{
    // Construct a valid weighted-bag shape with only ten vowels among 100
    // letters (the WPL2 loader's lower bound) and one with exactly fourteen.
    // This exercises the corner cases in which the remaining 44 tiles may
    // contain most vowels, without relying on natural-language frequencies.
    for (int count = 10; count <= 14; count += 4) {
        V4Tiles table = {0};
        table.valid = true;
        table.symbols = 7;
        table.vowels = UINT64_C(1) << 4;
        const uint8_t weights[7] = {20, 20, 20, 20, 10, 5, 5};
        memcpy(table.weight, weights, sizeof weights);
        // Shift four tiles from the last consonant to the vowel for the
        // 14-vowel fixture while retaining an exactly 100-tile histogram.
        if (count == 14) {
            table.weight[4] = 14;
            table.weight[5] = 3;
            table.weight[6] = 3;
        }
        vowelFloorRegression(&table, count == 10 ? "10-vowel" : "14-vowel");
    }
}

static void hintTargetRegression(void)
{
    // Valid two-symbol dictionary: BBBBBB is playable, while AAAAAAAAAA...
    // exists only at length 20. A dense A board must not spend the six-letter
    // search budget on paths that can only complete as a twenty-letter word.
    uint8_t image[104] = {0};
    memcpy(image, "WPD4", 4);
    image[4] = 1; image[6] = 2; image[7] = 1;
    image[8] = WPD4_MIN_WORD; image[9] = WPD4_MAX_WORD;
    image[12] = sizeof image;
    image[20] = 'A'; image[24] = 'B';
    image[28 + 4 * (6 - WPD4_MIN_WORD)] = 1;
    image[28 + 4 * (20 - WPD4_MIN_WORD)] = 1;
    image[100] = 0xfc; // six one-bit B IDs, zero padding/tier
    uint32_t checksum = 2166136261u;
    for (size_t i = 20; i < sizeof image; i++) checksum = (checksum ^ image[i]) * 16777619u;
    for (int i = 0; i < 4; i++) image[16 + i] = (uint8_t)(checksum >> (8 * i));
    V4Dict d;
    CHECK(v4DictOpen(&d, image, sizeof image), "hint decoy dictionary validates");
    V4Tiles t = {0};
    t.valid = true; t.symbols = 2;
    t.alphabet[0] = 'A'; t.alphabet[1] = 'B';
    t.weight[0] = t.weight[1] = 50;
    t.value[0] = t.value[1] = 1;
    ScriptBoard b = {0};
    for (int c = 0; c < SCRIPT_COLS; c++)
        for (int r = 0; r < SCRIPT_ROWS; r++) b.state[c][r] = SCRIPT_TILE;
    for (int r = 0; r < 6; r++) b.tile[SCRIPT_COLS - 1][r] = 1;
    ScriptBoard before = b;
    uint8_t cols[SCRIPT_HINT_MAX], rows[SCRIPT_HINT_MAX];
    int length = scriptBoardHint(&b, &d, &t, cols, rows);
    CHECK(length == 6, "hint reaches final-column word despite long-word decoys");
    CHECK(!memcmp(&b, &before, sizeof b), "hint search never mutates the board");
    if (length == 6) {
        for (int i = 0; i < length; i++)
            CHECK(scriptBoardAdd(&b, cols[i], rows[i]), "hint path is adjacent and nonrepeating");
        CHECK(scriptBoardPreview(&b, &d, &t, NULL) > 0, "targeted hint scores a real word");
    }
    // Two identical legal paths, but only the far path can extinguish
    // the lowest flame. A hint must not blindly pick the earlier column.
    b = before;
    for (int r = 0; r < 6; r++) b.tile[0][r] = 1;
    b.state[0][0] = SCRIPT_BURN;
    b.state[SCRIPT_COLS - 1][5] = SCRIPT_BURN;
    ScriptBoard fireBoard = b;
    length = scriptBoardHint(&b, &d, &t, cols, rows);
    bool savesFlame = false;
    for (int i = 0; i < length; i++)
        if (cols[i] == SCRIPT_COLS - 1 && rows[i] == 5) savesFlame = true;
    CHECK(length == 6 && savesFlame, "hint prioritizes the lowest reachable flame");
    CHECK(!memcmp(&b, &fireBoard, sizeof b), "fire-priority hint is read-only");
    // Add a lower flame that cannot be in any valid word. The bounded
    // priority search must fall back to a legal ordinary hint.
    b.state[3][7] = SCRIPT_BURN;
    fireBoard = b;
    length = scriptBoardHint(&b, &d, &t, cols, rows);
    CHECK(length == 6 && !memcmp(&b, &fireBoard, sizeof b),
          "unreachable flame does not suppress an ordinary hint");
    if (length == 6) {
        for (int i = 0; i < length; i++)
            CHECK(scriptBoardAdd(&b, cols[i], rows[i]), "fallback hint path stays legal");
        CHECK(scriptBoardPreview(&b, &d, &t, NULL) > 0, "fallback hint scores a real word");
    }
    b = before;
    b.state[SCRIPT_COLS - 1][2] = SCRIPT_HOLE;
    before = b;
    CHECK(!scriptBoardHint(&b, &d, &t, cols, rows) && !memcmp(&b, &before, sizeof b),
          "no hint for a broken target path despite many irrelevant prefixes");
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
    vowelFloorRegression(&t, uk ? "Ukrainian" : "Russian");
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
    // Exhaust the current bag and damage the NEXT shuffle's weights so
    // refill must fail. A valid word cannot partially clear the real board,
    // consume its seed, publish a clue, or pretend it was scored.
    V4Tiles damaged = t;
    damaged.weight[0]++;
    uint8_t originalPos = b.bagPos;
    b.bagPos = WPL2_BAG_SIZE;
    ScriptBoard rejected = b;
    tier = 99;
    CHECK(scriptBoardSubmit(&b,&d,&damaged,&tier)==0 && tier==0 &&
          !memcmp(&b,&rejected,sizeof b),
          "failed refill leaves full Cyrillic board, chain, RNG and clues unchanged");
    b.bagPos = originalPos;
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
    hintTargetRegression();
    sparseVowelFloorRegression();
    test(argv[1],argv[3],false);
    test(argv[2],argv[4],true);
    printf(failures?"SCRIPT BOARD FAILED: %d failures\n":"SCRIPT BOARD OK: 33-letter hex chains, preview, submit, deterministic refill\n",failures);
    return failures?1:0;
}
