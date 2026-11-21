#!/usr/bin/env python3
"""WPL2: alphabet-independent tile distributions for experimental WPD4 packs.

Make only native regression fixtures unless the caller supplies and licenses a
real dictionary. This does NOT add languages to the ROM's options or board.
  python3 tools/lang/tiles_v2.py --selftest build/
  python3 tools/lang/tiles_v2.py --build ru WORDS_UTF8.txt OUT.wpl2
  python3 tools/lang/tiles_v2.py --verify OUT.wpl2
"""
import collections
import pathlib
import struct
import sys
import dict_v4 as v4

MAGIC = b"WPL2"
VOWELS = {"ru": "АЕЁИОУЫЭЮЯ", "uk": "АЕЄИІЇОУЮЯ"}
SIZE = 100


def build(code, words):
    cps = v4.alphabet(code)
    index = {cp: i for i, cp in enumerate(cps)}
    words = {v4.clean(word, index) for word in words}
    if not words:
        raise ValueError("a dictionary with no words cannot generate a bag")
    freq = collections.Counter(ord(c) for word in words for c in word)
    present = [i for i, cp in enumerate(cps) if freq[cp]]
    if len(present) > SIZE:
        raise ValueError("too many distinct symbols to give each a tile")
    vowels = {ord(ch) for ch in VOWELS[code]}
    weights = [int(i in present) for i in range(len(cps))]
    ideal = [freq[cp] * SIZE / sum(freq.values()) for cp in cps]
    # Distribute the remaining 100 - distinct letters deterministically,
    # maintaining at least one tile for each supported observed symbol and
    # capping an individual letter at 25 tiles.
    for _ in range(SIZE - len(present)):
        candidates = [i for i in present if weights[i] < 25]
        if not candidates:
            raise ValueError("cannot apportion exactly 100 weights")
        winner = max(candidates, key=lambda i: (ideal[i] - weights[i], -i))
        weights[winner] += 1
    # Choose up to two rare, observed non-vowels that have exactly one tile.
    rare = sorted((i for i in present if cps[i] not in vowels and weights[i] == 1),
                  key=lambda i: (freq[cps[i]], i))[:2]
    vowel_mask = sum(1 << i for i, cp in enumerate(cps)
                     if cp in vowels and weights[i] > 0)
    vowel_share = sum(w for i, w in enumerate(weights) if vowel_mask & (1 << i))
    if vowel_share < 10:
        raise ValueError("vowel share too low for the new board")
    values = [0 if not freq[cp] else max(1, min(10, round(11 - 10 * freq[cp] / max(freq.values()))))
              for cp in cps]
    payload = (
        struct.pack(f"<{len(cps)}I", *cps)
        + struct.pack("<Q", vowel_mask)
        + bytes(rare + [255] * (4 - len(rare)))
        + bytes(weights) + bytes(values)
    )
    total = 20 + len(payload)
    assert total == 32 + 6 * len(cps)
    header = struct.pack("<4sBBBBHHII", MAGIC, 1, 0, len(cps),
                         len(rare), SIZE, 0, total, v4.fnv32(payload))
    assert len(header) == 20
    return header + payload


def verify(data, dictionary=None):
    if len(data) < 32:
        raise ValueError("short tile table")
    magic, ver, flags, n, rares, bag, reserved, size, checksum = struct.unpack_from(
        "<4sBBBBHHII", data)
    if (magic != MAGIC or ver != 1 or flags or not 2 <= n <= 64 or rares > 4
            or bag != SIZE or reserved or size != len(data) or
            len(data) != 32 + 6 * n or v4.fnv32(data[20:]) != checksum):
        raise ValueError("invalid WPL2 header or checksum")
    cps = struct.unpack_from(f"<{n}I", data, 20)
    if sorted(set(cps)) != list(cps):
        raise ValueError("alphabet must be sorted and unique")
    if dictionary is not None:
        info = v4.verify(dictionary)
        if info["symbols"] != n:
            raise ValueError("alphabet size differs from WPD4")
        dcps = struct.unpack_from(f"<{n}I", dictionary, 20)
        if cps != dcps:
            raise ValueError("different WPD4 and WPL2 alphabets")
    off = 20 + n * 4
    mask, = struct.unpack_from("<Q", data, off)
    off += 8
    if n < 64 and mask >> n:
        raise ValueError("vowel bits beyond alphabet")
    rare = data[off:off + 4]
    off += 4
    weights = list(data[off:off + n])
    values = list(data[off + n:off + 2*n])
    if (any(x >= n for x in rare[:rares]) or
            rare[rares:] != bytes([255] * (4 - rares)) or
            len(set(rare[:rares])) != rares or
            any(weights[x] != 1 for x in rare[:rares]) or
            sum(weights) != SIZE or any(w > 25 for w in weights) or
            any((v < 1 or v > 10) if w else bool(v)
                for w, v in zip(weights, values)) or
            sum(w for i, w in enumerate(weights) if mask & (1 << i)) < 10):
        raise ValueError("invalid tile distribution")
    return {"symbols": n, "rares": rares, "vowels": bin(mask).count("1"),
            "bytes": len(data), "weights": weights}


def selftest(out_dir):
    dest = pathlib.Path(out_dir)
    dest.mkdir(parents=True, exist_ok=True)
    for code in ("ru", "uk"):
        words, common = v4.FIXTURES[code]
        dict_data = v4.build(code, words, common)
        data = build(code, words)
        info = verify(data, dict_data)
        assert data == build(code, reversed(words))
        assert info["symbols"] == 33 and len(data) == 230
        for at in (0, 6, 20, len(data)-1):
            broken = bytearray(data)
            broken[at] ^= 1
            try:
                verify(broken)
                raise AssertionError(f"{code}: accepted corrupted WPL2 byte {at}")
            except ValueError:
                pass
        file = dest / f"wpl2_{code}.bin"
        file.write_bytes(data)
        print(f"WPL2 {code}: {info['symbols']} symbols, "
              f"{info['rares']} rares, {info['vowels']} vowel symbols -> {file}")
    print("WPL2 PYTHON OK: deterministic Cyrillic tile tables and checksum")


def main(a):
    if len(a) == 2 and a[0] == "--selftest":
        selftest(a[1]); return
    if len(a) == 2 and a[0] == "--verify":
        print("WPL2 verified:", verify(pathlib.Path(a[1]).read_bytes())); return
    if len(a) == 4 and a[0] == "--build":
        _, code, words, output = a
        data = build(code, v4.read_words(words))
        verify(data)
        pathlib.Path(output).write_bytes(data)
        print(f"WPL2 built {code}: {len(data)} bytes -> {output}")
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
