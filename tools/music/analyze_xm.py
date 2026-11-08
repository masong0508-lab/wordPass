#!/usr/bin/env python3
"""Analyse the visible songs of the bore-gba jukebox (FastTracker XM modules).

    python3 tools/music/analyze_xm.py [--src /path/to/bore-gba] [--docs]

Reads <src>/source/songs.h and <src>/source/unlocks.h to find the visible songs (hidden: names ending in
(ORIGINAL), PLACEHOLDER..., and the locked ones), parses every .xm with xmparse.py and writes one compact JSON
per song to tools/music/analysis/<id>.json. With --docs it also rewrites the summary table between the
`analysis:begin` / `analysis:end` markers of docs/MUSIC.md. The source repository is only read, never written.

Per song: tempo (default speed/BPM, every speed change, a rows-per-beat estimate), key and mode (pitch classes
weighted by duration), the lead motif (the most repeated 4..8 note interval-and-rhythm n-gram of the busiest
melodic channel), the bass riff (the most repeated one-bar bass pattern), the drum groove (the most common
16-step bar, percussion classified by instrument name, then by sample length and zero-crossing rate), the chord
roots per bar and the energy over time.
"""
import argparse
import collections
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xmparse  # noqa: E402

DEFAULT_SRC = "/home/user/masong0508-lab/bore-gba"
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT_DIR = os.path.join(HERE, "analysis")

NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
MODES = {
    "major": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10],
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "phrygian": [0, 1, 3, 5, 7, 8, 10],
    "mixolydian": [0, 2, 4, 5, 7, 9, 10],
    "lydian": [0, 2, 4, 6, 7, 9, 11],
    "harmonic minor": [0, 2, 3, 5, 7, 8, 11],
    "hijaz": [0, 1, 4, 5, 7, 8, 10],
}
MODE_PRIOR = {"major": 0.0, "minor": 0.0, "dorian": -0.03, "mixolydian": -0.03, "phrygian": -0.05,
              "lydian": -0.06, "harmonic minor": -0.04, "hijaz": -0.04}

KICK_W = {"kick", "bd", "kik", "808", "dum"}
SNARE_W = {"snare", "sd", "clap", "rim", "rimshot", "snr"}
HAT_W = {"hat", "hats", "hh", "ride", "crash", "cymbal", "shaker", "tambourine", "tamb", "tight", "open"}
PERC_W = {"tom", "toms", "conga", "bongo", "darbuka", "tek", "woodblock", "perc", "cowbell", "clave",
          "tabla", "snap", "click", "scratch", "doumbek", "bell"}


