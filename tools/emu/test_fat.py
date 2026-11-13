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
    from wp import ST_TITLE, ST_OPTIONS, ST_LANGPICK
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
    e.press('A', settle=30)
    need(not e.flags()['saveFailed'], 'the chooser answer was not saved (default device %s): %r' % (dev, e.flags()))
    b = read_cfg(img)
    need(b is not None and cfg_fields(b)['magic'] == SETTINGS_MAGIC, 'no wordPass.cfg on the card after the chooser: %r' % (card_files(img),))
    need(cfg_fields(b)['languageChosen'] == 1, 'the saved file does not record the language choice')
    t0 = cfg_fields(b)['theme']
    options_theme(e)
    need(not e.flags()['saveFailed'], 'leaving OPTIONS did not save')
    t1 = cfg_fields(read_cfg(img))['theme']
    need(t1 != t0 and t1 == e.S_theme(), 'the card holds theme %d, the game has %d (was %d)' % (t1, e.S_theme(), t0))
    e.power_cycle()

    # power off, boot again: the saved choice loads (no chooser, same theme)
    need(e.state()['state'] == ST_TITLE, 'second boot shows the chooser again: the save did not load (state %d)' % e.state()['state'])
    need(e.S_theme() == t1, 'second boot has theme %d, saved %d' % (e.S_theme(), t1))
    # a save with a file already there: libfat refuses a rename onto an existing name, so settingsSave removes and renames
    options_theme(e)
    t2 = cfg_fields(read_cfg(img))['theme']
    need(not e.flags()['saveFailed'] and t2 == e.S_theme() and t2 != t1, 'the second save did not reach the card (card %d, game %d)' % (t2, e.S_theme()))
    need(not any(n.lower() == 'wordpass.cfg.tmp' for n, _ in card_files(img)), 'a temp file was left on the card: %r' % (card_files(img),))
    e.power_cycle()
    need(e.state()['state'] == ST_TITLE and e.S_theme() == t2, 'third boot did not load the second save')


def main():
    if len(sys.argv) >= 6 and sys.argv[1] == '--case':          # child: one situation
        case(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]); print('CASE OK'); return 0
    rom, elf, out = sys.argv[1], sys.argv[2], sys.argv[3]
    from fatrig import patch_dldi
    d = os.path.join(out, 'fat'); shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    dl = patch_dldi(rom, os.path.join(d, 'wordPass.nds'))
    fails = 0
    for tag in SITUATIONS:
        r = subprocess.run([sys.executable, '-I', os.path.abspath(__file__), '--case', tag, dl, elf, d], capture_output=True, text=True,
                           env=dict(os.environ, PYTHONPATH=HERE))
        if 'CASE OK' in r.stdout: print('PASS fat card: save, power off, reboot, load (%s)' % tag)
        else:
            fails += 1; print('FAIL fat card: save, power off, reboot, load (%s) -' % tag, (r.stderr.strip().splitlines() or ['?'])[-1])
    print('FAT OK' if not fails else 'FAT FAILED: %d failure(s)' % fails)
    return fails


if __name__ == '__main__':
    sys.exit(main())
