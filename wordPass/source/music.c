// Dynamic chiptune: a fixed four-bar hook with stems mixed by game state.
// Uses the DS PSG channels directly, no samples, so stems mute for free.
#include "game.h"
#include <stdlib.h>

#define STEP_FRAMES 8          // one 16th note = 8 frames (112.5 BPM)
#define PATTERN 64             // 4 bars of 16ths

enum { V_HOOK = 0, V_PULSE, V_BASS, V_PAD, V_ALARM, V_CHIME, V_DRUM, NVOICE };

typedef struct {
    int  id;          // libnds sound id
    int  vol, target; // 0..127
    int  freq;
    bool noise;
} Voice;

static Voice V[NVOICE];
static u16 noteHz[128];
static int stepTimer, step;
static int rootNote = 60;      // C4
static bool minorMode;
static int drumLevel, drumDecay;
static int bassLife;
static bool peeking, passFlourish, failing;
static int flourishSteps, failFrames;
static int peekHookIdx;
static int chimeFrames, chimeNote;
static int cascadeLeft, cascadeTimer, cascadeNote;
static int lastWordStep = -100;

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

static void setVoice(int v, int note, int vol)
{
    Voice *x = &V[v];
    int hz = (note >= 0 && note < 128) ? noteHz[note] : 0;
    if (hz && hz != x->freq) { soundSetFreq(x->id, hz); x->freq = hz; }
    x->target = vol;
}

void musicInit(void)
{
    // equal-tempered table from one octave of integer frequencies: each lower octave is a
    // rounded halving, each higher one a doubling (no pow(), no libm): C4 = 262, A4 = 440
    static const u16 OCT8[12] = { 4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902 };
    for (int n = 0; n < 128; n++) {
        int s = 9 - n / 12;            // C8 (4186 Hz) is MIDI note 108
        noteHz[n] = s > 0 ? (u16)((OCT8[n % 12] + (1 << (s - 1))) >> s) : (u16)(OCT8[n % 12] << -s);
    }
    soundEnable();
    static const DutyCycle duty[NVOICE] = {
        DutyCycle_25, DutyCycle_12, DutyCycle_50, DutyCycle_37, DutyCycle_12, DutyCycle_50, DutyCycle_50
    };
    for (int v = 0; v < NVOICE; v++) {
        V[v].noise = (v == V_DRUM);
        V[v].vol = V[v].target = 0;
        V[v].freq = 440;
        V[v].id = V[v].noise ? soundPlayNoise(8000, 0, 64) : soundPlayPSG(duty[v], 440, 0, 64);
    }
    musicNewShelf(1, false);
}

void musicNewShelf(int shelf, bool lockdown)
{
    static const u8 keys[] = { 60, 62, 57, 65, 64, 67, 55, 69 };
    rootNote = keys[(shelf - 1) % 8];
    minorMode = lockdown;
    passFlourish = failing = false;
    drumLevel = 0; bassLife = 0;
    peekHookIdx = 0;
    step = 0; stepTimer = 0;
}

void musicWord(int len, int score)
{
    // quantized chime: pitch follows score, volume held until next step
    chimeNote = degreeToNote(1 + (score / 60) % 8, 1);
    chimeFrames = 10;
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
    chimeNote = degreeToNote(n, secret ? 2 : 1);
    chimeFrames = secret ? 24 : 7;
}

void musicPassComplete(void) { passFlourish = true; flourishSteps = PATTERN; drumLevel = 3; drumDecay = PATTERN; }
void musicFail(void) { failing = true; failFrames = 90; }

void musicStopAll(void)
{
    for (int v = 0; v < NVOICE; v++) { V[v].target = 0; V[v].vol = 0; soundSetVolume(V[v].id, 0); }
}

