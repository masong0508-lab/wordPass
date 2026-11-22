#!/usr/bin/env python3
"""Compare search CPU work and exact results against a Git revision.

Uses identical current language data and support modules for both binaries;
only source/board.c and source/dict.c come from the requested baseline. Timings
are host microbenchmarks, not Nintendo DS frame rates. No checkout is modified.
  python3 tools/check/benchmark_search.py --baseline COMMIT [--samples 100]
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = ['tools/check/stubs.c', 'source/pools.c', 'source/lang.c', 'source/langload.c',
           'source/music.c', 'source/songs.c', 'source/ui.c', 'source/ui_strings.c']


def run(command):
    return subprocess.run(command, cwd=ROOT, check=True, capture_output=True,
                          text=True, timeout=180).stdout


def build_and_measure(folder, label, board, dictionary, samples):
    # Copy beside the current header so quoted game.h includes are identical
    # across implementations; this comparison deliberately isolates search.
    target = folder / label
    target.mkdir()
    (target / 'board.c').write_text(board, encoding='utf-8')
    (target / 'dict.c').write_text(dictionary, encoding='utf-8')
    executable = target / 'bench'
    command = ['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Wshadow',
               '-I', str(ROOT / 'tools/check/stub'), '-I', str(ROOT / 'source'),
               str(ROOT / 'tools/check/search_bench.c'), str(target / 'board.c'),
               str(target / 'dict.c'), *[str(ROOT / path) for path in SUPPORT],
               '-Wl,--wrap=dictLookup', '-o', str(executable)]
    if 'dictHasPrefix(' in dictionary:
        command += ['-DSEARCH_PREFIX', '-Wl,--wrap=dictHasPrefix']
    run(command)
    output = run([str(executable), str(samples)])
    results = [line for line in output.splitlines() if line.startswith('RESULT ')]
    measures = {}
    for line in output.splitlines():
        if not line.startswith('MEASURE '):
            continue
        _, language, case, cases, elapsed, full, prefix = line.split()
        measures[(language, case)] = {'cases': int(cases), 'host_ms': float(elapsed),
                                     'full_lookups': int(full), 'prefix_queries': int(prefix)}
    return results, measures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--samples', type=int, default=100)
    parser.add_argument('--output', type=Path, help='optional full JSON report')
    args = parser.parse_args()
    if not 1 <= args.samples <= 1000:
        parser.error('--samples must be 1..1000')
    sha = run(['git', 'rev-parse', '--verify', '--end-of-options', args.baseline + '^{commit}']).strip()
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise RuntimeError('baseline did not resolve to a commit')
    old_board = run(['git', 'show', sha + ':source/board.c'])
    old_dict = run(['git', 'show', sha + ':source/dict.c'])
    with tempfile.TemporaryDirectory(prefix='wordpass-search-') as directory:
        folder = Path(directory)
        before, old = build_and_measure(folder, 'baseline', old_board, old_dict, args.samples)
        after, new = build_and_measure(folder, 'current', (ROOT / 'source/board.c').read_text(),
                                      (ROOT / 'source/dict.c').read_text(), args.samples)
    if before != after:
        for index, pair in enumerate(zip(before, after)):
            if pair[0] != pair[1]:
                raise RuntimeError(f'search result changed at case {index}:\n{pair[0]}\n{pair[1]}')
        raise RuntimeError(f'case count changed: baseline {len(before)}, current {len(after)}')
    report = {'baseline': sha, 'exact_result_cases': len(before), 'parity': True,
              'timing_scope': 'host microbenchmark; not DS FPS',
              'comparisons': [{'language': key[0], 'case': key[1], 'before': old[key], 'after': new[key]}
                              for key in old]}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'SEARCH PARITY OK: {len(before)} exact results; board and RNG unchanged')
    for label in ('seeded', 'all-x', 'all-q'):
        keys = [key for key in old if key[1].startswith(label)]
        a, b = sum(old[key]['host_ms'] for key in keys), sum(new[key]['host_ms'] for key in keys)
        full_a, full_b = sum(old[key]['full_lookups'] for key in keys), sum(new[key]['full_lookups'] for key in keys)
        prefix_b = sum(new[key]['prefix_queries'] for key in keys)
        print(f'{label}: host {a:.3f} -> {b:.3f} ms; full lookups {full_a} -> {full_b}; new prefix queries {prefix_b}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
