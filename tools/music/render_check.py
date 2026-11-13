#!/usr/bin/env python3
"""Drift check for docs/music: renders every song into a scratch directory and compares each WAV and MIDI byte for
byte with the committed file. Exit 1 on any difference (run `make music-render` and commit the result).

    python3 tools/music/render_check.py       # also: make music-check
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "docs", "music")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, os.path.join(HERE, "render.py"), "--out", tmp],
                       check=True, stdout=subprocess.DEVNULL)
        fresh = sorted(os.listdir(tmp))
        have = sorted(f for f in os.listdir(OUT) if f.endswith((".wav", ".mid")))
        bad = [f for f in sorted(set(fresh) | set(have))
               if f not in fresh or f not in have
               or open(os.path.join(tmp, f), "rb").read() != open(os.path.join(OUT, f), "rb").read()]
    if bad:
        print("docs/music is stale or incomplete (run `make music-render`): " + ", ".join(bad))
        return 1
    print(f"MUSIC RENDER OK: {len(fresh)} files match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
