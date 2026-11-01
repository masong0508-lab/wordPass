// wordPass - shared game definitions
#ifndef WORDPASS_GAME_H
#define WORDPASS_GAME_H

#include <nds.h>
#include <stdbool.h>
#include <stddef.h>

#define COLS 7
#define ROWS 8
#define MAX_WORD 20           // longest dictionary word
#define MAX_CHAIN 20          // gems in a chain
#define WORD_BUF 48           // room for a full chain spelled out: 20 gems, every one a Qu, plus the NUL
#define MSG_LEN 40            // a message of up to 39 characters (the tutorial prompts run to 34)
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
    ST_STATS          // 6: THE LEDGER, the lifetime stats screen (entered from the title)
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
    int  peekHeld;            // peeking this frame: a shoulder held or the latch on
    int  peekLatch;           // X or the PEEK button latched peek on (one peel, then off)
    int  curC, curR, curOn;   // d-pad cursor (hex cell) and whether it is shown
    int  armed, armC, armR;   // a peek touch is down on this gem; the peel happens on release
    int  wordsThisShelf;
    int  lastWordLen, lastWordScore;
    char lastWord[MAX_WORD + 1];
    char msg[MSG_LEN];        // the top screen's message line (the first 31 characters are mirrored for the tests)
    int  msgTimer;            // frames to show it; -1 = sticky (a tutorial prompt), 0 = none
    bool lockdown;
    char bestWord[MAX_WORD + 1];        // per-shelf results card
    int  bestWordScore;
    int  peels;
    int  mode;                // MODE_CLASSIC or MODE_DAILY
    int  dailySeed;           // YYYYMMDD
    int  hintShown;
    int  lastWordTier;        // 0 none, 1 common, 2 rare (last accepted word)
    int  rareWords;           // rare words spelled this shelf
    int  optRow;              // highlighted row on the options screen
    // ---- scoring ledger (per shelf): a ring of the words spelled, newest at ledgerHead - 1
    char ledger[LEDGER_N][MAX_WORD + 1];   // the whole word, so a repeat is found whatever its length
    int  ledgerPts[LEDGER_N];
    int  ledgerN, ledgerHead;
    int  uniqueWords;         // words this shelf that were not repeats
    int  lastWordDiamonds;    // CELL_SECRET cells in the last scored chain (capped at 2)
    int  lastWordRepeat;      // the last word was already in the ledger (half score)
    int  lastWordBurn;        // the last word went through a flame
    int  speedBonus, peelBonus, passBonusKind;   // results card: kind 0 none, 1 CLEAN, 2 PERFECT
    int  newBest;             // recordScore changed a saved record (cleared by startShelf)
    // ---- progression (item 08)
    int  lives;               // bookmarks left in a Classic run (3 at the start; Daily ignores them)
    int  passTheme;           // pool the current pass came from, 0..poolThemeCount()-1
    u32  recentPass[8];       // ring of the last passes as PASS_KEY(theme, len, index) (0 = empty)
    int  recentHead;
    int  twist;               // TWIST_* on this shelf (Classic, shelf >= 4 && shelf % 3 == 1)
    int  lastTwist;           // the previous twist, so two in a row are rerolled once
    // ---- Daily rules (item 09)
    bool practice;            // a Daily replay: today's attempt is already used, nothing is recorded
    bool clockOk;             // the DS clock reads a plausible date (settingsClockOk); Daily needs it
    int  dailyMod;            // DAILYMOD_*: today's modifier (set from the clock on the title, by boardNewShelf in Daily)
    // ---- pause menu (item 13)
    int  pauseRow;            // highlighted row (an index into the visible rows, see pauseRows)
    int  pauseArm;            // frames left of a first activation of RESTART or QUIT that wants a second one (0 = none)
    int  pauseArmId;          // which row that was (PAUSE_RESTART or PAUSE_QUIT)
    // ---- the guided first shelf (item 14)
    int  tutorStep;           // TUTOR_*: where the Classic shelf-1 guide has got to (TUTOR_DONE = off)
} Game;

