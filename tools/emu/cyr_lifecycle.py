"""Preview/retail heap lifetime and failure recovery on the actual ARM9 ROM."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from wp import Emu, symbol
from fatrig import rom_file_range


def need(condition, message):
    if not condition:
        raise AssertionError(message)


def value(e, name):
    address = symbol(e.rom_elf, name)
    return struct.unpack('<I', bytes(e.mem.unsigned[address:address + 4]))[0]


def retail_buffers(e):
    return [value(e, name) for name in ('bufDict', 'bufPools', 'bufLetters')]


def preview_buffers(e):
    address = symbol(e.rom_elf, 'images')
    return struct.unpack('<3I', bytes(e.mem.unsigned[address:address + 12]))


def preferences(e):
    # Settings' unchanged version-2 preference block; language is byte 17.
    address = symbol(e.rom_elf, 'S')
    return bytes(e.mem.unsigned[address + 8:address + 20])


def settle_state(e, state, language):
    for _ in range(600):
        current = e.state()
        if current['state'] == state and (language is None or current['language'] == language):
            return
        e.run(1)
    raise AssertionError(f'preview transition did not reach state={state}, language={language}')


def expect_retail(e, saved, state=0, language=6):
    settle_state(e, state, language)
    pointers = retail_buffers(e)
    need(all(pointers) if language else not any(pointers),
         'retail runtime must own exactly its current language buffers')
    need(not any(preview_buffers(e)), 'all three preview images must close before retail reload')
    need(preferences(e) == saved, 'temporary preview must preserve every saved preference')


def expect_preview(e, saved, language):
    settle_state(e, 9, None)
    need(value(e, 'scriptPreviewLanguage') == language, 'wrong preview language')
    need(not any(retail_buffers(e)), 'retail language buffers must be freed while preview owns the heap')
    need(e.state()['language'] == 0, 'preview must leave the retail runtime on built-in English')
    need(all(preview_buffers(e)), 'preview must own all three validated resource images')
    need(preferences(e) == saved, 'preview entry/switch must preserve saved language preferences')


def chooser(e, language, options=False):
    if options:
        e.tap(86, 170, settle=6)
        need(e.state()['state'] == 5, 'options did not open for preview lifecycle check')
        for _ in range(8):
            e.press('DOWN', settle=2)
        e.press('A', settle=6)
    else:
        e.tap(55, 14, settle=6)
    need(e.state()['state'] == 8, 'language chooser did not open')
    e.tap(80, 173 if language else 161, settle=18)


def hotkey(e):
    e.hold('L')
    e.hold('R')
    e.press('Y', settle=18)
    e.hold('R', False)
    e.hold('L', False)


def damaged_cases(rom, elf, output):
    # DeSmuME owns process-global state: each additional ROM must run in its
    # own process, as in the FAT suite, rather than instantiate a second Emu.
    e = Emu(rom, elf=elf)
    e.run(60)
    e.set_language(6)
    saved = preferences(e)
    chooser(e, 0)
    expect_retail(e, saved)
    need('MISSING' in e.state()['msg'], 'failed preview entry must report unavailable files')
    hotkey(e)
    expect_retail(e, saved)
    chooser(e, 1, options=True)
    expect_preview(e, saved, 1)
    e.press('X', settle=18)
    expect_retail(e, saved, state=5)
    need('MISSING' in e.state()['msg'], 'failed preview switch must report unavailable files')
    e.press('B', settle=10)
    e.shot(str(Path(output) / 'preview-failed-switch-restored-dutch.png'))
    print('CYRILLIC LOAD FAILURES OK: chooser, shortcut and switch restore Dutch')


def exercise(e, rom, output):
    # Dutch is the largest shipped heap-resident retail pack. These are real
    # menu inputs; pointers are read only as diagnostics, never modified.
    e.set_language(6)
    saved = preferences(e)
    expect_retail(e, saved)
    chooser(e, 0)
    expect_preview(e, saved, 0)
    e.press('X', settle=18)
    expect_preview(e, saved, 1)
    e.press('B', settle=18)
    expect_retail(e, saved)

    chooser(e, 1, options=True)
    expect_preview(e, saved, 1)
    e.press('START', settle=18)
    expect_retail(e, saved, state=5)
    e.press('B', settle=10)
    # In particular, the hidden title shortcut must not reuse the previous
    # options return screen. This used to depend on stale G.langFrom.
    hotkey(e)
    expect_preview(e, saved, 0)
    e.press('B', settle=18)
    expect_retail(e, saved)

    # Diagnostic fault injection: simulate the saved pack disappearing while
    # preview owns the heap. Never alter allocator pointers. The normal exit
    # must keep the user's preference and rebuild a playable English fallback.
    chooser(e, 0)
    expect_preview(e, saved, 0)
    available = symbol(e.rom_elf, 'avail')
    e.mem.write_byte(available + 6, 0)
    e.press('START', settle=18)
    expect_retail(e, saved, language=0)
    load_failed = symbol(e.rom_elf, 'loadFailed')
    need(int(e.mem.unsigned[load_failed]) == 1, 'failed restoration must keep visible language-load feedback')
    need(e.state()['msg'] == 'LANGUAGE FILE DAMAGED',
         'failed restoration must display the existing language-load message')
    e.shot(str(Path(output) / 'preview-return-language-unavailable.png'))
    e.mem.write_byte(available + 6, 1)

    # Use a private ROM with one damaged preview dictionary to exercise real
    # file validation and cleanup after both entry and language-switch failure.
    # The shipped ROM and its ELF stay untouched.
    offset, _ = rom_file_range(rom, 'cyrpreview/ru/dict.bin')
    damaged = bytearray(Path(rom).read_bytes())
    damaged[offset:offset + 4] = b'BAD!'
    with tempfile.TemporaryDirectory(prefix='wordpass-preview-failure-') as folder:
        bad_rom = Path(folder) / 'bad-russian-preview.nds'
        bad_rom.write_bytes(damaged)
        result = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), '--damaged',
             str(bad_rom), str(Path(e.rom_elf).resolve()), str(Path(output).resolve())],
            capture_output=True, text=True, timeout=180,
        )
        need(result.returncode == 0,
             'damaged-preview child failed:\n' + result.stdout + '\n' + result.stderr)
    print('CYRILLIC HEAP LIFETIME OK: Dutch release/restore, menu/hotkey/options, switches and load failures')


if __name__ == '__main__':
    if len(sys.argv) != 5 or sys.argv[1] != '--damaged':
        raise SystemExit('usage: cyr_lifecycle.py --damaged ROM ELF OUTPUT_DIR')
    damaged_cases(sys.argv[2], sys.argv[3], sys.argv[4])
