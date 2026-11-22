"""Save, power off, reboot, load on a real FAT card (tools/emu/fatrig.py): the scenarios DeSmuME alone cannot run.
    python3 tools/emu/test_fat.py wordPass.nds wordPass.elf DIR
Prints one PASS line per scenario and FAT OK at the end; the exit status is the number of failures.
Each scenario boots in its own process (a second DeSmuME object in one process crashes it, so a power cycle is FatEmu.power_cycle).

The three situations NitroFS can be in (see fatrig.py): 'ok' (argv names the .nds on the card: libfilesystem reads the ROM
through FAT and leaves the default device on fat:), 'norom' (argv, but the card has no .nds) and 'noargv' (no argv): both
read the card themselves and chdir() to nitro:/, which used to make every device-less save path land on the read-only ROM.
"""
import os, subprocess, sys, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault('SDL_VIDEODRIVER', 'dummy'); os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')

SITUATIONS = ['ok', 'norom', 'noargv']


def card_with_rom(d, tag, rom):
    from fatrig import make_card
    img = os.path.join(d, 'card_%s.img' % tag)
    make_card(img, [('wordPass.nds', open(rom, 'rb').read())] if tag != 'norom' else [('readme.txt', b'a card with no ROM on it\n')])
    return img


def case(tag, rom, elf, d):
    """One situation, end to end. Raises AssertionError on the first thing that is wrong."""
    from fatrig import FatEmu, read_cfg, card_files, cfg_fields, SETTINGS_MAGIC
    from wp import ST_TITLE, ST_OPTIONS, ST_LANGPICK, ST_FAIL, FAIL_WORDS, COLS, ROWS, TILE
    img = card_with_rom(d, tag, rom)
    argv = tag != 'noargv'

    def boot(**kw):
        return FatEmu(rom, elf, img, argv=argv, **kw)

    def need(cond, msg):
        assert cond, '%s: %s' % (tag, msg)

    def options_theme(e):
        e.tap(86, 170); e.run(30)
        need(e.state()['state'] == ST_OPTIONS, 'OPTIONS did not open')
        e.tap(130, 32 + 6); e.run(10)                  # THEME, right half: forward
        e.press('B', settle=30)                        # leaving the options saves
        need(e.state()['state'] == ST_TITLE, 'did not return to the title')

    # first boot: the card is blank, so the language chooser comes first, and answering it saves
    e = boot(dismiss=False)
    dev = e.cwd()[0]
    need(dev == ('fat' if tag == 'ok' else 'nitro'), 'the default device after boot is %r' % (dev,))
    need(e.flags()['fatReady'] == 1, 'FAT is not up: %r' % e.flags())
    need(e.state()['state'] == ST_LANGPICK, 'first boot did not show the language chooser')
    # the fast card path is for the setups where libfilesystem reads the card itself (the default device moved to nitro:)
    need(e.flags()['rawOk'] == (0 if tag == 'ok' else 1), 'rawOk is %d: the fast path must run exactly when NitroFS reads the card (%r)' % (e.flags()['rawOk'], e.flags()))
    e.press('A', settle=30)
    need(not e.flags()['saveFailed'], 'the chooser answer was not saved (default device %s): %r' % (dev, e.flags()))
    b = read_cfg(img)
    need(b is not None and cfg_fields(b)['magic'] == SETTINGS_MAGIC, 'no wordPass.cfg on the card after the chooser: %r' % (card_files(img),))
    need(cfg_fields(b)['languageChosen'] == 1, 'the saved file does not record the language choice')
    t0 = cfg_fields(b)['theme']
    e.set_language(6)                                  # NEDERLANDS: the biggest dictionary, a blocking load
    need(e.state()['language'] == 6, 'the language did not load')
    need(e.state()['loadMs'] < 2000, 'loading a language took %d ms (the slow NitroFS card path is 4500 to 7200)' % e.state()['loadMs'])
    e.set_language(0)
    options_theme(e)
    need(not e.flags()['saveFailed'], 'leaving OPTIONS did not save')
    t1 = cfg_fields(read_cfg(img))['theme']
    need(t1 != t0 and t1 == e.S_theme(), 'the card holds theme %d, the game has %d (was %d)' % (t1, e.S_theme(), t0))
    e.power_cycle()

    # power off, boot again: the saved choice loads (no chooser, same theme)
    need(e.state()['state'] == ST_TITLE, 'second boot shows the chooser again: the save did not load (state %d)' % e.state()['state'])
    need(e.S_theme() == t1, 'second boot has theme %d, saved %d' % (e.S_theme(), t1))
    # libfat refuses an existing destination, so settingsSave rotates through a backup.
    options_theme(e)
    t2 = cfg_fields(read_cfg(img))['theme']
    need(not e.flags()['saveFailed'] and t2 == e.S_theme() and t2 != t1, 'the second save did not reach the card (card %d, game %d)' % (t2, e.S_theme()))
    need(not any(n.lower() in ('wordpass.cfg.tmp', 'wordpass.cfg.bak') for n, _ in card_files(img)),
         'a recovery file was left after a successful save: %r' % (card_files(img),))
    e.power_cycle()
    need(e.state()['state'] == ST_TITLE and e.S_theme() == t2, 'third boot did not load the second save')

    # a Classic run that ends is on the card as soon as the RUN OVER card is up, without a tap on it (switch off there)
    games0 = cfg_fields(read_cfg(img))['gamesPlayed']
    e.start_classic(); e.set_tutor(1)
    e.set_lives(1)
    e.set_features(shuffles=0)
    for c in range(COLS):
        for r in range(ROWS): e.set_cell(c, r, TILE, 'x')     # a board with no word: the next peel is OUT OF WORDS
    g = e.state()
    plain = [(c, r) for c in range(COLS) for r in range(ROWS) if not g['back'][c][r]]
    e.peel(*plain[len(plain) // 2]); e.run(30)
    g = e.state()
    need(g['state'] == ST_FAIL and g['fail'] == FAIL_WORDS and g['lives'] == 0, 'the run did not end (state %d fail %d lives %d)' % (g['state'], g['fail'], g['lives']))
    need(not e.flags()['saveFailed'], 'the run-end save failed')
    c = cfg_fields(read_cfg(img))
    need(c['gamesPlayed'] == games0 + 1, 'the card says %d games, %d before the run, and the card is not tapped yet' % (c['gamesPlayed'], games0))
    e.power_cycle()
    need(e.state()['state'] == ST_TITLE, 'reboot after the run over did not reach the title')


DAMAGE = {
    'a dictionary with a damaged magic': ('lang/es/dict.bin', lambda b, st, n: b.__setitem__(st + 1, b[st + 1] ^ 1), False),
    'pools in which a theme has no word': ('lang/es/pools.bin', lambda b, st, n: b.__setitem__(slice(st + 16, st + 28), bytes(12)), True),
}


def damaged_case(kind, rom, elf, d):
    """Header damage is hidden at startup; deeper damage is refused on load.
    Both cases must leave a route to a playable English game."""
    from fatrig import rom_file_range
    from wp import Emu, ST_OPTIONS, ST_TITLE, ST_PLAY, ST_LANGPICK, symbol
    path, hit, listed = DAMAGE[kind]
    data = bytearray(open(rom, 'rb').read())
    st, n = rom_file_range(rom, path)
    hit(data, st, n)
    bad = os.path.join(d, 'damaged.nds'); open(bad, 'wb').write(bytes(data))
    e = Emu(bad, elf=elf)
    e.set_tutor(1)
    assert bool(e.state()['langMask'] & 2) == listed, 'startup availability did not match the damaged header'
    if listed:
        e.tap(86, 170); e.run(30)
        assert e.state()['state'] == ST_OPTIONS
        e.tap(130, 32 + 13 * 8 + 6); e.run(20)     # attempt the otherwise-listed Spanish pack
        g = e.state()
        assert g['language'] == 0, 'the damaged language was accepted (language %d)' % g['language']
        assert e.mem.unsigned[symbol(e.rom_elf, 'loadFailed')], 'deep pack damage must report load failure'
        e.shot(os.path.join(d, 'damaged_pools_refused.png'))
        e.press('B', settle=30)
    else:
        e.tap(40, 15, settle=10)
        assert e.state()['state'] == ST_LANGPICK
        e.tap(60, 22 + 13 + 6, settle=10)          # disabled Spanish row
        assert e.state()['state'] == ST_LANGPICK and e.state()['language'] == 0, 'a damaged-header row must not be selectable'
        e.shot(os.path.join(d, 'damaged_dictionary_disabled.png'))
        e.press('DOWN', settle=3); e.press('A', settle=30)
        assert e.state()['state'] == ST_TITLE and e.state()['language'] == 2, 'picker navigation must skip the damaged pack'
        e.set_language(0)
    e.start_classic(); e.run(20)
    g = e.state()
    assert g['state'] == ST_PLAY and g['passLen'] >= 4, 'English is not playable after the failed load'
    print('CASE OK')


def main():
    if len(sys.argv) >= 6 and sys.argv[1] == '--case':          # child: one situation
        case(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]); print('CASE OK'); return 0
    if len(sys.argv) >= 6 and sys.argv[1] == '--damaged':
        damaged_case(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]); return 0
    rom, elf, out = sys.argv[1], sys.argv[2], sys.argv[3]
    from fatrig import patch_dldi
    d = os.path.join(out, 'fat'); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    dl = patch_dldi(rom, os.path.join(d, 'wordPass.nds'))
    fails = 0
    for tag in SITUATIONS:
        # A fresh process isolates DeSmuME's global state. Python's -I would
        # also hide user-site wheels installed by CI, including py-desmume.
        r = subprocess.run([sys.executable, os.path.abspath(__file__), '--case', tag, dl, elf, d], capture_output=True, text=True,
                           env=dict(os.environ, PYTHONPATH=HERE))
        if r.returncode == 0 and 'CASE OK' in r.stdout: print('PASS fat card: save, power off, reboot, load (%s)' % tag)
        else:
            fails += 1; print('FAIL fat card: save, power off, reboot, load (%s) -' % tag, (r.stderr.strip().splitlines() or ['?'])[-1])
    for kind in DAMAGE:
        r = subprocess.run([sys.executable, os.path.abspath(__file__), '--damaged', kind, rom, elf, d], capture_output=True, text=True)
        if r.returncode == 0 and 'CASE OK' in r.stdout: print('PASS damaged language file stays English: %s' % kind)
        else:
            fails += 1; print('FAIL damaged language file stays English: %s -' % kind, (r.stderr.strip().splitlines() or ['?'])[-1])
    print('FAT OK' if not fails else 'FAT FAILED: %d failure(s)' % fails)
    return fails


if __name__ == '__main__':
    sys.exit(main())
