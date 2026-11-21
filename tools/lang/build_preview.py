#!/usr/bin/env python3
"""Create two SMALL experimental Cyrillic NitroFS language packs for the DS.

Only intended for an opt-in engineering preview ROM. Contains synthetic,
tiny, explicitly non-production dictionary/theme fixtures; cannot pass the
normal locale readiness check and never appears in the language selector.
"""
from pathlib import Path
import sys
import dict_v4 as dict4
import tiles_v2 as tiles2
import pools_v2 as pools2

def build(folder):
    root = Path(folder)
    for code in ("ru", "uk"):
        target = root / code
        target.mkdir(parents=True, exist_ok=True)
        words, common = dict4.FIXTURES[code]
        dictionary = dict4.build(code, words, common)
        tiles = tiles2.build(code, words)
        pools = pools2.build(code, dictionary, pools2.FIXTURES[code])
        assert dict4.verify(dictionary)["words"] == len(set(words))
        tiles2.verify(tiles, dictionary)
        assert pools2.verify(dictionary, pools)["themes"] == 3
        for name, contents in (("dict.bin", dictionary),
                               ("tiles.bin", tiles), ("pools.bin", pools)):
            (target / name).write_bytes(contents)
        print(f"{code} preview: {len(words)} demo words, 3 themes, "
              f"{len(dictionary)+len(tiles)+len(pools)} bytes total")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 tools/lang/build_preview.py nitrofiles/cyrpreview")
    build(sys.argv[1])
