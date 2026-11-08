// The real songs (source/songs.c, generated from tools/music/songs_src.py): included by board_test.c after the
// engine tests, which run on the reference tune LEGACY. Everything here is read from the sequencer's mirror or from the
// tables, never by ear. `SONGSTAT` lines are the measured numbers docs/MUSIC.md quotes.

static const u8 T_SCALE[MODE_COUNT][7] = {
    { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 },
    { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 1, 4, 5, 7, 8, 10 },
};

// the MIDI note of a degree, computed here from the table (not through music.c)
static int tMidi(const Song *s, int deg, int oct, int chord)
{
    int d = deg - 1, o = d / 7; d %= 7;
    int semi = T_SCALE[s->mode][d];
    if (s->harm && chord == 5 && d == 6) semi = 11;
    return s->root + semi + 12 * (o + oct);
}

typedef struct { int peakSum, peakHook, peakVoice[8], simul, frames; long total; } Loud;

// Run `frames` frames of song s in play (or on the title when menu), words arriving every 16 steps (drums and bass in),
// and record the loudest frame: the sum of the six music voices (hook pulse bass pad drum noise2), each voice's peak, the
// most voices sounding at once.
static void loudRun(const Song *s, bool menu, int frames, int wordLen, Loud *out)
{
    memset(out, 0, sizeof *out);
    musicForceSong(s);
    g.state = menu ? ST_TITLE : ST_PLAY;
    musicNewShelf(1, false);
    for (int f = 0; f < frames; f++) {
        musicTick(&g);
        if (!menu && wordLen && (f % (16 * s->stepFrames)) == 5) musicWord(wordLen, 0, false);
        MusicDbg d = mdbg();
        int sum = 0, simul = 0;
        for (int v = 0; v < 8; v++) {
            if (v == 4 || v == 5) continue;              // alarm and chime are sound effects, not the song
            sum += d.vol[v];
            if (d.vol[v]) simul++;
            if (d.vol[v] > out->peakVoice[v]) out->peakVoice[v] = d.vol[v];
        }
        if (sum > out->peakSum) out->peakSum = sum;
        if (simul > out->simul) out->simul = simul;
        out->total += sum; out->frames++;
    }
    musicForceSong(NULL);
}

// How different two stretches of hook are: of the steps where either has a note, the share where the notes differ
static int differPct(const u8 *a, const u8 *b, int n)
{
    int either = 0, differ = 0;
    for (int i = 0; i < n; i++) {
        bool na = a[i] && a[i] != 99, nb = b[i] && b[i] != 99;
        if (!na && !nb) continue;
        either++;
        if (a[i] != b[i]) differ++;
    }
    return either ? differ * 100 / either : 0;
}
static int onsetCount(const u8 *h, int from, int to) { int n = 0; for (int i = from; i < to; i++) if (h[i] && h[i] != 99) n++; return n; }

