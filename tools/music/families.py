"""The ten song families: which bore-gba songs feed which wordPass song, and where it plays.

This is the curated result of the clustering (cluster.py prints the automatic clusters next to it). Every
visible bore-gba song with an .xm file appears in exactly one family. songs_src.py takes `sources` from here.
role: menu (title, options, ledger, guide, language chooser, first-boot chooser), classic (the Classic shelf
rotation), daily (the Daily Pass), dark (Lockdown tiers and Classic on FIRE FAST).
"""

FAMILIES = [
    dict(id="morning", song="MORNING MUSIC BOX", role="menu",
         sources=["nursery_time", "magic_act", "spanish_flexicode"],
         why="the slow, soft end of the jukebox: a music-box ditty in C mixolydian at 94 BPM, a kora and metallophone "
             "arpeggio suite in Eb mixolydian at 94 BPM, and a marimba rework in Bb dorian at 112 BPM"),
    dict(id="chips", song="SUNNY CHIPS", role="classic",
         sources=["the_dipper_man", "amiga_music", "earth_and_the_space_citizens", "sunman_sunrise"],
         why="small tracker tunes (4 to 10 channels, 4 rows to the beat) at 100 to 125 BPM, lydian or mixolydian, "
             "a plain hat-and-kick bed under a bouncing lead"),
    dict(id="patio", song="PATIO HOUSE", role="classic",
         sources=["the_ticking_bomb", "mi_cora_zone", "gottcho_barracho_ii"],
         why="Latin house at 124 to 133 BPM with a call-and-answer lead, major or mixolydian, claves and a "
             "snare that rides the off-beat"),
    dict(id="meadow", song="MEADOW HOUSE", role="classic",
         sources=["excuses_house", "tree_fast", "meltdown_in_mars_house"],
         why="warm four-on-the-floor house at 124 to 126 BPM: rhodes and piano arpeggios over a walking bass, "
             "an acid line as the answer"),
    dict(id="stage", song="STAGE LIGHTS", role="classic",
         sources=["over_my_voice", "staged"],
         why="hi-NRG at 128 to 130 BPM, straight kick and offbeat hats, a pulse-synth arpeggio under a "
             "singing organ line"),
    dict(id="shuffle", song="PURDIE WALK", role="classic",
         sources=["whistler_shuffle"],
         why="the half-time shuffle: 75 BPM feel in C dorian, ghost snares and a whistle-and-horn lead"),
    dict(id="twostep", song="TWO STEP DAILY", role="daily",
         sources=["condensed_music"],
         why="UK garage 2-step at 132 BPM in C minor: a skipping kick, a snare on the 2 and the 4 plus a shuffled "
             "ghost, and a bass pop on the 1 and the 3"),
    dict(id="night", song="NIGHT TRAIN", role="dark",
         sources=["cynicaller_dnb", "ohio_hoe", "cocaine_cola_ii"],
         why="drum and bass at 172 to 180 BPM: a two-step breakbeat, a rolling sub, a four-note rising lead"),
    dict(id="acid", song="ACID ALARM", role="dark",
         sources=["hotdamn_rave", "emergency_hitech"],
         why="90s rave and hi-tech: acid lines in D mixolydian/C minor at 135 BPM, breakbeats and 16th hats"),
    dict(id="desert", song="DESERT FIRE", role="dark",
         sources=["aim_and_shoot", "quickbullet"],
         why="half-time downgrooves around 81 and 129 BPM in hijaz and Ab minor: a droning bass, an oud-like "
             "plucked line and a single snare on the 3"),
]

FAMILY_BY_ID = {f["id"]: f for f in FAMILIES}
