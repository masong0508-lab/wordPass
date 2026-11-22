#!/usr/bin/env python3
"""Guard linked DS resource budgets; this is not a free-RAM or FPS measurement.

The linker heap span minus the largest active resource pack is a static upper
bound on the space left for other allocations. Allocator metadata, NitroFS/FAT,
the filename table, stdio and other runtime allocations still need that space.
Retail and Cyrillic packs are exclusive: switching modes frees the previous
pack, so the preview build budgets their maximum rather than their sum.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
RETAIL_CODES = tuple('es fr de it pt nl sv pl da fi'.split())
PREVIEW_CODES = ('ru', 'uk')
ROM_LIMIT = 18 * 1024 * 1024
MIN_RESERVE = 64 * 1024
ITCM_BASE, ITCM_LIMIT = 0x01000000, 0x01008000
DTCM_BASE, DTCM_LIMIT = 0x02ff0000, 0x02ff4000
FLOAT_HELPERS = ('_dtoa_r', '_svfprintf_r', '_vfprintf_r', 'vfprintf')
SCRIPT_PREFIXES = ('scriptPreview', 'v4Dict', 'scriptBoard')
WPD3_WIDTHS = (4, 4, 4, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13)


class BudgetInputError(ValueError):
    """An input cannot produce a trustworthy budget report."""


def parse_nm(output):
    """Read GNU nm's default address/type/name format, ignoring undefineds."""
    symbols = {}
    global_names = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 3 or not re.fullmatch(r'[0-9a-fA-F]+', fields[0]):
            continue
        if len(fields[1]) != 1 or fields[1] in ('U', 'u'):
            continue
        address = int(fields[0], 16)
        name = fields[2]
        if name.startswith('$'):  # ARM mapping symbols repeat by design.
            continue
        is_global = fields[1].isupper()
        if is_global and name in global_names and symbols[name] != address:
            raise BudgetInputError(f'conflicting addresses for ELF symbol {name}')
        if is_global or name not in symbols:
            symbols[name] = address
        if is_global:
            global_names.add(name)
    if not symbols:
        raise BudgetInputError('nm returned no defined ELF symbols')
    return symbols


def read_symbols(elf, nm=None):
    elf = Path(elf).resolve()
    try:
        with elf.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                raise BudgetInputError(f'not an ELF file: {elf}')
    except OSError as exc:
        raise BudgetInputError(f'cannot read ELF {elf}: {exc}') from exc
    candidates = [nm] if nm else [shutil.which('arm-none-eabi-nm'), shutil.which('nm')]
    failures = []
    for executable in dict.fromkeys(tool for tool in candidates if tool):
        try:
            result = subprocess.run([executable, '--defined-only', str(elf)],
                                    capture_output=True, text=True, timeout=30,
                                    check=False)
            if result.returncode:
                failures.append(f'{executable}: {result.stderr.strip() or "exit " + str(result.returncode)}')
                continue
            return parse_nm(result.stdout)
        except (OSError, subprocess.TimeoutExpired, BudgetInputError) as exc:
            failures.append(f'{executable}: {exc}')
    raise BudgetInputError('cannot read ELF symbols: ' + ('; '.join(failures) or 'install arm-none-eabi-nm or GNU nm'))


def _resource(path, magic, minimum, maximum):
    try:
        size = path.stat().st_size
        if not path.is_file() or not minimum <= size <= maximum:
            raise BudgetInputError(f'{path}: size {size} outside {minimum}..{maximum} bytes')
        data = path.read_bytes()
    except OSError as exc:
        raise BudgetInputError(f'missing/unreadable pack resource {path}: {exc}') from exc
    if len(data) != size or data[:4] != magic:
        raise BudgetInputError(f'{path}: invalid {magic.decode()} resource or changed during read')
    return data


def _retail_pack(folder):
    dictionary = _resource(folder / 'dict.bin', b'WPD3', 76, 2 << 20)
    pools = _resource(folder / 'pools.bin', b'WPP1', 808, 256 << 10)
    letters = _resource(folder / 'letters.bin', b'WPL1', 68, 68)
    counts = struct.unpack_from('<18I', dictionary, 4)
    # Use the format's declared widths, including legacy padding, matching
    # source/dict.c and tools/gen_dict.py rather than recomputing tight packing.
    expected = 76 + sum(count * width for count, width in zip(counts, WPD3_WIDTHS))
    if not sum(counts) or expected != len(dictionary):
        raise BudgetInputError(f'{folder}/dict.bin: invalid WPD3 record lengths')
    themes, = struct.unpack_from('<I', pools, 4)
    if themes != 40:
        raise BudgetInputError(f'{folder}/pools.bin: expected all 40 retail themes')
    expected = 808
    for theme in range(themes):
        counts = struct.unpack_from('<6H', pools, 8 + theme * 20 + 8)
        if not sum(counts):
            raise BudgetInputError(f'{folder}/pools.bin: empty theme {theme}')
        expected += sum(length * count for length, count in zip(range(4, 10), counts))
    if expected != len(pools):
        raise BudgetInputError(f'{folder}/pools.bin: invalid WPP1 record lengths')
    if letters[4] > 1 or letters[5] or sum(letters[16:42]) != 100:
        raise BudgetInputError(f'{folder}/letters.bin: invalid WPL1 metadata/weights')
    return {'dict.bin': len(dictionary), 'pools.bin': len(pools), 'letters.bin': len(letters)}