static void sequenceStep(const Game *g)
{
    int bar = (step / 16) % 4;
    int chord = CHORD[bar];
    int hookVol = passFlourish ? 90 : 56;

    // hook: never leaves
    int h = HOOK[step];
    if (h == 99) setVoice(V_HOOK, -1, 0);
    else if (h) setVoice(V_HOOK, degreeToNote(h, 1), hookVol);

    // pulse: chord-tone arpeggio, always on, quiet
    static const u8 arp[4] = { 0, 2, 4, 2 };
    setVoice(V_PULSE, degreeToNote(chord + arp[step & 3], 0), peeking ? 30 : 22);

    // bass: 8ths on the chord root while words keep coming
    if ((step & 1) == 0) setVoice(V_BASS, degreeToNote(chord, -1), bassLife > 0 ? 64 : 0);
    if ((step % 16) == 0 && bassLife > 0 && step != lastWordStep) bassLife--;

    // pad: swells with the pass, slow alternation of chord tones
    int found = 0;
    for (int i = 0; i < g->passLen; i++) found += g->found[i];
    int padVol = g->passLen ? (40 * found) / g->passLen : 0;
    if ((step % 8) == 0) setVoice(V_PAD, degreeToNote(chord + ((step / 8) & 1 ? 4 : 2), 1), padVol);

    // drums: noise bursts by level
    bool kick = (step % 8) == 0, hat = (step & 2) != 0, snare = (step % 16) == 8;
    int dv = 0, df = 0;
    if (drumLevel >= 1 && hat && (step & 1) == 0) { dv = 18; df = 14000; }
    if (drumLevel >= 2 && kick) { dv = 48; df = 1800; }
    if (drumLevel >= 3 && snare) { dv = 44; df = 6000; }
    if (dv) { soundSetFreq(V[V_DRUM].id, df); V[V_DRUM].vol = dv; soundSetVolume(V[V_DRUM].id, dv); }
    V[V_DRUM].target = 0;
    if (drumDecay > 0 && --drumDecay == 0 && drumLevel > 0) { drumLevel--; drumDecay = PATTERN; }

    // alarm: a clashing tone that climbs as fire descends
    int low = boardLowestFireRow(g);
    if (low >= 0) setVoice(V_ALARM, rootNote + 13 + low * 2, 20 + low * 5);
    else setVoice(V_ALARM, -1, 0);

    if (passFlourish) {
        if (h && h != 99) { chimeNote = degreeToNote(h, 2); chimeFrames = 6; }
        if (--flourishSteps <= 0) passFlourish = false;
    }
    step = (step + 1) % PATTERN;
}

void musicTick(const Game *g)
{
    if (failing) {
        // everything dies except the alarm sliding down
        for (int v = 0; v < NVOICE; v++) if (v != V_ALARM) V[v].target = 0;
        if (failFrames > 0) {
            failFrames--;
            int hz = 200 + failFrames * 6;
            soundSetFreq(V[V_ALARM].id, hz);
            V[V_ALARM].target = failFrames > 10 ? 60 : 0;
        }
    } else {
        if (++stepTimer >= STEP_FRAMES) { stepTimer = 0; sequenceStep(g); }
    }

    // chime (one-shot, decays)
    if (chimeFrames > 0) {
        chimeFrames--;
        soundSetFreq(V[V_CHIME].id, noteHz[chimeNote]);
        V[V_CHIME].target = chimeFrames > 2 ? 70 : 0;
        V[V_CHIME].vol = V[V_CHIME].target;
    } else if (cascadeLeft > 0) {
        if (cascadeTimer == 0) {
            chimeNote = degreeToNote(1 + cascadeNote, 2);
            cascadeNote++; cascadeLeft--; chimeFrames = 4;
        }
        cascadeTimer = (cascadeTimer + 1) % 5;
    } else V[V_CHIME].target = 0;

    // ducking in peek mode: everything but the pulse to a whisper
    for (int v = 0; v < NVOICE; v++) {
        int t = V[v].target;
        if (peeking && v != V_PULSE && v != V_CHIME && v != V_ALARM) t = t / 5;
        if (!S.music) t = 0;
        // smooth ramps
        if (V[v].vol < t) { V[v].vol += 6; if (V[v].vol > t) V[v].vol = t; }
        else if (V[v].vol > t) { V[v].vol -= 4; if (V[v].vol < t) V[v].vol = t; }
        static int lastSent[NVOICE] = { -1, -1, -1, -1, -1, -1, -1 };
        if (lastSent[v] != V[v].vol) { soundSetVolume(V[v].id, V[v].vol); lastSent[v] = V[v].vol; }
    }
}
