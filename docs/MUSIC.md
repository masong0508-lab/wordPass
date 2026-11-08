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