def _preview_pack(folder):
    dictionary = _resource(folder / 'dict.bin', b'WPD4', 20, 2 << 20)
    tiles = _resource(folder / 'tiles.bin', b'WPL2', 32, 512)
    pools = _resource(folder / 'pools.bin', b'WPP2', 20, 256 << 10)
    # Reuse the canonical strict checksum/alphabet/record verifiers; a truncated
    # or mismatched preview pack must not appear to leave extra heap available.
    sys.path.insert(0, str(ROOT / 'tools' / 'lang'))
    import dict_v4
    import tiles_v2
    import pools_v2
    try:
        dict_v4.verify(dictionary)
        tiles_v2.verify(tiles, dictionary)
        pools_v2.verify(dictionary, pools)
    except (ValueError, struct.error) as exc:
        raise BudgetInputError(f'{folder}: invalid preview pack: {exc}') from exc
    return {'dict.bin': len(dictionary), 'tiles.bin': len(tiles), 'pools.bin': len(pools)}


def scan_packs(root, mode):
    root = Path(root)
    expected_codes = RETAIL_CODES if mode == 'retail' else PREVIEW_CODES
    if not root.is_dir():
        raise BudgetInputError(f'missing {mode} pack directory: {root}')
    missing = [code for code in expected_codes if not (root / code).is_dir()]
    if missing:
        raise BudgetInputError(f'{root}: missing {mode} packs: {", ".join(missing)}')
    packs = []
    for folder in sorted(path for path in root.iterdir() if path.is_dir()):
        sizes = _retail_pack(folder) if mode == 'retail' else _preview_pack(folder)
        payload = sum(sizes.values())
        # Raw cartridge reads reserve four extra bytes for each retail buffer.
        # Other allocation metadata is deliberately not represented as free RAM.
        padding = 12 if mode == 'retail' else 0
        packs.append({'language': folder.name, 'files': sizes,
                      'payload_bytes': payload, 'loader_padding_bytes': padding,
                      'allocation_bytes': payload + padding})
    return packs


