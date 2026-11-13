"""A FAT card for the headless emulator: the rig that lets a test save, power off, reboot and load.

Plain DeSmuME has no FAT card, so settings.c never saves there. DeSmuME does ship a CompactFlash (GBAMP) slot-2 device
that a DLDI-patched ROM can use. This module wires it up with ctypes (the python wrapper does not expose it):

    from fatrig import FatEmu, make_card, patch_dldi, read_cfg
    patch_dldi('wordPass.nds', '/tmp/wp_dldi.nds')            # once: a private copy of the ROM with the mpcf driver
    make_card('/tmp/card.img', [('readme.txt', b'x')])           # a blank 64 MB FAT16 image holding these files
    e = FatEmu('/tmp/wp_dldi.nds', 'wordPass.elf', '/tmp/card.img')
    e.flags()      # {'fatReady': 1, 'fsReady': 1, 'rawOk': 0, 'saveFailed': 0}
    read_cfg('/tmp/card.img')                                  # the bytes of wordPass.cfg on the card, or None

FatEmu is a wp.Emu (same state(), tap(), press(), run() ...). Writes go straight to the image file, so a "power off" is
just dropping the object and booting a new FatEmu on the same image. The three ways NitroFS can see the ROM are covered by
`argv` (what the loader hands over): 'ok' (the .nds is on the card and DeSmuME supplies argv: libfilesystem reads it
through FAT), 'norom' (argv, but the card does not hold the .nds: libfilesystem falls back to card reads and chdir()s
to nitro:/), 'noargv' (the loader gave no argv at all; same fallback). tools/emu/test_fat.py is the suite on top.

Needs py-desmume, the ELF of the ROM (for the static flags) and docker (arm-none-eabi-nm) like wp.py.
"""
import ctypes, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wp
from wp import Emu, IMAGE
from desmume.emulator import DeSmuME
import fatimg
from fatimg import Fat16

ARGV_MAGIC = 0x02FFFE70        # the libnds argv header in main RAM; zeroing the magic means "no argv"
SETTINGS_MAGIC = 0x57505332    # WPS2


def _lib():
    import desmume
    return ctypes.CDLL(os.path.join(os.path.dirname(desmume.__file__), 'libdesmume.so'))


def patch_dldi(src, dst):
    """Write a copy of the ROM patched with DeSmuME's CompactFlash (mpcf) DLDI driver, through DLDI::tryPatch."""
    lib = _lib()
    data = bytearray(open(src, 'rb').read())
    buf = (ctypes.c_ubyte * len(data)).from_buffer(data)
    f = lib['_ZN4DLDI8tryPatchEPvmj']; f.restype = ctypes.c_bool; f.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint]
    if not f(ctypes.addressof(buf), len(data), 0):
        raise RuntimeError('DLDI patch failed (is the ROM built with libfat?)')
    open(dst, 'wb').write(bytes(data))
    return dst


def make_card(path, files, total_mb=64):
    """A fresh FAT16 card image holding `files` [(name, bytes)] in its root directory."""
    return fatimg.create(path, files, total_mb=total_mb)


def read_cfg(path, name='wordPass.cfg'):
    """The named file on the card image (None when absent). Leftover wordPass.cfg.tmp is read with name=..."""
    return Fat16(path).read(name)


def card_files(path):
    return [(e['name'], e['size']) for e in Fat16(path).entries()]


def cfg_fields(b):
    """The few Settings fields the tests compare (offsets are pinned by _Static_asserts in game.h: magic u32, version
    u16, crc u16, then the option bytes from offset 8)."""
    magic, version, crc = struct.unpack_from('<IHH', b, 0)
    names = ['theme', 'frame', 'accent', 'hand', 'fire', 'music', 'library', 'spelling', 'tutorDone', 'language', 'languageChosen']
    d = dict(zip(names, b[8:8 + len(names)]))
    d.update(magic=magic, version=version, crc=crc, size=len(b))
    d['bestClassic'], d['bestClassicShelf'] = struct.unpack_from('<ii', b, 20)
    return d