enum { TUTOR_DRAG = 0, TUTOR_PEEK, TUTOR_FIND, TUTOR_REST, TUTOR_DONE };   // drag a word, peek, find the first pass letter, the closing tip, off

// pause menu rows; the visible ones are listed by pauseRows (Daily has no RESTART)
enum { PAUSE_RESUME = 0, PAUSE_RESTART, PAUSE_QUIT, PAUSE_ROW_IDS };
#define PAUSE_ROWS_MAX 4        // room for the HOW TO PLAY row a later item adds
int  pauseRows(const Game *g, int ids[PAUSE_ROWS_MAX]);   // main.c: the visible rows in order; returns how many

enum { DAILYMOD_STANDARD = 0, DAILYMOD_EMBER, DAILYMOD_DEEP, DAILYMOD_LONG, DAILYMOD_COUNT };

enum { TWIST_NONE = 0, TWIST_INK, TWIST_DAMP, TWIST_BURIED };
#define POOL_MIN_LEN 4        // pass pools hold words of 4..9 letters
#define POOL_MAX_LEN 9
#define PASS_KEY(theme, len, idx) ((u32)(((((theme) * 6) + (len) - POOL_MIN_LEN) << 10) + (idx) + 1))   // idx < 1000

enum { MODE_CLASSIC = 0, MODE_DAILY };

// ---- settings
enum { THEME_JEWEL = 0, THEME_INK, THEME_CANDY, THEME_EMBER, THEME_CLEAR, THEME_COUNT };   // CLEAR: colour-blind safe (hues that survive the common deficiencies)
enum { FRAME_GOLD = 0, FRAME_SILVER, FRAME_ACCENT, FRAME_COUNT };
enum { HAND_RIGHT = 0, HAND_LEFT };
enum { FIRE_RELAXED = 0, FIRE_NORMAL, FIRE_FAST, FIRE_COUNT };
enum { SPELL_BOTH = 0, SPELL_US, SPELL_UK, SPELL_COUNT };   // accepted spellings: both, American only, British only
enum { LIB_FULL = 0, LIB_COMMON, LIB_COUNT };   // dictionary: every ENABLE word, or common words only
#define ACCENT_COUNT 9                          // AUTO plus eight fixed colours

// Saved file (wordPass.cfg): this struct byte for byte. The CRC covers every byte from `theme` on.
#define SETTINGS_MAGIC    0x57505332u   // "WPS2"
#define SETTINGS_V1_MAGIC 0x57505331u   // "WPS1": the old 28-byte file, migrated on load
#define SETTINGS_VERSION  2
#define SETTINGS_WORD_BUF (MAX_WORD + 4)   // a stored word, NUL-terminated, padded to a word multiple

typedef struct {
    u32 magic;
    u16 version, crc;       // crc16 over bytes 8..end
    u8  theme, frame, accent, hand, fire, music, library, spelling, tutorDone;
    u8  reserved[3];
    int bestClassic;        // best Classic score
    int bestClassicShelf;   // highest shelf cleared
    int bestDaily;          // best Daily score
    int bestDailyDate;      // YYYYMMDD it was set on
    int dailyPlayedDate;    // YYYYMMDD of the Daily attempt already used (replays are PRACTICE)
    int dailyLastScore;     // score of the last scored Daily
    int dailyStreak;        // consecutive days with a scored Daily
    int dailyLastDate;      // YYYYMMDD of the last scored Daily
    int gamesPlayed, shelvesCleared, wordsSpelled, longestLen, bestWordPts, perfectPasses, rareWords, peels, peelsHit;
    char longestWord[SETTINGS_WORD_BUF], bestWordTxt[SETTINGS_WORD_BUF];   // lower case
} Settings;
_Static_assert(sizeof(Settings) % 4 == 0, "Settings must be a whole number of words");
_Static_assert(offsetof(Settings, theme) == 8, "the CRC covers bytes 8..end");

