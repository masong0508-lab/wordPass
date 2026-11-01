// Dynamic chiptune: a fixed four-bar hook with stems mixed by game state.
// Uses the DS PSG channels directly, no samples, so stems mute for free.
//
// Eight voices: six PSG squares and two noise channels. Every voice has an envelope
// (attack, decay to a sustain level, release) and every note goes through noteOn/noteOff,
// so the sound is described by the Inst table, not by ramps scattered through the code.
// One-shot sound effects (taps, peels, scorches ...) run as a tiny sequencer of their own on
// V_CHIME (a tone) and V_NOISE2 (a noise burst) with priorities, so an important sound is
// never cut short by a minor one. Every value that reaches the hardware is change-guarded,
// and musicDebug() mirrors what was actually sent so tests can read the sequencer.
#include "game.h"
#include <stdlib.h>

#define STEP_FRAMES 8          // one 16th note = 8 frames (112.5 BPM)
#define PATTERN 64             // 4 bars of 16ths

enum { V_HOOK = 0, V_PULSE, V_BASS, V_PAD, V_ALARM, V_CHIME, V_DRUM, V_NOISE2, NVOICE };

typedef struct {
    u8 dutyAtk, dutySus, atkFrames;   // duty (percent) for the first atkFrames frames, then dutySus
    u8 volOn, volSus, decay;          // level at the onset, the level it settles to, fall per frame
    u8 vibDelay, vibDiv;              // vibrato starts after vibDelay frames at +/- baseHz/vibDiv (0 = none)
    u8 release;                       // fall per frame once the note is released
} Inst;

static const Inst INST[NVOICE] = {
    [V_HOOK]   = { 12, 50, 3, 64, 44, 3, 10, 96, 8 },
    [V_PULSE]  = { 12, 12, 0, 26, 14, 2,  0,  0, 8 },
    [V_BASS]   = { 25, 25, 0, 64, 48, 4,  0,  0, 6 },
    [V_PAD]    = { 37, 50, 0,  0,  0, 0,  0,  0, 2 },
    [V_ALARM]  = { 12, 12, 0, 60, 60, 0,  0,  0, 8 },
    [V_CHIME]  = { 50, 50, 0,  0,  0, 0,  0,  0, 8 },
    [V_DRUM]   = { 50, 50, 0,  0,  0, 0,  0,  0, 127 },
    [V_NOISE2] = { 50, 50, 0,  0,  0, 0,  0,  0, 127 },
};

typedef struct {
    int  id;                // libnds sound id
    bool noise, gate, fresh;   // fresh: the note started this frame, so its envelope has not moved yet
    bool lockDuty;          // a stinger or a mix override holds the duty (the hook's attack duty must not replace it)
    int  cur, sus;          // envelope level now, and the level it settles to
    int  dec, atk, rel;     // fall per frame towards sus, rise in 1/16 per frame (0 = at once), fall per frame once released
    int  acc, out;          // the attack's fraction; the level actually sent (after the mix and its slew)
    int  life;              // frames until the gate closes by itself (0 = held until noteOff)
    int  freq, baseHz;      // last Hz sent; the note's own Hz (vibrato moves freq around it)
    int  slope;             // noise: Hz change per frame
    int  noteFrames;
    int  duty, pan;         // last values sent (duty as the DutyCycle enum value)
    int  vibDelay, vibDiv;  // active vibrato (the Inst's, or a chime sound effect's)
} Voice;

static Voice V[NVOICE];
static s16 lastSent[NVOICE];            // the volume last written to each channel
static u16 noteHz[128];
static int stepTimer, step, lastStep = 255, lastSection;
static int stepFrames = STEP_FRAMES, calmBars;
static int loop;               // completed passes through the 64 steps; section B plays on loop 2 of every four
static int alarmFlip;
static int rootNote = 60;      // C4
static int leadOct = 1;        // register window: the lead sits an octave lower in the high keys
static bool minorMode;
static int drumLevel, drumDecay;
static int bassLife;
static bool peeking, booted;
// the fail stinger (a few hundred ms) and the quiet bed after it
enum { FP_NONE = 0, FP_STINGER, FP_BED };
static int failPhase, failReason, failFrame, failLen, bedFrame;
// the pass fanfare, then the octave-up hook for the rest of the results card
enum { PM_NONE = 0, PM_PENDING, PM_FANFARE, PM_LOOP };
static int passMode, fanfareLeft;
// the state mix: what the music does on the title, in play, paused, on the results card and on the fail card
enum { MIX_PLAY = 0, MIX_ATTRACT, MIX_PAUSE, MIX_PASSED, MIX_FAIL };
static int mix, upFrames;
static int fade, pickupBars;       // the cross-fade into a new shelf, then one bar of bass and pulse alone
static int padBase, padAccent;
static int wordCount;
static u32 rng = 1;
static int rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (int)(rng & 0x7fff); }
static int peekHookIdx;
static int cascadeLeft, cascadeTimer, cascadeNote;
static int lastWordStep = -100;
static int lastSfx = -1, sfxCount;

