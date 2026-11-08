#!/usr/bin/env python3
"""Generate source/songs.c (the song tables music.c plays) deterministically.

    python3 tools/music/build_songs.py            # writes source/songs.c
    python3 tools/music/build_songs.py --docs     # also rewrites the songs block of docs/MUSIC.md
    python3 tools/music/build_songs.py --check    # exit 1 when source/songs.c is not what this would write

Inputs: tools/music/analysis/<id>.json (what analyze_xm.py found in the source songs) and the readable per-song
definitions in tools/music/songs_src.py. A definition may leave the hook, bass riff, chord walk or drums to be
derived from its sources (a motif contour mapped onto the song's scale, a bass riff folded into the register, a drum
groove thinned into three levels) and overrides any of them by hand.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "source", "songs.c")

MODES = ["major", "minor", "dorian", "mixolydian", "phrygian", "lydian", "harmonic minor", "hijaz"]
SCALES = {
    "major": [0, 2, 4, 5, 7, 9, 11], "minor": [0, 2, 3, 5, 7, 8, 10], "dorian": [0, 2, 3, 5, 7, 9, 10],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10], "phrygian": [0, 1, 3, 5, 7, 8, 10], "lydian": [0, 2, 4, 6, 7, 9, 11],
    "harmonic minor": [0, 2, 3, 5, 7, 8, 11], "hijaz": [0, 1, 4, 5, 7, 8, 10],
}
ROLES = {"menu": "SR_MENU", "classic": "SR_CLASSIC", "daily": "SR_DAILY", "dark": "SR_DARK"}
INT_STEPS = [0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7]       # semitone interval -> scale steps
REST, HOLD = 99, 0


def analysis(sid):
    with open(os.path.join(HERE, "analysis", sid + ".json"), encoding="utf-8") as f:
        return json.load(f)


# ------------------------------------------------------------------ bars
def parse_bar(text):
    """'1 . 3 . 5 - ...' -> 16 values: degree, 0 hold ('.'), 99 rest ('-')."""
    out = []
    for t in text.split():
        out.append(HOLD if t == "." else REST if t == "-" else int(t))
    if len(out) != 16:
        raise ValueError("a bar needs 16 steps, got %d: %r" % (len(out), text))
    return out


def semis_to_steps(d):
    a = abs(d)
    n = INT_STEPS[a] if a <= 12 else 7 + INT_STEPS[min(a - 12, 12)]
    return n if d >= 0 else -n


def seed_bar(src, idx, start, rscale=1, maxnotes=8, first_octave_shift=0):
    """The contour and rhythm of a source song's lead motif, mapped onto scale degrees from `start`."""
    mot = analysis(src)["lead_motifs"][idx]["melody"]
    notes = []
    deg = start
    prev = None
    for n in mot:
        if prev is not None:
            deg += semis_to_steps(n["semi"] - prev)
        prev = n["semi"]
        pos = n["step"] * rscale
        if pos > 15 or len(notes) >= maxnotes:
            break
        notes.append([pos, deg])
    if len(notes) < 3:
        raise ValueError("motif %s[%d] is too short once mapped" % (src, idx))
    lo, hi = min(d for _, d in notes), max(d for _, d in notes)
    shift = 0
    while hi + shift > 12:
        shift -= 7
    while lo + shift < 1:
        shift += 7
    bar = [HOLD] * 16
    for pos, d in notes:
        bar[pos] = d + shift
    return bar


def onsets(bar):
    return [(i, v) for i, v in enumerate(bar) if v not in (HOLD, REST)]


def to_chord_tone(deg, chord):
    """Nearest degree to `deg` that is a tone of the triad on `chord` (chord, +2, +4)."""
    tones = [chord + o + 7 * k for o in (0, 2, 4) for k in (-1, 0, 1)]
    return min(tones, key=lambda t: (abs(t - deg), t))


