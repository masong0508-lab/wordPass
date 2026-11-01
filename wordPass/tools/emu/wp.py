"""Headless DeSmuME harness for wordPass.

Usage from any script:
    from wp import Emu
    e = Emu('wordPass.nds')          # boots, releases all keys
    e.run(200); e.shot('title.png')
    e.tap(52, 147)                   # CLASSIC button
    g = e.state()                    # dict mirrored from the ROM's DBG struct
    words = e.find_words(g)          # solver over the live board
    e.chain(words[0][1]); e.press('A')
    e.set_cell(0, 0, 2, 'a')         # poke: column 0 row 0 becomes a burning A
    e.fire_step()                    # poke: one fire advance plus gravity

Requires: pip install py-desmume ; env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
(Emu sets those itself). ROMs must be built with the devkitpro/devkitarm:20240511 image;
libnds 2 (calico) ROMs do not boot in DeSmuME.

The DBG layout (source/debug.c) is pinned with _Static_asserts; OFF below is the same
table. Change both together.
"""
import os, struct, subprocess, sys
os.environ.setdefault('SDL_VIDEODRIVER', 'dummy'); os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
from desmume.emulator import DeSmuME
from desmume.controls import Keys, keymask

COLS, ROWS, TW, TH, GX, GY, ODD = 7, 8, 24, 22, 4, 2, 11
MAX_CHAIN = 10
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
IMAGE = 'devkitpro/devkitarm:20240511'

# cell states and fail codes as the ROM numbers them
EMPTY, TILE, BURN, HOLE, SECRET = 0, 1, 2, 3, 4
FAIL_NONE, FAIL_FIRE, FAIL_SCORCH, FAIL_WORDS = 0, 1, 2, 3
ST_TITLE, ST_PLAY, ST_PASSED, ST_FAIL, ST_PAUSE, ST_OPTIONS, ST_STATS = range(7)

# byte offsets into DebugView (source/debug.c pins every one of these with _Static_assert)
OFF = {
    'magic': 0, 'frame': 4,
    'state': 8, 'mode': 12, 'shelf': 16, 'score': 20, 'fail': 24, 'turn': 28, 'heat': 32,
    'chainLen': 36, 'peels': 40, 'lockdown': 44, 'bestWordScore': 48, 'dailySeed': 52, 'passLen': 56,
    'pass': 60, 'found': 76, 'cellLetter': 92, 'cellState': 148, 'back': 204,
    'msg': 260, 'lastWord': 292, 'bestWord': 308,
    'lastWordTier': 324, 'rareWords': 328, 'library': 332,
    'cmd': 336, 'a0': 340, 'a1': 344, 'a2': 348, 'a3': 352,
    'heatThreshold': 356, 'burnEvery': 360, 'burnTimer': 364,
    'scorched': 368, 'theme': 424, 'hand': 425, 'fire': 426, 'music': 427,
    'optRow': 428, 'chainC': 432, 'chainR': 442,
}
DBG_SIZE = 452
I32_FIELDS = ['state', 'mode', 'shelf', 'score', 'fail', 'turn', 'heat', 'chainLen', 'peels', 'lockdown',
              'bestWordScore', 'dailySeed', 'passLen', 'lastWordTier', 'rareWords', 'library',
              'heatThreshold', 'burnEvery', 'burnTimer', 'optRow']
# poke commands (debug.c applyPoke)
POKE_CELL, POKE_BACK, POKE_HEAT, POKE_BURN, POKE_FIRESTEP, POKE_TURN, POKE_LIVES = range(1, 8)

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

