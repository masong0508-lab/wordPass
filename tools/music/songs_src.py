"""The ten wordPass songs, one per family (families.py), as readable definitions for build_songs.py.

Notation
  hook bars   16 steps (16ths) separated by spaces: a number is a scale degree (1 = the song's tonic at
              root + 12 * lead_oct, 8 = the tonic an octave up), `.` = hold, `-` = rest. A bar can also be a
              recipe: ('seed', source_id, motif_index, start_degree, rhythm_scale) maps the contour and rhythm
              of a lead motif found in the analysis of a source song onto the song's scale;
              ('seq', up, bar) is bar number `bar` of this section moved `up` scale steps with its last note
              landing on a tone of the chord; ('inv', start, bar) is it turned upside down.
  chords      the root degree of the chord under each of the four bars (section A and section B)
  bass        two 16-step bars (the usual one, and bar 4): semitones over the chord root an octave down, `.` = none
  drums       kick/snare strings (x = hit) and hat strings in hex halves ('8' = level 16, '.' = none); see make_levels
  inst        (duty at the onset, duty after, attack frames, level at the onset, level it settles to, fall per frame,
              vibrato delay, vibrato divisor, release) for the hook, the pulse arpeggio, the bass and the pad

The sources of every song are the families in families.py; `credits` names them for docs/MUSIC.md.
"""
from families import FAMILY_BY_ID

HOOKS = {
    "bell":    (12, 25, 2, 46, 15, 3, 0, 0, 6),          # a music box: a bright tick that falls to a quiet ring
    "chip":    (25, 25, 0, 62, 40, 3, 12, 110, 8),
    "rhodes":  (25, 50, 3, 58, 38, 2, 12, 120, 8),
    "organ":   (50, 50, 0, 56, 46, 1, 8, 90, 8),
    "whistle": (50, 50, 0, 52, 44, 1, 6, 70, 8),
    "pluck":   (12, 25, 2, 62, 28, 4, 0, 0, 6),
    "acid":    (25, 12, 4, 60, 40, 2, 0, 0, 8),
    "oud":     (12, 25, 2, 60, 26, 3, 14, 120, 6),
    "lead":    (12, 50, 3, 64, 44, 3, 10, 96, 8),
}
PULSES = {
    "soft":  (12, 12, 0, 18, 9, 2, 0, 0, 8),
    "plain": (12, 12, 0, 24, 12, 2, 0, 0, 8),
    "tight": (12, 12, 0, 22, 8, 3, 0, 0, 8),
}
BASSES = {
    "round": (25, 25, 0, 64, 48, 4, 0, 0, 6),
    "sub":   (50, 50, 0, 58, 46, 3, 0, 0, 6),
    "soft":  (50, 50, 0, 38, 28, 2, 0, 0, 6),
    "pluck": (25, 25, 0, 62, 32, 5, 0, 0, 6),
}
PAD = (37, 50, 0, 0, 0, 0, 0, 0, 2)
PAD_SOFT = (50, 50, 0, 0, 0, 0, 0, 0, 2)


def song(id, name, family, role, where, root, mode, frames, hookA, hookB, chordA, chordB, bass, drums, hook, pulse,
         basso, notes, **kw):
    fam = FAMILY_BY_ID[family]
    d = dict(id=id, name=name, family=family, role=role, where=where, root=root, mode=mode, frames=frames,
             hookA=hookA, hookB=hookB, chordA=chordA, chordB=chordB, bass=bass, drums=drums,
             sources=fam["sources"], credits=kw.pop("credits"), notes=notes,
             inst=[HOOKS[hook], PULSES[pulse], BASSES[basso], kw.pop("pad", PAD)])
    d.update(kw)
    return d


