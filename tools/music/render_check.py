#!/usr/bin/env python3
"""Re-render the wordPass soundtrack previews and detect drift in docs/music.

Checks each committed WAV and MIDI against the current sequencer. Comparisons
stream from disk rather than reading all 13 MB into memory. Fix with
`make music-render`, and commit the changed previews.
"""
import filecmp
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "docs", "music")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, os.path.join(HERE, "render.py"), "--out", tmp],
                       check=True, stdout=subprocess.DEVNULL)
        fresh = set(f for f in os.listdir(tmp) if f.endswith((".wav", ".mid")))
        have = set(f for f in os.listdir(OUT) if f.endswith((".wav", ".mid")))
        missing = sorted(fresh - have)
        extra = sorted(have - fresh)
        stale = sorted(f for f in fresh & have
                       if not filecmp.cmp(os.path.join(tmp, f), os.path.join(OUT, f), shallow=False))

    if missing or extra or stale:
        print("docs/music is out of sync with the song renderer:")
        if missing:
            print("  missing: " + ", ".join(missing))
        if extra:
            print("  unexpected: " + ", ".join(extra))
        if stale:
            print("  outdated: " + ", ".join(stale))
        print("Regenerate with `make music-render` and commit the changed previews.")
        return 1
    print(f"MUSIC RENDER OK: {len(fresh)} files match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