def symbols(elf, names):
    """Addresses of static variables, one docker call for all of them."""
    elf = os.path.abspath(elf)
    cmd = f'export PATH=$PATH:/opt/devkitpro/devkitARM/bin; arm-none-eabi-nm /work/{os.path.basename(elf)}'
    out = subprocess.run(['docker', 'run', '--rm', '-v', f'{os.path.dirname(elf)}:/work', IMAGE, 'bash', '-c', cmd],
                         capture_output=True, text=True).stdout.splitlines()
    found = {}
    for line in out:
        p = line.split()
        if len(p) == 3 and p[2] in names and p[1] in 'bBdD':
            found[p[2]] = int(p[0], 16)
    missing = [n for n in names if n not in found]
    if missing: raise RuntimeError(f'symbols not found in {elf}: {missing}')
    return found


class FatEmu(Emu):
    def __init__(self, rom, elf, image, argv=True, dismiss=True, boot_limit=3000, left_hand=False):
        """argv=False clears the loader's argv before the first instruction runs (a loader that passes none)."""
        rom = os.path.abspath(rom)
        self.emu = DeSmuME()
        lib = self.lib = self.emu.lib
        ctypes.c_int.in_dll(lib, 'CFlash_Mode').value = 1            # File: a raw disk image, opened rb+
        stdcpp = ctypes.CDLL('libstdc++.so.6')
        assign = stdcpp['_ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE6assignEPKc']
        assign.restype = ctypes.c_void_p; assign.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        assign(ctypes.addressof(ctypes.c_char.in_dll(lib, '_Z11CFlash_PathB5cxx11')), os.path.abspath(image).encode())
        ch = lib['_Z16slot2_ChangeByIDh']; ch.restype = ctypes.c_bool; ch.argtypes = [ctypes.c_ubyte]
        ch(1)                                                          # slot-2 device 1: CompactFlash
        self.emu.open(rom)
        self.emu.input.keypad_update(0); self.emu.input.touch_release(); self.emu.volume_set(0)
        self.mem = self.emu.memory
        self.sym = symbols(elf, ['DBG', 'fatReady', 'fsReady', 'rawOk', 'lastSaveFailed', 'S', 'devoptab_list', 'defaultDevice', '_current_working_directory'])
        self.dbg = self.sym['DBG']
        self.bx = 82 if left_hand else 0
        self.words = None
        self.argv = argv; self.boot_limit = boot_limit
        if not argv: self.mem.write_long(ARGV_MAGIC, 0)
        self.boot_frames = self._wait_boot()
        if dismiss and self.state()['state'] == wp.ST_LANGPICK:
            self.press('A', settle=20)

    def _wait_boot(self):
        n = 0
        while self.mem.unsigned.read_long(self.dbg) != 0x44424731:
            self.run(1); n += 1
            if n > self.boot_limit: raise RuntimeError('boot did not reach the main loop in %d frames' % n)
        return n

    def power_cycle(self, dismiss=False):
        """Switch the DS off and on: a reset that keeps the card image (and clears the argv again when the loader passes
        none). Use this, not a second FatEmu: two DeSmuME objects in one process crash the interpreter on exit."""
        self.mem.write_long(self.dbg, 0)
        self.emu.reset()
        self.emu.input.keypad_update(0); self.emu.input.touch_release()
        if not self.argv: self.mem.write_long(ARGV_MAGIC, 0)
        self.boot_frames = self._wait_boot()
        if dismiss and self.state()['state'] == wp.ST_LANGPICK:
            self.press('A', settle=20)

    def flags(self):
        """The C library's view of the card: FAT up, NitroFS up, the raw fast path proven, the last save failed."""
        rd = lambda n: self.mem.unsigned.read_byte(self.sym[n])
        return dict(fatReady=rd('fatReady'), fsReady=rd('fsReady'), rawOk=rd('rawOk'), saveFailed=rd('lastSaveFailed'))

    def cwd(self):
        """The C library's default device name and working directory, e.g. ('fat', 'fat:/') or ('nitro', 'nitro:/'): where
        a path with no device name resolves. libfilesystem's nitroFSInit leaves this on nitro: when it reads the card."""
        u = self.mem.unsigned
        idx = u.read_long(self.sym['defaultDevice'])
        p = u.read_long(self.sym['devoptab_list'] + 4 * idx)
        name = bytes(u[u.read_long(p):u.read_long(p) + 8]).split(b'\0')[0].decode() if p and u.read_long(p) else None
        base = self.sym['_current_working_directory']
        return name, bytes(u[base:base + 64]).split(b'\0')[0].decode()

    def S_theme(self):
        """The game's current THEME option (the first byte of Settings after the header)."""
        return self.mem.unsigned.read_byte(self.sym['S'] + 8)