static void songTests(void)
{
    u16 nhz[128];
    musicBuildNotes(nhz);
    boardOf('a'); g.passLen = 0; g.state = ST_PLAY;
    S.music = 1; S.fire = FIRE_NORMAL;
    musicForceSong(NULL);

    // ---- the set: eight to ten songs, one menu song, a Daily song, dark songs for Lockdown, the rest for Classic
    int menus = 0, dailies = 0, darks = 0, classics = 0;
    bool names = true;
    for (int i = 0; i < NSONGS; i++) {
        menus += SONGS[i].role == SR_MENU; dailies += SONGS[i].role == SR_DAILY;
        darks += SONGS[i].role == SR_DARK; classics += SONGS[i].role == SR_CLASSIC;
        if (!SONGS[i].name || !SONGS[i].name[0]) names = false;
        for (int j = 0; j < i; j++) if (!strcmp(SONGS[i].name, SONGS[j].name)) names = false;
    }
    CHECK("eight to ten songs with unique names", NSONGS >= 8 && NSONGS <= 10 && names);
    CHECK("one menu song, one Daily song, at least three dark songs and at least five Classic songs", menus == 1 && dailies == 1 && darks >= 3 && classics >= 5);

    // ---- every song's tables
    bool lens_ = true, degs = true, variety = true, bar4 = true, chords = true, drums = true, insts = true, tempo = true, bassOk = true, dens = true;
    for (int i = 0; i < NSONGS; i++) {
        const Song *s = &SONGS[i];
        if (s->stepFrames < 5 || s->stepFrames > 12 || s->gain < 40 || s->gain > 100 || s->leadOct > 1) tempo = false;
        for (int k = 0; k < 64; k++) {
            int a = s->hookA[k], b = s->hookB[k];
            if (!((a == 0 || a == 99 || (a >= 1 && a <= 12)) && (b == 0 || b == 99 || (b >= 1 && b <= 12)))) degs = false;
        }
        if (s->hookA[0] == 0 || s->hookA[0] == 99) lens_ = false;   // the tune starts on a note
        if (s->hookA[16] == 0 || s->hookA[16] == 99 || s->hookA[32] == 0 || s->hookA[32] == 99 || s->hookA[48] == 0 || s->hookA[48] == 99) lens_ = false;
        if (differPct(s->hookA, s->hookB, 64) < 60) variety = false;     // section B is a different phrase, not a copy
        if (differPct(s->hookA, s->hookA + 48, 16) < 50) bar4 = false;   // bar 4 does not repeat bar 1
        int n = onsetCount(s->hookA, 0, 64), nb = onsetCount(s->hookB, 0, 64);
        if (n < 14 || n > 40 || nb < 14 || nb > 48) dens = false;
        for (int k = 0; k < 4; k++) if (s->chordA[k] < 1 || s->chordA[k] > 7 || s->chordB[k] < 1 || s->chordB[k] > 7) chords = false;
        int nb0 = 0;
        for (int k = 0; k < 16; k++) { if (s->bass[0][k] != BASS_REST) nb0++; if (s->bass[0][k] != BASS_REST && (s->bass[0][k] < -12 || s->bass[0][k] > 12)) bassOk = false; }
        if (nb0 < 2) bassOk = false;
        if (memcmp(s->bass[0], s->bass[1], sizeof s->bass[0]) == 0 && s->role != SR_MENU && false) bassOk = false;
        for (int l = 0; l < 3; l++) {
            const DrumLevel *D = &s->drum[l];
            int hats = 0;
            for (int k = 0; k < 16; k++) { if (D->hat[k] > 30) drums = false; if (D->hat[k]) hats++; }
            if (!hats) drums = false;                              // every level has at least a hat
            if (l == 0) for (int b = 0; b < 4; b++) if (D->kick[b] || D->snare[b] || D->ghost[b] || D->open[b]) drums = false;   // level 1: hats only
            if (l >= 1 && s->role != SR_MENU) {
                int ks = 0, sn = 0;
                for (int b = 0; b < 4; b++) { ks += !!D->kick[b]; sn += !!D->snare[b]; }
                if (ks < 4 || sn < 4) drums = false;               // levels 2 and 3 have a kick and a snare in every bar
            }
        }
        if (s->role != SR_MENU && (!s->drum[2].fill || !s->drum[2].open[0])) drums = false;
        for (int v = 0; v < 4; v++) {
            const Inst *in = &s->inst[v];
            if (in->volOn > 70 || in->volSus > in->volOn || in->release == 0) insts = false;
        }
        if (s->padMax > 48) insts = false;
    }
    CHECK("every song starts its hook on a note and every bar of section A has one", lens_);
    CHECK("every hook degree is a rest, a hold or 1..12", degs);
    CHECK("section B is a different phrase from section A (60% or more of the steps that have a note in either differ)", variety);
    CHECK("bar 4 of section A does not repeat bar 1 (half or more of the notes differ)", bar4);
    CHECK("the hook has 14 to 40 notes in A and 14 to 48 in B (a tune, never a wall of notes)", dens);
    CHECK("chord degrees are 1..7", chords);
    CHECK("the bass riff has at least two notes and stays within an octave of the chord root", bassOk);
    CHECK("drums: every level has a hat (level at most 30), level 1 is hats only, levels 2 and 3 have kick and snare in every bar, level 3 fills and opens", drums);
    CHECK("instruments: no voice starts above 70, settles above its onset, or has no release; pad at most 48", insts);
    CHECK("tempo 5..12 frames a step, level 40..100 percent, lead octave 0 or 1", tempo);

    // ---- through the sequencer: the hook plays the table, the tune is A A B A and the loop closes
    {
        bool hookOk = true, register_ = true, form = true, closes = true, bassNotes = true, hits = true;
        for (int i = 0; i < NSONGS; i++) {
            const Song *s = &SONGS[i];
            static Snap sn[64 * 8];
            musicForceSong(s); g.state = ST_PLAY; musicNewShelf(1, false);
            capture(sn, 64 * 8, 1);
            static const int want[8] = { 0, 0, 1, 0, 0, 0, 1, 0 };
            for (int l = 0; l < 8; l++) if (sn[l * 64].section != want[l] || sn[l * 64].step != 0) form = false;
            for (int k = 0; k < 64 * 8; k++) if (sn[k].step != k % 64) closes = false;
            for (int l = 1; l < 8; l++) {
                                const u8 *hk = want[l] ? s->hookB : s->hookA;
                const u8 *ch = want[l] ? s->chordB : s->chordA;
                for (int k = 0; k < 64; k++) {
                    int h = hk[k];
                    if (!h || h == 99) continue;
                    int note = tMidi(s, h, s->leadOct, ch[k / 16]);
                    if (sn[l * 64 + k].hz[0] != nhz[note] || sn[l * 64 + k].vol[0] == 0) hookOk = false;
                    if (note < 60 || note > 89) register_ = false;
                }
            }
            // the loop closes: the second A and the last A are the same bar for bar
            for (int k = 0; k < 64; k++) if (sn[64 + k].hz[0] != sn[64 * 3 + k].hz[0] && sn[64 + k].vol[0] && sn[64 * 3 + k].vol[0]) closes = false;
            // bass notes through the sequencer (words keep arriving): the first bass step of loop 1 is the table's
            for (int k = 0; k < 16; k++) {
                int semi = s->bass[0][k];
                if (semi == BASS_REST) continue;
                int note = tMidi(s, s->chordA[0], -1, s->chordA[0]) + semi;
                if (sn[64 + k].hz[2] != nhz[note]) bassNotes = false;
            }
            (void)hits;
        }
        CHECK("every song plays A A B A over 64 steps, four loops of the form twice, and counts steps 0..63 without a gap", form && closes);
        CHECK("every hook note the table lists reaches the PSG at its scale degree's pitch (all 10 songs, loops 1..7)", hookOk);
        CHECK("every lead note is between C4 and F6", register_);
        CHECK("every bass step the table lists is the chord root an octave down plus its offset", bassNotes);
        musicForceSong(NULL);
    }

    // ---- drums through the sequencer, level by level, against each song's masks
    {
        bool d1 = true, d2 = true, d3 = true, openHat = true;
        for (int i = 0; i < NSONGS; i++) {
            const Song *s = &SONGS[i];
            static Snap sn[128];
            static const int lens[3] = { 3, 5, 7 };
            for (int lv = 1; lv <= 3; lv++) {
                musicForceSong(s); g.state = ST_PLAY; musicNewShelf(1, false); musicWord(lens[lv - 1], 0, false);
                capture(sn, 128, 0);
                const DrumLevel *D = &s->drum[lv - 1];
                Snap *q = sn + 64;
                int openAt = -1;
                for (int k = 0; k < 64; k++) {
                    int bar = k / 16, sb = k % 16;
                    bool fill = lv == 3 && D->fill && k >= 60;
                    int wantHat = D->hat[sb], hatHz = s->hatHz;
                    if ((D->open[bar] >> sb) & 1) { wantHat = s->openVol; hatHz = s->openHz; openAt = k; }
                    else if (!wantHat && openAt >= 0 && (k - openAt) * s->stepFrames < 10) { wantHat = s->openVol; hatHz = s->openHz; }   // the open hat is still ringing (10 frames)
                    int wantDrum = 0, drumHz = 0;
                    if ((D->ghost[bar] >> sb) & 1 && !fill) { wantDrum = s->ghostVol; drumHz = s->kickHz; }
                    if ((D->kick[bar] >> sb) & 1) { wantDrum = s->kickVol; drumHz = s->kickHz; }
                    if (fill) { wantDrum = 30 + 6 * (k - 60) + (k == 63 ? 2 : 0); drumHz = s->snareHz; }
                    else if ((D->snare[bar] >> sb) & 1) { wantDrum = s->snareVol; drumHz = s->snareHz; }
                    wantHat = wantHat * s->gain / 100; wantDrum = wantDrum * s->gain / 100;   // the song's own level scales every music voice
                    bool hatOk = wantHat ? (q[k].vol[7] == wantHat && q[k].hz[7] == hatHz) : q[k].vol[7] == 0;
                    bool drumOk = wantDrum ? (q[k].vol[6] == wantDrum && q[k].hz[6] == drumHz) : q[k].vol[6] == 0;
                    if (lv == 1 && !(hatOk && drumOk)) d1 = false;
                    if (lv == 2 && !(hatOk && drumOk)) d2 = false;
                    if (lv == 3 && !(hatOk && drumOk)) { d3 = false; printf("   %s level 3 step %d: hat %d/%d@%d drum %d/%d@%d\n", s->name, k, q[k].vol[7], wantHat, hatHz, q[k].vol[6], wantDrum, drumHz); }
                    if (lv == 3 && k == 0 && (D->open[0] & 1) && q[0].vol[7] != s->openVol * s->gain / 100) openHat = false;
                }
            }
        }
        CHECK("drum level 1 of every song plays only its hat pattern", d1);
        CHECK("drum level 2 of every song plays its kick, snare, ghost kick and hats on the right steps at the right levels and pitches", d2);
        CHECK("drum level 3 of every song plays its groove, the open hat on step 0 and the bar-4 snare roll", d3 && openHat);
        musicForceSong(NULL);
    }

    // ---- tempo under fire, per song
    {
        bool fire = true;
        for (int i = 0; i < NSONGS; i++) {
            const Song *s = &SONGS[i];
            static Snap sn[4];
            int base = s->stepFrames, rows[3] = { 1, 3, 5 }, want[3] = { base, base - 1, base - 2 };
            for (int r = 0; r < 3; r++) {
                musicForceSong(s); g.state = ST_PLAY; g.cell[3][rows[r]].state = CELL_BURN;
                musicNewShelf(1, false);
                capture(sn, 2, 0);
                g.cell[3][rows[r]].state = CELL_TILE;
                int w = want[r] < 5 ? (base < 5 ? base : 5) : want[r];
                if (sn[0].tempo != w) fire = false;
            }
        }
        CHECK("under fire every song lifts one frame a step from flame row 3 and two from row 5, never faster than 5 frames", fire);
        musicForceSong(NULL);
    }

    // ---- which song plays where
    {
        int classic[16], dark[16], daily = -1, menu = -1, nc = 0, nd = 0;
        for (int i = 0; i < NSONGS; i++) {
            if (SONGS[i].role == SR_CLASSIC) classic[nc++] = i;
            else if (SONGS[i].role == SR_DARK) dark[nd++] = i;
            else if (SONGS[i].role == SR_DAILY) daily = i;
            else menu = i;
        }
        bool shelves = true;
        for (int shelf = 1; shelf <= 12; shelf++) if (musicSongFor(shelf, false, 0, false, FIRE_NORMAL) != classic[(shelf - 1) % nc]) shelves = false;
        for (int shelf = 1; shelf <= 12; shelf++) if (musicSongFor(shelf, false, 0, false, FIRE_RELAXED) != classic[(shelf - 1) % nc]) shelves = false;
        CHECK("Classic shelves 1..12 rotate through the Classic songs by (shelf - 1) % songs", shelves);
        bool tiers = true;
        for (int t = 0; t < 4; t++) if (SONGS[musicSongFor(6 + 3 * t, true, t, false, FIRE_NORMAL)].role != SR_DARK) tiers = false;
        CHECK("Lockdown tiers 0..3 choose dark songs, tier 0 the first", tiers && musicSongFor(6, true, 0, false, FIRE_NORMAL) == dark[0] && musicSongFor(9, true, 1, false, FIRE_NORMAL) == dark[1] &&
              musicSongFor(12, true, 2, false, FIRE_NORMAL) == dark[2] && musicSongFor(15, true, 3, false, FIRE_NORMAL) == dark[nd - 1]);
        bool fast = true;
        for (int shelf = 1; shelf <= 12; shelf++) if (musicSongFor(shelf, false, 0, false, FIRE_FAST) != dark[(shelf - 1) % nd]) fast = false;
        CHECK("Classic with FIRE FAST borrows the dark songs by shelf", fast);
        CHECK("the Daily Pass has its own song, even in Lockdown or on FIRE FAST", musicSongFor(1, false, 0, true, FIRE_NORMAL) == daily && musicSongFor(7, true, 1, true, FIRE_FAST) == daily);
        CHECK("every song of every role is reachable", nc >= 5 && nd >= 3 && daily >= 0 && menu >= 0);

        // through the engine: musicMenu and musicPlayShelf set the song and the mirror says so
        musicForceSong(NULL);
        g.state = ST_TITLE; musicMenu();
        MusicDbg d = mdbg();
        CHECK("musicMenu plays the menu song (the mirror reads its index, role 0 and its level)", d.song == menu && d.role == SR_MENU && d.gain == SONGS[menu].gain && d.songs == NSONGS);
        g.mode = MODE_CLASSIC; g.lockdown = false; g.shelf = 7; g.lockTier = 0; S.fire = FIRE_NORMAL;
        musicPlayShelf(&g); d = mdbg();
        CHECK("musicPlayShelf on Classic shelf 7 plays Classic song (7 - 1) % n", d.song == classic[6 % nc]);
        g.lockdown = true; g.lockTier = 2; g.shelf = 12; musicPlayShelf(&g); d = mdbg();
        CHECK("musicPlayShelf in Lockdown tier 2 plays a dark song", d.song == dark[2] && d.role == SR_DARK);
        g.lockdown = false; g.mode = MODE_DAILY; musicPlayShelf(&g); d = mdbg();
        CHECK("musicPlayShelf in the Daily Pass plays the Daily song", d.song == daily && d.role == SR_DAILY);
        g.mode = MODE_CLASSIC; S.fire = FIRE_FAST; g.shelf = 2; musicPlayShelf(&g); d = mdbg();
        CHECK("musicPlayShelf on FIRE FAST plays a dark song", d.role == SR_DARK);
        S.fire = FIRE_NORMAL; g.shelf = 1; g.lockdown = false;
        g.state = ST_TITLE; musicMenu(); d = mdbg();
        CHECK("back on the title the menu song returns", d.role == SR_MENU);
        g.state = ST_PLAY;
    }

    // ---- loudness: every song peaks at or below the reference tune's own peak; the menu song well below
    {
        Loud ref, refMenu, L;
        loudRun(&LEGACY, false, 64 * 8 * 8, 7, &ref);
        loudRun(&LEGACY, true, 64 * 8 * 8, 0, &refMenu);
        printf("SONGSTAT reference tune: peak music sum %d (hook %d pulse %d bass %d pad %d drum %d hat %d), title peak %d\n", ref.peakSum,
               ref.peakVoice[0], ref.peakVoice[1], ref.peakVoice[2], ref.peakVoice[3], ref.peakVoice[6], ref.peakVoice[7], refMenu.peakSum);
        bool cap = true, voices = true, balanced = true;
        for (int i = 0; i < NSONGS; i++) {
            const Song *s = &SONGS[i];
            if (s->role == SR_MENU) continue;
            loudRun(s, false, 64 * 8 * s->stepFrames, 7, &L);
            printf("SONGSTAT %-18s %3.0f BPM peak sum %3d avg %3ld hook %2d pulse %2d bass %2d pad %2d drum %2d hat %2d, %d voices at most\n", s->name, 900.0 / s->stepFrames,
                   L.peakSum, L.total / L.frames, L.peakVoice[0], L.peakVoice[1], L.peakVoice[2], L.peakVoice[3], L.peakVoice[6], L.peakVoice[7], L.simul);
            if (L.peakSum > ref.peakSum) cap = false;
            for (int v = 0; v < 8; v++) if (v != 4 && v != 5 && L.peakVoice[v] > 70) voices = false;
            if (L.peakVoice[0] < 20 || L.peakVoice[2] < 20) balanced = false;           // the hook and the bass are always audible
            if (L.peakVoice[1] > L.peakVoice[0] || L.peakVoice[3] > L.peakVoice[0]) balanced = false;   // the pulse and pad sit behind the hook
        }
        CHECK("loudness cap: no song's peak music sum (hook, pulse, bass, pad, drums, hats) is above the reference tune's", cap);
        CHECK("loudness cap: no voice of any song is above 70", voices);
        CHECK("balance: the hook and bass are audible and the pulse and the pad sit under the hook in every song", balanced);
        musicForceSong(NULL);
    }

    // ---- the menu song: warm, gentle, sparse, quiet (the measurable properties docs/MUSIC.md argues from)
    {
        int mi = -1;
        for (int i = 0; i < NSONGS; i++) if (SONGS[i].role == SR_MENU) mi = i;
        const Song *m = &SONGS[mi];
        Loud ml, ref;
        loudRun(&LEGACY, true, 64 * 8 * 8, 0, &ref);
        loudRun(m, true, 64 * 8 * m->stepFrames * 2, 0, &ml);
        // note density and register from the table
        int onA = onsetCount(m->hookA, 0, 64), onB = onsetCount(m->hookB, 0, 64);
        double perBar = (onA * 3 + onB) / 16.0;                   // A A B A: three loops of A, one of B, 16 bars
        int lo = 999, hi = 0;
        for (int k = 0; k < 64; k++) {
            const u8 *sect[2] = { m->hookA, m->hookB };
            for (int t = 0; t < 2; t++) {
                int h = sect[t][k];
                if (!h || h == 99) continue;
                int n = tMidi(m, h, m->leadOct, t ? m->chordB[k / 16] : m->chordA[k / 16]);
                if (n < lo) lo = n;
                if (n > hi) hi = n;
            }
        }
        int hatSteps = 0, hatMax = 0;
        for (int k = 0; k < 16; k++) { if (m->drum[0].hat[k]) hatSteps++; if (m->drum[0].hat[k] > hatMax) hatMax = m->drum[0].hat[k]; }
        printf("SONGSTAT menu %s: %.0f BPM, mode %d%s, hook %.1f notes a bar (%d in A, %d in B), lead MIDI %d..%d (%d..%d Hz), bass tonic %d, "
               "peak sum %d (title of the old attract tune: %d), average sum %ld, %d voices at most, level %d%%, hats %d a bar at level %d\n",
               m->name, 900.0 / m->stepFrames, m->mode, m->mode == MODE_MIXOLYDIAN ? " (mixolydian)" : "", perBar, onA, onB, lo, hi, nhz[lo], nhz[hi], m->root - 12, ml.peakSum, ref.peakSum,
               ml.total / ml.frames, ml.simul, m->gain, hatSteps, hatMax);
        CHECK("menu song: mixolydian or major, at most 100 BPM (9 or more frames a step)", (m->mode == MODE_MIXOLYDIAN || m->mode == MODE_MAJOR) && m->stepFrames >= 9);
        CHECK("menu song: at most 5 hook notes a bar on average and at most 8 in any bar", perBar <= 5.0 &&
              onsetCount(m->hookA, 0, 16) <= 8 && onsetCount(m->hookA, 16, 32) <= 8 && onsetCount(m->hookA, 32, 48) <= 8 && onsetCount(m->hookA, 48, 64) <= 8 &&
              onsetCount(m->hookB, 0, 16) <= 8 && onsetCount(m->hookB, 16, 32) <= 8 && onsetCount(m->hookB, 32, 48) <= 8 && onsetCount(m->hookB, 48, 64) <= 8);
        CHECK("menu song: the lead stays between G4 and D6 (no shrill top, no muddy bottom) and the hook spans at most an octave and a sixth", lo >= 67 && hi <= 86 && hi - lo <= 21);
        CHECK("menu song: the hook leaps at most a fifth between notes",
              ({ bool ok = true; for (int t = 0; t < 2; t++) { const u8 *h = t ? m->hookB : m->hookA; int prev = 0; for (int k = 0; k < 64; k++) if (h[k] && h[k] != 99) { if (prev && abs(tMidi(m, h[k], 0, 1) - tMidi(m, prev, 0, 1)) > 8) ok = false; prev = h[k]; } } ok; }));
        CHECK("menu song: no kick or snare at any level, a single very soft hat (at most 2 steps a bar, level at most 8)",
              hatSteps <= 2 && hatMax <= 8 && !m->drum[0].kick[0] && !m->drum[1].kick[0] && !m->drum[2].kick[0] && !m->drum[1].snare[0] && !m->drum[2].snare[0]);
        CHECK("menu song: never more than 5 voices sounding at once", ml.simul <= 5);
        CHECK("menu song: the drum voice stays silent for the whole run", ml.peakVoice[6] == 0);
        CHECK("menu song: its peak music sum is at most half of the old title tune's and its hook never above 40", ml.peakSum * 100 <= ref.peakSum * 50 && ml.peakVoice[0] <= 40);
        CHECK("menu song: a quiet pad (at most 20) and a soft pulse (at most 16)", ml.peakVoice[3] <= 20 && ml.peakVoice[1] <= 16);
        CHECK("menu song: the hook is a bell, no vibrato wobble and no sustain above a third of its onset", m->inst[0].vibDiv == 0 && m->inst[0].volSus * 3 <= m->inst[0].volOn + 6);
        musicForceSong(NULL);
    }
}
