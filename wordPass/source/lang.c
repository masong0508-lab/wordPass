// Languages, pure part: the language table, the letter tables of the active language (tile weights, letter
// values, vowel set, Qu flag, the four rare letters of the bag) and langApply, which makes one language's
// three buffers the active ones. Reading the buffers from the filesystem is langload.c, so the native tests
// can drive everything here without a filesystem. English is compiled in and its tables are the defaults.
#include "game.h"
#include <string.h>
#include "dict_bin.h"
#include "pools_bin.h"

static const char *const CODES[LANG_COUNT] = { "en", "es", "fr", "de", "it", "pt", "nl", "sv", "pl", "da", "fi" };
// the labels of the options row and the title tag: A-Z only, ten letters at most
static const char *const NAMES[LANG_COUNT] = { "ENGLISH", "ESPANOL", "FRANCAIS", "DEUTSCH", "ITALIANO", "PORTUGUES", "NEDERLANDS", "SVENSKA", "POLSKI", "DANSK", "SUOMI" };

const char *langCode(int idx) { return CODES[idx >= 0 && idx < LANG_COUNT ? idx : 0]; }
const char *langName(int idx) { return NAMES[idx >= 0 && idx < LANG_COUNT ? idx : 0]; }

// English: the weights sum to 102 (Q stands for Qu; the four rare letters share two slots a cycle, so a bag is 100)
#define EN_TABLES { \
    { 9,2,3,4,12,2,3,3,9,1,1,4,3,6,8,2,1,6,5,6,4,2,2,1,2,1 }, \
    { 1,3,3,2,1,4,2,4,1,8,5,1,3,1,1,3,10,1,1,1,1,4,4,8,4,10 }, \
    { 'j' - 'a', 'q' - 'a', 'x' - 'a', 'z' - 'a' }, \
    (1u << 0) | (1u << 4) | (1u << 8) | (1u << 14) | (1u << 20), \
    1 \
}
static const LangTables ENGLISH = EN_TABLES;

static LangTables T = EN_TABLES;   // English until a language is applied
static int cur;

int langCur(void) { return cur; }
const LangTables *langTables(void) { return &T; }
int  letterValueEn(int l) { return ENGLISH.value[l]; }
int  letterValue(int l) { return T.value[l]; }
int  langWeight(int l) { return T.weight[l]; }
bool langIsVowel(int l) { return (T.vowels >> l) & 1; }
bool langQuTile(void) { return T.qu != 0; }
int  langRare(int k) { return T.rare[k & 3]; }

// A letters.bin (WPL1, 68 bytes) into a table, validated: magic, a weight table of a sane size, values 1..10 on
// every letter that has a tile, rare letters that have a tile (255 = none), at least one vowel tile.
bool langParseLetters(const u8 *b, size_t n, LangTables *out)
{
    if (!b || n < LANG_LETTERS_BYTES || memcmp(b, "WPL1", 4) != 0) return false;
    LangTables t;
    memset(&t, 0, sizeof(t));
    t.qu = b[4] & 1;
    for (int i = 0; i < 4; i++) t.rare[i] = b[6 + i] < 26 && b[16 + b[6 + i]] ? b[6 + i] : 255;
    t.vowels = (u32)b[12] | ((u32)b[13] << 8) | ((u32)b[14] << 16) | ((u32)b[15] << 24);
    t.vowels &= (1u << 26) - 1;
    int sum = 0;
    bool vowelTile = false;
    for (int l = 0; l < 26; l++) {
        t.weight[l] = b[16 + l];
        t.value[l] = b[42 + l];
        sum += t.weight[l];
        if (t.weight[l] && (t.value[l] < 1 || t.value[l] > 10)) return false;
        if (t.weight[l] && ((t.vowels >> l) & 1)) vowelTile = true;
    }
    if (sum < 50 || sum > 200 || !vowelTile) return false;
    *out = t;
    return true;
}

// Make a language active: its validation dictionary, its pass pools and its letter tables. dict and pools NULL
// (or idx 0) select the compiled-in English; letters is only read for another language. Returns false, and
// leaves English active, when a buffer is not what its magic says.
bool langApply(int idx, const u8 *dict, const u8 *pools, const u8 *letters, size_t lettersBytes)
{
    LangTables t = ENGLISH;
    bool ok = true;
    if (idx > 0 && idx < LANG_COUNT && dict && pools && langParseLetters(letters, lettersBytes, &t)) {
        dictSet(dict);
        if (!poolsSet(pools)) ok = false;
    } else ok = idx == 0;
    if (!ok) { t = ENGLISH; idx = 0; }
    if (idx == 0) { dictSet(dict_bin); poolsSet(NULL); }
    T = t;
    cur = idx;
    boardBagReset();
    return ok;
}

void langInitEnglish(void) { T = ENGLISH; cur = 0; }
