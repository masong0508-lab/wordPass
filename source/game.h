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
    ST_STATS,         // 6: THE LEDGER, the lifetime stats screen (entered from the title)
    ST_GUIDE,         // 7: HOW TO PLAY, the in-game guide (from the title or the pause menu)
    ST_LANGPICK       // 8: the LANGUAGE chooser (first boot, the title tag, the options row)
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
    // ---- the in-game guide (item 19)
    int  guidePage;           // 0..GUIDE_PAGES-1
    int  guideFrom;           // the state the guide returns to: ST_TITLE or ST_PAUSE
    int  langPick, langFrom;  // the language chooser: highlighted language, and the state it returns to (ST_TITLE or ST_OPTIONS)
    // ---- the in-world cues of the guided first shelf: a ring and a pointer on what to act on, and a ghost drag
    int  cue;                 // CUE_*: what is being pointed at this frame (CUE_NONE while the player is busy)
    int  cueAge;              // frames since the cue began
    int  ghostDone;           // the idle ghost drag has been shown on this shelf
    int  idleFrames;          // frames without a touch or a key while the guide waits for a word
    int  hintLen;             // gems in hintC/hintR: a real, common word on the real board (0 = none)
    u8   hintC[8], hintR[8];
    // ---- fire fairness and the shuffle (balance pass)
    int  ignitions;           // times the meter has lit this shelf (the twin rule counts them)
    int  twinEighths;         // of every eight ignitions this many light two columns (0 on early shelves and in the Daily)
    int  shuffles;            // re-deals left on this shelf when the board has no word
    int  shuffled;            // the board was just re-dealt (main.c announces it and clears this)
    // ---- combo and hints
    int  combo;               // words spelled in a row without a peel on this shelf
    int  bestCombo;           // the longest such run this shelf
    int  hints;               // hints left on this shelf
    int  hintReq;             // a hint is on show (the ghost drag traces it until the player touches the board)
    int  hintIdle;            // frames the player has done nothing this shelf (the idle offer)
    int  hintOffered;         // the idle offer has been shown on this shelf
    // ---- pass-letter streak (second balance pass)
    int  hitStreak;           // pass letters found by consecutive peels (a hole or a scorch resets it, words do not)
    int  bestHitStreak;       // the longest such run this shelf
    int  lastStreakBonus;     // what the latest peel's streak paid (0 when it was not a hit or the first of a run); main.c adds it to the score
} Game;
enum { CUE_NONE = 0, CUE_GHOST, CUE_TAIL, CUE_PEEKBTN, CUE_ARMED };   // ghost drag, tail gem of a valid chain, the PEEK button, the armed cell
#define GHOST_IDLE_FRAMES 180   // idle this long before the ghost drag traces a word
#define GHOST_GEM_FRAMES   18   // frames the ghost pointer spends on each gem (and 30 more resting on the tail)

enum { TUTOR_DRAG = 0, TUTOR_PEEK, TUTOR_FIND, TUTOR_REST, TUTOR_DONE };   // drag a word, peek, find the first pass letter, the closing tip, off

// pause menu rows; the visible ones are listed by pauseRows, in the order RESUME, RESTART SHELF, HOW TO PLAY, QUIT
// TO TITLE (Daily has no RESTART)
enum { PAUSE_RESUME = 0, PAUSE_RESTART, PAUSE_QUIT, PAUSE_GUIDE, PAUSE_ROW_IDS };
#define PAUSE_ROWS_MAX 4
int  pauseRows(const Game *g, int ids[PAUSE_ROWS_MAX]);   // main.c: the visible rows in order; returns how many

