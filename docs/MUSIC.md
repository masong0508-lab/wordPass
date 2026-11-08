# wordPass music

The soundtrack is ten songs for the DS PSG, distilled from the songs of the author's other game, bore-gba
(`/masong0508-lab/bore-gba`, read only). This file is the listening guide, the credits and the evidence:

1. **Source songs**: which bore-gba songs are visible in its jukebox and were analysed.
2. **Analysis**: what `tools/music/analyze_xm.py` found in each one (tempo, key and mode, lead motif, bass riff,
   drum groove). The full per-song data is `tools/music/analysis/<id>.json`.
3. **Families**: how the 24 songs cluster into ten families and which wordPass song each family feeds.

Regenerate the tables with `python3 tools/music/analyze_xm.py --docs` and `python3 tools/music/cluster.py --docs`
(they only rewrite the blocks between their marker comments).

## 1. Source songs

bore-gba lists 40 songs in `source/songs.h`; the normal jukebox listing hides secret songs (names ending in
`(ORIGINAL)`), `PLACEHOLDER` songs and the songs locked in `source/unlocks.h`. Every one of the 24 visible songs is
a tracker module (`SONG_XM`); there is no streamed ADPCM song in the list, so every song could be analysed from its
note data.

<!-- songlist:begin -->
| # | Song | Status |
|---|------|--------|
| 1 | THE DIPPER MAN | visible, analysed from `the_dipper_man.xm` |
| 2 | EARTH AND THE SPACE CITIZENS | visible, analysed from `earth_and_the_space_citizens.xm` |
| 3 | AMIGA MUSIC | visible, analysed from `amiga_music.xm` |
| 4 | EMERGENCY ON THE DANCE FLOOR | visible, analysed from `emergency_hitech.xm` |
|  | EMERGENCY (ORIGINAL) | skipped: secret (ORIGINAL) |
|  | TREE-AGE IN ACTION | skipped: locked (unlocks.h) |
| 5 | MELTDOWN IN MARS | visible, analysed from `meltdown_in_mars_house.xm` |
|  | WORTHLESS CLOUDS | skipped: locked (unlocks.h) |
| 6 | SUNMAN SUNRISE | visible, analysed from `sunman_sunrise.xm` |
| 7 | GOTTCHO BARRACHO | visible, analysed from `gottcho_barracho_ii.xm` |
|  | GOTTCHO BARRACHO (ORIGINAL) | skipped: secret (ORIGINAL) |
| 8 | AN ODE TO THE SPANISH FLEXICODE | visible, analysed from `spanish_flexicode.xm` |
| 9 | MI CORA ZONE | visible, analysed from `mi_cora_zone.xm` |
| 10 | EXCUSES | visible, analysed from `excuses_house.xm` |
| 11 | WHISTLER MAN | visible, analysed from `whistler_shuffle.xm` |
|  | WHISTLER MAN (ORIGINAL) | skipped: secret (ORIGINAL) |
| 12 | THE CYNICAL SYNDICATION | visible, analysed from `cynicaller_dnb.xm` |
| 13 | HOT DAMN | visible, analysed from `hotdamn_rave.xm` |
|  | HOT DAMN (ORIGINAL) | skipped: secret (ORIGINAL) |
| 14 | AIM AND SHOOT | visible, analysed from `aim_and_shoot.xm` |
| 15 | THE MAGIC ACT | visible, analysed from `magic_act.xm` |
| 16 | NURSERY TIME | visible, analysed from `nursery_time.xm` |
|  | TREE-AGE IN ACTION (ORIGINAL) | skipped: secret (ORIGINAL) |
| 17 | TREE-AGE IN ACTION (FAST) | visible, analysed from `tree_fast.xm` |
|  | TREE-AGE IN ACTION (FAST) (ORIGINAL) | skipped: secret (ORIGINAL) |
| 18 | CONDENSED MUSIC | visible, analysed from `condensed_music.xm` |
| 19 | COCAINE COLA | visible, analysed from `cocaine_cola_ii.xm` |
|  | COCAINE COLA (ORIGINAL) | skipped: secret (ORIGINAL) |
| 20 | STAGED THE FULL PERFORMANCE | visible, analysed from `staged.xm` |
|  | CLOSER TO THE END | skipped: locked (unlocks.h) |
|  | CLOSER TO THE END (ORIGINAL) | skipped: secret (ORIGINAL) |
| 21 | THE TICKING BOMB | visible, analysed from `the_ticking_bomb.xm` |
| 22 | MISTER QUICKBULLET | visible, analysed from `quickbullet.xm` |
| 23 | OVER MY VOICE | visible, analysed from `over_my_voice.xm` |
|  | YOU'RE WINNER (ORIGINAL) | skipped: secret (ORIGINAL) |
|  | HERE TODAY (MISSY'S SONG) | skipped: locked (unlocks.h) |
|  | HERE TODAY (ORIGINAL) | skipped: secret (ORIGINAL) |
|  | HERE TODAY (FULL VERSION) | skipped: locked (unlocks.h) |
|  | HERE TODAY GONE TOMORROW | skipped: locked (unlocks.h) |
| 24 | OHIO HOE | visible, analysed from `ohio_hoe.xm` |
<!-- songlist:end -->

## 2. Analysis

Method (`tools/music/xmparse.py` reads the FastTracker II format, `analyze_xm.py` does the rest): the order list is
walked with every `Fxx` speed and tempo change, so each row has a time. Note-ons get a MIDI pitch (sample relative
note applied) and a duration. The **beat BPM** is the dominant speed/BPM turned into a tempo between 70 and 180 by
choosing rows-per-beat 1..32 (or the one nearest the BPM the song's own comment states); half-time songs get a
**feel BPM** at half. **Key and mode**: a duration-weighted pitch-class histogram is scored against eight modes
(major, minor, dorian, phrygian, mixolydian, lydian, harmonic minor, hijaz) for each of the 12 tonics, with
bonuses for the tonic, the fifth, the third and the bass channel's roots. The **lead motif** is the most repeated
4 to 8 note interval-and-rhythm n-gram of the busiest high channel (repeated-note figures are skipped because they
are pulses, not tunes); the table shows its semitones above the song's tonic (0 = tonic in the octave of middle C)
and how many times it recurs. The **bass riff** is the most repeated one-bar pattern of the lowest busy channel
(semitones from the bar's first note, on the 16 steps of the bar). The **drum groove** is the most common bar that has a
kick or a snare, with percussion classified by instrument name, then (for unnamed one-shots played on one pitch) by
sample length and zero-crossing rate: kick = short and low crossing rate, hat = crossing rate over 0.4.

<!-- analysis:begin -->
| # | Song | Source file | Bars | Beat BPM | Rows/beat | Key | Lead motif (semitones) | Bass riff | Drums (k/s/h hits per bar) |
|---|------|-------------|------|----------|-----------|-----|------------------------|-----------|-----------------------------|
| 1 | THE DIPPER MAN | `the_dipper_man.xm` | 89 | 125 | 4 | D lydian | 2,10,10,0,2 x68 | 0,0,8,8,-2 on steps 0,2,4,5,7 | k ---------------- s ---------------- h x-xxxxxxx-xxxxxx |
| 2 | EARTH AND THE SPACE CITIZENS | `earth_and_the_space_citizens.xm` | 52 | 100 | 4 | Ab minor | 0,2,-5,0 x4 | 0,2 on steps 0,6 | k -x-x-x------x--x s --------x------- h x-xx-x-----xx-x- |
| 3 | AMIGA MUSIC | `amiga_music.xm` | 34 | 120 | 4 | F# mixolydian | -10,-9,-7,-5,-2,-3,-5,-7 x7 | 0,-3 on steps 0,2 | k --x---xx--x-x-x- s ---------------- h ---------------- |
| 4 | EMERGENCY ON THE DANCE FLOOR | `emergency_hitech.xm` | 33 | 90 | 16 | C minor | 2,5,10,2,5,10 x60 | 0,7,0,7,0,7,-1,7,2,-1,7,2,0,9,4,0 on steps 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 | k x---xx--xx-xx--- s --x---x---x---x- h xxxxxxxxxxxxxxxx |
| 5 | MELTDOWN IN MARS | `meltdown_in_mars_house.xm` | 188 | 126 | 4 | Eb major | 19,21,22,23,23,18,18,19 x70 | 0,0,0,0 on steps 0,3,6,10 | k x---x---x---x--- s ----x-------x--- h xxxxxxxxxxxxxxxx |
| 6 | SUNMAN SUNRISE | `sunman_sunrise.xm` | 89 | 116 | 4 | A minor | 5,9,12,9 x48 | 0,0,-3,-3,-3 on steps 0,3,6,10,12 | k x---x---x---x--- s ----x-------x--- h -xxx-xxx-xxx-xxx |
| 7 | GOTTCHO BARRACHO | `gottcho_barracho_ii.xm` | 123 | 160 | 4 | E mixolydian | 7,7,4,2,0 x12 | 0,0 on steps 0,8 | k x-----------x--- s -------xx------- h x-x-x-x-x-x-x-x- |
| 8 | AN ODE TO THE SPANISH FLEXICODE | `spanish_flexicode.xm` | 68 | 112 | 4 | Bb dorian | 3,10,3,10,3,10,5,7 x5 | 0,0,-1 on steps 0,6,12 | k x--------x--x--- s ------x----x---- h x-x-xxx-x-xxx-x- |
| 9 | MI CORA ZONE | `mi_cora_zone.xm` | 92 | 133 | 8 | G major | 20,19,14,15,20,19,14,14 x37 | 0,1,6,5,0,0,3,6,5 on steps 0,2,3,5,7,9,10,11,14 | k ---------------- s -x-x-x-----x-x-x h --x---x---x---x- |
| 10 | EXCUSES | `excuses_house.xm` | 210 | 124 | 8 | D minor | -5,-2,2,5 x93 | - | k x---x---x---x--- s ----x-------x--- h xxxxxxxxxxxxxxxx |
| 11 | WHISTLER MAN | `whistler_shuffle.xm` | 211 | 150 | 4 | C dorian | 24,19,17,29,24,22 x66 | 0,5 on steps 10,12 | k x---------x----- s --x-----x---x--- h x-x-x-x-x-x-x-x- |
| 12 | THE CYNICAL SYNDICATION | `cynicaller_dnb.xm` | 174 | 172 | 16 | C major | 6,11,14,18 x378 | 0,-5 on steps 0,8 | k x---------x--x-- s ----x--x---xx--x h -x-x-xxx-x-x-xxx |
| 13 | HOT DAMN | `hotdamn_rave.xm` | 138 | 135 | 16 | D mixolydian | 10,10,10,11,13,14,15 x22 | 0,5,3,17,15 on steps 3,5,7,9,13 | k x-----x----x---x s ----x-------x--- h --x---x---x---x- |
| 14 | AIM AND SHOOT | `aim_and_shoot.xm` | 116 | 162 | 8 | F hijaz | 1,0,1,4,0,4,0 x5 | 0,2 on steps 0,8 | k x---x---x---x-xx s --------x------- h --x---x---x--xxx |
| 15 | THE MAGIC ACT | `magic_act.xm` | 157 | 94 | 16 | Eb mixolydian | 3,5,7,10,15,10,7,5 x119 | 0,-2 on steps 1,13 | k x---------x----- s ---------------- h ---------------- |
| 16 | NURSERY TIME | `nursery_time.xm` | 144 | 94 | 16 | C mixolydian | 12,15,17,12,19,20 x6 | 0,0,0,0,2,0,-1,-1,-1 on steps 0,1,3,4,6,9,12,13,15 | k ---x-x-----x---- s ---x------------ h -x-x-x-x-x-x-x-x |
| 17 | TREE-AGE IN ACTION (FAST) | `tree_fast.xm` | 124 | 126 | 8 | A major | 0,7,12,7,4,12,14,12 x68 | 0,0,-4,-7,-4 on steps 0,4,8,12,14 | k x-x-x---xx-xx--- s ------x--------- h xxxxxxxxxxxxxxxx |
| 18 | CONDENSED MUSIC | `condensed_music.xm` | 105 | 132 | 16 | C minor | 12,12,10,12,14,8,8 x28 | 0,-2 on steps 0,8 | k x---------x----- s ----x----x--x--x h xxxxxxxxxxxxxxxx |
| 19 | COCAINE COLA | `cocaine_cola_ii.xm` | 331 | 180 | 16 | D harmonic minor | 15,25,13,20 x19 | 0,0 on steps 4,13 | k x-----x--x-----x s ------x--------- h xx-xx-xx-xx-xx-x |
| 20 | STAGED THE FULL PERFORMANCE | `staged.xm` | 211 | 130 | 16 | G dorian | - | 0,0 on steps 0,8 | k x---x---x---x--- s ----x-------x--- h xxxxxxxxxxxxxxxx |
| 21 | THE TICKING BOMB | `the_ticking_bomb.xm` | 60 | 124 | 8 | C# major | 14,2,14,2,14,2,14,7 x42 | - | k x---x-xxx---x-x- s ----x--x----x--x h -xxx-xxx-xxx-xxx |
| 22 | MISTER QUICKBULLET | `quickbullet.xm` | 128 | 129 | 16 | Ab minor | -9,-5,-9,-5,-12,-9,-5,-2 x12 | 0,2,5,7,8,12,8,7,8,5,8,5,3,0,2,8 on steps 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 | k x--------------- s x--------------- h x-x-x-x-x-x-x-x- |
| 23 | OVER MY VOICE | `over_my_voice.xm` | 114 | 128 | 16 | D major | 10,10,10,12,14,12,15,14 x10 | 0,3,5,7,10,12,17,15,12,10 on steps 3,5,7,8,9,10,11,12,13,15 | k x---x---x---x--- s ----x-------x--- h -xxx-xxx-xxx-xxx |
| 24 | OHIO HOE | `ohio_hoe.xm` | 114 | 172 | 16 | C# phrygian | 15,17,19,22 x108 | 0,0,-12,-11 on steps 0,6,10,12 | k x---------x----- s ----x--x---xxx-x h xxxxxxxxxxxxxxxx |
<!-- analysis:end -->

Caveats the numbers cannot resolve: the key estimate is a histogram, so a song that sits on the relative major and
minor equally reports one of them (the JSON lists the two runners-up and the margin); the busiest-channel rule picks
an arpeggio over a tune in a few songs (STAGED THE FULL PERFORMANCE has no non-repeating lead at all, the figure is a
pulse-synth arpeggio); and tempo is the tracker's, so GOTTCHO BARRACHO reads 160 and may be felt at 80.

## 3. Families

<!-- families:begin -->
Automatic clusters (average linkage on tempo, mode brightness, groove class, hat density, busyness):

1. WHISTLER MAN: 75 BPM avg (75..75); dorian; broken beat
2. AIM AND SHOOT: 81 BPM avg (81..81); hijaz; half-time
3. EMERGENCY ON THE DANCE FLOOR: 90 BPM avg (90..90); minor; broken beat
4. THE MAGIC ACT, NURSERY TIME: 94 BPM avg (94..94); mixolydian; broken beat
5. EARTH AND THE SPACE CITIZENS: 100 BPM avg (100..100); minor; half-time
6. AN ODE TO THE SPANISH FLEXICODE, MISTER QUICKBULLET: 120 BPM avg (112..129); dorian/minor; broken beat
7. SUNMAN SUNRISE, EXCUSES, STAGED THE FULL PERFORMANCE, CONDENSED MUSIC: 126 BPM avg (116..132); dorian/minor; backbeat/four on the floor
8. AMIGA MUSIC, THE TICKING BOMB, THE DIPPER MAN, TREE-AGE IN ACTION (FAST), MELTDOWN IN MARS, OVER MY VOICE, MI CORA ZONE, HOT DAMN: 127 BPM avg (120..135); lydian/major/mixolydian; backbeat/broken beat/four on the floor/hats and percussion only
9. GOTTCHO BARRACHO, THE CYNICAL SYNDICATION: 166 BPM avg (160..172); major/mixolydian; backbeat/broken beat
10. OHIO HOE, COCAINE COLA: 176 BPM avg (172..180); harmonic minor/phrygian; backbeat/broken beat

Curated families (what the game uses; songs_src.py takes its sources from here):

| Family | wordPass song | Plays | Fed by | Why these belong together |
|--------|---------------|-------|--------|---------------------------|
| morning | MORNING MUSIC BOX | menu screens | NURSERY TIME, THE MAGIC ACT, AN ODE TO THE SPANISH FLEXICODE | the slow, soft end of the jukebox: a music-box ditty in C mixolydian at 94 BPM, a kora and metallophone arpeggio suite in Eb mixolydian at 94 BPM, and a marimba rework in Bb dorian at 112 BPM |
| chips | SUNNY CHIPS | Classic shelves | THE DIPPER MAN, AMIGA MUSIC, EARTH AND THE SPACE CITIZENS, SUNMAN SUNRISE | small tracker tunes (4 to 10 channels, 4 rows to the beat) at 100 to 125 BPM, lydian or mixolydian, a plain hat-and-kick bed under a bouncing lead |
| patio | PATIO HOUSE | Classic shelves | THE TICKING BOMB, MI CORA ZONE, GOTTCHO BARRACHO | Latin house at 124 to 133 BPM with a call-and-answer lead, major or mixolydian, claves and a snare that rides the off-beat |
| meadow | MEADOW HOUSE | Classic shelves | EXCUSES, TREE-AGE IN ACTION (FAST), MELTDOWN IN MARS | warm four-on-the-floor house at 124 to 126 BPM: rhodes and piano arpeggios over a walking bass, an acid line as the answer |
| stage | STAGE LIGHTS | Classic shelves | OVER MY VOICE, STAGED THE FULL PERFORMANCE | hi-NRG at 128 to 130 BPM, straight kick and offbeat hats, a pulse-synth arpeggio under a singing organ line |
| shuffle | PURDIE WALK | Classic shelves | WHISTLER MAN | the half-time shuffle: 75 BPM feel in C dorian, ghost snares and a whistle-and-horn lead |
| twostep | TWO STEP DAILY | Daily Pass | CONDENSED MUSIC | UK garage 2-step at 132 BPM in C minor: a skipping kick, a snare on the 2 and the 4 plus a shuffled ghost, and a bass pop on the 1 and the 3 |
| night | NIGHT TRAIN | Lockdown, FIRE FAST | THE CYNICAL SYNDICATION, OHIO HOE, COCAINE COLA | drum and bass at 172 to 180 BPM: a two-step breakbeat, a rolling sub, a four-note rising lead |
| acid | ACID ALARM | Lockdown, FIRE FAST | HOT DAMN, EMERGENCY ON THE DANCE FLOOR | 90s rave and hi-tech: acid lines in D mixolydian/C minor at 135 BPM, breakbeats and 16th hats |
| desert | DESERT FIRE | Lockdown, FIRE FAST | AIM AND SHOOT, MISTER QUICKBULLET | half-time downgrooves around 81 and 129 BPM in hijaz and Ab minor: a droning bass, an oud-like plucked line and a single snare on the 3 |

Every analysed song feeds exactly one family: 24 analysed, 24 assigned.
<!-- families:end -->

## 4. Listening guide

Every song is rendered to `docs/music/<id>.wav` (mono, 16-bit, 22050 Hz, one full A A B A cycle, 13 MB in all) and
exported to `docs/music/<id>.mid` (tempo, hook, pulse, bass, pad and drum tracks; open it in any MIDI player for a
General MIDI approximation). Regenerate with `make music-render` (`python3 tools/music/render.py`; numpy makes it
faster but is not required). The WAVs come from `render.py`, a software model of the PSG that plays the same song tables
as the ROM: it is a model, not a recording of a DS, so judge the tunes and the balance with it, not the exact timbre.
In the renders the game songs play as a shelf does when the player keeps spelling words (a bar of bass and pulse, then the
hook; the drums climb a level each pass: 1, 2, 3, 3), and the menu song plays the title's mix.

| Song | Where it plays | BPM | Key and mode | Listen for | Files |
|------|----------------|-----|--------------|------------|-------|
| MORNING MUSIC BOX | the title, options, ledger, guide, language chooser, first-boot chooser | 90 | C mixolydian | the five-note bell tune, the soft arpeggio under it, one barely-there hat | [wav](music/morning.wav) [mid](music/morning.mid) |
| SUNNY CHIPS | Classic shelves 1, 6, 11 ... | 112 | D lydian | the octave-bouncing bass, the sparkle of the raised fourth | [wav](music/chips.wav) [mid](music/chips.mid) |
| PATIO HOUSE | Classic shelves 2, 7, 12 ... | 129 | G major | the syncopated call, answered a step up; the off-beat snare | [wav](music/patio.wav) [mid](music/patio.mid) |
| MEADOW HOUSE | Classic shelves 3, 8, 13 ... | 129 | A major | the rhodes line over the off-beat bass | [wav](music/meadow.wav) [mid](music/meadow.mid) |
| STAGE LIGHTS | Classic shelves 4, 9, 14 ... | 129 | F dorian | the sung motif and its sequence over the rolling 16th bass | [wav](music/stage.wav) [mid](music/stage.mid) |
| PURDIE WALK | Classic shelves 5, 10, 15 ... | 150 (75 feel) | C dorian | the whistle, the single snare on the third beat | [wav](music/shuffle.wav) [mid](music/shuffle.mid) |
| TWO STEP DAILY | the Daily Pass | 129 | C minor | the skipping kick and the syncopated hook | [wav](music/twostep.wav) [mid](music/twostep.mid) |
| DESERT FIRE | Lockdown tier 0; FIRE FAST | 150 (75 feel) | F hijaz | the augmented second in the oud line, the droning bass | [wav](music/desert.wav) [mid](music/desert.mid) |
| ACID ALARM | Lockdown tier 1; FIRE FAST | 129 | A minor | the descending acid riff and its sequence | [wav](music/acid.wav) [mid](music/acid.mid) |
| NIGHT TRAIN | Lockdown tiers 2 and 3; FIRE FAST | 180 | D harmonic minor | the rising four-note run, the raised seventh on the V chord | [wav](music/night.wav) [mid](music/night.mid) |

The tempi are the nearest the engine can play to the sources (a sixteenth step is 5 to 12 frames at 60 fps, 180 down to
75 BPM): the menu song 94 -> 90, the chips family 100..125 -> 112, the house songs 124..133 -> 129, the shuffle and the
hijaz song 150 on the sixteenth grid (a 75 BPM half-time feel, snare on the third beat only), drum and bass 172..180 ->
180. Under fire every song lifts one frame a step from flame row 3 and two from row 5, never past 180 BPM.

### The songs, one by one

Generated from `tools/music/songs_src.py` (`python3 tools/music/build_songs.py --docs`): the source songs that fed each
one, its key, and hook A bar by bar as scale degrees.

<!-- songs:begin -->
### 1. MORNING MUSIC BOX

- Family `morning`, plays: title, options, ledger, guide, language chooser and first-boot chooser.
- Fed by: NURSERY TIME (the music-box motif: contour and rhythm of bar 1, C mixolydian, 94 BPM), THE MAGIC ACT (the 94 BPM mixolydian tempo and the rising/falling plucked arpeggio colour), AN ODE TO THE SPANISH FLEXICODE (soft marimba pulse, sparse hats, the bass on the one and the three).
- C mixolydian, 90 BPM (10 frames to a 16th step), lead from C5 up, level 70%.
- Chords A: 1 7 4 1, B: 4 6 7 1 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
1 . . . 3 . . . 4 . 1 . . . 5 .
4 . . . 7 . . . 6 . 4 . . . 2 .
6 . . . 4 . . . 6 . 8 . . . 6 .
5 . . . 3 . . . 2 . 3 . . . 1 .
```

- Menu song. The hook is Nursery Time's music-box ditty at half speed; the pulse is a quiet arpeggio, the pad holds a chord tone, the only percussion is a very soft hat on steps 6 and 14.

### 2. SUNNY CHIPS

- Family `chips`, plays: Classic shelves 1, 6, 11 ....
- Fed by: SUNMAN SUNRISE (the rising chord-tone motif 1 3 5 3 and the four-on-the-floor bed), AMIGA MUSIC (the run up and down the scale, mixolydian/lydian colour at 120 BPM), THE DIPPER MAN (the bouncing octave bass, 125 BPM), EARTH AND THE SPACE CITIZENS (the half-time snare in the quiet bars).
- D lydian, 112 BPM (8 frames to a 16th step), lead from D4 up, level 100%.
- Chords A: 1 2 1 5, B: 6 2 5 1 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
1 . 3 . 5 . 3 . 5 . 6 . 5 . 3 .
2 . 4 . 6 . 4 . 6 . 7 . 6 . 4 .
1 . 3 . 5 . 8 . 7 . 5 . 3 . 5 .
5 . 7 . 9 . 7 . 5 . 4 . 2 . - .
```

- A bright tracker ditty: an arpeggio-and-answer hook in D lydian, an octave-bouncing bass.

### 3. PATIO HOUSE

- Family `patio`, plays: Classic shelves 2, 7, 12 ....
- Fed by: MI CORA ZONE (the call-and-answer lead: contour and syncopated rhythm of its motif, G major, 133 BPM), THE TICKING BOMB (the claves-style snare on 4, 7, 12, 15 and the off-beat hats, 124 BPM), GOTTCHO BARRACHO (the descending answer phrase and the tumbao bass).
- G major, 129 BPM (7 frames to a 16th step), lead from G3 up, level 100%.
- Chords A: 1 5 6 4, B: 6 4 1 5 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
8 . 7 . 4 . 5 8 . 7 . 4 . 4 . .
9 . 8 . 5 . 6 9 . 8 . 5 . 5 . .
10 . 9 . 6 . 7 10 . 9 . 6 . 6 . .
4 . 6 . 8 . 6 11 . 8 . 6 . 8 . .
```

- Latin house: the hook is Mi Cora Zone's syncopated phrase, answered up a step and up a third, then arpeggiated in section B.

### 4. MEADOW HOUSE

- Family `meadow`, plays: Classic shelves 3, 8, 13 ....
- Fed by: TREE-AGE IN ACTION (FAST) (the piano/rhodes lead: contour and rhythm of its motif, A major, 126 BPM), EXCUSES (the four-on-the-floor kick, clap on 2 and 4 and the 124 BPM house bed), MELTDOWN IN MARS (the off-beat bass pulses and the stepwise answer).
- A major, 129 BPM (7 frames to a 16th step), lead from A4 up, level 100%.
- Chords A: 1 5 6 4, B: 6 4 1 5 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
1 . 5 . 8 . 5 . 3 . 8 . 9 . 8 .
5 . 9 . 7 . 9 . 10 . 9 . 7 . 5 .
6 . 8 . 10 . 8 . 6 . 10 . 11 . 10 .
8 . 6 . 4 . 6 . 8 . 6 . 5 . 3 .
```

- Warm house: a rhodes arpeggio-melody over an off-beat bass; the lead sits in A major.

### 5. STAGE LIGHTS

- Family `stage`, plays: Classic shelves 4, 9, 14 ....
- Fed by: OVER MY VOICE (the sing-song lead: contour and rhythm of its motif, hi-NRG, 128 BPM), STAGED THE FULL PERFORMANCE (the rolling 16th bass, straight kick and off-beat hats, G dorian, 130 BPM).
- F dorian, 129 BPM (7 frames to a 16th step), lead from F4 up, level 100%.
- Chords A: 1 4 1 7, B: 4 7 1 5 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
1 2 3 2 4 . . 3 . . 3 . . . . .
4 5 6 5 7 . . 6 . . 6 . . . . .
5 4 3 4 2 . . 3 . . 3 . . . . .
11 . 9 . 7 . 9 . 11 . 9 . 7 . - .
```

- Hi-NRG: a sung motif and its sequence and inversion over a rolling 16th bass in F dorian.

### 6. PURDIE WALK

- Family `shuffle`, plays: Classic shelves 5, 10, 15 ....
- Fed by: WHISTLER MAN (the whistled phrase and the half-time shuffle: kick on 1 and the 3-and, snare on 3, ghost snares; C dorian, 75 BPM feel, 150 on the 16th grid).
- C dorian, 150 BPM (6 frames to a 16th step), lead from C4 up, level 100%.
- Chords A: 1 4 7 1, B: 1 7 4 5 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
8 . . . . . 5 . 4 . . . 11 . . .
6 . . . . . 4 . 3 . . . 8 . . .
9 . . . . . 7 . 5 . . . 11 . . .
8 . . 7 . . 5 . . . . . 3 . - .
```

- Half-time: a snare only on the third beat (75 BPM feel on a 150 BPM grid), a whistle lead.

### 7. TWO STEP DAILY

- Family `twostep`, plays: the Daily Pass.
- Fed by: CONDENSED MUSIC (the skipping garage motif: contour and rhythm, C minor, 132 BPM; the 2-step kick on 1 and the 3-and and the snare on 2, the shuffled ghost, 4-and and 4-the-last).
- C minor, 129 BPM (7 frames to a 16th step), lead from C4 up, level 100%.
- Chords A: 1 6 3 7, B: 6 7 1 1 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
8 . . 8 . 7 8 9 5 . . 5 . . . .
6 . . 6 . 5 6 7 3 . . 3 . . . .
10 . . 10 . 9 10 11 7 . . 7 . . . .
9 . . 9 . 7 9 11 9 . . 7 . . - .
```

- UK garage 2-step: a syncopated hook over i VI III VII.

### 8. DESERT FIRE

- Family `desert`, plays: Lockdown tier 0; FIRE FAST shelves 1, 4 ....
- Fed by: AIM AND SHOOT (F hijaz, the half-time groove with a single snare on the 3, the plucked oud line), MISTER QUICKBULLET (the half-time downgroove and the droning sub bass, Ab minor, 129 BPM).
- F hijaz, 150 BPM (6 frames to a 16th step), lead from F4 up, level 96%.
- Chords A: 1 2 1 5, B: 4 2 1 5 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
5 . . 6 5 . 4 . 3 . . . 2 . 3 .
6 . . 7 6 . 4 . 2 . . . 4 . 2 .
8 . . 9 8 . 7 . 6 . . . 5 . 3 .
5 . 7 . 9 . 7 . 5 . 4 . 3 . 2 .
```

- Half-time hijaz: an oud-like descent from the fifth around the augmented second.

### 9. ACID ALARM

- Family `acid`, plays: Lockdown tier 1; FIRE FAST shelves 2, 5 ....
- Fed by: HOT DAMN (the descending acid riff 8 7 6 5, the breakbeat kick, 135 BPM), EMERGENCY ON THE DANCE FLOOR (the minor arpeggio chords and the 16th hat, C minor).
- A minor, 129 BPM (7 frames to a 16th step), lead from A3 up, level 94%.
- Chords A: 1 6 7 1, B: 1 6 7 1 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
8 7 6 5 . . 5 . 8 . 7 . 6 . 5 .
10 9 8 7 . . 7 . 10 . 9 . 8 . 6 .
11 10 9 8 . . 8 . 11 . 10 . 9 . 7 .
10 . 8 . 7 . 5 . 8 . 7 . 5 . - .
```

- Rave: a descending acid riff in A minor and its sequence over i VI VII i.

### 10. NIGHT TRAIN

- Family `night`, plays: Lockdown tiers 2 and 3; FIRE FAST shelves 3, 6 ....
- Fed by: OHIO HOE (the four-note rising run 1 2 3 5, 172 BPM), THE CYNICAL SYNDICATION (the two-step breakbeat and the rolling sub, 172 BPM), COCAINE COLA (the harmonic-minor colour and the 180 BPM drive).
- D harmonic minor, 180 BPM (5 frames to a 16th step), lead from D4 up, level 92%.
- Chords A: 1 6 4 5, B: 1 4 6 5 (scale degrees).
- Hook A, bar by bar (degrees; `.` hold, `-` rest):

```
1 2 3 5 . . . . 6 . 5 . 3 . 2 .
6 4 6 8 . . . . 10 . 8 . 6 . 4 .
4 5 6 8 . . . . 9 . 8 . 6 . 4 .
9 . 7 . 5 . 7 . 9 . 10 . 9 . - .
```

- Drum and bass at 180 BPM (5 frames to a step): a rising run, a V chord with the raised seventh.

<!-- songs:end -->

## 5. The menu song

The old title played the shelf-1 tune in attract mode: C major at 112.5 BPM, the hook flat at level 60 on a 50% pulse with
a vibrato, about seven notes a bar, the full bass riff and a hat under it, a 16th-note pulse arpeggio, a peak music sum of
187 on the 0..127 volume scale. The user's report was that this put them off playing. The replacement, *Morning Music
Box*, is built from the opposite end of the jukebox: Nursery Time's music-box ditty (its tune's contour and rhythm,
stretched to half speed), the 94 BPM mixolydian colour of The Magic Act and the soft marimba pulse and sparse hats of An
Ode to the Spanish Flexicode.

| Property | Morning Music Box | The old title loop | How it is measured |
|----------|-------------------|--------------------|--------------------|
| Tempo | 90 BPM (10 frames a step) | 112.5 BPM | `songs.c` (the test asserts at most 100) |
| Mode | C mixolydian (the flat seventh softens the V and the cadence; the chords are I, bVII, IV, I and IV, vi, bVII, I) | C major | table |
| Hook notes per bar | 4.9 on average (20 in the A pass, 19 in B; no bar above 8) | 6.75 | table, asserted at most 5 on average and 8 in any bar |
| Lead register | C5 to C6 (523 to 1047 Hz), no shrill top | C5 to D6 (A section) | table, asserted G4 to D6 |
| Largest leap in the tune | a fifth | a major sixth | table, asserted at most a minor sixth (8 semitones; the real maximum is 7) |
| Hook sound | a bell: onset 46, ringing at 15, no vibrato, 12.5% then 25% duty | flat 60, 50% duty, vibrato | instrument table, asserted no vibrato and a sustain under a third of the onset |
| Pulse | a soft arpeggio, level 18 falling to 9 (12 and 6 after the song's 70% gain) | level 26 falling to 14 | asserted at most 16 |
| Bass | one note on the one and one on the three (the root, then the fifth), 50% duty | a riff of eighths | table |
| Pad | a held chord tone, level 20 (14 after the song's 70% gain), 50% duty | 36 | asserted at most 20 |
| Percussion | one hat, level 6 (4 after the gain), on steps 6 and 14, no kick, no snare, at every drum level | hat plus the groove | asserted: hats only, at most 2 steps a bar, level at most 8, the drum voice silent |
| Voices sounding at once | at most 5 (hook, pulse, bass, pad, hat) | 6 | `make check` loudness run |
| Peak music sum | 87 (46% of the old 187) | 187 | `make check` loudness run; the renderer's own model reads 84 |
| Level against a game song | 70% | 100% | `gain` in the table, asserted through the mirror in the ROM scenarios |
| Form | A A B A, 16 bars, 43 seconds a cycle, B a new phrase over F, Am, Bb, C | | table |

Why these numbers should sound pleasant, and how that is argued without ears: a slow tempo and a half-speed tune leave
a rest after almost every phrase, so nothing is dense (about one hook note a beat, against two a beat in the game
songs); a bell envelope decays to a faint ring, so there is no sustained buzz on a 50% pulse; the
intervals never leap more than a fifth, the register stays at or below C6, and every note but three of the 39 is a tone of
the chord under it or a stepwise neighbour (the three are a G over A minor and over B flat major, the seventh and the sixth,
which colour a chord and do not clash with it; the notation above shows the notes); the only percussion is a hat at about a third of the old hat's level, so there
is no beat to be nagged by; and the whole song is at 70% of a game song with the loudest sum at 46% of the old title's, so
it sits behind the menus instead of competing with them. The pickup bar (bass and pulse alone) means the tune enters
gently, and the cross-fade is a quarter of a second.

What was **not** verified: that it sounds good. Everything above is a measurement of a table or of what the sequencer
sends to the hardware (`make check`, the ROM scenarios and the renderer's port); no one has listened to it on a DS or in
an emulator with sound, and the PSG's timbre (a harsher square than the model's) may change how warm it feels. The WAV
and MIDI renders exist so the author can listen before accepting it; the numbers to adjust, if it needs to be softer
or sparser, are in `tools/music/songs_src.py` (`gain`, `att`, the `bell` and `soft` instruments, the hook bars) and
`make songs` rebuilds the tables.

## 6. What is and is not verified

- Verified by tests (`make check`, `tools/emu/test_rules.py`): every song's table is well formed, every hook, bass and drum
  note reaches the PSG at the pitch and level the table says, the form A A B A and its loop, the tempo under fire, which song plays
  where (title and menus, Classic shelves 1..12, Lockdown tiers, FIRE FAST, Daily), 60 fps with the music running, the
  loudness cap against the old tune's own peak, the menu song's properties above, no hardware value written twice, MUSIC off.
- Measured, not heard: the balance between voices (each voice's peak level and the sum), the register of every lead, the
  density of every hook, the claim that the menu song is gentle. The guesses an ear would settle are the swing of the
  shuffle on a 16th grid (a true shuffle needs triplets, which the 16-step bar cannot hold; PURDIE WALK keeps the
  half-time feel and the ghost snare instead), whether the dark songs are hard enough in tier 3, and whether a short
  hook repeated four times (A A B A) tires: bar 4 and section B vary from bar 1 by construction, which is checked, not
  heard.
- Not modelled in the renders: the sound effects, the chimes, the alarm, the pass fanfare and the fail stingers (they
  are unchanged and tested in `make check`).
- How much of each song is mechanically derived from the analysis and how much is written by hand: the first bar of the
  hook is the source motif's contour and rhythm mapped onto the song's scale (`build_songs.py`, the `seed` recipe) for
  MORNING MUSIC BOX (Nursery Time, at half speed), PATIO HOUSE (Mi Cora Zone), MEADOW HOUSE (Tree-age in Action (Fast)),
  STAGE LIGHTS (Over My Voice), PURDIE WALK (Whistler Man) and TWO STEP DAILY (Condensed Music). SUNNY CHIPS, ACID ALARM,
  NIGHT TRAIN and DESERT FIRE take their first bar from the motif lists by hand (Sunman Sunrise's 1 3 5 3, Hot Damn's
  8 7 6 5, Ohio Hoe's 1 2 3 5, Aim and Shoot's descent from the fifth), because a mechanical map of a hijaz or pulse
  figure loses its character. The answer bars are sequences and inversions of that bar where the song says so
  (`seq`, `inv`) and written by hand otherwise; every B section and every bar 4 is written by hand. The bass riffs,
  chord walks and drum grooves are the sources' (analysis JSON) thinned into the engine's one-bar, 16-step form and
  tuned by hand; they are not transcriptions.
