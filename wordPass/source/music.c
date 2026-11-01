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
    int  cur, sus;          // envelope level now, and the level it settles to
    int  dec, atk, rel;     // fall per frame towards sus, rise per frame (0 = at once), fall per frame once released
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
static int stepTimer, step, lastStep = 255;
static int stepFrames = STEP_FRAMES;
static int rootNote = 60;      // C4
static int leadOct = 1;        // register window: the lead sits an octave lower in the high keys
static bool minorMode;
static int drumLevel, drumDecay;
static int bassLife;
static bool peeking, passFlourish, failing;
static int flourishSteps, failFrames;
static int peekHookIdx;
static int cascadeLeft, cascadeTimer, cascadeNote;
static int lastWordStep = -100;
static int lastSfx = -1, sfxCount;

// Hook: scale degrees (1..14), 0 = hold, 99 = rest. Same rhythm every shelf.
static const u8 HOOK[PATTERN] = {
    1,0,3,0, 5,0,3,0, 6,0,5,0, 0,0,3,0,
    1,0,3,0, 5,0,8,0, 7,0,5,0, 0,0,99,0,
    6,0,8,0, 7,0,5,0, 3,0,5,0, 6,0,5,0,
    3,0,2,0, 1,0,0,0, 5,0,0,0, 1,0,99,0,
};
// chord roots per bar (I vi IV V) as degrees
static const u8 CHORD[4] = { 1, 6, 4, 5 };
static const u8 MAJOR[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const u8 MINOR[7] = { 0, 2, 3, 5, 7, 8, 10 };

static int degreeToNote(int degree, int octave)
{
    const u8 *sc = minorMode ? MINOR : MAJOR;
    int d = degree - 1;
    int oct = d / 7; d %= 7;
    return rootNote + sc[d] + 12 * (oct + octave);
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
    x->gate = true; x->cur = volOn; x->sus = volSus; x->dec = dec; x->atk = 0;
    x->life = 0; x->noteFrames = 0; x->slope = 0; x->fresh = true;
    x->vibDelay = in->vibDelay; x->vibDiv = in->vibDiv;
}

// The Inst's own onset, sustain and decay.
static void notePlay(int v, int note, int scalePct)
{
    const Inst *in = &INST[v];
    noteOn(v, note, in->volOn * scalePct / 100, in->volSus * scalePct / 100, in->decay);
}

static void noteOff(int v) { V[v].gate = false; }

// A held level that follows `vol` without retriggering (the alarm).
static void voiceHold(int v, int note, int vol)
{
    Voice *x = &V[v];
    if (note >= 0 && note < 128) { x->baseHz = noteHz[note]; vFreq(x, x->baseHz); }
    x->gate = vol > 0; x->cur = x->sus = vol; x->dec = 0; x->atk = 0; x->life = 0;
}

// A noise hit: starts at hz, moves `slope` Hz per frame, starts at vol and falls by `decay` per frame,
// and closes after `life` frames.
static void noiseHit(int v, int hz, int slope, int vol, int decay, int life)
{
    Voice *x = &V[v];
    x->baseHz = hz; vFreq(x, hz);
    x->gate = true; x->cur = vol; x->sus = 0; x->dec = decay; x->atk = 0;
    x->life = life; x->slope = slope; x->noteFrames = 0; x->fresh = true;
    x->vibDiv = 0;
}

// ---- sound effects: a short run of notes on V_CHIME and/or a noise burst on V_NOISE2
typedef struct {
    s8  semi[6];                    // tone: semitones over rootNote (TAP and UNTAP patch semi[0])
    u8  n, frames, duty, vol, prio; // n notes of `frames` frames each, duty percent, level, priority
    u8  hold, vib;                  // frames the last note is held after the run, with vibrato
    u8  noiseFrames, noiseVol;      // noise burst length and level (0 frames = no noise part)
    s16 noiseStart, noiseSlope;     // noise Hz at the start and the change per frame
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
    if (t->frame < run && t->frame % t->s.frames == 0) {
        int note = t->base + t->s.semi[t->frame / t->s.frames];
        x->baseHz = noteHz[note < 0 ? 0 : note > 127 ? 127 : note];
        vFreq(x, x->baseHz);
        vDuty(x, t->s.duty);
        x->gate = true; x->cur = x->sus = t->s.vol; x->dec = 0; x->atk = 0; x->life = 0;
        x->noteFrames = 0; x->vibDiv = 0; x->fresh = true;
    }
    if (t->frame >= run && t->s.vib) { x->vibDelay = 0; x->vibDiv = 96; }
    if (++t->frame >= run + t->s.hold) { t->on = false; t->prio = -1; noteOff(V_CHIME); }
}

static void tickNoise(void)
{
    SfxState *t = &sfxNoise;
    if (!t->on) return;
    Voice *x = &V[V_NOISE2];
    int hz = t->s.noiseStart + t->s.noiseSlope * t->frame;
    x->gate = true; x->cur = x->sus = t->s.noiseVol; x->dec = 0; x->atk = 0; x->life = 0; x->slope = 0;
    x->baseHz = hz < 100 ? 100 : hz;
    vFreq(x, x->baseHz);
    if (++t->frame >= t->s.noiseFrames) { t->on = false; t->prio = -1; x->gate = false; x->cur = 0; }
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
}

void musicNewShelf(int shelf, bool lockdown)
{
    static const u8 keys[] = { 60, 62, 57, 65, 64, 67, 55, 69 };
    rootNote = keys[(shelf - 1) % 8];
    leadOct = rootNote >= 64 ? 0 : 1;
    minorMode = lockdown;
    passFlourish = failing = false;
    drumLevel = 0; bassLife = 0;
    peekHookIdx = 0;
    step = 0; stepTimer = 0; lastStep = 255;
}

void musicWord(int len, int score)
{
    // quantized chime: pitch follows score, volume held until next step
    Sfx s = { {0}, 1, 10, 50, 70, 3, 0, 0, 0, 0, 0, 0 };
    s.semi[0] = (s8)(degreeToNote(1 + (score / 60) % 8, leadOct) - rootNote);
    startTone(&s, rootNote, 3);
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
    int tries = 0, n;
    do { n = HOOK[peekHookIdx]; peekHookIdx = (peekHookIdx + 1) % PATTERN; }
    while ((n == 0 || n == 99) && ++tries < PATTERN);
    Sfx s = { {0}, 1, secret ? 24 : 7, 50, 70, 3, 0, 0, 0, 0, 0, 0 };
    s.semi[0] = (s8)(degreeToNote(n, leadOct + (secret ? 1 : 0)) - rootNote);
    startTone(&s, rootNote, 3);
}

void musicPassComplete(void) { passFlourish = true; flourishSteps = PATTERN; drumLevel = 3; drumDecay = PATTERN; }
void musicFail(void) { failing = true; failFrames = 90; }

static void sequenceStep(const Game *g)
{
    int bar = (step / 16) % 4;
    int chord = CHORD[bar];
    int hookScale = passFlourish ? 160 : 100;

    // hook: never leaves
    int h = HOOK[step];
    if (h == 99) noteOff(V_HOOK);
    else if (h) notePlay(V_HOOK, degreeToNote(h, leadOct), hookScale);

    // pulse: chord-tone arpeggio, always on, plucked
    static const u8 arp[4] = { 0, 2, 4, 2 };
    notePlay(V_PULSE, degreeToNote(chord + arp[step & 3], 0), 100);

    // bass: 8ths on the chord root while words keep coming
    if ((step & 1) == 0) {
        if (bassLife > 0) notePlay(V_BASS, degreeToNote(chord, -1), 100);
        else noteOff(V_BASS);
    }
    if ((step % 16) == 0 && bassLife > 0 && step != lastWordStep) bassLife--;

    // pad: swells with the pass, slow alternation of chord tones; the duty flips every four steps
    int found = 0;
    for (int i = 0; i < g->passLen; i++) found += g->found[i];
    int padVol = g->passLen ? (40 * found) / g->passLen : 0;
    Voice *pad = &V[V_PAD];
    if ((step % 8) == 0) {
        int note = degreeToNote(chord + ((step / 8) & 1 ? 4 : 2), leadOct);
        pad->baseHz = noteHz[note]; vFreq(pad, pad->baseHz);
    }
    if ((step % 4) == 0) vDuty(pad, (step / 4) & 1 ? INST[V_PAD].dutySus : INST[V_PAD].dutyAtk);
    pad->gate = true; pad->sus = padVol; pad->atk = 1; pad->dec = 0;

    // drums: noise bursts by level
    bool kick = (step % 8) == 0, hat = (step & 2) != 0, snare = (step % 16) == 8;
    int dv = 0, df = 0;
    if (drumLevel >= 1 && hat && (step & 1) == 0) { dv = 18; df = 14000; }
    if (drumLevel >= 2 && kick) { dv = 48; df = 1800; }
    if (drumLevel >= 3 && snare) { dv = 44; df = 6000; }
    if (dv) noiseHit(V_DRUM, df, 0, dv, dv / 4 + 1, 12);
    if (drumDecay > 0 && --drumDecay == 0 && drumLevel > 0) { drumLevel--; drumDecay = PATTERN; }

    // alarm: a clashing tone that climbs as fire descends
    int low = boardLowestFireRow(g);
    if (low >= 0) voiceHold(V_ALARM, rootNote + 13 + low * 2, 20 + low * 5);
    else voiceHold(V_ALARM, -1, 0);

    if (passFlourish) {
        if (h && h != 99) {
            Sfx s = { {0}, 1, 6, 50, 70, 3, 0, 0, 0, 0, 0, 0 };
            s.semi[0] = (s8)(degreeToNote(h, leadOct + 1) - rootNote);
            startTone(&s, rootNote, 3);
        }
        if (--flourishSteps <= 0) passFlourish = false;
    }
    lastStep = step;
    step = (step + 1) % PATTERN;
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
            else if (x->cur < x->sus) { x->cur += x->atk ? x->atk : x->sus - x->cur; if (x->cur > x->sus) x->cur = x->sus; }
        } else if (x->cur > 0) { x->cur -= x->rel; if (x->cur < 0) x->cur = 0; }
    }
    if (v == V_HOOK && in->atkFrames && x->noteFrames == in->atkFrames) vDuty(x, in->dutySus);
    if (x->noise && x->gate && x->slope && !onset) {
        int hz = x->freq + x->slope;
        vFreq(x, hz < 100 ? 100 : hz);
    } else if (!x->noise && x->vibDiv && x->gate && x->noteFrames >= x->vibDelay) {
        static const s8 TRI[6] = { 0, 1, 1, 0, -1, -1 };
        vFreq(x, x->baseHz + x->baseHz / x->vibDiv * TRI[(x->noteFrames - x->vibDelay) % 6]);
    }

    int t = x->cur;
    if (peeking && v != V_PULSE && v != V_CHIME && v != V_ALARM && v != V_NOISE2) t = t / 5;
    if (!S.music) t = 0;
    if (t > 127) t = 127;
    if (lastSent[v] != t) { soundSetVolume(x->id, (u8)t); lastSent[v] = (s16)t; }
}

