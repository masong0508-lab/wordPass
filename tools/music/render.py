#!/usr/bin/env python3
"""Listen to the wordPass songs on a computer: a software model of the DS PSG playing the same song tables.

    python3 tools/music/render.py                 # every song: docs/music/<id>.wav and <id>.mid
    python3 tools/music/render.py --only chips    # one song
    python3 tools/music/render.py --midi-only     # skip the WAVs (fast)
    python3 tools/music/render.py --stats         # print each song's measured loudness and density, write nothing

What it models (the ROM's sequencer in source/music.c, ported frame for frame for the music voices): the hook, the
pulse arpeggio, the bass riff, the pad and the drums (kick and snare on the noise channel, hats on the second noise
channel), each with the song's instrument (duty, onset and sustain level, decay, vibrato, release), square waves at the
PSG's 12.5 / 25 / 37.5 / 50 percent duties and a 15-bit noise register, 60 frames a second. A render is one full cycle of
the tune, A A B A (four passes through the 64 steps), mono, 16-bit, 22050 Hz. The game songs play the way a shelf does
when the player keeps spelling words: one bar of bass and pulse, then the hook, with the drums climbing a level each pass
(1, 2, 3, 3). The menu song plays the title's mix (its bass, quiet pad and very soft hat are always in, one level).
Sound effects, chimes, the alarm and the pass and fail stingers are not rendered. numpy is used when installed (about
ten times faster); without it the same code runs on plain lists.

Not a recording of the console: the real PSG's analogue filtering and the speakers' response are not modelled, and the
mix level is one fixed gain, so loudness here is only a comparison between songs.
"""
import array
import os
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "docs", "music")
RATE = 22050
FPS = 60
GAIN = 0.0030            # one DS volume unit (0..127) in full-scale; a 230-unit peak sum is 0.69

try:
    import numpy as np
except ImportError:      # the renderer works without it, only slower
    np = None

import build_songs  # noqa: E402
import songs_src  # noqa: E402

V_HOOK, V_PULSE, V_BASS, V_PAD, V_DRUM, V_NOISE2 = range(6)     # the music voices (the alarm and chime are effects)
OCT8 = [4186, 4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902]


def note_hz(n):
    s = 9 - n // 12
    return ((OCT8[n % 12] + (1 << (s - 1))) >> s) if s > 0 else OCT8[n % 12] << -s


def duty_pct(p):
    return 12.5 if p <= 12 else 25.0 if p <= 25 else 37.5 if p <= 37 else 50.0


class Voice:
    def __init__(self, noise=False):
        self.noise = noise
        self.gate = False
        self.cur = self.sus = self.dec = self.rel = self.acc = self.atk = self.life = 0
        self.freq = self.base = 440
        self.nf = 0
        self.fresh = False
        self.vib_delay = self.vib_div = 0
        self.duty = 50.0
        self.slope = 0
        self.lock = False
        self.out = 0