// ---- the rules' numbers, in one place. board.c and main.c score, pay and set up shelves from these, and the
// in-game guide (guide.c) prints them, so a page can never say something the rules do not do.
#define SCORE_UNIT                10    // a word is worth (letter values) x (length multiplier) x this
#define LONG_WORD_LEN              7    // this many letters and up add LONG_WORD_BONUS
#define LONG_WORD_BONUS          200
#define BURN_BONUS_PER_LETTER     30    // a word through a flame
#define DIAMOND_CAP                2    // diamonds that count in one word; each adds half again
#define PASS_LETTER_PER_SHELF    100    // every pass letter found pays this x the shelf
#define SHELF_BONUS_PER_SHELF   1000
#define SPEED_TURNS               40    // the speed bonus pays for every turn under this
#define SPEED_PER_SHELF_TURN      25
#define PERFECT_PER_SHELF        400    // every peel found a pass letter
#define CLEAN_PER_SHELF          150    // at most CLEAN_EXTRA_PEELS wasted peels
#define CLEAN_EXTRA_PEELS          2
#define BOOKMARKS_MAX              3
#define OVERTIME_TURNS            24    // past this many turns on a shelf every turn adds heat of its own
#define OVERTIME_TURNS_LOCKDOWN   16
#define LOCKDOWN_FROM_SHELF        6
#define LOCKDOWN_FIRST_METER       1    // the first Lockdown shelf holds this much more meter than its place on the ladder (it eases Lockdown in) ...
#define OVERTIME_EASE_STEP         2    // ... and Lockdown overtime starts 24 turns in on that shelf, this many turns earlier each shelf, until 16 on shelf 10
#define TWIN_FIRE_FROM_SHELF      11    // from this shelf some ignitions light two columns at once ...
#define TWIN_FIRE_STEP             2    // ... one more ignition in eight every this many shelves, every one from shelf 25
#define OVERTIME_SCORE_DIV        10    // a word spelled past the turn limit scores this many times less (no farming a burning shelf)
#define OVERTIME_HEAT_STEP        12    // overtime heat is 1, plus 1 more every this many turns past the limit
#define SHUFFLES_PER_SHELF         1    // a board with no word left is re-dealt this many times a shelf before the shelf is lost
#define SHUFFLE_HEAT               3    // the re-deal costs this much heat, like a three-letter word
#define COMBO_PCT_PER_WORD        10    // each word of a streak without a peel scores this much more percent than the one before ...
#define COMBO_CAP_PCT             50    // ... up to this bonus (the sixth word of a streak)
#ifndef STREAK_PER_SHELF            // (overridable so the bot can measure a build without the reward)
#define STREAK_PER_SHELF         100    // a pass letter found right after another pays a streak bonus: this x shelf per step ...
#endif
#define STREAK_CAP                 3    // ... the second in a row one step, the third two, the fourth and every later one three (the cap)
#define HINTS_PER_SHELF            2    // SELECT shows a word on the board this many times a shelf
#define HINT_IDLE_FRAMES         600    // idle this long (ten seconds) with hints left and the game offers one
#define INK_SPILL_HOLES            3
#define TWIST_DAMP_SLOWER          1    // DAMP: the fire creeps this many turns slower ...
#define TWIST_DAMP_METER           2    // ... but the meter is this much shorter
#define TWIST_BURIED_EXTRA         1    // BURIED: the pass is this many letters longer ...
#define TWIST_BURIED_ROW           5    // ... and lies in this row and below
#define DAILY_LONG_LEN             7    // the Daily modifier LONG asks for this pass length