def letter_index(l):
    """'a'..'z' or 'qu' -> 0..25; ints pass through."""
    if isinstance(l, int): return l
    return ord(l[0].lower()) - 97

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
    def pixels(self):
        """RGB pixel access for the 256x384 screenshot (top screen y 0..191, touch y 192..383)."""
        return self.emu.screenshot().convert('RGB').load()
    def tap(self, x, y, hold=5, settle=12):
        self.emu.input.touch_set_pos(x, y); self.run(hold); self.emu.input.touch_release(); self.run(settle)
    def press(self, key, hold=3, settle=50):
        k = keymask(getattr(Keys, 'KEY_' + key)); self.emu.input.keypad_add_key(k); self.run(hold); self.emu.input.keypad_rm_key(k); self.run(settle)
    def hold(self, key, on=True):
        k = keymask(getattr(Keys, 'KEY_' + key)); (self.emu.input.keypad_add_key if on else self.emu.input.keypad_rm_key)(k); self.run(4)
    # ---- state
    def raw(self):
        return bytes(self.mem.unsigned[self.dbg:self.dbg + DBG_SIZE])
    @staticmethod
    def i32(raw, off):
        return struct.unpack_from('<i', raw, off)[0]
    @staticmethod
    def cstr(raw, off, n):
        return raw[off:off + n].split(b'\0')[0].decode()
    def state(self):
        raw = self.raw()
        magic, frame = struct.unpack_from('<II', raw, OFF['magic'])
        assert magic == 0x44424731, 'DBG magic mismatch: wrong address or ROM not running'
        g = {k: self.i32(raw, OFF[k]) for k in I32_FIELDS}
        g['frame'] = frame
        g['pass'] = self.cstr(raw, OFF['pass'], 16)
        g['found'] = list(raw[OFF['found']:OFF['found'] + g['passLen']])
        L = raw[OFF['cellLetter']:OFF['cellLetter'] + 56]; St = raw[OFF['cellState']:OFF['cellState'] + 56]
        B = raw[OFF['back']:OFF['back'] + 56]; Sc = raw[OFF['scorched']:OFF['scorched'] + 56]
        g['cell'] = [[(L[c*ROWS+r], St[c*ROWS+r]) for r in range(ROWS)] for c in range(COLS)]
        g['back'] = [[B[c*ROWS+r] for r in range(ROWS)] for c in range(COLS)]
        g['scorched'] = [[Sc[c*ROWS+r] for r in range(ROWS)] for c in range(COLS)]
        g['msg'] = self.cstr(raw, OFF['msg'], 32)
        g['lastWord'] = self.cstr(raw, OFF['lastWord'], 16)
        g['bestWord'] = self.cstr(raw, OFF['bestWord'], 16)
        g['settings'] = dict(zip(['theme', 'hand', 'fire', 'music'], raw[OFF['theme']:OFF['theme'] + 4]))
        cc = raw[OFF['chainC']:OFF['chainC'] + MAX_CHAIN]; cr = raw[OFF['chainR']:OFF['chainR'] + MAX_CHAIN]
        g['chain'] = [(cc[i], cr[i]) for i in range(g['chainLen'])]
        return g
    def fps(self, frames=120):
        a = self.state()['frame']; self.run(frames); return self.state()['frame'] - a
    def strip_brightness(self):
        """Mean R+G+B over the touch screen's board strip (0..765): ~260 lit, ~120 under the
        pause/results dim. Catches a brightness-decrease effect left on after a state change."""
        px = self.pixels()
        x0 = self.bx
        return sum(sum(px[x, y]) for y in range(196, 380) for x in range(x0, x0 + 174)) // (184 * 174)
    # ---- poke channel: write cmd + args, the ROM's next debugSync applies them
    def poke(self, cmd, a0=0, a1=0, a2=0, a3=0, frames=2):
        a = self.dbg
        for off, v in ((OFF['a0'], a0), (OFF['a1'], a1), (OFF['a2'], a2), (OFF['a3'], a3), (OFF['cmd'], cmd)):
            self.mem.write_long(a + off, v & 0xFFFFFFFF)
        self.run(frames)
        assert struct.unpack_from('<I', self.raw(), OFF['cmd'])[0] == 0, f'poke {cmd} not consumed (is the ROM running debugSync?)'
    def set_cell(self, c, r, state, letter=0):
        """Put a cell in `state` (EMPTY/TILE/BURN/HOLE/SECRET) with `letter` ('a'..'z' or 0..25), at rest."""
        self.poke(POKE_CELL, c, r, state, letter_index(letter))
    def set_back(self, c, r, v):
        """Back layer: 0 empty, else pass letter index + 1."""
        self.poke(POKE_BACK, c, r, v)
    def set_heat(self, h):
        self.poke(POKE_HEAT, h)
    def set_burn(self, timer, every):
        self.poke(POKE_BURN, timer, every)
    def fire_step(self):
        """One fire advance plus gravity, no turn (board.c boardDebugFireStep)."""
        self.poke(POKE_FIRESTEP)
    def set_turn(self, n):
        self.poke(POKE_TURN, n)
    # ---- board helpers
    def center(self, c, r):
        return self.bx + GX + c*TW + TW//2, GY + r*TH + (ODD if c & 1 else 0) + TH//2
    def cell_rect(self, c, r):
        """(x0, y0, x1, y1) of a cell on the 256x384 screenshot (touch screen half)."""
        x = self.bx + GX + c*TW; y = 192 + GY + r*TH + (ODD if c & 1 else 0)
        return x, y, x + TW, y + TH
    @staticmethod
    def neighbors(c, r):
        d = [(0,-1),(0,1),(-1,0),(-1,1),(1,0),(1,1)] if c & 1 else [(0,-1),(0,1),(-1,-1),(-1,0),(1,-1),(1,0)]
        return [(c+dc, r+dr) for dc, dr in d if 0 <= c+dc < COLS and 0 <= r+dr < ROWS]
    @staticmethod
    def letter(cell):
        l = chr(97 + cell[0]); return 'qu' if l == 'q' else l
    def find_words(self, g, minlen=3, maxlen=7, avoid=()):
        """Spellable words on the board, longest first, as (word, path). `avoid` is a set of (c, r)
        cells the path must not use."""
        if self.words is None: self.words = load_words()
        found = []
        avoid = set(avoid)
        def ok(c, r): return g['cell'][c][r][1] in (TILE, BURN, SECRET) and (c, r) not in avoid
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
    def chain(self, path, release=True):
        for (c, r) in path:
            self.emu.input.touch_set_pos(*self.center(c, r)); self.run(5)
        if release: self.emu.input.touch_release(); self.run(3)
    def peel(self, c, r):
        self.hold('L', True); self.tap(*self.center(c, r), settle=6); self.hold('L', False); self.run(30)
    def secret_cells(self, g):
        return {g['back'][c][r]: (c, r) for c in range(COLS) for r in range(ROWS) if g['back'][c][r]}
    def start_classic(self):
        self.run(200); self.tap(self.bx + 52, 147); self.run(150)
    def start_daily(self):
        self.run(200); self.tap(self.bx + 120, 147); self.run(150)