def derive_seq(bar, up, chord):
    """The motif again `up` scale steps higher, its last note landing on a tone of the bar's chord."""
    out = list(bar)
    on = onsets(out)
    for i, v in on:
        out[i] = v + up
    i, v = on[-1]
    out[i] = to_chord_tone(v + up, chord)
    return clamp_bar(out)


def derive_inv(bar, centre, chord):
    """The motif upside down around its first note (shifted to start on `centre`), ending on a chord tone."""
    out = list(bar)
    on = onsets(out)
    first = on[0][1]
    for i, v in on:
        out[i] = centre - (v - first)
    i, v = on[-1]
    out[i] = to_chord_tone(v, chord)
    return clamp_bar(out)


def clamp_bar(bar):
    on = onsets(bar)
    lo, hi = min(v for _, v in on), max(v for _, v in on)
    shift = 0
    while hi + shift > 12:
        shift -= 7
    while lo + shift < 1:
        shift += 7
    if hi + shift > 12:
        raise ValueError("a bar spans more than an octave and a half: %r" % (bar,))
    return [v + shift if v not in (HOLD, REST) else v for v in bar]


def resolve_hook(spec, chords):
    """spec: four items, each a bar string or a tuple: ('seed', src, idx, start[, rscale]) ('seq', up, from_bar)
    ('inv', centre, from_bar). Returns 64 values."""
    bars = []
    for k, it in enumerate(spec):
        if isinstance(it, str):
            bars.append(parse_bar(it))
        elif it[0] == "seed":
            bars.append(seed_bar(it[1], it[2], it[3], it[4] if len(it) > 4 else 1, it[5] if len(it) > 5 else 8))
        elif it[0] == "seq":
            bars.append(derive_seq(bars[it[2]], it[1], chords[k]))
        elif it[0] == "inv":
            bars.append(derive_inv(bars[it[2]], it[1], chords[k]))
        else:
            raise ValueError("unknown bar spec %r" % (it,))
    out = []
    for b in bars:
        out += b
    return out


# ------------------------------------------------------------------ bass, chords, drums
def derive_bass(src, mode):
    br = analysis(src)["bass_riff"]
    pat = [None] * 16
    if br:
        for s, d in zip(br["steps"], br["semis"]):
            d0 = d
            while d0 > 12:
                d0 -= 12
            while d0 < -7:
                d0 += 12
            pat[s] = d0
    else:
        pat[0] = 0
        pat[8] = 7
    return pat


def parse_bass(text):
    out = []
    for t in text.split():
        out.append(None if t == "." else int(t))
    if len(out) != 16:
        raise ValueError("a bass bar needs 16 steps: %r" % text)
    return out


def derive_chords(src, mode, bars=4):
    """Degrees of the source's most repeated 4-bar progression (roots in semitones from the tonic)."""
    prog = analysis(src)["chords"]["progression"] or [0, 0, 5, 7]
    sc = SCALES[mode]
    out = []
    for r in prog[:bars]:
        out.append(1 + min(range(7), key=lambda i: (abs(sc[i] - r), i)))
    return out


def parse_mask(text):
    text = text.replace(" ", "")
    if len(text) != 16:
        raise ValueError("a drum bar needs 16 steps: %r" % text)
    return sum(1 << i for i, c in enumerate(text) if c in "xX")


def parse_hats(text):
    """16 characters: '.' none, else a hex digit n = level 2n (f = 30)."""
    text = text.replace(" ", "")
    if len(text) != 16:
        raise ValueError("a hat bar needs 16 steps: %r" % text)
    return [0 if c == "." else int(c, 16) * 2 for c in text]


