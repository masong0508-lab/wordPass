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
MAX_CHAIN = 10          # the mirror shows the first ten gems of a chain
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
    'pulseEvery': 452, 'lockTier': 456, 'overtime': 460,
    'chainScore': 464, 'speedBonus': 468, 'peelBonus': 472, 'passBonusKind': 476, 'ledgerN': 480, 'newBest': 484,
    'lives': 488, 'passTheme': 492, 'twist': 496, 'spelling': 500,
    'practice': 504, 'dailyMod': 508, 'clockOk': 512, 'statWords': 516, 'statGames': 520, 'streak': 524,
    'dailyPlayed': 528, 'dailyLast': 532, 'dailyLastScore': 536,
    'peek': 540, 'armed': 544, 'armC': 548, 'armR': 552,
    'curC': 556, 'curR': 560, 'curOn': 564,
    'calloutLife': 568, 'pressBits': 572, 'callout': 576,
    'shownScore': 596, 'pauseRow': 600, 'pauseArm': 604,
    'tutorStep': 608, 'tutorDone': 612, 'tipIndex': 616, 'msgTimer': 620,
    'seq': 624,
}
DBG_SIZE = 672
I32_FIELDS = ['state', 'mode', 'shelf', 'score', 'fail', 'turn', 'heat', 'chainLen', 'peels', 'lockdown',
              'bestWordScore', 'dailySeed', 'passLen', 'lastWordTier', 'rareWords', 'library',
              'heatThreshold', 'burnEvery', 'burnTimer', 'optRow', 'pulseEvery', 'lockTier', 'overtime',
              'chainScore', 'speedBonus', 'peelBonus', 'passBonusKind', 'ledgerN', 'newBest',
              'lives', 'passTheme', 'twist', 'spelling', 'practice', 'dailyMod', 'clockOk', 'statWords', 'statGames',
              'streak', 'dailyPlayed', 'dailyLast', 'dailyLastScore', 'peek', 'armed', 'armC', 'armR', 'curC', 'curR', 'curOn',
              'calloutLife', 'pressBits', 'shownScore', 'pauseRow', 'pauseArm',
              'tutorStep', 'tutorDone', 'tipIndex', 'msgTimer']
