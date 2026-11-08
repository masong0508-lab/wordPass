#!/usr/bin/env python3
"""Cluster the analysed songs by tempo, mode and groove, and compare with the curated families.

    python3 tools/music/cluster.py [--docs]

Features per song: the feel tempo (log scale), mode brightness (major/lydian/mixolydian bright, dorian in the
middle, minor/phrygian/hijaz dark), the drum groove class, the hat density and the busyness (note onsets per
bar). Deterministic average-linkage agglomerative clustering down to ten clusters; the curated families in
families.py start from this and move a few songs where the genre line of the song says more than the numbers
(for example the two half-time songs, or the music-box ditty that is slow but busy). --docs rewrites the
`families` block of docs/MUSIC.md.
"""
import glob
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import analyze_xm  # noqa: E402
from families import FAMILIES  # noqa: E402

BRIGHT = {"major": 1.0, "lydian": 1.0, "mixolydian": 0.8, "dorian": 0.4, "minor": 0.0, "harmonic minor": 0.0,
          "phrygian": 0.0, "hijaz": 0.0}
GROOVE = {"four on the floor": 0, "backbeat": 1, "broken beat": 2, "half-time": 3, "hats and percussion only": 4}


def load():
    out = []
    for f in sorted(glob.glob(os.path.join(HERE, "analysis", "*.json"))):
        with open(f, encoding="utf-8") as fh:
            r = json.load(fh)
        if r.get("source") == "xm":
            out.append(r)
    return out


def features(r):
    d = r["drums"] or {}
    hat = (d.get("hat", "." * 16)).count("x") / 16.0
    return {"tempo": math.log2(r["feel_bpm"] / 100.0), "bright": BRIGHT.get(r["key"]["mode"], 0.5),
            "groove": GROOVE.get(d.get("class", "hats and percussion only"), 4), "hat": hat,
            "busy": min(1.0, r["energy"]["avg_onsets_per_bar"] / 60.0)}


def dist(a, b):
    return math.sqrt(((a["tempo"] - b["tempo"]) * 2.5) ** 2 + ((a["bright"] - b["bright"]) * 0.8) ** 2 +
                     (0.6 if a["groove"] != b["groove"] else 0.0) ** 2 + ((a["hat"] - b["hat"]) * 0.5) ** 2 +
                     ((a["busy"] - b["busy"]) * 0.5) ** 2)


def cluster(songs, k):
    feats = [features(s) for s in songs]
    groups = [[i] for i in range(len(songs))]
    while len(groups) > k:
        best = None
        for a in range(len(groups)):
            for b in range(a + 1, len(groups)):
                d = sum(dist(feats[i], feats[j]) for i in groups[a] for j in groups[b]) / (len(groups[a]) * len(groups[b]))
                if best is None or d < best[0] - 1e-12:
                    best = (d, a, b)
        _, a, b = best
        groups[a] = groups[a] + groups[b]
        del groups[b]
    return [[songs[i] for i in g] for g in groups]


def describe(group):
    n = len(group)
    tempo = sum(s["feel_bpm"] for s in group) / n
    modes = sorted({s["key"]["mode"] for s in group})
    grooves = sorted({(s["drums"] or {}).get("class", "none") for s in group})
    return "%.0f BPM avg (%.0f..%.0f); %s; %s" % (tempo, min(s["feel_bpm"] for s in group), max(s["feel_bpm"] for s in group),
                                                  "/".join(modes), "/".join(grooves))


def markdown(songs):
    auto = cluster(songs, 10)
    by_id = {s["id"]: s for s in songs}
    lines = ["Automatic clusters (average linkage on tempo, mode brightness, groove class, hat density, busyness):", ""]
    for i, g in enumerate(sorted(auto, key=lambda g: min(s["feel_bpm"] for s in g)), 1):
        lines.append("%d. %s: %s" % (i, ", ".join(s["name"] for s in sorted(g, key=lambda s: s["feel_bpm"])), describe(g)))
    lines += ["", "Curated families (what the game uses; songs_src.py takes its sources from here):", "",
              "| Family | wordPass song | Plays | Fed by | Why these belong together |",
              "|--------|---------------|-------|--------|---------------------------|"]
    where = {"menu": "menu screens", "classic": "Classic shelves", "daily": "Daily Pass", "dark": "Lockdown, FIRE FAST"}
    for f in FAMILIES:
        names = ", ".join(by_id[s]["name"] for s in f["sources"] if s in by_id)
        lines.append("| %s | %s | %s | %s | %s |" % (f["id"], f["song"], where[f["role"]], names, f["why"]))
    covered = sorted(s for f in FAMILIES for s in f["sources"])
    missing = sorted(set(by_id) - set(covered))
    lines += ["", "Every analysed song feeds exactly one family: %d analysed, %d assigned%s." %
              (len(by_id), len(covered), "" if not missing else "; MISSING " + ", ".join(missing))]
    return "\n".join(lines)


def main():
    songs = load()
    text = markdown(songs)
    print(text)
    if "--docs" in sys.argv:
        analyze_xm.patch_docs(text, "families")


if __name__ == "__main__":
    main()