// The tune is A A B A over four loops. Hook: scale degrees (1..14), 0 = hold, 99 = rest. Same rhythm every shelf.
static const u8 HOOK[PATTERN] = {
    1,0,3,0, 5,0,3,0, 6,0,5,0, 0,0,3,0,
    1,0,3,0, 5,0,8,0, 7,0,5,0, 0,0,99,0,
    6,0,8,0, 9,0,8,0, 6,0,5,0, 4,0,5,0,
    3,0,2,0, 7,0,0,0, 5,0,2,0, 7,0,99,0,
};
static const u8 HOOK_B[PATTERN] = {
    6,0,8,0, 10,0,8,0, 6,0,5,0, 0,0,3,0,
    4,0,6,0, 8,0,9,0, 8,0,6,0, 0,0,99,0,
    5,0,3,0, 1,0,3,0, 5,0,6,0, 8,0,6,0,
    5,0,4,0, 2,0,0,0, 7,0,0,0, 9,0,99,0,
};
// chord roots per bar (I vi IV V, and vi iv I V in the B section) as degrees
static const u8 CHORD[4] = { 1, 6, 4, 5 };
static const u8 CHORD_B[4] = { 6, 4, 1, 5 };
static const u8 MAJOR[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const u8 MINOR[7] = { 0, 2, 3, 5, 7, 8, 10 };

// `chord` is the degree of the bar's chord (0 = none): in Lockdown's minor key the chord on the fifth
// takes the raised seventh (harmonic minor), so the V chord is a major chord that pulls home.
static int degreeToNote(int degree, int octave, int chord)
{
    const u8 *sc = minorMode ? MINOR : MAJOR;
    int d = degree - 1;
    int oct = d / 7; d %= 7;
    int semi = sc[d];
    if (minorMode && chord == 5 && d == 6) semi = 11;
    return rootNote + semi + 12 * (oct + octave);
}

static DutyCycle dutyOf(int pct)
{
    return pct <= 12 ? DutyCycle_12 : pct <= 25 ? DutyCycle_25 : pct <= 37 ? DutyCycle_37 : DutyCycle_50;
}

// ---- voices: every write to the hardware goes through these, and only when the value changed
static void vFreq(Voice *x, int hz)
{
    if (hz < 1) hz = 1;
    if (hz > 65535) hz = 65535;
    if (hz != x->freq) { soundSetFreq(x->id, (u16)hz); x->freq = hz; }
}

static void vDuty(Voice *x, int pct)
{
    int d = (int)dutyOf(pct);
    if (d != x->duty) { soundSetWaveDuty(x->id, (DutyCycle)d); x->duty = d; }
}

static void vPan(Voice *x, int pan)
{
    if (pan != x->pan) { soundSetPan(x->id, (u8)pan); x->pan = pan; }
}

// A new note: instant onset at volOn, then it falls by `dec` per frame to volSus.
static void noteOn(int v, int note, int volOn, int volSus, int dec)
{
    Voice *x = &V[v];
    const Inst *in = &INST[v];
    if (note < 0 || note > 127) return;
    x->baseHz = noteHz[note];
    vFreq(x, x->baseHz);
    vDuty(x, in->atkFrames ? in->dutyAtk : in->dutySus);
    x->gate = true; x->cur = volOn; x->sus = volSus; x->dec = dec; x->atk = 0; x->acc = 0;
    x->life = 0; x->noteFrames = 0; x->slope = 0; x->fresh = true; x->lockDuty = false;
    x->vibDelay = in->vibDelay; x->vibDiv = in->vibDiv;
}

// The Inst's own onset, sustain and decay.
static void notePlay(int v, int note, int scalePct)
{
    const Inst *in = &INST[v];
    noteOn(v, note, in->volOn * scalePct / 100, in->volSus * scalePct / 100, in->decay);
}

static void noteOff(int v) { V[v].gate = false; }

// Hold a duty a mix or a stinger asked for (and drop the vibrato) until the next note
static void dutyLock(int v, int pct) { vDuty(&V[v], pct); V[v].lockDuty = true; V[v].vibDiv = 0; }

// A noise hit: starts at hz, moves `slope` Hz per frame, starts at vol and falls by `decay` per frame,
// and closes after `life` frames.
static void noiseHit(int v, int hz, int slope, int vol, int decay, int life)
{
    Voice *x = &V[v];
    x->baseHz = hz; vFreq(x, hz);
    x->gate = true; x->cur = vol; x->sus = 0; x->dec = decay; x->atk = 0;
    x->life = life; x->slope = slope; x->noteFrames = 0; x->fresh = true; x->acc = 0;
    x->vibDiv = 0;
}

// ---- sound effects: a short run of notes on V_CHIME and/or a noise burst on V_NOISE2
typedef struct {
    s8  semi[6];                    // tone: semitones over rootNote (TAP and UNTAP patch semi[0])
    u8  n, frames, duty, vol, prio; // n notes of `frames` frames each, duty percent, level, priority
    u8  hold, vib;                  // frames the last note is held after the run, with vibrato
    u8  noiseFrames, noiseVol;      // noise burst length and level (0 frames = no noise part)
    s16 noiseStart, noiseSlope;     // noise Hz at the start and the change per frame
    u8  noiseDecay;                 // noise level lost per frame
    s16 noiseRand;                  // plus a random 0..noiseRand Hz every frame (crackle)
} Sfx;

typedef struct { Sfx s; int frame, prio, base; bool on; } SfxState;
static SfxState sfxTone, sfxNoise;

static const u8 PENTA[10] = { 0, 2, 4, 7, 9, 12, 14, 16, 19, 21 };

static const Sfx SFX_TAB[SFX_COUNT] = {
    [SFX_TAP]      = { {12}, 1, 3, 12, 48, 1, 0, 0, 0, 0, 0, 0 },
    [SFX_UNTAP]    = { {12}, 1, 2, 12, 36, 1, 0, 0, 0, 0, 0, 0 },
    [SFX_INVALID]  = { {13, 12, 13, 12}, 4, 3, 12, 60, 2, 0, 0, 0, 0, 0, 0 },
    [SFX_PEEL]     = { {36}, 1, 1, 50, 40, 2, 0, 0, 3, 56, 32000, -6000 },
    [SFX_SCORCH]   = { {18, 16, 13, 9, 6, 1}, 6, 2, 50, 60, 3, 0, 0, 14, 72, 20000, -1200 },
    [SFX_IGNITE]   = { {0}, 0, 0, 0, 0, 3, 0, 0, 12, 50, 1500, 1300 },
    [SFX_FIRESTEP] = { {0}, 0, 0, 0, 0, 2, 0, 0, 6, 58, 1200, -100 },
    [SFX_MENU]     = { {24}, 1, 2, 50, 40, 2, 0, 0, 0, 0, 0, 0 },
};

static bool startTone(const Sfx *s, int base, int prio)
{
    if (sfxTone.on && prio < sfxTone.prio) return false;
    sfxTone.s = *s; sfxTone.frame = 0; sfxTone.prio = prio; sfxTone.base = base; sfxTone.on = true;
    return true;
}

static bool startNoise(const Sfx *s, int prio)
{
    if (sfxNoise.on && prio < sfxNoise.prio) return false;
    sfxNoise.s = *s; sfxNoise.frame = 0; sfxNoise.prio = prio; sfxNoise.base = 0; sfxNoise.on = true;
    return true;
}

static void tickTone(void)
{
    SfxState *t = &sfxTone;
    if (!t->on) return;
    Voice *x = &V[V_CHIME];
    int run = t->s.n * t->s.frames;
    if (t->frame >= run + t->s.hold) { t->on = false; t->prio = -1; noteOff(V_CHIME); return; }   // the frame after the last: release
    if (t->frame < run && t->frame % t->s.frames == 0) {
        int note = t->base + t->s.semi[t->frame / t->s.frames];
        x->baseHz = noteHz[note < 0 ? 0 : note > 127 ? 127 : note];
        vFreq(x, x->baseHz);
        vDuty(x, t->s.duty);
        x->gate = true; x->cur = x->sus = t->s.vol; x->dec = 0; x->atk = 0; x->life = 0;
        x->noteFrames = 0; x->vibDiv = 0; x->fresh = true;
    }
    if (t->frame >= run && t->s.vib) { x->vibDelay = 0; x->vibDiv = 96; }
    t->frame++;
}

static void tickNoise(void)
{
    SfxState *t = &sfxNoise;
    if (!t->on) return;
    Voice *x = &V[V_NOISE2];
    if (t->frame >= t->s.noiseFrames) { t->on = false; t->prio = -1; x->gate = false; x->cur = 0; return; }
    int hz = t->s.noiseStart + t->s.noiseSlope * t->frame + (t->s.noiseRand ? rnd() % t->s.noiseRand : 0);
    int lvl = t->s.noiseVol - t->s.noiseDecay * t->frame;
    x->gate = lvl > 0; x->cur = x->sus = lvl > 0 ? lvl : 0; x->dec = 0; x->atk = 0; x->life = 0; x->slope = 0;
    x->baseHz = hz < 100 ? 100 : hz;
    vFreq(x, x->baseHz);
    t->frame++;
}

// Fire a sound effect. `param` is the chain length for SFX_TAP / SFX_UNTAP (after the change).
void musicSfx(int id, int param)
{
    if (id < 0 || id >= SFX_COUNT) return;
    Sfx s = SFX_TAB[id];
    if (id == SFX_TAP || id == SFX_UNTAP) {
        int k = param < 1 ? 1 : param > 10 ? 10 : param;
        s.semi[0] = (s8)(12 + PENTA[k - 1]);
    }
    bool took = false;
    if (s.n && startTone(&s, rootNote, s.prio)) took = true;
    if (s.noiseFrames && startNoise(&s, s.prio)) took = true;
    if (took) { lastSfx = id; sfxCount++; }
}

// Equal-tempered table from one octave of integer frequencies: each lower octave is a
// rounded halving, each higher one a doubling (no pow(), no libm): C4 = 262, A4 = 440.
// Exported so tools/check can assert the table without the PSG.
void musicBuildNotes(u16 *out)
{
    static const u16 OCT8[12] = { 4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902 };
    for (int n = 0; n < 128; n++) {
        int s = 9 - n / 12;            // C8 (4186 Hz) is MIDI note 108
        out[n] = s > 0 ? (u16)((OCT8[n % 12] + (1 << (s - 1))) >> s) : (u16)(OCT8[n % 12] << -s);
    }
}

void musicInit(void)
{
    musicBuildNotes(noteHz);
    soundEnable();
    sfxTone.on = sfxNoise.on = false; sfxTone.prio = sfxNoise.prio = -1; lastSfx = -1; sfxCount = 0;
    static const u8 pan[NVOICE] = { 64, 64, 64, 72, 24, 70, 60, 92 };
    for (int v = 0; v < NVOICE; v++) {
        Voice *x = &V[v];
        x->noise = (v == V_DRUM || v == V_NOISE2);
        x->freq = x->noise ? 8000 : 440;
        x->rel = INST[v].release;
        x->duty = (int)dutyOf(INST[v].dutySus);
        x->pan = 64;
        x->id = x->noise ? soundPlayNoise(8000, 0, 64) : soundPlayPSG((DutyCycle)x->duty, 440, 0, 64);
        vPan(x, pan[v]);
        lastSent[v] = 0;
    }
    musicNewShelf(1, false);
    booted = true;
}

// The cross-fade is over: the new shelf's tune starts from its top, one bar of bass and pulse first.
static void beginShelf(void)
{
    for (int v = 0; v < NVOICE; v++) V[v].rel = INST[v].release;
    step = 0; stepTimer = 0; lastStep = 255; lastSection = 0;
    loop = 0; stepFrames = STEP_FRAMES; calmBars = 0; alarmFlip = 0;
    pickupBars = 1;
}

// A new shelf (or the title): sixteen frames of release on every music voice, then the tune from step 0 in
// the new key. Sound effects ring through the cross-fade.
void musicNewShelf(int shelf, bool lockdown)
{
    static const u8 keys[] = { 60, 62, 57, 65, 64, 67, 55, 69 };
    rootNote = keys[(shelf - 1) % 8];
    leadOct = rootNote >= 64 ? 0 : 1;
    minorMode = lockdown;
    failPhase = FP_NONE; passMode = PM_NONE; fanfareLeft = 0; padAccent = 0; padBase = 0;
    drumLevel = 0; drumDecay = 0; bassLife = 0;
    peekHookIdx = 0;
    lastStep = 255; pickupBars = 0;
    fade = booted ? 16 : 0;
    if (!fade) { beginShelf(); return; }
    for (int v = 0; v < NVOICE; v++) {
        if (v == V_CHIME || v == V_NOISE2) continue;
        V[v].gate = false; V[v].life = 0;
        if (V[v].rel < 8) V[v].rel = 8;
    }
    if (sfxNoise.on && sfxNoise.prio == 0) { sfxNoise.on = false; sfxNoise.prio = -1; V[V_NOISE2].gate = false; V[V_NOISE2].cur = 0; }
}

// The chime of a word: longer words climb further up the chord, and a long one rings with vibrato.
static void wordChime(int len, int score)
{
    static const u8 D[6] = { 1, 3, 5, 8, 10, 12 };
    int L = len < 3 ? 3 : len > 20 ? 20 : len;
    int n, frames, hold = 0, vib = 0, vol;
    if (L == 3)      { n = 1; frames = 6; vol = 56; }
    else if (L == 4) { n = 2; frames = 5; vol = 60; }
    else if (L <= 6) { n = 3; frames = 5; vol = 64; }
    else if (L == 7) { n = 4; frames = 4; hold = 16; vib = 1; vol = 72; }
    else             { n = 6; frames = 4; hold = 16; vib = 1; vol = 76; }
    if (score >= 500) vol += 8;
    Sfx s = { {0}, (u8)n, (u8)frames, 50, (u8)vol, 3, (u8)hold, (u8)vib, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < n; i++) s.semi[i] = (s8)(degreeToNote(D[i], leadOct, 0) - rootNote);
    startTone(&s, rootNote, 3);
    if (L >= 8) padAccent = 32;                // a long word lifts the pad for a moment
}

void musicWord(int len, int score, bool burn)
{
    wordChime(len, score);
    if (burn) {                                // through a flame: a steam hiss
        Sfx h = { {0}, 0, 0, 0, 0, 2, 0, 0, 20, 44, 12000, -500, 0, 0 };
        startNoise(&h, 2);
    }
    wordCount++;
    bassLife = 3;
    int lvl = len >= 7 ? 3 : len >= 5 ? 2 : 1;
    if (lvl > drumLevel) drumLevel = lvl;
    drumDecay = PATTERN * 2;
    lastWordStep = step;
}

void musicCascade(int tiles)
{
    cascadeLeft = tiles > 8 ? 8 : tiles;
    cascadeTimer = 0;
    cascadeNote = 0;
}

void musicPeek(bool on) { peeking = on; }

void musicReveal(bool secret)
{
    // play the hook, one note per reveal
    int tries = 0, n, at;
    do { at = peekHookIdx; n = HOOK[at]; peekHookIdx = (peekHookIdx + 1) % PATTERN; }
    while ((n == 0 || n == 99) && ++tries < PATTERN);
    Sfx s = { {0}, 1, secret ? 24 : 7, 50, 70, 3, 0, 0, 0, 0, 0, 0, 0, 0 };
    s.semi[0] = (s8)(degreeToNote(n, leadOct + (secret ? 1 : 0), CHORD[at / 16]) - rootNote);
    startTone(&s, rootNote, 3);
}

// The pass is complete: the fanfare lands on the next beat (step % 4 == 0).
void musicPassComplete(void) { if (failPhase == FP_NONE) passMode = PM_PENDING; }

// Three stingers, then a quiet bed. The sequencer never restarts until the next shelf.
void musicFail(int reason)
{
    failPhase = FP_STINGER; failReason = reason; failFrame = 0; bedFrame = 0;
    passMode = PM_NONE;
    for (int v = 0; v < NVOICE; v++) if (v != V_CHIME && v != V_NOISE2) { noteOff(v); V[v].life = 0; }
    if (reason == FAIL_WORDS) { failLen = 80; return; }                // the hook sighs down: no alarm
    if (reason == FAIL_SCORCH) {
        failLen = 45;
        Sfx crack = { {0}, 0, 0, 0, 0, 3, 0, 0, 3, 110, 9000, -2000, 0, 0 };
        startNoise(&crack, 3);
        Sfx stab = { {18}, 1, 8, 50, 80, 3, 0, 0, 0, 0, 0, 0, 0, 0 };    // square stab: hook on +12, chime on +18
        startTone(&stab, rootNote, 3);
        noteOn(V_HOOK, rootNote + 12, 80, 80, 0);
        dutyLock(V_HOOK, 50); V[V_HOOK].life = 8;
        return;
    }
    failLen = 90;                                                       // FAIL_FIRE: the slide and a crackle under it
    Sfx crackle = { {0}, 0, 0, 0, 0, 2, 0, 0, 60, 40, 3000, 0, 1, 6000 };
    startNoise(&crackle, 2);
}

// A noise hit on the hat channel, below every sound effect (priority 0).
static void hat(int hz, int frames, int vol)
{
    Sfx s = { {0}, 0, 0, 0, 0, 0, 0, 0, (u8)frames, (u8)vol, (s16)hz, 0, 0, 0 };
    startNoise(&s, 0);
}

static void snare(int vol) { noiseHit(V_DRUM, 7000, 0, vol, 8, stepFrames < 6 ? stepFrames : 6); }
static void kick(int vol) { noiseHit(V_DRUM, 2400, -400, vol, vol / 4 + 1, 4); }

static const u8 BASS_RIFF[2][8] = {
    { 0, 0, 7, 0, 0, 12, 7, 0 },
    { 0, 0, 7, 0, 0, 12, 10, 7 },        // the fourth bar climbs back into the loop
};

static void startFanfare(void)
{
    static const u8 D[6] = { 1, 3, 5, 8, 10, 12 };
    Sfx f = { {0}, 6, 8, 25, 80, 4, 24, 1, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) f.semi[i] = (s8)(degreeToNote(D[i], leadOct, 0) - rootNote);
    startTone(&f, rootNote, 4);
    passMode = PM_FANFARE; fanfareLeft = 8;
    drumLevel = 0; drumDecay = 0;
    Voice *pad = &V[V_PAD];                      // the pad snaps up on the third of the tonic chord
    int note = degreeToNote(3, leadOct - 1, 1);
    pad->baseHz = noteHz[note]; vFreq(pad, pad->baseHz);
    pad->cur = 60;
}

static void sequenceStep(const Game *g)
{
    bool attract = mix == MIX_ATTRACT, paused = mix == MIX_PAUSE;
    if (passMode == PM_PENDING && (step % 4) == 0) startFanfare();
    bool fanfare = passMode == PM_FANFARE, passLoop = passMode == PM_LOOP;
    if ((step % 16) == 0 && step > 0 && pickupBars > 0) pickupBars--;
    bool pickup = pickupBars > 0;                // the first bar of a shelf: bass and pulse only

    int sec = (!passLoop && !fanfare && (loop & 3) == 2) ? 1 : 0;
    const u8 *hook = sec ? HOOK_B : HOOK;
    const u8 *chords = sec ? CHORD_B : CHORD;
    int bar = (step / 16) % 4, sb = step % 16;
    int chord = chords[bar];
    int low = boardLowestFireRow(g);
    if (attract && bassLife < 1) bassLife = 1;
    if (attract && drumLevel < 1) { drumLevel = 1; drumDecay = PATTERN; }

    // the tempo lifts under fire and relaxes a bar after the last flame is gone; only ever chosen on a bar line
    if (sb == 0) {
        if (low >= 0) { calmBars = 0; stepFrames = low >= 5 ? 6 : low >= 3 ? 7 : STEP_FRAMES; }
        else if (calmBars < 1) calmBars++;
        else stepFrames = STEP_FRAMES;
    }

    // hook: never leaves (but waits out the pickup bar and the fanfare); after the fanfare it loops an octave up
    int h = hook[step];
    if (pickup || fanfare || h == 99) noteOff(V_HOOK);
    else if (h) {
        if (passLoop) notePlay(V_HOOK, degreeToNote(h, leadOct + 1, chord), 140);
        else if (attract) { noteOn(V_HOOK, degreeToNote(h, leadOct, chord), 60, 60, 0); dutyLock(V_HOOK, 50); V[V_HOOK].vibDiv = INST[V_HOOK].vibDiv; V[V_HOOK].vibDelay = INST[V_HOOK].vibDelay; }
        else notePlay(V_HOOK, degreeToNote(h, leadOct, chord), 100);
    }

    // pulse: a plucked chord-tone arpeggio, ping-ponging across the stereo field. Paused it drops an octave
    // to a flat 50% tone; while peeking it keeps only a heartbeat, on steps 0 and 2 of each beat
    static const u8 arp[4] = { 0, 2, 4, 7 };
    int pnote = degreeToNote(chord + arp[step & 3], 0, chord);
    if (paused) { noteOn(V_PULSE, pnote - 12, 30, 30, 0); dutyLock(V_PULSE, 50); }
    else if (peeking) {
        if ((step & 3) == 0 || (step & 3) == 2) { noteOn(V_PULSE, pnote - 12, 34, 18, 3); dutyLock(V_PULSE, 50); }
        else noteOff(V_PULSE);
    } else notePlay(V_PULSE, pnote, 100);
    vPan(&V[V_PULSE], (step & 1) ? 84 : 44);

    // bass: a riff in 8ths on the chord root while words keep coming (always in the pickup bar); the fanfare
    // holds the tonic for two beats and the results loop leaves it out
    if (fanfare) {
        if (fanfareLeft == 8) { noteOn(V_BASS, degreeToNote(1, -1, 1), 70, 70, 0); V[V_BASS].life = 8 * stepFrames; }
    } else if (passLoop) noteOff(V_BASS);
    else if ((step & 1) == 0) {
        if (pickup || bassLife > 0) {
            int note = degreeToNote(chord, -1, chord) + BASS_RIFF[bar == 3][sb / 2];
            notePlay(V_BASS, note, (sb == 0 || sb == 8) ? 100 : 78);
        } else noteOff(V_BASS);
    }
    if (sb == 0 && bassLife > 0 && step != lastWordStep) bassLife--;

    // pad: swells with the pass, slow alternation of chord tones an octave under the lead; the duty flips every four steps
    int found = 0;
    for (int i = 0; i < g->passLen; i++) found += g->found[i];
    int padVol = g->passLen ? (40 * found) / g->passLen : 0;
    if (attract) padVol = 36;                    // the title's pad holds the third of the chord
    if (fanfare || passLoop) padVol = 60;
    if (pickup) padVol = 0;
    Voice *pad = &V[V_PAD];
    if ((step % 8) == 0) {
        int note = degreeToNote(chord + (attract || !((step / 8) & 1) ? 2 : 4), leadOct - 1, chord);
        if (!(fanfare && fanfareLeft == 8)) { pad->baseHz = noteHz[note]; vFreq(pad, pad->baseHz); }
    }
    if ((step % 4) == 0) vDuty(pad, (step / 4) & 1 ? INST[V_PAD].dutySus : INST[V_PAD].dutyAtk);
    pad->gate = true; pad->atk = 16; pad->dec = 0; padBase = padVol;

    // drums: kick and snare on V_DRUM, hats on V_NOISE2 (any sound effect there wins; the hats return next step).
    // The fanfare runs the fill; the results loop is without drums
    if (fanfare) snare(30 + 6 * ((8 - fanfareLeft) & 3) + (((8 - fanfareLeft) & 3) == 3 ? 2 : 0));
    else if (drumLevel >= 1 && !pickup && !passLoop) {
        bool fill = drumLevel >= 3 && step >= 60;
        if (drumLevel >= 3) {
            static const u8 acc[4] = { 16, 8, 12, 8 };
            if (step == 0) hat(12000, 10, 24);                  // an open hat marks the top of every loop
            else hat(16000, 2, acc[step & 3]);
        } else if ((sb & 3) == 2) hat(16000, 2, 16);
        if (drumLevel >= 2) {
            if (sb == 0 || sb == 8) kick(54);
            if (fill) snare(30 + 6 * (step - 60) + (step == 63 ? 2 : 0));   // 30, 36, 42, 50
            else if (sb == 4 || sb == 12) snare(46);
            if (sb == 14 && (bar == 1 || bar == 3) && !fill) kick(30);   // a ghost kick before the next bar
        }
    }
    if (drumDecay > 0 && --drumDecay == 0 && drumLevel > 0) { drumLevel--; drumDecay = PATTERN; }

    // alarm: two-tone blips that come faster and louder as the fire descends
    if (low >= 0 && !pickup && !fanfare && !passLoop) {
        int every = low <= 1 ? 8 : low <= 3 ? 4 : low <= 5 ? 2 : 1;
        if (step % every == 0) {
            int note = rootNote + 13 + low * 2 + (alarmFlip ? 6 : 0);
            int vol = 28 + low * 5; if (vol > 60) vol = 60;
            alarmFlip ^= 1;
            noteOn(V_ALARM, note, vol, vol, 0);
            V[V_ALARM].life = 4;
        }
    }

    if (fanfare && --fanfareLeft == 0) passMode = PM_LOOP;
    lastStep = step; lastSection = sec;
    step = (step + 1) % PATTERN;
    if (step == 0) loop++;
}

// The fail stinger, then a bed of one pad note and a slow bass toll.
static void failTick(void)
{
    Voice *a = &V[V_ALARM];
    if (failPhase == FP_STINGER) {
        int f = failFrame;
        if (failReason == FAIL_WORDS) {
            static const u8 down[4] = { 7, 5, 3, 0 };
            if (f % 20 == 0 && f < 80) { noteOn(V_HOOK, rootNote + 12 + down[f / 20], 50, 50, 0); dutyLock(V_HOOK, 25); }
        } else {
            int left = failLen - 1 - f;               // the slide falls 6 Hz a frame and goes quiet for its last ten
            vFreq(a, 200 + left * 6); a->baseHz = a->freq;
            a->gate = left > 10; a->cur = a->sus = a->gate ? 70 : 0; a->dec = 0; a->life = 0;
        }
        if (++failFrame >= failLen) {
            failPhase = FP_BED; bedFrame = 0;
            for (int v = 0; v < NVOICE; v++) if (v != V_CHIME && v != V_NOISE2) { noteOff(v); V[v].life = 0; }
            Voice *pad = &V[V_PAD];
            pad->baseHz = noteHz[rootNote + 3]; vFreq(pad, pad->baseHz); vDuty(pad, 50);
            pad->gate = true; pad->cur = 0; pad->sus = 22; pad->atk = 6; pad->acc = 0; pad->dec = 0;
        }
    } else if (failPhase == FP_BED) {
        if (bedFrame % 48 == 0) { noteOn(V_BASS, rootNote - 12, 40, 40, 0); V[V_BASS].life = 6; }
        bedFrame++;
    }
}

// The voices a quiet mix turns down: the music proper, and the hats (not the sound effects on the same channel)
static bool slowVoice(int v)
{
    return v == V_HOOK || v == V_BASS || v == V_PAD || v == V_DRUM || (v == V_NOISE2 && sfxNoise.on && sfxNoise.prio == 0);
}

// One frame of one voice: envelope, vibrato, noise slide, then the level the channel gets
// (the peek duck and the MUSIC option apply after the envelope).
static void voiceFrame(int v)
{
    Voice *x = &V[v];
    const Inst *in = &INST[v];
    bool onset = x->fresh;      // a note that started this very frame is sent exactly as it began
    x->fresh = false;
    if (!onset) {
        x->noteFrames++;
        if (x->life > 0 && --x->life == 0) { x->gate = false; x->cur = 0; }
        if (x->gate) {
            if (x->cur > x->sus) { x->cur -= x->dec ? x->dec : x->rel ? x->rel : 127; if (x->cur < x->sus) x->cur = x->sus; }
            else if (x->cur < x->sus) {
                if (!x->atk) x->cur = x->sus;
                else { x->acc += x->atk; x->cur += x->acc >> 4; x->acc &= 15; }
                if (x->cur > x->sus) x->cur = x->sus;
            }
        } else if (x->cur > 0) { x->cur -= x->rel; if (x->cur < 0) x->cur = 0; }
    }
    if (v == V_HOOK && in->atkFrames && x->noteFrames == in->atkFrames && !x->lockDuty) vDuty(x, in->dutySus);
    if (x->noise && x->gate && x->slope && !onset) {
        int hz = x->freq + x->slope;
        vFreq(x, hz < 100 ? 100 : hz);
    } else if (!x->noise && x->vibDiv && x->gate && x->noteFrames >= x->vibDelay) {
        static const s8 TRI[6] = { 0, 1, 1, 0, -1, -1 };
        vFreq(x, x->baseHz + x->baseHz / x->vibDiv * TRI[(x->noteFrames - x->vibDelay) % 6]);
    }

    int t = x->cur;
    bool slow = slowVoice(v);
    if (peeking && slow) t = t / 5;                           // peek ducks everything but the pulse, the chime and the alarm
    if (mix == MIX_PAUSE) { if (slow) t = t / 4; else if (v == V_ALARM) t = t / 2; }
    if (t > 127) t = 127;
    if (!S.music) x->out = 0;
    else if (t < x->out) { int d = (mix == MIX_PAUSE && slow) ? 3 : 127; x->out = x->out - t > d ? x->out - d : t; }
    else if (t > x->out) { int u = (upFrames > 0 && slow) ? 8 : 127; x->out = t - x->out > u ? x->out + u : t; }
    if (lastSent[v] != x->out) { soundSetVolume(x->id, (u8)x->out); lastSent[v] = (s16)x->out; }
}

// Which mix a game state gets. Anything that is not play, the results card, the fail card or the pause
// menu (the title, options, the ledger, and any screen added later) gets the attract mix.
static int mixFor(const Game *g)
{
    switch (g->state) {
        case ST_PLAY:   return MIX_PLAY;
        case ST_PASSED: return MIX_PASSED;
        case ST_FAIL:   return MIX_FAIL;
        case ST_PAUSE:  return MIX_PAUSE;
        default:        return MIX_ATTRACT;
    }
}

void musicTick(const Game *g)
{
    int m = mixFor(g);
    if (mix == MIX_PAUSE && m != MIX_PAUSE) upFrames = 16;        // resuming brings the music back with a short attack
    mix = m;
    if (upFrames > 0) upFrames--;
    if (fade > 0) {
        for (int v = 0; v < NVOICE; v++) if (v != V_CHIME && v != V_NOISE2) { V[v].gate = false; V[v].life = 0; }
        if (--fade == 0) beginShelf();
    } else if (failPhase != FP_NONE) failTick();
    else if (++stepTimer >= stepFrames) { stepTimer = 0; sequenceStep(g); }
    if (fade == 0 && failPhase == FP_NONE) {
        V[V_PAD].sus = padBase + (padAccent > 0 ? 24 : 0);
        if (padAccent > 0) padAccent--;
    }

    // cascade: a rising run of chimes, one per five frames (a louder sound effect wins)
    if (cascadeLeft > 0 && --cascadeTimer <= 0) {
        Sfx s = { {0}, 1, 4, 50, 70, 2, 0, 0, 0, 0, 0, 0, 0, 0 };
        s.semi[0] = (s8)(degreeToNote(1 + cascadeNote, leadOct + 1, 0) - rootNote);
        startTone(&s, rootNote, 2);
        cascadeNote++; cascadeLeft--; cascadeTimer = 5;
    }

    tickTone();
    tickNoise();
    for (int v = 0; v < NVOICE; v++) voiceFrame(v);
}

// What the sequencer last sent to the hardware (for tests: tools/emu/wp.py music()).
void musicDebug(MusicDbg *out)
{
    for (int v = 0; v < NVOICE; v++) {
        out->hz[v] = (u16)V[v].freq;
        out->vol[v] = (u8)lastSent[v];
        out->duty[v] = (u8)V[v].duty;
    }
    out->step = (u8)lastStep;
    out->section = (u8)lastSection;
    out->tempo = (u8)stepFrames;
    out->drumLevel = (u8)drumLevel;
    out->lastSfx = (s16)lastSfx;
    out->sfxCount = (u16)sfxCount;
    out->mix = (u8)mix; out->failPhase = (u8)failPhase; out->fanfare = (u8)passMode;
    out->pickup = (u8)pickupBars; out->fade = (u8)fade; out->reserved = 0;
    out->wordCount = (u16)wordCount;
}