def make_levels(spec):
    """The three drum levels of a song.
    spec keys: kick, snare (16 step strings, or four of them for the four bars), hat1 (level 1: a soft hat only),
    hat2 (level 2: kick and snare with this hat), hat3 (level 3), optional kick3 / snare3 (level 3's own), ghost (a soft
    kick on step 14 of bars 2 and 4, default True), open (an open hat at the top of every loop at level 3, default
    True), fill (a snare roll into the last beat of the pattern at level 3, default True). A level with no kick and
    no snare in the spec (level 1, and every level of a song that has neither) plays hats only."""
    def four(v):
        if v is None:
            return [0] * 4
        if isinstance(v, str):
            v = [v] * 4
        return [parse_mask(x) for x in v]
    kick, snare = four(spec.get("kick")), four(spec.get("snare"))
    kick3 = four(spec["kick3"]) if "kick3" in spec else kick
    snare3 = four(spec["snare3"]) if "snare3" in spec else snare
    ghost_on = spec.get("ghost", True)
    ghost = [0, 1 << 14 if ghost_on and not kick[1] >> 14 & 1 else 0, 0, 1 << 14 if ghost_on and not kick[3] >> 14 & 1 else 0]
    none = [0] * 4
    h1 = parse_hats(spec["hat1"])
    h2 = parse_hats(spec.get("hat2", spec["hat1"]))
    h3 = parse_hats(spec.get("hat3", spec.get("hat2", spec["hat1"])))
    mute = not any(kick) and not any(snare)
    return [
        dict(kick=none, snare=none, ghost=none, open=none, hat=h1, fill=0),
        dict(kick=kick, snare=snare, ghost=none if mute else ghost, open=none, hat=h2, fill=0),
        dict(kick=kick3, snare=snare3, ghost=none if mute else ghost,
             open=[1 if spec.get("open", True) and not mute else 0, 0, 0, 0], hat=h3,
             fill=1 if spec.get("fill", True) and not mute else 0),
    ]


# ------------------------------------------------------------------ the C
MODE_NAMES = {"major": "MAJOR", "minor": "MINOR", "dorian": "DORIAN", "mixolydian": "MIXOLYDIAN", "phrygian": "PHRYGIAN",
              "lydian": "LYDIAN", "harmonic minor": "HARMINOR", "hijaz": "HIJAZ"}