SONGS = [
    # 1 ------------------------------------------------------------------------------------------ the menu
    song("morning", "MORNING MUSIC BOX", "morning", "menu",
         "title, options, ledger, guide, language chooser and first-boot chooser",
         60, "mixolydian", 10,
         hookA=[("seed", "nursery_time", 0, 1, 2),                               # C E F C G: the music-box ditty, half as fast
                "4 . . . 7 . . . 6 . 4 . . . 2 .",                              # over Bb: F Bb A F D
                "6 . . . 4 . . . 6 . 8 . . . 6 .",                              # over F: A F A C A
                "5 . . . 3 . . . 2 . 3 . . . 1 ."],                             # over C: G E D E C
         hookB=["8 . . . 6 . . . 4 . 6 . . . 8 .",                              # over F
                "8 . . . 6 . . . 3 . 5 . . . 6 .",                              # over Am
                "7 . . . 5 . . . 7 . 5 . . . 4 .",                              # over Bb
                "8 . . . 5 . . . 3 . . . 5 . - ."],                             # over C, back to the top
         chordA=[1, 7, 4, 1], chordB=[4, 6, 7, 1],
         bass=["0 . . . . . . . 7 . . . . . . .", "0 . . . . . . . 7 . . . . . 5 ."],
         drums=dict(hat1="......3.......3."),
         hook="bell", pulse="soft", basso="soft", pad=PAD_SOFT,
         gain=70, att=(0, 0, 20), pad_max=24, pad_deg=(2, 4), lead_oct=1,
         bass_accent=0x0101,
         credits=["NURSERY TIME (the music-box motif: contour and rhythm of bar 1, C mixolydian, 94 BPM)",
                  "THE MAGIC ACT (the 94 BPM mixolydian tempo and the rising/falling plucked arpeggio colour)",
                  "AN ODE TO THE SPANISH FLEXICODE (soft marimba pulse, sparse hats, the bass on the one and the three)"],
         notes="Menu song. The hook is Nursery Time's music-box ditty at half speed; the pulse is a quiet arpeggio, "
               "the pad holds a chord tone, the only percussion is a very soft hat on steps 6 and 14."),
    # 2 --------------------------------------------------------------------------------------- classic: chips
    song("chips", "SUNNY CHIPS", "chips", "classic", "Classic shelves 1, 6, 11 ...",
         62, "lydian", 8,
         hookA=["1 . 3 . 5 . 3 . 5 . 6 . 5 . 3 .",                              # D F# A F# A B A F#
                ("seq", 1, 0),                                                  # over E: the tune a step up
                "1 . 3 . 5 . 8 . 7 . 5 . 3 . 5 .",
                "5 . 7 . 9 . 7 . 5 . 4 . 2 . - ."],
         hookB=["6 . 8 . 10 . 8 . 6 . 8 . 10 . 11 .",
                "9 . 8 . 6 . 4 . 6 . 8 . 9 . 11 .",
                "9 . 7 . 5 . 7 . 9 . 11 . 9 . 7 .",
                "8 . 7 . 5 . 3 . 5 . 3 . 1 . - ."],
         chordA=[1, 2, 1, 5], chordB=[6, 2, 5, 1],
         bass=["0 . 12 . 0 . 12 . 0 . 12 . 0 . 12 .", "0 . 12 . 0 . 12 . 0 . 7 . 5 . 7 ."],
         drums=dict(kick="x...x...x...x...", snare="....x.......x...", hat1="..6...6...6...6.", hat2="..6...6...6...6.",
                    hat3="8464846484648464"),
         hook="chip", pulse="plain", basso="pluck", lead_oct=0,
         bass_accent=0x5555,
         credits=["SUNMAN SUNRISE (the rising chord-tone motif 1 3 5 3 and the four-on-the-floor bed)",
                  "AMIGA MUSIC (the run up and down the scale, mixolydian/lydian colour at 120 BPM)",
                  "THE DIPPER MAN (the bouncing octave bass, 125 BPM)",
                  "EARTH AND THE SPACE CITIZENS (the half-time snare in the quiet bars)"],
         notes="A bright tracker ditty: an arpeggio-and-answer hook in D lydian, an octave-bouncing bass."),
    # 3 -------------------------------------------------------------------------------------- classic: patio
    song("patio", "PATIO HOUSE", "patio", "classic", "Classic shelves 2, 7, 12 ...",
         55, "major", 7,
         hookA=[("seed", "mi_cora_zone", 0, 8, 1),                              # G F# C D G F# C C: the call
                ("seq", 1, 0),                                                  # over D: the answer a step up
                ("seq", 2, 0),                                                  # over Em
                "4 . 6 . 8 . 6 11 . 8 . 6 . 8 . ."],                            # over C: the arpeggio rises
         hookB=["10 . 8 . 6 . 8 10 . 8 . 6 . 6 . .",
                "11 . 8 . 6 . 8 11 . 8 . 6 . 6 . .",
                "10 . 8 . 5 . 8 10 . 8 . 5 . 5 . .",
                "9 . 7 . 5 . 7 9 . 7 . 5 . 9 . ."],
         chordA=[1, 5, 6, 4], chordB=[6, 4, 1, 5],
         bass=[". . . 0 . . 7 . . . . 0 . . 7 .", ". . . 0 . . 7 . . . . 7 . 5 . 7"],
         drums=dict(kick="x...x...x...x...", snare="....x..x....x..x", hat1="..6...6...6...6.", hat2="..6...6...6...6.",
                    hat3=".4.6.4.6.4.6.4.6"),
         hook="rhodes", pulse="plain", basso="round", lead_oct=0,
         bass_accent=0x0849,
         credits=["MI CORA ZONE (the call-and-answer lead: contour and syncopated rhythm of its motif, G major, 133 BPM)",
                  "THE TICKING BOMB (the claves-style snare on 4, 7, 12, 15 and the off-beat hats, 124 BPM)",
                  "GOTTCHO BARRACHO (the descending answer phrase and the tumbao bass)"],
         notes="Latin house: the hook is Mi Cora Zone's syncopated phrase, answered up a step and up a third, then "
               "arpeggiated in section B."),
    # 4 ------------------------------------------------------------------------------------- classic: meadow
    song("meadow", "MEADOW HOUSE", "meadow", "classic", "Classic shelves 3, 8, 13 ...",
         57, "major", 7,
         hookA=[("seed", "tree_fast", 0, 1, 1),                                 # A E A' E C# A' B' A'
                "5 . 9 . 7 . 9 . 10 . 9 . 7 . 5 .",
                "6 . 8 . 10 . 8 . 6 . 10 . 11 . 10 .",
                "8 . 6 . 4 . 6 . 8 . 6 . 5 . 3 ."],
         hookB=["10 . 8 . 6 . 8 . 10 . 11 . 10 . 8 .",
                "8 . 9 . 11 . 9 . 8 . 6 . 4 . 6 .",
                "8 . 10 . 11 . 10 . 8 . 10 . 8 . 5 .",
                "9 . 7 . 5 . 7 . 9 . 8 . 7 . - ."],
         chordA=[1, 5, 6, 4], chordB=[6, 4, 1, 5],
         bass=[". . 0 . . . 0 . . . 0 . . . 0 .", ". . 0 . . . 0 . . . 0 . . . 7 ."],
         drums=dict(kick="x...x...x...x...", snare="....x.......x...", hat1="..5...5...5...5.", hat2="..6...6...6...6.",
                    hat3="8464846484648464"),
         hook="rhodes", pulse="soft", basso="soft", pad_max=44, lead_oct=1,
         bass_accent=0x0444,
         credits=["TREE-AGE IN ACTION (FAST) (the piano/rhodes lead: contour and rhythm of its motif, A major, 126 BPM)",
                  "EXCUSES (the four-on-the-floor kick, clap on 2 and 4 and the 124 BPM house bed)",
                  "MELTDOWN IN MARS (the off-beat bass pulses and the stepwise answer)"],
         notes="Warm house: a rhodes arpeggio-melody over an off-beat bass; the lead sits in A major."),
    # 5 -------------------------------------------------------------------------------------- classic: stage
    song("stage", "STAGE LIGHTS", "stage", "classic", "Classic shelves 4, 9, 14 ...",
         65, "dorian", 7,
         hookA=[("seed", "over_my_voice", 1, 1, 1),                             # F G Ab G Bb . . Ab
                ("seq", 3, 0),                                                  # over Bb
                ("inv", 5, 0),                                                  # over F, upside down
                "11 . 9 . 7 . 9 . 11 . 9 . 7 . - ."],                          # over Eb
         hookB=["8 . . 8 . . 6 . 4 . . . 6 . 8 .",
                "9 . . 9 . . 7 . 9 . . . 11 . 9 .",
                "8 . . 8 . . 5 . 3 . . . 5 . 8 .",
                "9 . 7 . 5 . 7 . 9 . . . 7 . 5 ."],
         chordA=[1, 4, 1, 7], chordB=[4, 7, 1, 5],
         bass=[". 0 0 0 . 0 0 0 . 0 0 0 . 0 0 0", ". 0 0 0 . 0 0 0 . 0 0 0 . 7 7 5"],
         drums=dict(kick="x...x...x...x...", snare="....x.......x...", hat1="..4...4...4...4.", hat2="..8...8...8...8.",
                    hat3=".4.8.4.8.4.8.4.8"),
         hook="organ", pulse="tight", basso="sub", lead_oct=0,
         bass_accent=0x4444,
         credits=["OVER MY VOICE (the sing-song lead: contour and rhythm of its motif, hi-NRG, 128 BPM)",
                  "STAGED THE FULL PERFORMANCE (the rolling 16th bass, straight kick and off-beat hats, G dorian, 130 BPM)"],
         notes="Hi-NRG: a sung motif and its sequence and inversion over a rolling 16th bass in F dorian."),
    # 6 ------------------------------------------------------------------------------------- classic: shuffle
    song("shuffle", "PURDIE WALK", "shuffle", "classic", "Classic shelves 5, 10, 15 ...",
         60, "dorian", 6,
         hookA=[("seed", "whistler_shuffle", 0, 8, 1),                          # C G F F': the whistled leap
                "6 . . . . . 4 . 3 . . . 8 . . .",
                "9 . . . . . 7 . 5 . . . 11 . . .",
                "8 . . 7 . . 5 . . . . . 3 . - ."],
         hookB=["10 . . . 8 . 7 . 5 . . . 3 . . .",
                "9 . . . 7 . . . 4 . . . 2 . 4 .",
                "8 . . . 6 . . . 4 . . . 3 . 4 .",
                "9 . . . 7 . 5 . . . . . 5 . - ."],
         chordA=[1, 4, 7, 1], chordB=[1, 7, 4, 5],
         bass=["0 . . . . . 0 . 7 . . . . . 5 .", "0 . . . . . 0 . 7 . . . 10 . 12 ."],
         drums=dict(kick="x.........x.....", snare="........x.......", snare3="..x.....x...x...",
                    hat1="3.3.3.3.3.3.3.3.", hat2="4.4.4.4.4.4.4.4.", hat3="8.4.6.4.8.4.6.4."),
         hook="whistle", pulse="soft", basso="round", lead_oct=0,
         bass_accent=0x0101,
         credits=["WHISTLER MAN (the whistled phrase and the half-time shuffle: kick on 1 and the 3-and, snare on 3, "
                  "ghost snares; C dorian, 75 BPM feel, 150 on the 16th grid)"],
         notes="Half-time: a snare only on the third beat (75 BPM feel on a 150 BPM grid), a whistle lead."),
    # 7 --------------------------------------------------------------------------------------------- daily
    song("twostep", "TWO STEP DAILY", "twostep", "daily", "the Daily Pass",
         60, "minor", 7,
         hookA=[("seed", "condensed_music", 0, 8, 1),                           # C . . C . Bb C D G . . G
                ("seq", -2, 0),                                                 # over Ab
                ("seq", 2, 0),                                                  # over Eb
                "9 . . 9 . 7 9 11 9 . . 7 . . - ."],                           # over Bb
         hookB=["10 . . 8 . . 6 . . 8 . . 10 . 8 .",
                "11 . . 9 . . 7 . . 9 . . 11 . 9 .",
                "10 . . 8 . . 5 . . 8 . . 10 . 8 .",
                "9 . . 7 . . 5 . . 3 . . 5 . - ."],
         chordA=[1, 6, 3, 7], chordB=[6, 7, 1, 1],
         bass=["0 . . 0 . . . . 0 . . . . . 12 .", "0 . . 0 . . . . 0 . . . . . 7 ."],
         drums=dict(kick="x.........x.....", snare="....x....x..x..x", hat1="..6...6...6...6.", hat2=".4.4.4.4.4.4.4.4",
                    hat3="8464846484648464"),
         hook="pluck", pulse="plain", basso="sub", lead_oct=0, tap_minor=True,
         bass_accent=0x0101,
         credits=["CONDENSED MUSIC (the skipping garage motif: contour and rhythm, C minor, 132 BPM; the 2-step "
                  "kick on 1 and the 3-and and the snare on 2, the shuffled ghost, 4-and and 4-the-last)"],
         notes="UK garage 2-step: a syncopated hook over i VI III VII."),
    # 10 ---------------------------------------------------------------------------------------- dark: desert
    song("desert", "DESERT FIRE", "desert", "dark", "Lockdown tier 0; FIRE FAST shelves 1, 4 ...",
         53, "hijaz", 6,
         hookA=["5 . . 6 5 . 4 . 3 . . . 2 . 3 .",
                "6 . . 7 6 . 4 . 2 . . . 4 . 2 .",
                "8 . . 9 8 . 7 . 6 . . . 5 . 3 .",
                "5 . 7 . 9 . 7 . 5 . 4 . 3 . 2 ."],
         hookB=["8 . . 9 8 . 6 . 4 . . . 6 . 8 .",
                "6 . . 7 6 . 4 . 2 . . . 4 . 6 .",
                "5 . . 6 5 . 3 . 1 . . . 3 . 5 .",
                "9 . . 10 9 . 7 . 5 . . . 4 . - ."],
         chordA=[1, 2, 1, 5], chordB=[4, 2, 1, 5],
         bass=["0 . . . . . . . 0 . . . . . 7 .", "0 . . . . . . . 0 . . . 1 . 0 ."],
         drums=dict(kick="x.....x...x.....", snare="........x.......", hat1="..4...4...4...4.", hat2="4.4.4.4.4.4.4.4.",
                    hat3="6.4.6.4.6.4.6.4."),
         hook="oud", pulse="soft", basso="sub", lead_oct=1, tap_minor=True,
         bass_accent=0x0101, gain=96,
         credits=["AIM AND SHOOT (F hijaz, the half-time groove with a single snare on the 3, the plucked oud line)",
                  "MISTER QUICKBULLET (the half-time downgroove and the droning sub bass, Ab minor, 129 BPM)"],
         notes="Half-time hijaz: an oud-like descent from the fifth around the augmented second."),
    # 9 ------------------------------------------------------------------------------------------- dark: acid
    song("acid", "ACID ALARM", "acid", "dark", "Lockdown tier 1; FIRE FAST shelves 2, 5 ...",
         57, "minor", 7,
         hookA=["8 7 6 5 . . 5 . 8 . 7 . 6 . 5 .",
                ("seq", 2, 0),
                ("seq", 3, 0),
                "10 . 8 . 7 . 5 . 8 . 7 . 5 . - ."],
         hookB=["8 . . 8 . . 10 . . 8 . . 6 . . 5",
                "6 . . 6 . . 8 . . 6 . . 5 . . 3",
                "7 . . 7 . . 9 . . 7 . . 5 . . 4",
                "8 . . 8 . . 10 . . 11 . . 10 . . 8"],
         chordA=[1, 6, 7, 1], chordB=[1, 6, 7, 1],
         bass=["0 . 12 . 0 . 0 . 12 . 0 . 0 . 7 .", "0 . 12 . 0 . 0 . 12 . 0 . 7 . 5 ."],
         drums=dict(kick="x.....x....x....", snare="....x.......x...", hat1="..5...5...5...5.", hat2="..6...6...6...6.",
                    hat3="8464846484648464"),
         hook="acid", pulse="tight", basso="pluck", lead_oct=0, tap_minor=True,
         bass_accent=0x0555, gain=94,
         credits=["HOT DAMN (the descending acid riff 8 7 6 5, the breakbeat kick, 135 BPM)",
                  "EMERGENCY ON THE DANCE FLOOR (the minor arpeggio chords and the 16th hat, C minor)"],
         notes="Rave: a descending acid riff in A minor and its sequence over i VI VII i."),
    # 8 -------------------------------------------------------------------------------------------- dark: dnb
    song("night", "NIGHT TRAIN", "night", "dark", "Lockdown tiers 2 and 3; FIRE FAST shelves 3, 6 ...",
         62, "harmonic minor", 5,
         hookA=["1 2 3 5 . . . . 6 . 5 . 3 . 2 .",
                "6 4 6 8 . . . . 10 . 8 . 6 . 4 .",
                "4 5 6 8 . . . . 9 . 8 . 6 . 4 .",
                "9 . 7 . 5 . 7 . 9 . 10 . 9 . - ."],
         hookB=["1 . 6 . 5 . 6 . 1 . 6 . 5 . 3 .",
                "8 . 6 . 4 . 6 . 8 . 9 . 8 . 6 .",
                "8 . 6 . 4 . 6 . 8 . 6 . 4 . 6 .",
                "9 . 7 . 5 . 7 . 9 . 7 . 5 . - ."],
         chordA=[1, 6, 4, 5], chordB=[1, 4, 6, 5],
         bass=["0 . . . . . 0 . . . 0 . . . 5 .", "0 . . . . . 0 . . . 0 . . . 7 ."],
         drums=dict(kick="x.........x.....", snare="....x.......x...", hat1="..5...5...5...5.", hat2="4.4.4.4.4.4.4.4.",
                    hat3="6.4.6.4.6.4.6.4.", kick3=["x.........x.....", "x.........x.....", "x.........x.....", "x.........x..x.."]),
         hook="lead", pulse="tight", basso="sub", harm=1, lead_oct=0, tap_minor=True,
         bass_accent=0x0101, gain=92,
         credits=["OHIO HOE (the four-note rising run 1 2 3 5, 172 BPM)",
                  "THE CYNICAL SYNDICATION (the two-step breakbeat and the rolling sub, 172 BPM)",
                  "COCAINE COLA (the harmonic-minor colour and the 180 BPM drive)"],
         notes="Drum and bass at 180 BPM (5 frames to a step): a rising run, a V chord with the raised seventh."),
]