extern Settings S;
void settingsInit(void);
void settingsDefaults(void);
bool settingsSave(void);
bool settingsPersistent(void);
bool settingsSaveFailed(void);                    // the last settingsSave could not write the card
bool settingsDecode(const void *buf, size_t n);   // pure: a saved file (WPS2, or a WPS1 legacy file) into S; false = defaults
size_t settingsEncode(void *buf);                 // pure: S into a WPS2 file image (sizeof(Settings) bytes, CRC filled)
int  settingsToday(void);                         // YYYYMMDD from the DS clock (0 when it cannot be read)
bool settingsClockOk(int ymd);                    // a plausible date: 2025-01-01 .. 2099-12-31
int  settingsDayNumber(int ymd);                  // days since 1970-01-01 (civil calendar), for streaks
int  settingsNextStreak(int lastDate, int streak, int today);   // the streak after a scored Daily on `today`
int  settingsLiveStreak(int today);               // the streak still alive on `today` (0 once a day was missed)
void settingsDebugClock(int ymd);                 // test channel: pretend the clock reads ymd (0 = the real clock)

#define OPT_ROWS 8
void gfxApplySettings(void);          // rebuild caches after a settings change
int  gfxOptionRowAt(int px, int py);  // -1 none, row * 2 + (the right half of the row) for the rows, OPT_ROWS * 2 = back; the list never mirrors
void gfxOptionNudge(int dir);         // the chevron on that side (-1 left, +1 right) of the lit row flashes white for six frames
void gfxTipFlip(int dir);             // the title's tip card: -1 previous, +1 next (the timer restarts)
int  gfxTipIndex(void);               // debug: which tip card shows
bool gfxInTitleOptions(int px, int py);
bool gfxInTitleLedger(int px, int py);   // the BEST line under the title buttons: opens THE LEDGER

extern Game G;

// board.c
void boardNewShelf(Game *g);
u32  boardRand(void);                // xorshift32 for every board decision (never for cosmetics)
void boardSeed(u32 seed);
int  boardDailyMod(int seed);                // DAILYMOD_* for a date
const char *boardDailyModName(int mod);      // "STANDARD", "EMBER", "DEEP", "LONG"
const char *boardPassTheme(const Game *g);   // "BINDERY", "INK" ... for the plaque label and the intro
const char *boardTwistName(int twist);       // "INK SPILL", "DAMP", "BURIED" ("" for none)
void boardBagReset(void);            // next randomLetter() refills the bag (tests; boardNewShelf does it)
int  boardRandomLetter(void);        // one draw from the shuffled letter bag (exposed for tools/check)
void boardDebugFireStep(Game *g);    // test channel: one fire advance plus gravity, no turn
enum ChainTouch { CT_IGNORED, CT_ADDED, CT_BACKTRACKED, CT_TRUNCATED, CT_BRIDGED, CT_RESTARTED, CT_NOT_ADJACENT, CT_SUBMIT_TAP };
enum ChainTouch boardChainTouch(Game *g, int c, int r, bool fresh);   // one touch sample; fresh = a new tap, false = a drag
bool boardTryAddToChain(Game *g, int c, int r);   // a drag sample: true if the chain grew, shrank by one or bridged
void boardClearChain(Game *g);
int  boardSubmit(Game *g);           // returns score gained, 0 if invalid
int  boardChainScore(const Game *g); // exactly what boardSubmit would award for the current chain (0 if invalid)
int  ledgerIndex(const Game *g, int age);   // ring index of the age-th newest ledger entry (0 = newest)
int  boardHeatFor(int len);          // heat a word of this length adds: 3, 2, 1, 1, 0 (3,4,5,6,7+)
int  boardTurnHeat(const Game *g, int baseHeat);   // all the heat the next turn adds: baseHeat plus the Lockdown pulse and overtime
int  boardTallyNext(int shown, int target);   // the counting score of the results tally: +1/6 of the gap, at least 10 a frame, capped at target
void boardTallySplit(int tally, int shelfBonus, int speedBonus, int peelBonus, int out[3]);   // what each bonus row shows after `tally` points of counting
int  boardLenMult(int len);          // length multiplier of a word: 1 (to 4 letters), 2, 3, 4 (9 and up)
int  boardPeel(Game *g, int c, int r); // returns 0 none, 1 hole, 2 secret, 3 scorched, -1 fail; a peel is a turn
void boardAnimate(Game *g);
bool boardIsChainable(const Game *g, int c, int r);
int  boardNeighbors(int c, int r, int outC[6], int outR[6]);
int  boardBackNumber(const Game *g, int c, int r);
bool boardHasWords(const Game *g);
bool boardWordOk(const char *w, int len);     // accepted under the current LIBRARY setting
int  boardWordSpelling(const char *w, int len);    // 0 none, 1 American-only spelling, 2 British-only, that the SPELLING setting rejects
int  boardWordTier(const char *w, int len);   // raw dictionary tier: 0 none, 1 common, 2 rare
int  boardLowestFireRow(const Game *g);
bool boardFireAbove(const Game *g, int c, int r);
void boardChainWord(const Game *g, char *out);
bool boardPassComplete(const Game *g);
int  letterValue(int letter);