def evaluate(symbols, rom_bytes, mode, retail_packs, preview_packs=None,
             forbid_float_printf=False):
    required = ('__end__', '__eheap_end', '__itcm_start', '__itcm_end', '__dtcm_start', '__dtcm_top')
    missing = [name for name in required if name not in symbols]
    sbss_end = symbols.get('__sbss_end', symbols.get('__sbss_end__'))
    if sbss_end is None:
        missing.append('__sbss_end (or __sbss_end__)')
    if missing:
        raise BudgetInputError('missing linker budget symbols: ' + ', '.join(missing))
    if mode not in ('retail', 'preview') or not retail_packs or (mode == 'preview' and not preview_packs):
        raise BudgetInputError('budget requires the selected mode and its complete resource pack list')
    violations = []
    if not 512 <= rom_bytes <= ROM_LIMIT:
        violations.append(f'ROM has {rom_bytes} bytes; required range is 512..{ROM_LIMIT} bytes')
    heap_start, heap_end = symbols['__end__'], symbols['__eheap_end']
    if not 0x02000000 <= heap_start <= heap_end <= 0x02400000:
        violations.append('linker heap lies outside ordered original-DS main RAM bounds')
    heap_bytes = heap_end - heap_start
    itcm_start, itcm_end = symbols['__itcm_start'], symbols['__itcm_end']
    if not ITCM_BASE <= itcm_start <= itcm_end <= ITCM_LIMIT:
        violations.append(f'ITCM exceeds 32 KiB address boundary 0x{ITCM_LIMIT:08x}')
    dtcm_start, dtcm_top = symbols['__dtcm_start'], symbols['__dtcm_top']
    if not DTCM_BASE <= dtcm_start <= sbss_end <= dtcm_top <= DTCM_LIMIT:
        violations.append(f'DTCM static data exceeds its 16 KiB address boundary 0x{DTCM_LIMIT:08x}')
    largest_retail = max(retail_packs, key=lambda pack: pack['allocation_bytes'])
    active = [('retail', largest_retail)]
    largest_preview = None
    if mode == 'preview':
        largest_preview = max(preview_packs, key=lambda pack: pack['allocation_bytes'])
        active.append(('preview', largest_preview))
    for label, pack in active:
        reserve = heap_bytes - pack['allocation_bytes']
        if reserve < MIN_RESERVE:
            violations.append(f'{label} pack {pack["language"]}: static reserve {reserve} bytes is below {MIN_RESERVE}; reduce linked data/code or pack size')
    worst_mode, worst_pack = max(active, key=lambda item: item[1]['allocation_bytes'])
    script_symbols = sorted(name for name in symbols if name.startswith(SCRIPT_PREFIXES))
    if mode == 'retail' and script_symbols:
        violations.append('retail ROM includes preview code/state: ' + ', '.join(script_symbols))
    float_helpers = sorted(name for name in FLOAT_HELPERS if name in symbols)
    if forbid_float_printf and float_helpers:
        violations.append('full floating printf helpers linked: ' + ', '.join(float_helpers) + '; use integer formatting for DS UI')
    return {
        'status': 'fail' if violations else 'pass', 'mode': mode,
        'rom_bytes': rom_bytes, 'rom_limit_bytes': ROM_LIMIT,
        'heap_start': f'0x{heap_start:08x}', 'heap_end': f'0x{heap_end:08x}',
        'linker_heap_span_bytes': heap_bytes, 'minimum_static_reserve_bytes': MIN_RESERVE,
        'largest_retail_pack': largest_retail, 'largest_preview_pack': largest_preview,
        'worst_active_pack_mode': worst_mode,
        'largest_active_pack_allocation_bytes': worst_pack['allocation_bytes'],
        'remaining_before_other_allocations_bytes': heap_bytes - worst_pack['allocation_bytes'],
        'itcm_used_bytes': itcm_end - itcm_start,
        'itcm_end': f'0x{itcm_end:08x}', 'itcm_limit': f'0x{ITCM_LIMIT:08x}',
        'dtcm_static_bytes': sbss_end - dtcm_start,
        'dtcm_static_end': f'0x{sbss_end:08x}', 'dtcm_top': f'0x{dtcm_top:08x}',
        'dtcm_remaining_before_stack_bytes': dtcm_top - sbss_end,
        'preview_symbols': script_symbols, 'float_printf_helpers': float_helpers,
        'retail_packs': retail_packs, 'preview_packs': preview_packs or [],
        'assumptions': [
            'One retail or one preview pack is allocated at a time; transitions release the previous pack.',
            'English dictionary/pools are linked into the image and already included in the linker heap start.',
            'Reserve excludes allocator metadata, filesystem/filename-table/stdio and other dynamic allocations.',
            'This static bound is not measured free RAM, peak runtime memory, hardware timing or FPS.',
        ],
        'violations': violations,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--mode', required=True, choices=('retail', 'preview'))
    parser.add_argument('--retail-root', type=Path, default=ROOT / 'data' / 'lang')
    parser.add_argument('--preview-root', type=Path)
    parser.add_argument('--output', type=Path, help='write the complete JSON report (stdout otherwise)')
    parser.add_argument('--nm', help='explicit nm executable; cross nm then host nm are tried by default')
    parser.add_argument('--forbid-float-printf', action='store_true')
    args = parser.parse_args(argv)
    try:
        if not args.rom.is_file():
            raise BudgetInputError(f'missing ROM: {args.rom}')
        if args.mode == 'preview' and args.preview_root is None:
            raise BudgetInputError('--preview-root is required for the preview ROM budget')
        report = evaluate(read_symbols(args.elf, args.nm), args.rom.stat().st_size,
                          args.mode, scan_packs(args.retail_root, 'retail'),
                          scan_packs(args.preview_root, 'preview') if args.mode == 'preview' else None,
                          args.forbid_float_printf)
        code = 0 if report['status'] == 'pass' else 1
    except (BudgetInputError, OSError) as exc:
        report = {'status': 'error', 'mode': args.mode, 'errors': [str(exc)]}
        code = 2
    report['rom'] = str(args.rom)
    report['elf'] = str(args.elf)
    rendered = json.dumps(report, indent=2, sort_keys=True) + '\n'
    if args.output:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding='utf-8')
        except OSError as exc:
            print(f'ROM BUDGET ERROR: cannot write {args.output}: {exc}', file=sys.stderr)
            return 2
        print(f'ROM BUDGET {report["status"].upper()}: {args.mode} -> {args.output}')
        for issue in report.get('violations', report.get('errors', [])):
            print(issue, file=sys.stderr)
    else:
        print(rendered, end='')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
