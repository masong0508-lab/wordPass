#!/usr/bin/env python3
"""WPP2 Cyrillic themed passwords, compatible with WPD4, not the shipped WPP1.

A real source-licensed word list, 40 reviewed themes, correct translations,
balance and DS gameplay are still required before enabling a new locale.

Usage:
  python3 tools/lang/pools_v2.py --selftest build/
  python3 tools/lang/pools_v2.py --build ru dict.wpd4 themes.json out.wpp2
  python3 tools/lang/pools_v2.py --verify dict.wpd4 out.wpp2

themes.json is a UTF-8 map of localized theme names to arrays of pass words.
Words must have 4..9 alphabet symbols, be present in WPD4, and belong to
exactly one theme. No third-party word lists are bundled by this tool.
"""
import json
from pathlib import Path
import struct
import sys
import dict_v4 as v4

MAGIC = b"WPP2"
MIN_LEN, MAX_LEN = 4, 9
MAX_THEMES = 48
MAX_BYTES = 2 * 1024 * 1024
NAME_BYTES = 24
HEADER = 20
THEME_HEADER = NAME_BYTES + 2 * (MAX_LEN - MIN_LEN + 1)

# Intentionally small synthetic test collections, NOT curated 40-theme packs.
FIXTURES = {
    "ru": {
        "КНИГИ": ["КНИГА", "СЛОВО", "УЧЁБА"],
        "МУЗЫКА": ["МУЗЫКА", "ПЕСНЯ"],
        "ПРИРОДА": ["ЗЕМЛЯ", "РЫБА", "ЁЖИК"],
    },
    "uk": {
        "КНИГИ": ["КНИГА", "СЛОВО", "УРОК"],
        "ПРИРОДА": ["СОНЦЕ", "КВІТКА", "ЇЖАК"],
        "МІСТО": ["МІСТО", "ҐАНОК"],
    },
}


def dictionary_info(data, code):
    v4.verify(data)  # strict record lengths, tiers, ordering, metadata
    symbols = v4.alphabet(code)
    on_disk = struct.unpack_from(f"<{len(symbols)}I", data, HEADER)
    if tuple(symbols) != on_disk:
        raise ValueError("dictionary alphabet does not match requested language")
    n = len(symbols)
    counts = struct.unpack_from("<18I", data, HEADER + 4 * n)
    base = HEADER + 4 * n + 18 * 4
    offsets = {}
    for length in range(v4.MIN_LEN, v4.MAX_LEN + 1):
        offsets[length] = base
        base += counts[length - v4.MIN_LEN] * v4.rec_width(length, v4.bits_for(n))
    return symbols, counts, offsets


def dictionary_has(data, ids, alphabet_size, counts, offsets):
    length = len(ids)
    if not v4.MIN_LEN <= length <= v4.MAX_LEN:
        return False
    rec = v4.rec_width(length, v4.bits_for(alphabet_size))
    target = v4.pack(ids, v4.bits_for(alphabet_size))
    lo, hi = 0, counts[length - v4.MIN_LEN] - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        off = offsets[length] + mid * rec
        found = bytes(data[off:off + rec])
        key = found[:-1] + bytes([found[-1] & 0xfe])
        if key == target:
            return True
        if key < target:
            lo = mid + 1
        else:
            hi = mid - 1
    return False


def canonical_name(name):
    name = str(name).strip()
    encoded = name.encode("utf-8")
    if (not name or len(encoded) >= NAME_BYTES or
            any(ord(ch) < 32 or ord(ch) == 127 for ch in name)):
        raise ValueError(f"invalid/overlong theme name: {name!r}")
    return encoded


def build(code, dictionary, themes):
    symbols, counts, offsets = dictionary_info(dictionary, code)
    if not isinstance(themes, dict) or not 1 <= len(themes) <= MAX_THEMES:
        raise ValueError("WPP2 requires 1..48 named themes")
    idx = {cp: i for i, cp in enumerate(symbols)}
    theme_headers, word_payload = bytearray(), bytearray()
    seen_words, seen_names = set(), set()
    for name in sorted(themes, key=lambda s: str(s).encode("utf-8")):
        raw_name = canonical_name(name)
        if raw_name in seen_names:
            raise ValueError("duplicate theme name")
        seen_names.add(raw_name)
        original = themes[name]
        if not isinstance(original, list) or not original:
            raise ValueError(f"{name}: theme has no words")
        words = set()
        for word in original:
            w = v4.clean(str(word), idx)
            if not MIN_LEN <= len(w) <= MAX_LEN:
                raise ValueError(f"{name}: pass must be 4..9 symbols: {word!r}")
            if w in seen_words or w in words:
                raise ValueError(f"word {w} duplicated across/in themes")
            ids = tuple(idx[ord(ch)] for ch in w)
            if not dictionary_has(dictionary, ids, len(symbols), counts, offsets):
                raise ValueError(f"{name}: {w} not in matching WPD4 dictionary")
            words.add(w)
        seen_words |= words
        groups = {length: [] for length in range(MIN_LEN, MAX_LEN + 1)}
        for word in words:
            groups[len(word)].append(tuple(idx[ord(ch)] for ch in word))
        header = bytearray(raw_name)
        header.extend(b"\0" * (NAME_BYTES - len(header)))
        for length in range(MIN_LEN, MAX_LEN + 1):
            group = sorted(groups[length])
            if len(group) > 65535:
                raise ValueError("too many words at one length")
            header.extend(struct.pack("<H", len(group)))
            for ids in group:
                word_payload.extend(bytes(ids))
        theme_headers.extend(header)
    alphabet = struct.pack(f"<{len(symbols)}I", *symbols)
    payload = alphabet + theme_headers + word_payload
    total = HEADER + len(payload)
    if total > MAX_BYTES:
        raise ValueError("pool file exceeds 2MiB")
    hdr = struct.pack("<4sBBBBBBBBII", MAGIC, 1, 0, len(themes), 0,
                      len(symbols), MIN_LEN, MAX_LEN, 0,
                      total, v4.fnv32(payload))
    assert len(hdr) == HEADER
    return hdr + payload


