// wordPass - shared game definitions
#ifndef WORDPASS_GAME_H
#define WORDPASS_GAME_H

#include <nds.h>
#include <stdbool.h>

#define COLS 7
#define ROWS 8
#define MAX_CHAIN 10
#define LEDGER_N 8            // words remembered per shelf for the repeat penalty and the HUD ledger

#define TILE_W 24
#define TILE_H 22
#define GRID_X 4
#define GRID_Y 2
#define ODD_SHIFT 11          // odd columns are pushed down half a tile

#define SCREEN_W 256
#define SCREEN_H 192

// gem sprites are blitted with 32-bit copies, so a gem row must be a whole number of words
_Static_assert((TILE_W * 2) % 4 == 0, "gem rows must be word multiples");

// Cell states on the front layer
enum {
    CELL_EMPTY = 0,   // nothing here: transient between a clear or fire step and the gravity
                      // pass that refills every segment of the column, under holes too
    CELL_TILE,        // normal letter gem
    CELL_BURN,        // burning gem, creeps downward
    CELL_HOLE,        // peeled: shows the back layer number forever
    CELL_SECRET       // revealed pass letter: diamond, chainable, firebreak
};

typedef struct {
    u8 letter;        // 0..25 ('q' - 'a' stands for Qu)
    u8 state;
    s16 yoff;         // falling offset in 1/16 px (negative = above its slot)
    s16 yv;           // fall velocity in 1/16 px per frame
    u8 squash;        // frames of landing squash left
    u8 flash;         // frames of white flash left (word removal)
} Cell;

#define YOFF_PX(cell) ((cell)->yoff >> 4)
#define SQUASH_MAX 6

enum {
    ST_TITLE = 0,
    ST_PLAY,
    ST_PASSED,        // pass accepted, shelf cleared
    ST_FAIL,
    ST_PAUSE,
    ST_OPTIONS,       // 5
    ST_STATS          // 6 (reserved: lifetime stats screen)
};

enum {
    FAIL_NONE = 0,
    FAIL_FIRE,        // Library Lost
    FAIL_SCORCH,      // Pass Destroyed
    FAIL_WORDS        // Out of Words
};

typedef struct {
    Cell cell[COLS][ROWS];
    u8   back[COLS][ROWS];    // 0 = empty, else pass letter index + 1
    u8   scorched[COLS][ROWS];

    char pass[12];            // the shelf's password
    u8   passLen;
    u8   found[12];           // per pass letter: revealed?

    u8   chainC[MAX_CHAIN], chainR[MAX_CHAIN];
    u8   chainLen;

    int  shelf;               // 1-based level
    int  score, shelfStartScore;
    int  turn;
    int  heat, heatThreshold;
    int  burnEvery;           // fire advances every N turns
    int  burnTimer;
    int  pulseEvery;          // Lockdown: +2 heat every N turns
    int  lockTier;            // Lockdown tier 0..3 (shelf 6-8, 9-11, 12-14, 15+)
    bool overtime;            // past the shelf's turn budget: extra heat every turn
    u8   fireC[2], fireR[2];  // cells spawnFire ignited this turn, for main.c's burst
    int  fireN;               // how many (main.c clears it after use)
    int  fail;
    int  state;
    int  stateTimer;
    int  peekHeld;
    int  wordsThisShelf;
    int  lastWordLen, lastWordScore;
    char lastWord[16];
    char msg[32];
    int  msgTimer;
    bool lockdown;
    char bestWord[16];        // per-shelf results card
    int  bestWordScore;
    int  peels;
    int  mode;                // MODE_CLASSIC or MODE_DAILY
    int  dailySeed;           // YYYYMMDD
    int  hintShown;
    int  lastWordTier;        // 0 none, 1 common, 2 rare (last accepted word)
    int  rareWords;           // rare words spelled this shelf
    int  optRow;              // highlighted row on the options screen
    // ---- scoring ledger (per shelf): a ring of the words spelled, newest at ledgerHead - 1
    char ledger[LEDGER_N][14];   // 13 chars: a 10-gem chain through two Qu gems is 12
    int  ledgerPts[LEDGER_N];
    int  ledgerN, ledgerHead;
    int  uniqueWords;         // words this shelf that were not repeats
    int  lastWordDiamonds;    // CELL_SECRET cells in the last scored chain (capped at 2)
    int  lastWordRepeat;      // the last word was already in the ledger (half score)
    int  lastWordBurn;        // the last word went through a flame
    int  speedBonus, peelBonus, passBonusKind;   // results card: kind 0 none, 1 CLEAN, 2 PERFECT
    int  newBest;             // recordScore changed a saved record (cleared by startShelf)
} Game;