def render_frames(song_def, built, passes=4, menu=False):
    """Yield one tuple per frame: [(kind, freq, duty, level 0..127)] for the six voices."""
    d = song_def
    mode = d["mode"]
    sc = build_songs.SCALES[mode]
    root, lead_oct = d["root"], d.get("lead_oct", 1)
    gain = d.get("gain", 100)
    inst = d["inst"]
    att = d.get("att", (60, 50, 36))
    pad_max, pad_deg = d.get("pad_max", 40), d.get("pad_deg", (2, 4))
    arp = d.get("arp", (0, 2, 4, 7))
    kvol, svol, gvol, ovol = d.get("drum_vol", (54, 46, 30, 24))
    khz, shz, hhz, ohz = d.get("drum_hz", (2400, 7000, 16000, 12000))
    accent = built["accent"]
    harm = d.get("harm", 0)
    frames = d["frames"]
    V = [Voice(), Voice(), Voice(), Voice(), Voice(True), Voice(True)]
    for i, v in enumerate(V):
        v.rel = inst[i][8] if i < 4 else 127

    def degree_note(deg, octave, chord):
        dd = deg - 1
        o, dd = divmod(dd, 7)
        semi = sc[dd]
        if harm and chord == 5 and dd == 6:
            semi = 11
        return root + semi + 12 * (o + octave)

    def note_on(i, midi, von, vsus, dec):
        v = V[i]
        ins = inst[i]
        v.base = note_hz(midi)
        v.freq = v.base
        v.duty = duty_pct(ins[0] if ins[2] else ins[1])
        v.gate = True
        v.cur, v.sus, v.dec, v.atk, v.acc = von, vsus, dec, 0, 0
        v.life = 0
        v.nf = 0
        v.slope = 0
        v.fresh = True
        v.lock = False
        v.vib_delay, v.vib_div = ins[6], ins[7]

    def note_play(i, midi, pct):
        ins = inst[i]
        note_on(i, midi, ins[3] * pct // 100, ins[4] * pct // 100, ins[5])

    def noise_hit(i, hz, slope, vol, decay, life):
        v = V[i]
        v.base = v.freq = hz
        v.gate = True
        v.cur, v.sus, v.dec, v.atk = vol, 0, decay, 0
        v.life, v.slope, v.nf, v.fresh, v.acc, v.vib_div = life, slope, 0, True, 0, 0

    def hat(hz, nframes, vol):                      # the second noise channel: a flat level for nframes
        v = V[V_NOISE2]
        v.base = v.freq = hz
        v.gate = True
        v.cur = v.sus = vol
        v.dec = v.atk = 0
        v.life = nframes
        v.slope = 0
        v.fresh = True

    state = dict(step=0, timer=0, loop=0, pickup=1, drum_level=0, bass_life=0, sf=frames, pad_vol=0)
    order_pad = d.get("pad_swell", 0.75)

    def voice_frame(i):
        v = V[i]
        ins = inst[i] if i < 4 else None
        onset = v.fresh
        v.fresh = False
        if not onset:
            v.nf += 1
            if v.life > 0:
                v.life -= 1
                if v.life == 0:
                    v.gate = False
                    v.cur = 0
            if v.gate:
                if v.cur > v.sus:
                    v.cur -= v.dec if v.dec else (v.rel if v.rel else 127)
                    if v.cur < v.sus:
                        v.cur = v.sus
                elif v.cur < v.sus:
                    if not v.atk:
                        v.cur = v.sus
                    else:
                        v.acc += v.atk
                        v.cur += v.acc >> 4
                        v.acc &= 15
                    if v.cur > v.sus:
                        v.cur = v.sus
            elif v.cur > 0:
                v.cur = max(0, v.cur - v.rel)
        if i == V_HOOK and ins[2] and v.nf == ins[2] and not v.lock:
            v.duty = duty_pct(ins[1])
        if v.noise and v.gate and v.slope and not onset:
            v.freq = max(100, v.freq + v.slope)
        elif not v.noise and v.vib_div and v.gate and v.nf >= v.vib_delay:
            tri = (0, 1, 1, 0, -1, -1)[(v.nf - v.vib_delay) % 6]
            v.freq = v.base + v.base // v.vib_div * tri
        t = v.cur
        if gain != 100 and i in (V_HOOK, V_PULSE, V_BASS, V_PAD, V_DRUM, V_NOISE2):
            t = t * gain // 100
        v.out = min(127, t)

    total_steps = 64 * passes + 0
    step_count = 0
    while step_count < total_steps:
        # one frame of the sequencer
        state["timer"] += 1
        if state["timer"] >= state["sf"]:
            state["timer"] = 0
            step = state["step"]
            loop = state["loop"]
            sec = 1 if (loop & 3) == 2 else 0
            hook = built["hookB"] if sec else built["hookA"]
            chords = built["chordsB"] if sec else built["chordsA"]
            bar, sb = (step // 16) % 4, step % 16
            chord = chords[bar]
            if step % 16 == 0 and step > 0 and state["pickup"] > 0:
                state["pickup"] -= 1
            pickup = state["pickup"] > 0
            if menu:
                state["bass_life"] = max(state["bass_life"], 1)
                state["drum_level"] = 1
            else:                                   # the drums climb a level each pass: 1, 2, 3, 3 (the player keeps spelling)
                state["bass_life"] = 3
                if not pickup:
                    state["drum_level"] = min(3, 1 + loop)
            h = hook[step]
            if pickup or h == 99:
                V[V_HOOK].gate = False
            elif h:
                if menu and att[0]:
                    note_on(V_HOOK, degree_note(h, lead_oct, chord), att[0], att[0], 0)
                    V[V_HOOK].duty = duty_pct(att[1])
                    V[V_HOOK].lock = True
                else:
                    note_play(V_HOOK, degree_note(h, lead_oct, chord), 100)
            pn = degree_note(chord + arp[step & 3], 0, chord)
            note_play(V_PULSE, pn, 100)
            b = built["bass"][1 if bar == 3 else 0][sb]
            if b is not None:
                if pickup or state["bass_life"] > 0:
                    note_play(V_BASS, degree_note(chord, -1, chord) + b, 100 if (accent >> sb) & 1 else 78)
                else:
                    V[V_BASS].gate = False
            pad_vol = att[2] if menu else int(pad_max * order_pad)
            if pickup:
                pad_vol = 0
            if step % 8 == 0:
                nn = degree_note(chord + pad_deg[0 if (menu or not (step // 8) & 1) else 1], lead_oct - 1, chord)
                V[V_PAD].base = V[V_PAD].freq = note_hz(nn)
            if step % 4 == 0:
                pd = inst[V_PAD]
                V[V_PAD].duty = duty_pct(pd[1] if (step // 4) & 1 else pd[0])
            pad = V[V_PAD]
            pad.gate, pad.atk, pad.dec, pad.sus = True, 16, 0, pad_vol
            if state["drum_level"] >= 1 and not pickup:
                D = built["levels"][min(3, state["drum_level"]) - 1]
                fill = D["fill"] and step >= 60
                if (D["open"][bar] >> sb) & 1:
                    hat(ohz, 10, ovol)
                elif D["hat"][sb]:
                    hat(hhz, 2, D["hat"][sb])
                if (D["kick"][bar] >> sb) & 1:
                    noise_hit(V_DRUM, khz, -400, kvol, kvol // 4 + 1, 4)
                if fill:
                    noise_hit(V_DRUM, shz, 0, 30 + 6 * (step - 60) + (2 if step == 63 else 0), 8, min(6, state["sf"]))
                elif (D["snare"][bar] >> sb) & 1:
                    noise_hit(V_DRUM, shz, 0, svol, 8, min(6, state["sf"]))
                if not fill and (D["ghost"][bar] >> sb) & 1:
                    noise_hit(V_DRUM, khz, -400, gvol, gvol // 4 + 1, 4)
            state["step"] = (step + 1) % 64
            step_count += 1
            if state["step"] == 0:
                state["loop"] += 1
        for i in range(6):
            voice_frame(i)
        yield [("n" if V[i].noise else "s", V[i].freq, V[i].duty, V[i].out) for i in range(6)], V


class Synth:
    """The six voices as square (duty) and noise (15-bit LFSR) generators, summed to mono."""

    def __init__(self):
        self.phase = [0.0] * 6
        self.lfsr = [0x7FFF] * 6
        self.nacc = [0.0] * 6
        self.nval = [1.0] * 6
        self.carry = 0.0

    def frame(self, voices):
        n = int(RATE / FPS + self.carry)
        self.carry = RATE / FPS + self.carry - n
        mix = [0.0] * n if np is None else np.zeros(n)
        for i, (kind, freq, duty, level) in enumerate(voices):
            if level <= 0:
                self.phase[i] = (self.phase[i] + freq * n / RATE) % 1.0
                continue
            amp = level * GAIN
            if kind == "s":
                ph = self.phase[i]
                step = freq / RATE
                if np is not None:
                    p = (ph + step * np.arange(n)) % 1.0
                    mix += np.where(p < duty / 100.0, amp, -amp)
                else:
                    dd = duty / 100.0
                    for k in range(n):
                        mix[k] += amp if ph < dd else -amp
                        ph += step
                        if ph >= 1.0:
                            ph -= 1.0
                self.phase[i] = (ph + step * n) % 1.0 if np is not None else ph
            else:
                clock = min(freq, RATE * 1.0) / RATE * 2.0          # LFSR clocks per output sample
                acc, val, lf = self.nacc[i], self.nval[i], self.lfsr[i]
                for k in range(n):
                    acc += clock
                    while acc >= 1.0:
                        acc -= 1.0
                        bit = (lf ^ (lf >> 1)) & 1
                        lf = (lf >> 1) | (bit << 14)
                        val = 1.0 if (lf & 1) else -1.0
                    mix[k] += amp * val
                self.nacc[i], self.nval[i], self.lfsr[i] = acc, val, lf
        return mix


def write_wav(path, built, song_def, menu):
    syn = Synth()
    chunks = []
    stats = dict(peak_sum=0, frames=0, simul=0, total=0)
    for voices, _V in render_frames(song_def, built, 4, menu):
        lv = [v[3] for v in voices]
        s = sum(lv)
        stats["peak_sum"] = max(stats["peak_sum"], s)
        stats["simul"] = max(stats["simul"], sum(1 for x in lv if x))
        stats["total"] += s
        stats["frames"] += 1
        chunks.append(syn.frame(voices))
    if path:
        if np is not None:
            pcm = np.clip(np.concatenate(chunks), -1.0, 1.0)
            data = (pcm * 32000).astype("<i2").tobytes()
        else:
            a = array.array("h")
            for ch in chunks:
                a.extend(int(max(-1.0, min(1.0, x)) * 32000) for x in ch)
            if sys.byteorder == "big":
                a.byteswap()
            data = a.tobytes()
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(data)
    return stats


# ------------------------------------------------------------------ MIDI (standard library only)
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def midi_for(song_def, built, menu):
    """Format 1: tempo, hook, pulse, bass, pad and drums over one A A B A cycle (the same note choices as render_frames)."""
    d = song_def
    sc = build_songs.SCALES[d["mode"]]
    root, lead_oct = d["root"], d.get("lead_oct", 1)
    harm = d.get("harm", 0)
    pad_deg = d.get("pad_deg", (2, 4))
    arp = d.get("arp", (0, 2, 4, 7))
    TPQ = 480
    TPS = TPQ // 4                                   # one step = a 16th note
    step_s = d["frames"] / 60.0
    us_per_quarter = int(round(step_s * 4 * 1e6))

    def note(deg, octave, chord):
        dd = deg - 1
        o, dd = divmod(dd, 7)
        semi = sc[dd]
        if harm and chord == 5 and dd == 6:
            semi = 11
        return root + semi + 12 * (o + octave)

    tracks = {k: [] for k in ("hook", "pulse", "bass", "pad", "drums")}      # (tick, on, ch, note, vel)
    total = 64 * 4
    for g in range(total):
        loop, step = divmod(g, 64)
        sec = 1 if (loop & 3) == 2 else 0
        hook = built["hookB"] if sec else built["hookA"]
        chords = built["chordsB"] if sec else built["chordsA"]
        bar, sb = (step // 16) % 4, step % 16
        chord = chords[bar]
        t = g * TPS
        h = hook[step]
        if h and h != 99:
            n = note(h, lead_oct, chord)
            end = g + 1
            while end < total and hook[end % 64] == 0 and end - g < 16:
                end += 1
            tracks["hook"].append((t, True, 0, n, 96))
            tracks["hook"].append((end * TPS - 4, False, 0, n, 0))
        if h == 99 and False:
            pass
        pn = note(chord + arp[step & 3], 0, chord)
        tracks["pulse"].append((t, True, 1, pn, 48))
        tracks["pulse"].append((t + TPS - 6, False, 1, pn, 0))
        b = built["bass"][1 if bar == 3 else 0][sb]
        if b is not None:
            n = note(chord, -1, chord) + b
            tracks["bass"].append((t, True, 2, n, 100 if (built["accent"] >> sb) & 1 else 78))
            tracks["bass"].append((t + 2 * TPS - 6, False, 2, n, 0))
        if step % 8 == 0:
            n = note(chord + pad_deg[0 if (menu or not (step // 8) & 1) else 1], lead_oct - 1, chord)
            tracks["pad"].append((t, True, 3, n, 50))
            tracks["pad"].append((t + 8 * TPS - 4, False, 3, n, 0))
        lv = built["levels"][0 if menu else min(2, loop)]
        fill = lv["fill"] and step >= 60
        hv = lv["hat"][sb]
        if (lv["open"][bar] >> sb) & 1:
            tracks["drums"].append((t, True, 9, 46, 70))
            tracks["drums"].append((t + TPS * 2, False, 9, 46, 0))
        elif hv:
            tracks["drums"].append((t, True, 9, 42, min(127, hv * 4)))
            tracks["drums"].append((t + TPS // 2, False, 9, 42, 0))
        if (lv["kick"][bar] >> sb) & 1:
            tracks["drums"].append((t, True, 9, 36, 110))
            tracks["drums"].append((t + TPS // 2, False, 9, 36, 0))
        if fill or (lv["snare"][bar] >> sb) & 1:
            tracks["drums"].append((t, True, 9, 38, 90 if not fill else 50 + 10 * (step - 60)))
            tracks["drums"].append((t + TPS // 2, False, 9, 38, 0))
    chunks = []
    meta = bytes([0x00, 0xFF, 0x51, 0x03]) + us_per_quarter.to_bytes(3, "big")
    meta += bytes([0x00, 0xFF, 0x58, 0x04, 4, 2, 24, 8])
    name = d["name"].encode("latin-1")
    meta += bytes([0x00, 0xFF, 0x03, len(name)]) + name + bytes([0x00, 0xFF, 0x2F, 0x00])
    chunks.append(b"MTrk" + struct.pack(">I", len(meta)) + meta)
    programs = {"hook": 80 if d["inst"][0][1] == 25 else 73, "pulse": 80, "bass": 38, "pad": 89, "drums": 0}
    for k, ev in tracks.items():
        ev.sort(key=lambda e: (e[0], e[1]))              # note offs before ons at the same tick
        ch = ev[0][2] if ev else 0
        body = b""
        if k != "drums":
            body += vlq(0) + bytes([0xC0 | ch, programs[k]])
        last = 0
        for tick, on, c, n, vel in ev:
            body += vlq(tick - last) + bytes([(0x90 if on else 0x80) | c, n & 0x7F, vel & 0x7F])
            last = tick
        body += vlq(0) + bytes([0xFF, 0x2F, 0x00])
        chunks.append(b"MTrk" + struct.pack(">I", len(body)) + body)
    head = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), TPQ)
    return head + b"".join(chunks)


def main():
    args = sys.argv[1:]
    only = args[args.index("--only") + 1].split(",") if "--only" in args else None
    midi_only = "--midi-only" in args
    stats_only = "--stats" in args
    defs = songs_src.SONGS
    built = build_songs.build(defs)
    here_text = build_songs.emit(built)
    out_c = os.path.join(ROOT, "source", "songs.c")
    if not os.path.exists(out_c) or open(out_c, encoding="utf-8").read() != here_text:
        print("warning: source/songs.c is not what songs_src.py generates; run `make songs` (rendering the definitions)", file=sys.stderr)
    if not stats_only:
        os.makedirs(OUT, exist_ok=True)
    total = 0
    for d, b in zip(defs, built):
        if only and d["id"] not in only:
            continue
        menu = d["role"] == "menu"
        wav = None if (midi_only or stats_only) else os.path.join(OUT, d["id"] + ".wav")
        if stats_only or not midi_only:
            st = write_wav(wav, b, d, menu)
            secs = st["frames"] / FPS
            print("%-20s %5.1f s  peak sum %3d  average %3d  voices at once %d%s" % (
                d["name"], secs, st["peak_sum"], st["total"] // max(1, st["frames"]), st["simul"],
                "  -> %s (%.1f MB)" % (os.path.relpath(wav, ROOT), os.path.getsize(wav) / 1e6) if wav else ""))
            if wav:
                total += os.path.getsize(wav)
        if not stats_only:
            mid = os.path.join(OUT, d["id"] + ".mid")
            with open(mid, "wb") as f:
                f.write(midi_for(d, b, menu))
    if total:
        print("WAV total %.1f MB" % (total / 1e6))


if __name__ == "__main__":
    main()