typedef struct { bool lockdown; int tier, threshold, burnEvery, pulseEvery, minRow, twinEighths; } ShelfRules;
void boardShelfRules(int shelf, ShelfRules *out);   // a Classic shelf on FIRE NORMAL with no twist: Lockdown, tier, meter, fire speed, pulse, shallowest pass row
void boardDailyRules(int mod, ShelfRules *out);     // the same for a Daily with modifier DAILYMOD_*
int  boardComboPct(int combo);                       // the bonus percent the next word scores after `combo` words in a row without a peel
int  boardStreakBonus(int shelf, int streak);        // the bonus the streak-th pass letter in a row pays (0 for the first)
int  boardTwinEighths(int shelf);                   // of every eight ignitions on this Classic shelf, how many light two columns (0 before shelf 11)
int  boardPassLenFor(int shelf);                    // the pass length a Classic shelf asks for (4, 5, 6, 7, 8)
bool boardTwistShelf(int shelf);                    // does a Classic shelf carry a twist (4, 7, 10 ...)
int  boardPeelHeat(bool lockdown);                  // heat a peel adds: 1, or 2 in Lockdown
int  boardOvertimeFor(int shelf, bool lockdown);          // turns before overtime heat on this shelf: Classic 24; Lockdown 24, 22, 20, 18 on shelves 6 to 9, then 16
int  boardOvertimeTurns(bool lockdown);             // turns before overtime heat starts
int  boardWordPoints(int base, int len, bool burn, int diamonds, bool repeat);   // the scoring curve; base = the gems' letter values added up
int  boardPassLetterReward(int shelf);              // paid on the spot for every pass letter found
int  boardShelfBonus(int shelf);
int  boardSpeedBonus(int shelf, int turn);
int  boardPeelBonus(int shelf, int peels, int passLen, int *kind);   // kind: 0 none, 1 CLEAN, 2 PERFECT
const char *boardFailName(int fail);                // "LIBRARY LOST", "PASS DESTROYED", "OUT OF WORDS"
const char *boardFailWhy(int fail);                 // "FIRE REACHED THE FLOOR" ...
const char *boardLockdownName(int tier);            // "LOCKDOWN", "LOCKDOWN II" ...

// ---- guide.c: the pages of HOW TO PLAY. Text is built from the rules above; \001..\006 switch the colour
// (accent, gold, fire, base, dim, white). No visible line is longer than GUIDE_MAX_CHARS.
#define GUIDE_PAGES      8
#define GUIDE_MAX_LINES 16
#define GUIDE_LINE_BUF  64
#define GUIDE_MAX_CHARS 38
#define GUIDE_LINE_H    10      // pixels a line takes on screen, and a gap (an empty line) GUIDE_GAP_H
#define GUIDE_GAP_H      4
#define GUIDE_MAX_H    139      // the tallest page: 29 px below the title to the buttons
#define GM_ACCENT "\001"
#define GM_GOLD   "\002"
#define GM_FIRE   "\003"
#define GM_BASE   "\004"
#define GM_DIM    "\005"
#define GM_WHITE  "\006"
const char *guideTitle(int page);
int  guideLines(int page, char out[GUIDE_MAX_LINES][GUIDE_LINE_BUF]);   // the page's lines ("" is a gap); returns how many
int  guideVisibleLen(const char *line);                // characters on screen (colour codes do not count)
void guideStrip(char *dst, const char *line);          // the line without colour codes

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
    u8  language;           // LANG_*: 0 English; an unknown one reads as English
    u8  languageChosen;     // 1 once the player has picked a language (the first-boot chooser shows until then)
    u8  reserved;
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

#define OPT_ROWS 9
void gfxApplySettings(void);          // rebuild caches after a settings change
int  gfxOptionRowAt(int px, int py);  // -1 none, row * 2 + (the right half of the row) for the rows, OPT_ROWS * 2 = back; the list never mirrors
void gfxOptionNudge(int dir);         // the chevron on that side (-1 left, +1 right) of the lit row flashes white for six frames
void gfxTipFlip(int dir);             // the title's tip card: -1 previous, +1 next (the timer restarts)
int  gfxTipIndex(void);               // debug: which tip card shows
bool gfxInTitleOptions(int px, int py);
bool gfxInTitleLedger(int px, int py);   // the BEST line under the title buttons: opens THE LEDGER
int  gfxLangPickRowAt(int px, int py);  // the chooser's row under a touch (0..LANG_COUNT-1), -1 none
bool gfxInTitleLanguage(int px, int py); // the language tag in the title's other top corner: next language
bool gfxInTitleGuide(int px, int py);    // the GUIDE button in the title's top corner
enum { GH_NONE = -1, GH_BACK = 0, GH_PREV, GH_NEXT, GH_DOT };
void gfxGuideNavReset(void);             // test channel: the prev/next buttons pulse again
void gfxGuideSkip(void);                 // a key or a tap: the page's words finish typing in at once
int  gfxGuideAge(void);                  // debug: frames the page has been open (the typing runs over the first ~50)
int  gfxGuideHit(int px, int py);        // the guide's touch targets: GH_BACK, GH_PREV, GH_NEXT or GH_DOT + page; GH_NONE elsewhere