def c_arr(vals, per=16):
    rows = []
    for i in range(0, len(vals), per):
        rows.append("        " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
    return "{\n" + "\n".join(rows) + "\n    }"


def inst_c(t):
    return "{ %s }" % ", ".join(str(int(x)) for x in t)


def bar_text(vals):
    return " ".join("." if v == HOLD else "-" if v == REST else str(v) for v in vals)


def build(defs):
    songs = []
    for d in defs:
        mode = d["mode"]
        chordsA = d.get("chordA") or derive_chords(d["sources"][0], mode)
        chordsB = d.get("chordB") or chordsA
        hookA = resolve_hook(d["hookA"], chordsA)
        hookB = resolve_hook(d["hookB"], chordsB)
        bass = d.get("bass")
        if bass is None:
            b0 = derive_bass(d["sources"][0], mode)
            b1 = list(b0)
        else:
            b0 = parse_bass(bass[0])
            b1 = parse_bass(bass[1]) if len(bass) > 1 else list(b0)
        lv = make_levels(d["drums"])
        accent = d.get("bass_accent")
        if accent is None:
            accent = sum(1 << i for i in (0, 8))
        songs.append(dict(d=d, mode=mode, chordsA=chordsA, chordsB=chordsB, hookA=hookA, hookB=hookB,
                          bass=[b0, b1], levels=lv, accent=accent))
    return songs


def emit(songs):
    L = []
    L.append("// GENERATED by tools/music/build_songs.py from tools/music/analysis/*.json and tools/music/songs_src.py.")
    L.append("// Do not edit by hand: run `make songs`. The tables music.c plays (see source/songs.h).")
    L.append('#include "songs.h"')
    L.append("")
    L.append("const Song SONGS[] = {")
    for s in songs:
        d = s["d"]
        L.append("    {   // %s: %s" % (d["id"], d["name"]))
        L.append('        "%s",' % d["name"])
        att = d.get("att", (60, 50, 36))
        L.append("        %s, %d, MODE_%s, %d," % (ROLES[d["role"]], d["root"], MODE_NAMES[d["mode"]], int(d.get("harm", 0))))
        L.append("        %d, %d, %d, %d," % (d["frames"], d.get("lead_oct", 1), d.get("gain", 100), 1 if d.get("tap_minor") else 0))
        L.append("        %d, %d, %d," % att)
        dv = d.get("drum_vol", (54, 46, 30, 24))
        dh = d.get("drum_hz", (2400, 7000, 16000, 12000))
        L.append("        %d, %d, %d, %d," % tuple(dv))
        L.append("        %d, %d, %d, %d," % tuple(dh))
        L.append("        %d, { %d, %d }," % (d.get("pad_max", 40), d.get("pad_deg", (2, 4))[0], d.get("pad_deg", (2, 4))[1]))
        L.append("        { %s }," % ", ".join(str(x) for x in d.get("arp", (0, 2, 4, 7))))
        L.append("        { %s }, { %s }," % (", ".join(str(x) for x in s["chordsA"]), ", ".join(str(x) for x in s["chordsB"])))
        L.append("        {")
        for b in s["bass"]:
            L.append("            { %s }," % ", ".join("BASS_REST" if v is None else str(v) for v in b))
        L.append("        },")
        L.append("        0x%04x," % s["accent"])
        L.append("        %s," % c_arr(s["hookA"]))
        L.append("        %s," % c_arr(s["hookB"]))
        L.append("        {")
        for lv in s["levels"]:
            L.append("            { { %s }, { %s }, { %s }, { %s },"
                     % tuple(", ".join("0x%04x" % m for m in lv[k]) for k in ("kick", "snare", "ghost", "open")))
            L.append("              { %s }, %d }," % (", ".join(str(x) for x in lv["hat"]), lv["fill"]))
        L.append("        },")
        L.append("        { %s }," % ", ".join(inst_c(d["inst"][k]) for k in range(4)))
        L.append("    },")
    L.append("};")
    L.append("const int NSONGS = %d;" % len(songs))
    L.append("")
    return "\n".join(L)


def docs_block(songs):
    names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
    lines = []
    for n, s in enumerate(songs):
        d = s["d"]
        bpm = 900.0 / d["frames"]
        lines.append("### %d. %s" % (n + 1, d["name"]))
        lines.append("")
        lines.append("- Family `%s`, plays: %s." % (d["family"], d["where"]))
        lines.append("- Fed by: %s." % ", ".join(d["credits"]))
        lines.append("- %s %s, %.0f BPM (%d frames to a 16th step), lead from %s up, level %d%%." % (
            names[d["root"] % 12], d["mode"], bpm, d["frames"], names[(d["root"] + 12 * d.get("lead_oct", 1)) % 12] + str(d["root"] // 12 - 1 + d.get("lead_oct", 1)), d.get("gain", 100)))
        lines.append("- Chords A: %s, B: %s (scale degrees)." % (" ".join(map(str, s["chordsA"])), " ".join(map(str, s["chordsB"]))))
        lines.append("- Hook A, bar by bar (degrees; `.` hold, `-` rest):")
        lines.append("")
        lines.append("```")
        for k in range(4):
            lines.append(bar_text(s["hookA"][k * 16:k * 16 + 16]))
        lines.append("```")
        lines.append("")
        lines.append("- %s" % d["notes"])
        lines.append("")
    return "\n".join(lines)


def main():
    import songs_src
    songs = build(songs_src.SONGS)
    text = emit(songs)
    if "--check" in sys.argv:
        cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        sys.exit(0 if cur == text else 1)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote %s: %d songs" % (os.path.relpath(OUT, ROOT), len(songs)))
    if "--docs" in sys.argv:
        import analyze_xm
        analyze_xm.patch_docs(docs_block(songs), "songs")


if __name__ == "__main__":
    main()
