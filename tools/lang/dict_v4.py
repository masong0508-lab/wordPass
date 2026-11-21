#!/usr/bin/env python3
"""Build / validate WPD4 Unicode-alphabet dictionaries without replacing WPD3.

WPD4 supports up to 64 symbols, 3..20 Unicode-scalar words and compact
alphabet-indexed bit records with a common/rare tier. Its header includes a
length and FNV-1a checksum for accidental corruption (not authentication).

Commands:
  python3 tools/lang/dict_v4.py --selftest build/
  python3 tools/lang/dict_v4.py --build ru WORDS.txt COMMON.txt OUT.wpd4
  python3 tools/lang/dict_v4.py --build uk WORDS.txt COMMON.txt OUT.wpd4
  python3 tools/lang/dict_v4.py --verify OUT.wpd4

Inputs are UTF-8 words, one per line. No dictionary licenses are bundled by
this builder. The selftest creates miniature synthetic fixtures only.
"""
import pathlib
import struct
import sys
import unicodedata

MAGIC = b"WPD4"
VERSION = 1
MIN_LEN, MAX_LEN = 3, 20
HEADER = 20
MAX_BYTES = 8 * 1024 * 1024
ALPHABETS = {
    "ru": "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ",
    "uk": "АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯ",
}
FIXTURES = {
    "ru": (
        ("ПРИВЕТ", "МИР", "КОТ", "СЛОВО", "КНИГА", "БИБЛИОТЕКА",
         "ЁЖИК", "ЗЕМЛЯ", "ДРУГ", "ПЕСНЯ", "МУЗЫКА", "ГОРОД",
         "ЧАЙ", "РЫБА", "СОН", "УЧЁБА"),
        {"ПРИВЕТ", "МИР", "КОТ", "СЛОВО", "ЁЖИК"},
    ),
    "uk": (
        ("ПРИВІТ", "ЇЖАК", "ҐАНОК", "ЄДНІСТЬ", "СЛОВО", "КНИГА",
         "МОВА", "ДОБРО", "КРАЇНА", "ХЛІБ", "СОНЦЕ", "МІСТО",
         "УРОК", "ДРУГ", "ПІСНЯ", "КВІТКА"),
        {"ПРИВІТ", "ЇЖАК", "СЛОВО", "МОВА", "ДОБРО"},
    ),
}


def alphabet(code):
    if code not in ALPHABETS:
        raise ValueError(f"unsupported alphabet {code!r}: use ru or uk")
    symbols = sorted({ord(ch) for ch in ALPHABETS[code]})
    if len(symbols) != 33 or not 2 <= len(symbols) <= 64:
        raise ValueError("the Cyrillic alphabet must have exactly 33 distinct symbols")
    return symbols


def bits_for(n):
    return (n - 1).bit_length()


def rec_width(n, bits):
    return (n * bits + 1 + 7) // 8


