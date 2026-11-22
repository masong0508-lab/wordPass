#!/usr/bin/env python3
"""Reproducible bounded competent/weak-player balance sweep.

Uses the existing bot's policies and real rules. When --baseline is supplied,
only board.c/dict.c are replaced: both builds use the same current bot, packs
and support code. Results describe this player model, not human clear rates.
"""
import argparse
import csv
import io
import json
from pathlib import Path
import re
import tempfile
from benchmark_search import ROOT, SUPPORT, run


def compile_sweep(folder, label, board, dictionary):
    target = folder / label
    target.mkdir()
    (target / 'board.c').write_text(board, encoding='utf-8')
    (target / 'dict.c').write_text(dictionary, encoding='utf-8')
    binary = target / 'sweep'
    run(['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Wshadow',
         '-I', str(ROOT / 'tools/check/stub'), '-I', str(ROOT / 'source'),
         str(ROOT / 'tools/check/balance_sweep.c'), str(target / 'board.c'),
         str(target / 'dict.c'), *[str(ROOT / path) for path in SUPPORT],
         '-lm', '-o', str(binary)])
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--languages', nargs='+', choices='en es fr de it pt nl sv pl da fi'.split(),
                        default=['en', 'de', 'fi'])
    parser.add_argument('--first', type=int, default=12)
    parser.add_argument('--last', type=int, default=17)
    parser.add_argument('--attempts', type=int, default=200)
    parser.add_argument('--seed-base', type=int, default=1000)
    parser.add_argument('--baseline', help='optional Git revision for a paired comparison')
    parser.add_argument('--require-identical', action='store_true', help='fail if paired per-shelf reports differ')
    parser.add_argument('--output', type=Path, help='save CSV reports and the JSON summary in this directory')
    args = parser.parse_args()
    if not 1 <= args.first <= args.last <= 39 or not 1 <= args.attempts <= 1000:
        parser.error('require shelves 1..39 in order and attempts 1..1000')
    if not 0 <= args.seed_base <= 0xffffffff:
        parser.error('--seed-base must fit an unsigned 32-bit value')
    if args.require_identical and not args.baseline:
        parser.error('--require-identical needs --baseline')
    implementations = {'current': ((ROOT / 'source/board.c').read_text(), (ROOT / 'source/dict.c').read_text())}
    sha = None
    if args.baseline:
        sha = run(['git', 'rev-parse', '--verify', '--end-of-options', args.baseline + '^{commit}']).strip()
        if not re.fullmatch(r'[0-9a-f]{40}', sha):
            raise RuntimeError('baseline did not resolve to a commit')
        implementations['baseline'] = (run(['git', 'show', sha + ':source/board.c']),
                                       run(['git', 'show', sha + ':source/dict.c']))
    reports = {}
    with tempfile.TemporaryDirectory(prefix='wordpass-balance-') as directory:
        for label, sources in implementations.items():
            binary = compile_sweep(Path(directory), label, *sources)
            for language in args.languages:
                reports[(label, language)] = run([str(binary), language, str(args.first),
                                                 str(args.last), str(args.attempts), str(args.seed_base)])
    identical = all(reports[('current', lang)] == reports[('baseline', lang)]
                    for lang in args.languages) if sha else None
    summary = {'baseline': sha, 'languages': args.languages, 'first_shelf': args.first,
               'last_shelf': args.last, 'attempts_per_player_shelf': args.attempts,
               'attempts_per_implementation': len(args.languages) * (args.last - args.first + 1) * args.attempts * 2,
               'board_seed': f'{args.seed_base} + shelf*100003 + attempt*7919 (uint32)',
               'bot_seed': 'board_seed ^ 0xABCDEF', 'per_shelf_reports_identical': identical,
               'scope': 'fixed bot policies and current data; no human-player inference',
               'current': {lang: list(csv.DictReader(io.StringIO(reports[('current', lang)]))) for lang in args.languages}}
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        for (label, language), text in reports.items():
            (args.output / f'{label}-{language}.csv').write_text(text, encoding='utf-8')
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    else:
        print(json.dumps(summary, indent=2))
    print(f'BALANCE SWEEP: {summary["attempts_per_implementation"]} attempts per implementation; paired reports identical={identical}')
    return 1 if args.require_identical and not identical else 0


if __name__ == '__main__':
    raise SystemExit(main())