enum { MODE_CLASSIC = 0, MODE_DAILY };

// ---- settings
enum { THEME_JEWEL = 0, THEME_INK, THEME_CANDY, THEME_EMBER, THEME_COUNT };
enum { FRAME_GOLD = 0, FRAME_SILVER, FRAME_ACCENT, FRAME_COUNT };
enum { HAND_RIGHT = 0, HAND_LEFT };
enum { FIRE_RELAXED = 0, FIRE_NORMAL, FIRE_FAST, FIRE_COUNT };
enum { LIB_FULL = 0, LIB_COMMON, LIB_COUNT };   // dictionary: every ENABLE word, or common words only
#define ACCENT_COUNT 9                          // AUTO plus eight fixed colours

typedef struct {
    u32 magic;
    u8  theme, frame, accent, hand, fire, music, library, pad1;
    int bestClassic;        // best Classic score
    int bestClassicShelf;   // highest shelf cleared
    int bestDaily;          // best Daily score
    int bestDailyDate;      // YYYYMMDD it was set on
} Settings;

extern Settings S;
void settingsInit(void);
void settingsDefaults(void);
bool settingsSave(void);
bool settingsPersistent(void);

#define OPT_ROWS 7
void gfxApplySettings(void);          // rebuild caches after a settings change
int  gfxOptionRowAt(int px, int py);  // -1 none, 0..OPT_ROWS-1 rows, OPT_ROWS = back
bool gfxInTitleOptions(int px, int py);

extern Game G;

// board.c
void boardNewShelf(Game *g);
void boardDebugFireStep(Game *g);    // test channel: one fire advance plus gravity, no turn
bool boardTryAddToChain(Game *g, int c, int r);
void boardClearChain(Game *g);
int  boardSubmit(Game *g);           // returns score gained, 0 if invalid
int  boardChainScore(const Game *g); // exactly what boardSubmit would award for the current chain (0 if invalid)
int  ledgerIndex(const Game *g, int age);   // ring index of the age-th newest ledger entry (0 = newest)
int  boardHeatFor(int len);          // heat a word of this length adds: 3, 2, 1, 1, 0 (3,4,5,6,7+)
int  boardPeel(Game *g, int c, int r); // returns 0 none, 1 hole, 2 secret, 3 scorched, -1 fail; a peel is a turn
void boardAnimate(Game *g);
bool boardIsChainable(const Game *g, int c, int r);
int  boardNeighbors(int c, int r, int outC[6], int outR[6]);
int  boardBackNumber(const Game *g, int c, int r);
bool boardHasWords(const Game *g);
bool boardWordOk(const char *w, int len);     // accepted under the current LIBRARY setting
int  boardWordTier(const char *w, int len);   // raw dictionary tier: 0 none, 1 common, 2 rare
int  boardLowestFireRow(const Game *g);
bool boardFireAbove(const Game *g, int c, int r);
void boardChainWord(const Game *g, char *out);
bool boardPassComplete(const Game *g);
int  letterValue(int letter);

// dict.c
void dictInit(void);
int  dictLookup(const char *word, int len);   // 0 none, 1 common, 2 rare

// gfx.c
void gfxInit(void);
void gfxFrame(const Game *g);        // draws both screens and flips
int  gfxCellAt(int px, int py, int *c, int *r);
bool gfxInSubmit(int px, int py);
bool gfxInClear(int px, int py);
enum { BURST_SHATTER = 0, BURST_REVEAL, BURST_SCORCH, BURST_FIRE };
void gfxBurst(int c, int r, int kind, int letter);   // letter colours the shards; gfxBurst never reads G
void gfxPopup(int c, int r, const char *txt, int gold);
void gfxFadeIn(void);
void gfxFlash(void);
void gfxShake(int frames);
void gfxInvalidWord(void);
bool gfxInTitleDaily(int px, int py);
bool gfxInTitleClassic(int px, int py);

// music.c
void musicInit(void);
void musicTick(const Game *g);       // once per vblank
void musicNewShelf(int shelf, bool lockdown);
void musicWord(int len, int score);
void musicCascade(int tiles);
void musicPeek(bool on);
void musicReveal(bool secret);
void musicPassComplete(void);
void musicFail(void);
void musicBuildNotes(u16 *out);     // 128-entry MIDI note -> Hz table (shared with tools/check)

// debug.c
void debugSync(Game *g);             // mirrors state for the emulator harness and applies pokes

// font.c
extern const u8 font5x7[][7];
int fontIndex(char ch);
int fontLogoIndex(char ch);   // lowercase w o r d for the wordmark

#endif
