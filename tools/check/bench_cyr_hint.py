#!/usr/bin/env python3
"""Compare old and narrowed WPD4 Cyrillic hints with large synthetic data.

60k six-symbol "words" per language are random symbols, NOT real Russian or
Ukrainian words. Checks exact path equivalence and measures host CPU only.
"""
import argparse
import json
from pathlib import Path
import random
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/lang"))
import dict_v4 as d4
import tiles_v2 as t2

BASELINE = "18d5ab0fea67079475e02c9c9a1ddf75b2f6ec0e"
SOURCES = ("source/dict_v4.c", "source/script_board.c")
SHARED = ("source/tiles_v2.c", "source/pools_v2.c")
LOG_RE = re.compile(
    r"language=(ru|uk) total=(\d+) found=(\d+) "
    r"fingerprint=([0-9a-f]{8}) elapsed_ms=([0-9.]+)")


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, text=True,
                          capture_output=True, **kwargs).stdout.strip()


def synth(code, count, dest):
    alphabet = [chr(cp) for cp in d4.alphabet(code)]
    rng = random.Random(0x20161122 if code == "ru" else 0x20261122)
    words = set()
    while len(words) < count:
        words.add("".join(alphabet[rng.randrange(len(alphabet))]
                          for _ in range(6)))
    ordered = sorted(words)
    data = d4.build(code, words, set(ordered[::4]))
    info = d4.verify(data)
    tiles = t2.build(code, words)
    t2.verify(tiles, data)
    if info["words"] != count or info["symbols"] != 33:
        raise AssertionError(f"invalid synthetic corpus for {code}")
    dp, tp = dest / f"{code}.dict", dest / f"{code}.tiles"
    dp.write_bytes(data)
    tp.write_bytes(tiles)
    return dp, tp, len(data)


def compile_program(label, dest, baseline):
    sources = []
    if baseline:
        checkout = dest / "legacy"
        checkout.mkdir(exist_ok=True)
        for path in SOURCES:
            text = run(["git", "show", f"{baseline}:{path}"], cwd=ROOT)
            target = checkout / Path(path).name
            target.write_text(text + "\n", encoding="utf-8")
            sources.append(str(target))
    else:
        sources.extend(str(ROOT / path) for path in SOURCES)
    output = dest / f"bench-{label}"
    run(["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wshadow",
         "-I", str(ROOT / "source"), str(ROOT / "tools/check/hint_span_bench.c"),
         *sources, *(str(ROOT / f) for f in SHARED),
         "-o", str(output)])
    return output


def measure(binary, code, dictionary, tiles, boards, repeats):
    raw = run([str(binary), str(dictionary), str(tiles), code,
               str(boards), str(repeats)])
    match = LOG_RE.fullmatch(raw)
    if not match or match[1] != code:
        raise ValueError(f"unexpected benchmark result: {raw}")
    return {"language": code, "boards_total": int(match[2]),
            "found": int(match[3]), "fingerprint": match[4],
            "elapsed_ms": float(match[5])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default=BASELINE)
    parser.add_argument("--count", type=int, default=60000)
    parser.add_argument("--boards", type=int, default=80)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if not 1000 <= args.count <= 180000:
        parser.error("--count must be 1000..180000")
    if not 1 <= args.boards <= 1000 or not 1 <= args.repeats <= 20:
        parser.error("boards must be 1..1000 and repeats 1..20")
    if not re.fullmatch(r"[0-9a-f]{40}", args.baseline):
        parser.error("baseline must be a 40-character lowercase Git SHA")
    entries = []
    with tempfile.TemporaryDirectory(prefix="wp-cyr-hint-bench-") as td:
        tmp = Path(td)
        old = compile_program("baseline", tmp, args.baseline)
        new = compile_program("optimized", tmp, None)
        for code in ("ru", "uk"):
            dictionary, tiles, size = synth(code, args.count, tmp)
            baseline = measure(old, code, dictionary, tiles,
                               args.boards, args.repeats)
            optimized = measure(new, code, dictionary, tiles,
                                args.boards, args.repeats)
            for key in ("boards_total", "found", "fingerprint"):
                if baseline[key] != optimized[key]:
                    raise AssertionError(f"{code} {key} differs: "
                                         f"{baseline[key]} != {optimized[key]}")
            entries.append({
                "language": code, "synthetic_word_count": args.count,
                "synthetic_dictionary_bytes": size,
                "boards_total": baseline["boards_total"],
                "found": baseline["found"],
                "fingerprint": baseline["fingerprint"],
                "baseline_ms": baseline["elapsed_ms"],
                "optimized_ms": optimized["elapsed_ms"],
                "host_speedup_factor": (baseline["elapsed_ms"] /
                                        max(optimized["elapsed_ms"], 0.001)),
                "equivalent_paths": True,
            })
    report = [
        "# Russian/Ukrainian hint search: host microbenchmark",
        "",
        "Two distinct 33-symbol random 60,000-word six-character corpora.",
        "These are synthetic test fixtures, not real language dictionaries.",
        "Both versions share board seeds, weights, and compiler settings.",
        "",
        f"Baseline source commit: {args.baseline}.",
        f"{args.boards} boards times {args.repeats} repetitions per language.",
        "",
        "| Language | Baseline full-table | Narrowed spans | Host timing ratio | Fingerprint |",
        "| --- | ---: | ---: | ---: | --- |",
    ]
    for item in entries:
        report.append(
            f"| {item['language']} | {item['baseline_ms']:.2f} ms "
            f"| {item['optimized_ms']:.2f} ms "
            f"| {item['host_speedup_factor']:.2f}x "
            f"| {item['fingerprint']} |")
    report += [
        "",
        "These are noisy HOST CPU timings, NOT Nintendo DS hardware FPS/cycles.",
        "The measurement is an equivalence gate, not a fixed speed threshold.",
    ]
    output = "\n".join(report) + "\n"
    print(output)
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "report.md").write_text(output, encoding="utf-8")
        (args.out / "summary.json").write_text(
            json.dumps({"baseline": args.baseline, "data": entries,
                        "method": "host synthetic lexicon, not hardware"},
                       indent=2) + "\n", encoding="utf-8")
    print("CYR HINT SPAN BENCH OK: baseline/optimized paths agree")


if __name__ == "__main__":
    main()
