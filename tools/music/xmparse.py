"""A small FastTracker II (.xm) reader: header, packed patterns, instruments and samples.

Stdlib only. Used by analyze_xm.py. Reads the file, never writes it.
"""
import struct


class XMError(Exception):
    pass


class Sample:
    __slots__ = ("name", "length", "loop_start", "loop_len", "loop_type", "bits16", "volume",
                 "finetune", "pan", "relnote", "zcr", "peak")


class Instrument:
    __slots__ = ("name", "samples", "keymap")


class Note:
    __slots__ = ("note", "inst", "vol", "fx", "fxp")

    def __init__(self, note=0, inst=0, vol=0, fx=0, fxp=0):
        self.note, self.inst, self.vol, self.fx, self.fxp = note, inst, vol, fx, fxp


class Module:
    pass


def _unpack_pattern(data, rows, channels):
    """Return rows x channels of Note-or-None, expanding the packed format."""
    out = []
    p = 0
    n = len(data)
    for _ in range(rows):
        row = []
        for _c in range(channels):
            if p >= n:
                row.append(None)
                continue
            b = data[p]
            note = inst = vol = fx = fxp = 0
            if b & 0x80:
                p += 1
                if b & 1:
                    note = data[p]; p += 1
                if b & 2:
                    inst = data[p]; p += 1
                if b & 4:
                    vol = data[p]; p += 1
                if b & 8:
                    fx = data[p]; p += 1
                if b & 16:
                    fxp = data[p]; p += 1
            else:
                note, inst, vol, fx, fxp = data[p:p + 5]
                p += 5
            row.append(Note(note, inst, vol, fx, fxp) if (note or inst or vol or fx or fxp) else None)
        out.append(row)
    return out


def _zcr_peak(raw, bits16):
    """Zero-crossing rate and peak of the delta-decoded sample (first 6000 samples)."""
    if bits16:
        n = min(len(raw) // 2, 6000)
        vals = struct.unpack_from("<%dh" % n, raw, 0)
        acc = 0
        pcm = []
        for v in vals:
            acc = (acc + v) & 0xFFFF
            pcm.append(acc - 65536 if acc >= 32768 else acc)
        scale = 32768.0
    else:
        n = min(len(raw), 6000)
        vals = struct.unpack_from("<%db" % n, raw, 0)
        acc = 0
        pcm = []
        for v in vals:
            acc = (acc + v) & 0xFF
            pcm.append(acc - 256 if acc >= 128 else acc)
        scale = 128.0
    if len(pcm) < 2:
        return 0.0, 0.0
    peak = max(abs(x) for x in pcm) / scale
    mean = sum(pcm) / len(pcm)
    zc = 0
    prev = pcm[0] - mean
    for x in pcm[1:]:
        cur = x - mean
        if (cur >= 0) != (prev >= 0):
            zc += 1
        prev = cur
    return zc / len(pcm), peak


def parse(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:17] != b"Extended Module: ":
        raise XMError("not an XM file")
    m = Module()
    m.title = d[17:37].decode("latin-1").strip()
    m.tracker = d[38:58].decode("latin-1").strip()
    m.version = struct.unpack_from("<H", d, 58)[0]
    hdr = 60
    (hsize, m.song_len, m.restart, m.channels, npat, ninst, flags, m.speed, m.bpm) = \
        struct.unpack_from("<IHHHHHHHH", d, hdr)
    m.linear = bool(flags & 1)
    m.order = list(d[hdr + 20: hdr + 20 + m.song_len])
    p = hdr + hsize
    m.patterns = []
    for _ in range(npat):
        plen, packing, rows, psize = struct.unpack_from("<IBHH", d, p)
        if m.version < 0x0104:
            raise XMError("old XM versions are not supported")
        body = d[p + plen: p + plen + psize]
        p += plen + psize
        m.patterns.append((rows, _unpack_pattern(body, rows, m.channels) if psize else
                           [[None] * m.channels for _ in range(rows)]))
    m.instruments = [None]  # 1-based
    for _ in range(ninst):
        isize = struct.unpack_from("<I", d, p)[0]
        ins = Instrument()
        ins.name = d[p + 4:p + 26].decode("latin-1").strip("\0 ").strip()
        nsamp = struct.unpack_from("<H", d, p + 27)[0]
        ins.samples = []
        ins.keymap = [0] * 96
        if nsamp:
            shsize = struct.unpack_from("<I", d, p + 29)[0]
            ins.keymap = list(d[p + 33:p + 33 + 96])
            q = p + isize
            heads = []
            for _s in range(nsamp):
                (ln, ls, ll, vol, fine, typ, pan, rel) = struct.unpack_from("<IIIBbBBb", d, q)
                s = Sample()
                s.name = d[q + 18:q + 40].decode("latin-1").strip("\0 ").strip()
                s.length, s.loop_start, s.loop_len = ln, ls, ll
                s.loop_type = typ & 3
                s.bits16 = bool(typ & 16)
                s.volume, s.finetune, s.pan, s.relnote = vol, fine, pan, rel
                s.zcr = s.peak = 0.0
                heads.append(s)
                q += shsize
            for s in heads:
                raw = d[q:q + s.length]
                s.zcr, s.peak = _zcr_peak(raw, s.bits16) if s.length else (0.0, 0.0)
                if s.bits16:
                    s.length //= 2
                    s.loop_start //= 2
                    s.loop_len //= 2
                q += len(raw)
            ins.samples = heads
            p = q
        else:
            p += isize
        m.instruments.append(ins)
    return m