// dict.c
void dictInit(void);

// pools.c: themed pass-word library (data/pools.bin)
void poolsInit(void);
int  poolThemeCount(void);
const char *poolName(int theme);                              // "BINDERY"
int  poolCount(int theme, int len);                           // words of that length in the theme
void poolWord(int theme, int len, int idx, char *out);        // out holds len + 1 bytes
int  dictLookup(const char *word, int len);   // 0 none, 1 common, 2 rare
int  dictSpelling(const char *word, int len); // 0 same everywhere, 1 American only, 2 British only

// gfx.c
void gfxInit(void);
void gfxFrame(const Game *g);        // draws both screens and flips
int  gfxCellAt(int px, int py, int *c, int *r);
bool gfxInSubmit(int px, int py);
bool gfxInClear(int px, int py);
bool gfxInPeekButton(int px, int py);
int  gfxPauseRowAt(int px, int py, int rows);   // the pause menu row (0..rows-1) under a touch, -1 for none
int  gfxShownScore(void);            // the score the HUD shows (counts up during the results tally)
bool gfxInPauseButton(int px, int py);   // the 12 x 12 "II" button at the top of the panel
enum { PRESS_SUBMIT = 0, PRESS_CLEAR, PRESS_PEEK, PRESS_PAUSE };
void gfxPress(int which);            // a panel button looks pressed for six frames
void gfxSubmitFlash(void);           // the SUBMIT button flashes white for three frames (a word scored)
enum { CALLOUT_GOLD = 0, CALLOUT_FIRE, CALLOUT_ACCENT };
void gfxCallout(const char *txt, int kind);   // one big word over the board (at most 19 characters); a newer one replaces it
int  gfxPressBits(void);             // debug: bit 0 submit, 1 clear, 2 peek, 3 pause pressed, 4 submit flash
int  gfxCalloutLife(void);           // debug: frames left of the callout (0 = none)
const char *gfxCalloutText(void);
void gfxNope(int c, int r);          // red flash on a gem the drag could not reach
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
// One-shot sound effects on the chime and second noise voices, with priorities (music.c SFX_TAB)
enum { SFX_TAP = 0, SFX_UNTAP, SFX_INVALID, SFX_PEEL, SFX_SCORCH, SFX_IGNITE, SFX_FIRESTEP, SFX_MENU, SFX_COUNT };
void musicSfx(int id, int param);   // param: the chain length after the change for SFX_TAP / SFX_UNTAP
// What the sequencer last sent to the sound hardware, mirrored by debug.c (tools/emu/wp.py music())
typedef struct {
    u16 hz[8];                      // per voice: HOOK PULSE BASS PAD ALARM CHIME DRUM NOISE2
    u8  vol[8];
    u8  duty[8];                    // the libnds DutyCycle value (0 12.5%, 1 25%, 2 37.5%, 3 50%)
    u8  step, section, tempo, drumLevel;   // the step most recently sequenced (255 = none yet), A/B section, frames per step
    s16 lastSfx;                    // the last sound effect that started (-1 none)
    u16 sfxCount;                   // sound effects started since boot
} MusicDbg;
void musicDebug(MusicDbg *out);

// debug.c
void debugSync(Game *g);             // mirrors state for the emulator harness and applies pokes

// font.c
extern const u8 font5x7[][7];
int fontIndex(char ch);
int fontLogoIndex(char ch);   // lowercase w o r d for the wordmark

#endif