def verify(dictionary, data):
    v4.verify(dictionary)
    if not HEADER <= len(data) <= MAX_BYTES:
        raise ValueError("WPP2 size outside bounds")
    magic, version, flags, nt, zero, ns, low, high, extra, size, checksum = struct.unpack_from(
        "<4sBBBBBBBBII", data)
    if (magic != MAGIC or version != 1 or flags or zero or extra or
            not 1 <= nt <= MAX_THEMES or not 2 <= ns <= 64 or
            low != MIN_LEN or high != MAX_LEN or size != len(data) or
            checksum != v4.fnv32(data[HEADER:])):
        raise ValueError("bad WPP2 metadata/checksum")
    if HEADER + 4 * ns + THEME_HEADER * nt > len(data):
        raise ValueError("truncated WPP2 theme headers")
    dictionary_symbols = struct.unpack_from(f"<{ns}I", dictionary, HEADER)
    file_symbols = struct.unpack_from(f"<{ns}I", data, HEADER)
    if dictionary_symbols != file_symbols:
        raise ValueError("WPP2 alphabet differs from dictionary")
    # The runtime rejects corrupted headers even if an attacker recomputes
    # the inexpensive checksum. Keep the builder equally strict.
    counts = struct.unpack_from("<18I", dictionary, HEADER + 4 * ns)
    db_offsets = {}
    off = HEADER + ns * 4 + 18 * 4
    for length in range(v4.MIN_LEN, v4.MAX_LEN + 1):
        db_offsets[length] = off
        off += counts[length - v4.MIN_LEN] * v4.rec_width(length, v4.bits_for(ns))
    offset = HEADER + ns * 4 + THEME_HEADER * nt
    themes = []
    seen = set()
    for theme in range(nt):
        h = HEADER + ns * 4 + theme * THEME_HEADER
        name_field = data[h:h + NAME_BYTES]
        if b"\0" not in name_field or not name_field[0]:
            raise ValueError("theme name is missing/unterminated")
        name, padding = name_field.split(b"\0", 1)
        if any(padding):
            raise ValueError("garbage in theme name padding")
        try:
            name = canonical_name(name.decode("utf-8")).decode("utf-8")
        except (UnicodeError, ValueError) as exc:
            raise ValueError("invalid UTF-8 theme name") from exc
        if name in seen:
            raise ValueError("duplicate theme name")
        seen.add(name)
        nums = struct.unpack_from("<6H", data, h + NAME_BYTES)
        if not sum(nums):
            raise ValueError("empty theme")
        themes.append((name, nums))
    seen_words = set()
    for name, nums in themes:
        for length, num in enumerate(nums, MIN_LEN):
            if num > (len(data) - offset) // length:
                raise ValueError("truncated WPP2 word run")
            last = None
            for _ in range(num):
                ids = tuple(data[offset:offset + length])
                if any(i >= ns for i in ids):
                    raise ValueError("invalid symbol in pass word")
                if last is not None and ids <= last:
                    raise ValueError("unsorted/duplicated pass-word group")
                if ids in seen_words:
                    raise ValueError("word reused between themes")
                if not dictionary_has(dictionary, ids, ns, counts, db_offsets):
                    raise ValueError("pass is absent from WPD4 dictionary")
                seen_words.add(ids)
                last = ids
                offset += length
    if offset != len(data):
        raise ValueError("unparsed bytes at end of pool file")
    return {"themes": len(themes), "words": len(seen_words), "bytes": len(data)}


def selftest(folder):
    target = Path(folder)
    target.mkdir(parents=True, exist_ok=True)
    for code in ("ru", "uk"):
        words, common = v4.FIXTURES[code]
        dictionary = v4.build(code, words, common)
        pools = build(code, dictionary, FIXTURES[code])
        result = verify(dictionary, pools)
        shuffled = dict(reversed(list(FIXTURES[code].items())))
        assert pools == build(code, dictionary, shuffled)
        assert result["themes"] == 3 and result["words"] == 8
        for at in (0, 6, 20, len(pools) - 1):
            damaged = bytearray(pools)
            damaged[at] ^= 1
            try:
                verify(dictionary, damaged)
                raise AssertionError(f"{code}: corrupted byte {at} passed")
            except ValueError:
                pass
        path = target / f"wpp2_{code}.bin"
        path.write_bytes(pools)
        print(f"WPP2 {code}: {result['themes']} synthetic themes / "
              f"{result['words']} pass words, {result['bytes']} bytes -> {path}")
    print("WPP2 PYTHON OK: deterministic theme builder and strict verification")


def main(args):
    if len(args) == 2 and args[0] == "--selftest":
        selftest(args[1]); return
    if len(args) == 3 and args[0] == "--verify":
        result = verify(Path(args[1]).read_bytes(), Path(args[2]).read_bytes())
        print("WPP2 verified:", result)
        return
    if len(args) == 5 and args[0] == "--build":
        _, code, dictionary, themes_file, output = args
        themes = json.loads(Path(themes_file).read_text(encoding="utf-8"))
        db = Path(dictionary).read_bytes()
        pools = build(code, db, themes)
        verify(db, pools)
        Path(output).write_bytes(pools)
        print(f"WPP2 built {code}: {len(pools)} bytes -> {output}")
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