# ------------------------------------------------------------------ the song list
def read_song_list(src):
    """[(id, name, file, comment)] for every SONG_XM / SONG_ADP in order, plus the locked names."""
    songs, comment = [], []
    with open(os.path.join(src, "source", "songs.h"), encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if s.startswith("//"):
                comment.append(s[2:].strip())
                continue
            m = re.match(r'SONG_(XM|ADP)\(\s*(\w+)\s*,\s*"([^"]*)"\s*(?:,\s*"([^"]*)")?\s*\)', s)
            if m:
                songs.append({"kind": m.group(1), "id": m.group(2), "name": m.group(3), "file": m.group(4),
                              "comment": " ".join(comment)})
                comment = []
            elif not s:
                comment = []
    locked = set()
    with open(os.path.join(src, "source", "unlocks.h"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r'UNLOCK\("([^"]*)"', line.strip())
            if m:
                locked.add(m.group(1))
    return songs, locked


def hidden_reason(song, locked):
    n = song["name"]
    if n.endswith("(ORIGINAL)"):
        return "secret (ORIGINAL)"
    if n.startswith("PLACEHOLDER"):
        return "placeholder"
    if n in locked:
        return "locked (unlocks.h)"
    return None


# ------------------------------------------------------------------ the timeline
def timeline(m):
    """Walk the order list: returns rows [(order, pattern, row, seconds, speed, bpm)] and a list of tempo changes."""
    speed, bpm = m.speed or 6, m.bpm or 125
    t = 0.0
    rows, changes = [], [(0, speed, bpm)]
    visited = set()
    oi, start_row = 0, 0
    while oi < len(m.order):
        pat = m.order[oi]
        if pat >= len(m.patterns):
            oi += 1
            continue
        nrows, data = m.patterns[pat]
        if (oi, start_row) in visited:
            break
        visited.add((oi, start_row))
        jump_to, brk = None, None
        r = start_row
        while r < nrows:
            for n in data[r]:
                if n is None:
                    continue
                if n.fx == 0x0F and n.fxp:
                    if n.fxp < 32:
                        speed = n.fxp
                    else:
                        bpm = n.fxp
                    if changes[-1][1:] != (speed, bpm):
                        changes.append((len(rows), speed, bpm))
                elif n.fx == 0x0B:
                    jump_to = n.fxp
                elif n.fx == 0x0D:
                    brk = (n.fxp >> 4) * 10 + (n.fxp & 15)
            rows.append((oi, pat, r, t, speed, bpm))
            t += 2.5 * speed / bpm
            r += 1
            if jump_to is not None or brk is not None:
                break
        if jump_to is not None:
            if jump_to <= oi:
                break
            oi, start_row = jump_to, brk or 0
        else:
            oi, start_row = oi + 1, brk or 0
    return rows, changes, t


def comment_bpm(comment):
    """A tempo the song's own comment states ("172 BPM", "133 BPM"), or None."""
    m = re.search(r"\b(\d{2,3}) BPM", comment)
    return int(m.group(1)) if m else None


def rows_per_beat(rows, comment=""):
    """Dominant (speed, BPM), then the rows-per-beat that puts the tempo in 70..180 (or closest to the BPM the
    comment states). The "feel" tempo halves a half-time song that does not state its own BPM."""
    weight = collections.Counter()
    for (_o, _p, _r, _t, speed, bpm) in rows:
        weight[(speed, bpm)] += 1
    (speed, bpm), _ = weight.most_common(1)[0]
    tempo4 = 6.0 * bpm / speed
    stated = comment_bpm(comment)
    cands = [(rpb, tempo4 * 4.0 / rpb) for rpb in (1, 2, 4, 8, 16, 32)]
    if stated:
        rpb, tempo = min(cands, key=lambda c: abs(c[1] - stated))
    else:
        rpb, tempo = 4, tempo4
        for r, t in ((4, tempo4), (8, tempo4 / 2), (16, tempo4 / 4), (2, tempo4 * 2), (1, tempo4 * 4), (32, tempo4 / 8)):
            if 70 <= t <= 180:
                rpb, tempo = r, t
                break
    feel = tempo
    if not stated and "half-time" in comment.lower():
        feel = tempo / 2
    return rpb, tempo, (speed, bpm), feel, stated


# ------------------------------------------------------------------ instruments and notes
def tokens(name):
    return set(re.findall(r"[a-z0-9]+", name.lower()))


def sample_for(ins, note):
    if not ins or not ins.samples:
        return None
    k = ins.keymap[note - 1] if 1 <= note <= 96 else 0
    return ins.samples[k if k < len(ins.samples) else 0]


class Hit:
    __slots__ = ("ch", "row", "t", "midi", "inst", "dur_rows", "dur_s")


def collect_hits(m, rows):
    """Every note-on as a Hit: row index into `rows`, time, MIDI pitch (relative note applied), duration."""
    last_inst = [0] * m.channels
    open_hit = [None] * m.channels
    hits = []
    for gi, (_o, pat, r, t, _s, _b) in enumerate(rows):
        line = m.patterns[pat][1][r]
        for ch, n in enumerate(line):
            if n is None:
                continue
            if n.inst:
                last_inst[ch] = n.inst
            if n.note == 97:
                h = open_hit[ch]
                if h is not None:
                    h.dur_rows, h.dur_s = gi - h.row, t - h.t
                    open_hit[ch] = None
                continue
            if not 1 <= n.note <= 96:
                continue
            if n.fx == 3 or n.fx == 5 or (n.vol & 0xF0) == 0xF0:
                continue                                  # tone portamento: not a new note
            if n.vol == 0x10:
                continue                                  # volume 0
            ins = m.instruments[last_inst[ch]] if last_inst[ch] < len(m.instruments) else None
            smp = sample_for(ins, n.note)
            rel = smp.relnote if smp else 0
            h = Hit()
            h.ch, h.row, h.t, h.inst = ch, gi, t, last_inst[ch]
            h.midi = n.note + 11 + rel
            h.dur_rows, h.dur_s = 0, 0.0
            old = open_hit[ch]
            if old is not None:
                old.dur_rows, old.dur_s = gi - old.row, t - old.t
            open_hit[ch] = h
            hits.append(h)
    end_t = rows[-1][3] if rows else 0.0
    for h in open_hit:
        if h is not None:
            h.dur_rows, h.dur_s = len(rows) - h.row, max(0.05, end_t - h.t)
    return hits


def classify_instruments(m, hits):
    """inst index -> 'kick' | 'snare' | 'hat' | 'perc' | None (melodic)."""
    notes_used = collections.defaultdict(set)
    for h in hits:
        notes_used[h.inst].add(h.midi)
    out = {}
    for i in range(1, len(m.instruments)):
        ins = m.instruments[i]
        smp = ins.samples[0] if ins.samples else None
        tk = tokens(ins.name)
        looped = bool(smp and smp.loop_type)
        variety = len(notes_used.get(i, ()))
        cls = None
        if tk & {"bass"} and not tk & {"drum", "kick"}:
            cls = None
        elif tk & KICK_W:
            cls = "kick"
        elif tk & SNARE_W:
            cls = "snare"
        elif tk & HAT_W:
            cls = "hat"
        elif tk & PERC_W:
            cls = "perc"
        elif smp and not looped and variety <= 2 and smp.length < 26000 and variety > 0:
            # unnamed one-shot played on a single pitch: judge it by its sample
            if smp.zcr < 0.08 and smp.length < 12000:
                cls = "kick"
            elif smp.zcr >= 0.4:
                cls = "hat"
            elif smp.zcr >= 0.18:
                cls = "snare"
            else:
                cls = "perc"
        out[i] = cls
    return out


# ------------------------------------------------------------------ analysis parts
def estimate_key(hits, cls, bass_ch):
    hist = [0.0] * 12
    bass = [0.0] * 12
    for h in hits:
        if cls.get(h.inst):
            continue
        w = min(max(h.dur_s, 0.06), 3.0)
        pc = h.midi % 12
        hist[pc] += w
        if h.ch == bass_ch:
            bass[pc] += w
    tot = sum(hist) or 1.0
    btot = sum(bass) or 1.0
    best = None
    ranked = []
    for tonic in range(12):
        for mode, sc in MODES.items():
            inside = sum(hist[(tonic + s) % 12] for s in sc) / tot
            third = max(hist[(tonic + 3) % 12], hist[(tonic + 4) % 12]) / tot
            score = (2 * inside - 1) + 1.2 * hist[tonic] / tot + 0.5 * hist[(tonic + 7) % 12] / tot \
                + 0.4 * third + 0.8 * bass[tonic] / btot + MODE_PRIOR[mode]
            ranked.append((score, tonic, mode, inside))
    ranked.sort(reverse=True)
    score, tonic, mode, inside = ranked[0]
    margin = score - ranked[1][0]
    alt = [(NOTE_NAMES[t], mo) for (_s, t, mo, _i) in ranked[1:3]]
    return {"tonic": tonic, "tonic_name": NOTE_NAMES[tonic], "mode": mode, "in_scale": round(inside, 3),
            "margin": round(margin, 3), "alternatives": ["%s %s" % a for a in alt],
            "pc_hist": [round(x / tot, 3) for x in hist]}


def channel_stats(hits, cls):
    st = collections.defaultdict(lambda: {"n": 0, "sum": 0, "pcs": set()})
    for h in hits:
        if cls.get(h.inst):
            continue
        s = st[h.ch]
        s["n"] += 1
        s["sum"] += h.midi
        s["pcs"].add(h.midi)
    return st


def pick_lead_and_bass(hits, cls):
    st = channel_stats(hits, cls)
    lead, bass, lead_score, bass_low = None, None, 0, 999
    for ch, s in st.items():
        mean = s["sum"] / s["n"]
        if s["n"] >= 8 and len(s["pcs"]) >= 4 and mean >= 57:
            score = s["n"] * (1.0 if mean < 90 else 0.5)
            if score > lead_score:
                lead, lead_score = ch, score
        if s["n"] >= 24 and mean < bass_low and len(s["pcs"]) >= 2:
            bass, bass_low = ch, mean
    if lead is None:                       # a song with no high channel: the busiest non-bass one
        cand = [(s["n"], ch) for ch, s in st.items() if ch != bass and s["n"] >= 4]
        if cand:
            lead = max(cand)[1]
    return lead, bass


def motifs(hits, ch, rows_pb, tonic, top=5):
    """The most repeated interval+rhythm n-grams (4..8 notes) of one channel's note stream."""
    seq = sorted((h for h in hits if h.ch == ch), key=lambda h: h.row)
    # collapse chords/duplicates on the same row
    notes = []
    for h in seq:
        if notes and notes[-1].row == h.row:
            continue
        notes.append(h)
    steps_per_row = 4.0 / rows_pb
    grams = collections.defaultdict(list)
    for n in range(4, 9):
        for i in range(len(notes) - n + 1):
            seg = notes[i:i + n]
            ivs = tuple(seg[k + 1].midi - seg[k].midi for k in range(n - 1))
            if len(set(s.midi for s in seg)) < 3 or max(abs(x) for x in ivs) > 12:
                continue
            if sum(1 for x in ivs if x == 0) > len(ivs) // 3:
                continue                                   # a repeated-note figure is a pulse, not a tune
            if max(s.midi for s in seg) - min(s.midi for s in seg) > 14:
                continue
            gaps = tuple(min(16, max(1, int(round((seg[k + 1].row - seg[k].row) * steps_per_row))))
                         for k in range(n - 1))
            if sum(gaps) > 32:
                continue
            grams[(ivs, gaps)].append(i)
    scored = []
    for key, pos in grams.items():
        # non-overlapping occurrences
        cnt, last = 0, -99
        for p in pos:
            if p >= last + len(key[0]) + 1:
                cnt += 1
                last = p
        if cnt >= 2:
            scored.append((cnt * (len(key[0]) + 1), cnt, key, pos[0]))
    scored.sort(key=lambda x: (-x[0], x[3]))
    out, seen = [], []
    for _sc, cnt, (ivs, gaps), first in scored:
        if any(set(ivs) <= set(s) and len(ivs) <= len(s) and abs(len(s) - len(ivs)) <= 2 and ivs[0] == s[0]
               for s in seen):
            continue
        seen.append(ivs)
        seg = notes[first:first + len(ivs) + 1]
        t0 = seg[0].row
        mel, at = [], 0
        for k, s in enumerate(seg):
            gap = gaps[k] if k < len(gaps) else max(1, int(round(max(1, s.dur_rows) * steps_per_row)))
            mel.append({"semi": s.midi - (60 + tonic),   # 0 = the tonic in the octave of middle C
                        "step": at, "len": gap})
            at += gap
        out.append({"count": cnt, "notes": len(ivs) + 1, "intervals": list(ivs), "gaps": list(gaps),
                    "first_row": first, "melody": mel})
        if len(out) >= top:
            break
    return out


def bars_of(rows, rpb):
    """Rows per bar (4 beats) and a function row index -> (bar, step 0..15)."""
    bar_rows = 4 * rpb
    return bar_rows


def bass_riff(hits, ch, rpb, tonic):
    if ch is None:
        return None
    bar_rows = 4 * rpb
    per_bar = collections.defaultdict(list)
    for h in sorted((h for h in hits if h.ch == ch), key=lambda h: h.row):
        bar, rin = divmod(h.row, bar_rows)
        per_bar[bar].append((rin * 16 // bar_rows, h.midi))
    pats = collections.Counter()
    for bar, lst in per_bar.items():
        if len(lst) < 2:
            continue
        base = lst[0][1]
        steps = []
        seen = set()
        for s, mid in lst:
            if s in seen:
                continue
            seen.add(s)
            steps.append((s, mid - base))
        pats[(tuple(s for s, _ in steps), tuple(d for _, d in steps))] += 1
    if not pats:
        return None
    (steps, semis), cnt = pats.most_common(1)[0]
    # the root of that pattern: the tonic-relative pitch class of the most common bar start
    firsts = collections.Counter()
    for bar, lst in per_bar.items():
        firsts[(lst[0][1] - tonic) % 12] += 1
    return {"steps": list(steps), "semis": list(semis), "bars_with_it": cnt, "bars_with_bass": len(per_bar),
            "root_pcs": [[pc, n] for pc, n in firsts.most_common(4)]}


def drum_groove(hits, cls, rpb, nbars):
    bar_rows = 4 * rpb
    per = collections.defaultdict(lambda: {"kick": 0, "snare": 0, "hat": 0, "perc": 0})
    for h in hits:
        c = cls.get(h.inst)
        if not c:
            continue
        bar, rin = divmod(h.row, bar_rows)
        per[bar][c] |= 1 << (rin * 16 // bar_rows)
    common = collections.Counter()
    for bar, d in per.items():
        if d["kick"] or d["snare"]:
            common[(d["kick"], d["snare"], d["hat"], d["perc"])] += 1
    if not common:
        for bar, d in per.items():
            if d["hat"] or d["perc"]:
                common[(d["kick"], d["snare"], d["hat"], d["perc"])] += 1
    if not common:
        return None

    def txt(mask):
        return "".join("x" if mask >> i & 1 else "." for i in range(16))

    (k, s, hh, pc), cnt = common.most_common(1)[0]
    kick4 = all(k >> i & 1 for i in (0, 4, 8, 12))
    if kick4 and (s >> 4 & 1) and (s >> 12 & 1):
        gclass = "four on the floor"
    elif bin(s).count("1") == 1 and (s >> 8 & 1):
        gclass = "half-time"
    elif (s >> 4 & 1) and (s >> 12 & 1):
        gclass = "backbeat"
    elif not s and not k:
        gclass = "hats and percussion only"
    else:
        gclass = "broken beat"
    dens = sum(bin(v["kick"]).count("1") + bin(v["snare"]).count("1") + bin(v["hat"]).count("1") for v in per.values())
    return {"class": gclass, "kick": txt(k), "snare": txt(s), "hat": txt(hh), "perc": txt(pc), "bars_with_it": cnt,
            "bars_with_drums": len(per), "hits_per_bar": round(dens / max(1, len(per)), 1)}


def chord_roots(hits, cls, bass_ch, rpb, tonic, nbars):
    bar_rows = 4 * rpb
    weight = collections.defaultdict(lambda: collections.defaultdict(float))
    lowest = {}
    for h in hits:
        if cls.get(h.inst):
            continue
        bar = h.row // bar_rows
        w = min(max(h.dur_rows, 1), bar_rows) * (3.0 if h.ch == bass_ch else 1.0)
        # low notes carry the root
        w *= 1.5 if h.midi < 55 else 1.0
        weight[bar][h.midi % 12] += w
    roots = []
    for b in range(nbars):
        d = weight.get(b)
        if not d:
            roots.append(None)
            continue
        pc = max(d.items(), key=lambda kv: kv[1])[0]
        roots.append((pc - tonic) % 12)
    # most repeated 4-bar progression
    prog = collections.Counter()
    for i in range(0, len(roots) - 3):
        w = tuple(roots[i:i + 4])
        if None not in w and len(set(w)) > 1:
            prog[w] += 1
    best = prog.most_common(1)[0] if prog else None
    return {"per_bar_first_32": roots[:32],
            "progression": list(best[0]) if best else None, "progression_count": best[1] if best else 0}


def energy(hits, cls, rpb, nbars):
    bar_rows = 4 * rpb
    per = [0] * max(nbars, 1)
    chans = [set() for _ in per]
    for h in hits:
        b = min(h.row // bar_rows, len(per) - 1)
        per[b] += 1
        chans[b].add(h.ch)
    nb = 16
    out = []
    size = max(1, len(per) / nb)
    for i in range(nb):
        seg = per[int(i * size):max(int((i + 1) * size), int(i * size) + 1)]
        out.append(round(sum(seg) / max(1, len(seg)), 1))
    return {"onsets_per_bar_16_slices": out, "avg_onsets_per_bar": round(sum(per) / len(per), 1),
            "max_channels_in_a_bar": max(len(c) for c in chans) if chans else 0}


def analyse_song(song, path):
    m = xmparse.parse(path)
    rows, changes, seconds = timeline(m)
    rpb, tempo, (speed, bpm), feel, stated = rows_per_beat(rows, song["comment"])
    hits = collect_hits(m, rows)
    cls = classify_instruments(m, hits)
    lead, bass = pick_lead_and_bass(hits, cls)
    key = estimate_key(hits, cls, bass)
    nbars = max(1, len(rows) // (4 * rpb))
    perc_counts = collections.Counter(cls[h.inst] for h in hits if cls.get(h.inst))
    ins_names = {i: m.instruments[i].name for i in range(1, len(m.instruments))}
    res = {
        "id": song["id"], "name": song["name"], "file": song["file"],
        "comment": song["comment"],
        "tracker_title": m.title, "tracker": m.tracker,
        "channels": m.channels, "patterns": len(m.patterns), "orders": m.song_len,
        "instruments": len(m.instruments) - 1,
        "rows": len(rows), "seconds": round(seconds, 1),
        "default_speed": m.speed, "default_bpm": m.bpm,
        "dominant_speed": speed, "dominant_bpm": bpm,
        "tempo_changes": len(changes),
        "tempo_change_list": [[r, s, b] for (r, s, b) in changes[:12]],
        "rows_per_beat": rpb, "beat_bpm": round(tempo, 1), "feel_bpm": round(feel, 1), "bpm_in_comment": stated,
        "bars": nbars,
        "key": key,
        "lead_channel": lead, "bass_channel": bass,
        "lead_instruments": sorted({ins_names.get(h.inst, "?") for h in hits if h.ch == lead})[:6] if lead is not None else [],
        "lead_motifs": motifs(hits, lead, rpb, key["tonic"]) if lead is not None else [],
        "bass_riff": bass_riff(hits, bass, rpb, key["tonic"]),
        "drums": drum_groove(hits, cls, rpb, nbars),
        "percussion_hits": dict(perc_counts),
        "percussion_instruments": {ins_names[i]: c for i, c in cls.items() if c},
        "chords": chord_roots(hits, cls, bass, rpb, key["tonic"], nbars),
        "energy": energy(hits, cls, rpb, nbars),
        "note_onsets": len(hits),
        "source": "xm",
    }
    return res


def describe_without_xm(song):
    return {"id": song["id"], "name": song["name"], "file": song["file"], "comment": song["comment"],
            "source": "songs.h comment only (no .xm note data)"}


# ------------------------------------------------------------------ docs table
def table(results):
    lines = ["| # | Song | Source file | Bars | Beat BPM | Rows/beat | Key | Lead motif (semitones) | Bass riff | Drums (k/s/h hits per bar) |",
             "|---|------|-------------|------|----------|-----------|-----|------------------------|-----------|-----------------------------|"]
    for i, r in enumerate(results, 1):
        if r["source"] != "xm":
            lines.append("| %d | %s | %s | | | | | (not analysed: %s) | | |" % (i, r["name"], r["file"], r["source"]))
            continue
        k = r["key"]
        mot = r["lead_motifs"][0] if r["lead_motifs"] else None
        mtxt = "%s x%d" % (",".join(str(n["semi"]) for n in mot["melody"]), mot["count"]) if mot else "-"
        br = r["bass_riff"]
        btxt = ("%s on steps %s" % (",".join(str(x) for x in br["semis"]), ",".join(str(x) for x in br["steps"]))) if br else "-"
        d = r["drums"]
        dtxt = ("k %s s %s h %s" % (d["kick"].replace(".", "-"), d["snare"].replace(".", "-"), d["hat"].replace(".", "-"))) if d else "none"
        lines.append("| %d | %s | `%s` | %d | %.0f | %d | %s %s | %s | %s | %s |" % (
            i, r["name"], os.path.basename(r["file"]), r["bars"], r["beat_bpm"], r["rows_per_beat"],
            k["tonic_name"], k["mode"], mtxt, btxt, dtxt))
    return "\n".join(lines)


def patch_docs(text, name="analysis"):
    path = os.path.join(ROOT, "docs", "MUSIC.md")
    begin, end = "<!-- %s:begin -->" % name, "<!-- %s:end -->" % name
    if not os.path.exists(path):
        body = "# wordPass music\n\n%s\n%s\n%s\n" % (begin, text, end)
    else:
        with open(path, encoding="utf-8") as f:
            body = f.read()
        if begin in body:
            a, b = body.index(begin), body.index(end)
            body = body[:a] + begin + "\n" + text + "\n" + body[b:]
        else:
            body += "\n%s\n%s\n%s\n" % (begin, text, end)
    with open(path, "w", encoding="utf-8") as f:
        f.write(body)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=os.environ.get("BORE_GBA", DEFAULT_SRC))
    ap.add_argument("--docs", action="store_true", help="rewrite the analysis table in docs/MUSIC.md")
    ap.add_argument("--only", help="comma separated song ids")
    args = ap.parse_args()
    songs, locked = read_song_list(args.src)
    visible = []
    for s in songs:
        why = hidden_reason(s, locked)
        if why:
            continue
        visible.append(s)
    only = set(args.only.split(",")) if args.only else None
    os.makedirs(OUT_DIR, exist_ok=True)
    results = []
    for s in visible:
        if only and s["id"] not in only:
            continue
        if s["kind"] == "XM" and s["file"]:
            r = analyse_song(s, os.path.join(args.src, s["file"]))
        else:
            r = describe_without_xm(s)
        with open(os.path.join(OUT_DIR, s["id"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(r, f, indent=None, separators=(",", ":"), sort_keys=True)
            f.write("\n")
        results.append(r)
        print("%-34s %s" % (s["name"], ("%.0f bpm %s %s" % (r["beat_bpm"], r["key"]["tonic_name"], r["key"]["mode"])) if r["source"] == "xm" else r["source"]))
    print("%d visible songs of %d listed (%d hidden or locked)" % (len(visible), len(songs), len(songs) - len(visible)))
    if args.docs:
        patch_docs(table(results))
        lines = ["| # | Song | Status |", "|---|------|--------|"]
        n = 0
        for s in songs:
            why = hidden_reason(s, locked)
            if why is None:
                n += 1
            lines.append("| %s | %s | %s |" % (n if why is None else "", s["name"],
                                                "visible, " + ("analysed from `%s`" % os.path.basename(s["file"]) if s["kind"] == "XM" else "streamed ADPCM, no note data") if why is None else "skipped: " + why))
        patch_docs("\n".join(lines), "songlist")


if __name__ == "__main__":
    main()