void musicTick(const Game *g)
{
    if (failing) {
        // everything dies except the alarm sliding down
        for (int v = 0; v < NVOICE; v++) if (v != V_ALARM && v != V_CHIME && v != V_NOISE2) noteOff(v);
        if (failFrames > 0) {
            failFrames--;
            Voice *a = &V[V_ALARM];
            vFreq(a, 200 + failFrames * 6);
            a->baseHz = a->freq;
            a->gate = failFrames > 10; a->cur = a->sus = 60; a->dec = 0; a->life = 0;
        } else noteOff(V_ALARM);
    } else {
        if (++stepTimer >= stepFrames) { stepTimer = 0; sequenceStep(g); }
    }

    // cascade: a rising run of chimes, one per five frames (a louder sound effect wins)
    if (cascadeLeft > 0 && --cascadeTimer <= 0) {
        Sfx s = { {0}, 1, 4, 50, 70, 2, 0, 0, 0, 0, 0, 0 };
        s.semi[0] = (s8)(degreeToNote(1 + cascadeNote, leadOct + 1) - rootNote);
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
    out->section = 0;
    out->tempo = (u8)stepFrames;
    out->drumLevel = (u8)drumLevel;
    out->lastSfx = (s16)lastSfx;
    out->sfxCount = (u16)sfxCount;
}