PASS_THEMES = ['BINDERY', 'INK', 'VERSE', 'TALES', 'ATLAS', 'ARCHIVE', 'LAMP', 'STUDY']
TWIST_NONE, TWIST_INK, TWIST_DAMP, TWIST_BURIED = range(4)
VOWELS = {0, 4, 8, 14, 20}; RARE = {9, 16, 23, 25}   # a e i o u; j q x z
# letter values (board.c letterValue), Q stands for Qu
LETTER_VALUE = [1,3,3,2,1,4,2,4,1,8,5,1,3,1,1,3,10,1,1,1,1,4,4,8,4,10]
# poke commands (debug.c applyPoke)
POKE_CELL, POKE_BACK, POKE_HEAT, POKE_BURN, POKE_FIRESTEP, POKE_TURN, POKE_LIVES, POKE_SCORE, POKE_SPELL, POKE_CLOCK, POKE_DAILY, POKE_PASS, POKE_TUTOR = range(1, 14)

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
        g['callout'] = self.cstr(raw, OFF['callout'], 20)
        g['settings'] = dict(zip(['theme', 'hand', 'fire', 'music'], raw[OFF['theme']:OFF['theme'] + 4]))
        cc = raw[OFF['chainC']:OFF['chainC'] + MAX_CHAIN]; cr = raw[OFF['chainR']:OFF['chainR'] + MAX_CHAIN]
        g['chain'] = [(cc[i], cr[i]) for i in range(min(g['chainLen'], MAX_CHAIN))]   # the mirror holds the first ten
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
    def set_score(self, n):
        self.poke(POKE_SCORE, n)
    def set_clock(self, ymd):
        """Make the game's DS clock read YYYYMMDD (0 = the real clock). The title re-reads it once a second."""
        self.poke(POKE_CLOCK, ymd); self.run(70)
    def set_daily(self, played=0, last=0, streak=0):
        """Daily records: the date of the attempt already used, of the last scored Daily, and the streak."""
        self.poke(POKE_DAILY, played, last, streak)
    def set_pass(self, n, found=0):
        """Layout checks: the pass becomes the first n letters of the alphabet (1..9), `found` is a bitmask of the
        letters shown as found. The back layer is left alone, so do not peel after this."""
        self.poke(POKE_PASS, n, found)
    def set_tutor(self, done):
        """The guided first shelf: done=1 switches it off (tests that read the message line want that), done=0 lets the
        next Classic shelf 1 guide again. Takes effect for the game in progress at once."""
        self.poke(POKE_TUTOR, done)
    def set_lives(self, n):
        """Bookmarks left in the Classic run (3 at the start; a fail or a pause restart costs one)."""
        self.poke(POKE_LIVES, n)
    @staticmethod
    def word_score(letters, burn=False, diamonds=0, repeat=False):
        """What boardSubmit awards: `letters` is the chain's letter indices (or a lowercase word;
        'qu' is one gem), burn = a flame in the chain, diamonds = CELL_SECRET gems in it (cap 2)."""
        if isinstance(letters, str):
            idx = []; i = 0
            while i < len(letters):
                idx.append(ord(letters[i]) - 97); i += 2 if letters[i] == 'q' and letters[i:i+2] == 'qu' else 1
            n = len(letters)
        else:
            idx = list(letters); n = sum(2 if l == 16 else 1 for l in idx)
        base = sum(LETTER_VALUE[l] for l in idx)
        mult = 1 if n <= 4 else 2 if n <= 6 else 3 if n <= 8 else 4
        gained = base * mult * 10 + (200 if n >= 7 else 0) + (30 * n if burn else 0)
        gained += gained * min(diamonds, 2) // 2
        return gained // 2 if repeat else gained
    @staticmethod
    def pass_bonus(shelf, turn, peels, pass_len):
        """(shelfBonus, speedBonus, peelBonus, kind) main.c awards when the pass completes."""
        speed = 25 * shelf * max(0, 40 - turn)
        if peels == pass_len: return 1000 * shelf, speed, 400 * shelf, 2
        if peels <= pass_len + 2: return 1000 * shelf, speed, 150 * shelf, 1
        return 1000 * shelf, speed, 0, 0
    @staticmethod
    def heat_for(n):
        """Heat a word of n letters adds (board.c boardHeatFor)."""
        return 3 if n <= 3 else 2 if n == 4 else 1 if n <= 6 else 0
    def burn_cells(self, g):
        return [(c, r) for c in range(COLS) for r in range(ROWS) if g['cell'][c][r][1] == BURN]
    def lowest_fire_row(self, g):
        return max([r for (_, r) in self.burn_cells(g)], default=-1)
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
            if minlen <= len(word) <= maxlen and len(path) >= 3 and word in self.words: found.append((word, list(path)))   # a chain needs three gems (Qu counts two letters)
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
    def letter_mix(self, g):
        """(vowels, rare) counts over the board's gems (TILE and BURN cells)."""
        ls = [g['cell'][c][r][0] for c in range(COLS) for r in range(ROWS) if g['cell'][c][r][1] in (TILE, BURN)]
        return sum(l in VOWELS for l in ls), sum(l in RARE for l in ls), len(ls)
    def secret_cells(self, g):
        return {g['back'][c][r]: (c, r) for c in range(COLS) for r in range(ROWS) if g['back'][c][r]}
    def start_classic(self):
        self.run(200); self.tap(self.bx + 52, 147); self.run(150)
    def start_daily(self):
        self.run(200); self.tap(self.bx + 120, 147); self.run(150)
    # ---- music: the sequencer's mirror (source/music.c MusicDbg, 48 bytes at OFF['seq'])
    VOICES = ['hook', 'pulse', 'bass', 'pad', 'alarm', 'chime', 'drum', 'noise2']
    def music(self):
        """What the sequencer last sent to the hardware: hz/vol/duty lists per voice (HOOK PULSE BASS PAD ALARM CHIME
        DRUM NOISE2; duty is the libnds enum, 0 12.5% 1 25% 2 37.5% 3 50%), step (255 = none yet), section, tempo
        (frames per step), drumLevel, lastSfx (-1 none), sfxCount, and the state of the show: mix (0 play, 1 attract, 2 pause,
        3 results, 4 fail), failPhase (0 none, 1 stinger, 2 bed), fanfare (0 none, 1 pending, 2 playing, 3 the loop after it),
        pickup (bars of bass and pulse alone left), fade (cross-fade frames left) and wordCount."""
        v = struct.unpack_from('<8H8B8B4BhH6BH', self.raw(), OFF['seq'])
        return {'hz': list(v[0:8]), 'vol': list(v[8:16]), 'duty': list(v[16:24]), 'step': v[24], 'section': v[25],
                'tempo': v[26], 'drumLevel': v[27], 'lastSfx': v[28], 'sfxCount': v[29],
                'mix': v[30], 'failPhase': v[31], 'fanfare': v[32], 'pickup': v[33], 'fade': v[34], 'wordCount': v[36]}
    def wait_step(self, n, limit=600):
        """Run frame by frame until the sequencer has sequenced step n (its notes were sent on that frame)."""
        for _ in range(8):                              # already on step n: let it pass, the next onset is wanted
            if self.music()['step'] != n: break
            self.run(1)
        for _ in range(limit):
            self.run(1)
            if self.music()['step'] == n: return self.music()
        raise AssertionError(f'step {n} never came (last {self.music()["step"]})')