extern Game G;

// board.c
void boardNewShelf(Game *g);
u32  boardRand(void);                // xorshift32 for every board decision (never for cosmetics)
void boardSeed(u32 seed);
int  boardDailyMod(int seed);                // DAILYMOD_* for a date
const char *boardDailyModName(int mod);      // "STANDARD", "EMBER", "DEEP", "LONG"
const char *boardPassTheme(const Game *g);   // "BINDERY", "INK" ... for the plaque label and the intro
const char *boardTwistName(int twist);       // "INK SPILL", "DAMP", "BURIED" ("" for none)
void boardBagRotation(int r);       // which pair of the four rare letters the next cycle deals (tests; boardNewShelf picks it)
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
int  boardHintChain(const Game *g, u8 *outC, u8 *outR, int max);   // a path spelling a common word of 3 to 5 letters on this board: its length, 0 if none
bool boardWordOk(const char *w, int len);     // accepted under the current LIBRARY setting
int  boardWordSpelling(const char *w, int len);    // 0 none, 1 American-only spelling, 2 British-only, that the SPELLING setting rejects
int  boardWordTier(const char *w, int len);   // raw dictionary tier: 0 none, 1 common, 2 rare
int  boardLowestFireRow(const Game *g);
bool boardFireAbove(const Game *g, int c, int r);
void boardChainWord(const Game *g, char *out);
bool boardPassComplete(const Game *g);
int  letterValue(int letter);        // of the active language

// dict.c
void dictInit(void);
void dictSet(const u8 *p);                    // activate another WPD3 image (a loaded language)

// pools.c: themed pass-word library (data/pools.bin)
void poolsInit(void);
int  poolThemeCount(void);
const char *poolName(int theme);                              // "BINDERY"
void poolWordEn(int theme, int len, int idx, char *out);      // the same, English
const char *poolNameEn(int theme);                            // the English name, whatever the language (the guide)
bool poolsSet(const u8 *p);                                   // activate a WPP1 image (NULL = the compiled-in English)
int  poolCount(int theme, int len);                           // words of that length in the theme
void poolWord(int theme, int len, int idx, char *out);        // out holds len + 1 bytes
int  dictLookup(const char *word, int len);   // 0 none, 1 common, 2 rare
int  dictSpelling(const char *word, int len); // 0 same everywhere, 1 American only, 2 British only

// gfx.c
void gfxInit(void);
#define CARD_GATE_PASSED 24   // frames before the results card takes a tap (an earlier one is queued)
#define CARD_GATE_FAIL 36
void gfxLateSample(void);   // main loop: wait until the frame's work just fits before vblank, then read the pad
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