def fnv32(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def clean(word, index):
    s = unicodedata.normalize("NFC", word.strip()).upper()
    if len(s) < MIN_LEN or len(s) > MAX_LEN or any(ord(c) not in index for c in s):
        raise ValueError(f"word outside alphabet or length range: {word!r}")
    return s


def pack(ids, bits, tier=0):
    n = len(ids)
    size = rec_width(n, bits)
    rec = bytearray(size)
    for i, sym in enumerate(ids):
        if sym >= 1 << bits or sym < 0:
            raise ValueError("symbol index not representable")
        for b in range(bits - 1, -1, -1):
            at = i * bits + (bits - 1 - b)
            rec[at // 8] |= ((sym >> b) & 1) << (7 - (at % 8))
    rec[-1] |= tier & 1
    return bytes(rec)


def build(code, words, common):
    a = alphabet(code)
    index = {cp: i for i, cp in enumerate(a)}
    wset = {clean(w, index) for w in words}
    cset = {clean(w, index) for w in common}
    if not wset or not cset <= wset:
        raise ValueError("nonempty dictionary required; common words must be a subset")
    bits = bits_for(len(a))
    groups = {n: [] for n in range(MIN_LEN, MAX_LEN + 1)}
    for word in wset:
        ids = tuple(index[ord(ch)] for ch in word)
        groups[len(word)].append((ids, word, 0 if word in cset else 1))
    payload = bytearray()
    for cp in a:
        payload.extend(struct.pack("<I", cp))
    for n in range(MIN_LEN, MAX_LEN + 1):
        payload.extend(struct.pack("<I", len(groups[n])))
    for n in range(MIN_LEN, MAX_LEN + 1):
        for ids, _, tier in sorted(groups[n]):
            payload.extend(pack(ids, bits, tier))
    total = HEADER + len(payload)
    if total > MAX_BYTES:
        raise ValueError(f"dictionary {total} bytes exceeds {MAX_BYTES} bound")
    hdr = struct.pack("<4sBBBBBBHII", MAGIC, VERSION, 0, len(a),
                      bits, MIN_LEN, MAX_LEN, 0, total, fnv32(payload))
    assert len(hdr) == HEADER
    return hdr + payload


def verify(data):
    if len(data) < HEADER or len(data) > MAX_BYTES:
        raise ValueError("invalid file length")
    magic, ver, flags, n, bits, mini, maxi, reserved, size, checksum = struct.unpack_from(
        "<4sBBBBBBHII", data)
    if (magic != MAGIC or ver != VERSION or flags or reserved or
            not 2 <= n <= 64 or bits != bits_for(n) or
            mini != MIN_LEN or maxi != MAX_LEN or size != len(data) or
            fnv32(data[HEADER:]) != checksum):
        raise ValueError("invalid WPD4 header or checksum")
    off = HEADER
    a = list(struct.unpack_from(f"<{n}I", data, off))
    if a != sorted(set(a)) or any(cp <= 0x20 or cp > 0x10FFFF or 0xD800 <= cp <= 0xDFFF for cp in a):
        raise ValueError("invalid alphabet")
    off += 4 * n
    counts = struct.unpack_from("<18I", data, off)
    off += 18 * 4
    words = 0
    for length, count in enumerate(counts, MIN_LEN):
        width = rec_width(length, bits)
        for _ in range(count):
            if off + width > len(data):
                raise ValueError("truncated record")
            value = int.from_bytes(data[off:off+width], "big")
            padding = width * 8 - length * bits - 1
            if (value >> 1) & ((1 << padding) - 1):
                raise ValueError("nonzero padding bits")
            ids = tuple((value >> (width * 8 - (j + 1) * bits)) & ((1 << bits) - 1)
                        for j in range(length))
            if max(ids) >= n:
                raise ValueError("symbol index outside alphabet")
            # Validate strict group ordering; duplicate words would corrupt tier lookup.
            if _ > 0 and ids <= previous:
                raise ValueError("out-of-order or duplicate word")
            previous = ids
            off += width
            words += 1
    if off != len(data) or not words:
        raise ValueError("trailing data or empty dictionary")
    return {"symbols": n, "words": words, "counts": counts, "bytes": len(data)}


def read_words(file):
    return pathlib.Path(file).read_text(encoding="utf-8").splitlines()


def selftest(dest):
    path = pathlib.Path(dest)
    path.mkdir(parents=True, exist_ok=True)
    for code in ("ru", "uk"):
        words, common = FIXTURES[code]
        data = build(code, words, common)
        result = verify(data)
        assert data == build(code, reversed(words), reversed(sorted(common)))
        assert result["words"] == len(set(words)) and result["symbols"] == 33
        # Accidental changes in header, alphabet and records must fail integrity.
        for where in (0, 6, HEADER + 2, len(data) - 1):
            broken = bytearray(data)
            broken[where] ^= 1
            try:
                verify(broken)
                raise AssertionError(f"{code}: accepted a mutated byte {where}")
            except ValueError:
                pass
        out = path / f"wpd4_{code}.bin"
        out.write_bytes(data)
        print(f"WPD4 {code}: {result['words']} synthetic words, "
              f"{result['symbols']} symbols, {len(data)} bytes -> {out}")
    print("WPD4 PYTHON OK: deterministic, checksum, alphabet and record validation")


def main(argv):
    if argv and argv[0] == "--selftest" and len(argv) == 2:
        selftest(argv[1])
        return
    if argv and argv[0] == "--verify" and len(argv) == 2:
        print("WPD4 verified:", verify(pathlib.Path(argv[1]).read_bytes()))
        return
    if argv and argv[0] == "--build" and len(argv) == 5:
        _, code, words, common, out = argv
        data = build(code, read_words(words), read_words(common))
        verify(data)
        pathlib.Path(out).write_bytes(data)
        print(f"WPD4 built {code}: {len(data)} bytes -> {out}")
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
