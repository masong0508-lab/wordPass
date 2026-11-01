"""Headless DeSmuME harness for wordPass.

Usage from any script:
    from wp import Emu
    e = Emu('wordPass.nds')          # boots, releases all keys
    e.run(200); e.shot('title.png')
    e.tap(52, 147)                   # CLASSIC button
    g = e.state()                    # dict mirrored from the ROM's DBG struct
    words = e.find_words(g)          # solver over the live board
    e.chain(words[0][1]); e.press('A')

Requires: pip install py-desmume ; env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
(Emu sets those itself). ROMs must be built with the devkitpro/devkitarm:20240511 image;
libnds 2 (calico) ROMs do not boot in DeSmuME.
"""
import os, struct, subprocess, sys
os.environ.setdefault('SDL_VIDEODRIVER', 'dummy'); os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
from desmume.emulator import DeSmuME
from desmume.controls import Keys, keymask

COLS, ROWS, TW, TH, GX, GY, ODD = 7, 8, 24, 22, 4, 2, 11
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
IMAGE = 'devkitpro/devkitarm:20240511'

def symbol(elf, name):
    """Address of a symbol via arm-none-eabi-nm inside the toolchain container."""
    elf = os.path.abspath(elf)
    out = subprocess.run(['docker', 'run', '--rm', '-v', f'{os.path.dirname(elf)}:/work', IMAGE, 'bash', '-c',
                          f'export PATH=$PATH:/opt/devkitpro/devkitARM/bin; arm-none-eabi-nm /work/{os.path.basename(elf)} | grep " [bBdD] {name}$" | cut -d" " -f1'],
                         capture_output=True, text=True).stdout.strip().splitlines()
    out = [l for l in out if l and 'IPv4' not in l]
    if not out: raise RuntimeError(f'symbol {name} not found in {elf}')
    return int(out[-1], 16)

def load_words(path=None):
    path = path or os.path.join(REPO, 'tools', 'enable1.txt')
    return set(w.strip() for w in open(path) if 3 <= len(w.strip()) <= 10)

class Emu:
    def __init__(self, rom, elf=None, dbg_addr=None, left_hand=False):
        rom = os.path.abspath(rom)
        self.emu = DeSmuME(); self.emu.open(rom)
        self.emu.input.keypad_update(0); self.emu.input.touch_release(); self.emu.volume_set(0)
        self.mem = self.emu.memory
        self.dbg = dbg_addr or symbol(elf or rom.replace('.nds', '.elf'), 'DBG')
        self.bx = 82 if left_hand else 0          # left-hand layout shifts the board right
        self.words = None
    def set_hand(self, left):
        """Switch the coordinate mapping only (the ROM's own HAND option must already match).
        DeSmuME has no FAT, so a reboot comes back right-handed; flip hands inside one boot."""
        self.bx = 82 if left else 0
    # ---- time and input
    def run(self, n):
        for _ in range(n): self.emu.cycle()
    def shot(self, path):
        self.emu.screenshot().save(path); return path
    def tap(self, x, y, hold=5, settle=12):
        self.emu.input.touch_set_pos(x, y); self.run(hold); self.emu.input.touch_release(); self.run(settle)
    def press(self, key, hold=3, settle=50):
        k = keymask(getattr(Keys, 'KEY_' + key)); self.emu.input.keypad_add_key(k); self.run(hold); self.emu.input.keypad_rm_key(k); self.run(settle)
    def hold(self, key, on=True):
        k = keymask(getattr(Keys, 'KEY_' + key)); (self.emu.input.keypad_add_key if on else self.emu.input.keypad_rm_key)(k); self.run(4)
    # ---- state
    def state(self):
        a = self.dbg; raw = bytes(self.mem.unsigned[a:a + 400])
        magic, frame = struct.unpack_from('<II', raw, 0)
        assert magic == 0x44424731, 'DBG magic mismatch: wrong address or ROM not running'
        ints = struct.unpack_from('<13i', raw, 8)
        g = dict(zip(['state','mode','shelf','score','fail','turn','heat','chainLen','peels','lockdown','bestWordScore','dailySeed','passLen'], ints))
        g['frame'] = frame
        o = 8 + 13 * 4
        g['pass'] = raw[o:o+16].split(b'\0')[0].decode(); o += 16
        g['found'] = list(raw[o:o+g['passLen']]); o += 16
        L = raw[o:o+56]; o += 56; St = raw[o:o+56]; o += 56; B = raw[o:o+56]; o += 56
        g['cell'] = [[(L[c*ROWS+r], St[c*ROWS+r]) for r in range(ROWS)] for c in range(COLS)]
        g['back'] = [[B[c*ROWS+r] for r in range(ROWS)] for c in range(COLS)]
        g['msg'] = raw[o:o+32].split(b'\0')[0].decode(); o += 32
        g['lastWord'] = raw[o:o+16].split(b'\0')[0].decode(); o += 16
        g['bestWord'] = raw[o:o+16].split(b'\0')[0].decode()
        return g
    def fps(self, frames=120):
        a = self.state()['frame']; self.run(frames); return self.state()['frame'] - a
    def strip_brightness(self):
        """Mean R+G+B over the touch screen's board strip (0..765): ~260 lit, ~120 under the
        pause/results dim. Catches a brightness-decrease effect left on after a state change."""
        px = self.emu.screenshot().convert('RGB').load()
        x0 = self.bx
        return sum(sum(px[x, y]) for y in range(196, 380) for x in range(x0, x0 + 174)) // (184 * 174)
    # ---- board helpers
    def center(self, c, r):
        return self.bx + GX + c*TW + TW//2, GY + r*TH + (ODD if c & 1 else 0) + TH//2
    @staticmethod
    def neighbors(c, r):
        d = [(0,-1),(0,1),(-1,0),(-1,1),(1,0),(1,1)] if c & 1 else [(0,-1),(0,1),(-1,-1),(-1,0),(1,-1),(1,0)]
        return [(c+dc, r+dr) for dc, dr in d if 0 <= c+dc < COLS and 0 <= r+dr < ROWS]
    @staticmethod
    def letter(cell):
        l = chr(97 + cell[0]); return 'qu' if l == 'q' else l
    def find_words(self, g, minlen=3, maxlen=7):
        if self.words is None: self.words = load_words()
        found = []
        def ok(c, r): return g['cell'][c][r][1] in (1, 2, 4)
        def dfs(path, word):
            if minlen <= len(word) <= maxlen and word in self.words: found.append((word, list(path)))
            if len(path) >= 7: return
            c, r = path[-1]
            for n in self.neighbors(c, r):
                if n in path or not ok(*n): continue
                dfs(path + [n], word + self.letter(g['cell'][n[0]][n[1]]))
        for c in range(COLS):
            for r in range(ROWS):
                if ok(c, r): dfs([(c, r)], self.letter(g['cell'][c][r]))
        found.sort(key=lambda w: -len(w[0]))
        return found
    def chain(self, path):
        for (c, r) in path:
            self.emu.input.touch_set_pos(*self.center(c, r)); self.run(5)
        self.emu.input.touch_release(); self.run(3)
    def peel(self, c, r):
        self.hold('L', True); self.tap(*self.center(c, r), settle=6); self.hold('L', False); self.run(30)
    def secret_cells(self, g):
        return {g['back'][c][r]: (c, r) for c in range(COLS) for r in range(ROWS) if g['back'][c][r]}
    def start_classic(self):
        self.run(200); self.tap(self.bx + 52, 147); self.run(150)
    def start_daily(self):
        self.run(200); self.tap(self.bx + 120, 147); self.run(150)