// ---- languages (item 21): lang.c holds the active language's letter tables, langload.c reads a language from the
// ROM filesystem (NitroFS: nitro:/lang/<code>/{dict,pools,letters}.bin; English is compiled in).
enum { LANG_EN = 0, LANG_ES, LANG_FR, LANG_DE, LANG_IT, LANG_PT, LANG_NL, LANG_SV, LANG_PL, LANG_DA, LANG_FI, LANG_COUNT };
#define LANG_LETTERS_BYTES 68
typedef struct {
    u8 weight[26];        // tiles of the letter in a bag (0 = the language has no such tile)
    u8 value[26];         // letter value 1..10
    u8 rare[4];           // the four rarest letters, which take two bag slots a cycle in rotation (255 = none)
    u32 vowels;           // bit i = letter i is a vowel (the vowel floor)
    u8 qu;                // the Q tile is Qu
} LangTables;
const char *langCode(int idx);                // "en" "es" ...
const char *langName(int idx);                // "ENGLISH" "ESPANOL" ...: the label of the options row (A-Z, at most ten letters)
#include "ui_strings.h"
#define UI_MAX_SLOTS 8
const char *ui(int id);                           // the active language's text for a UI_* id (English is the default)
void uiTestLang(int lang);                        // tests: make ui() answer in this language (-1: follow langCur)
const char *uiIn(int id, int lang);               // the text of one language (the tests walk all of them)
int  uiFmt(char *out, int cap, int id, ...);      // fill the %d / %s / %02d / %2$d slots of a string; returns its length
int  langCur(void);                           // the active language
const LangTables *langTables(void);
bool langParseLetters(const u8 *b, size_t n, LangTables *out);
bool langApply(int idx, const u8 *dict, const u8 *pools, const u8 *letters, size_t lettersBytes);
void langInitEnglish(void);
int  letterValueEn(int letter);               // the English value table (the compiled-in one, whatever the language)
int  guideGems(int id, u8 gems[4], int *wordLen);   // the gems of a guide example word (a UI_EX_* string); Qu is one gem
int  langWeight(int letter);
bool langIsVowel(int letter);
bool langQuTile(void);
int  langRare(int k);                         // k = 0..3
// langload.c
void langInit(void);                          // mount the filesystem and find out which languages are on it
bool langAvailable(int idx);
bool langLoad(int idx);                       // load and activate (free the previous one first); false = could not, English is active
bool langFastPath(void);                      // reads go straight from the card (see langload.c) rather than through NitroFS
int  langLoadMs(void);                        // milliseconds the last langLoad took (0 for English)
void gfxLoading(const char *name);            // gfx.c: the LOADING screen on both displays (one frame, flips)
void gameSetLanguage(int idx);                // main.c: from the title or the options only; loads, saves, rebuilds the title board

// music.c
void musicInit(void);
void musicTick(const Game *g);       // once per vblank
void musicNewShelf(int shelf, bool lockdown);   // a Classic shelf with no fire pressure (tests); the game uses musicPlayShelf
void musicPlayShelf(const Game *g);   // a new shelf of the running game: the song comes from its mode, tier and the FIRE setting
void musicMenu(void);                 // the title, options, ledger, guide and chooser screens: the one menu song
int  musicSongFor(int shelf, bool lockdown, int tier, bool daily, int fire);   // which song (index into SONGS) a shelf plays
void musicWord(int len, int score, bool burn);   // a scored word: the chime by length, a steam hiss when it went through a flame
void musicCascade(int tiles);
void musicPeek(bool on);
void musicReveal(bool secret);
void musicPassComplete(void);
void musicFail(int reason);        // FAIL_FIRE / FAIL_SCORCH / FAIL_WORDS: a stinger, then a quiet bed
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
    u8  mix, failPhase, fanfare, pickup;   // state mix (0 play, 1 attract, 2 pause, 3 results, 4 fail), 0 none / 1 stinger / 2 bed, 0 none / 1 pending / 2 fanfare / 3 loop, pickup bars left
    u8  fade, reserved;             // frames of cross-fade left
    u16 wordCount;                  // words chimed since boot
    u8  song, role, gain, songs;    // the current song (index into SONGS), its role (SR_*), its level in percent, the number of songs
} MusicDbg;
void musicDebug(MusicDbg *out);

// debug.c
void debugSync(Game *g);             // mirrors state for the emulator harness and applies pokes

// font.c
extern const u8 font5x7[][7];
int fontIndex(char ch);
int fontLogoIndex(char ch);   // lowercase w o r d for the wordmark

#endif
