"""Input-to-screen latency, in frames, for wordPass.

    python3 tools/emu/latency.py wordPass.nds [--left] [--only "gem|submit"] [--json out.json]

Method. Each scenario is brought to a state by ordinary input, saved as an emulator savestate, then run twice from
that state: a control run (no input) and a trial run (the input is applied before the first frame). DeSmuME is
deterministic, so any pixel that differs between the two screenshots after k emulated frames was caused by the
input. The latency is the smallest k that shows a difference:

    touch  k for the touch (bottom) screen          top  k for the top screen
    sfx    k until the sequencer mirror (music.c MusicDbg) reports a new effect
    done   k until the touch screen has reached its final brightness (fades and wipes finished; transitions only)
    state  k until the game's own state changes (the DBG mirror)

k = 1 means the change is on the screen after the very next frame, the floor of the 60 fps pipeline (input is read at
the top of the loop, drawn into the hidden bank and flipped at the next vblank). `-` means no change within the window.
"""
import argparse, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('SDL_VIDEODRIVER', 'dummy'); os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
from wp import *
from desmume.controls import Keys, keymask

WINDOW = 45


def key(name): return keymask(getattr(Keys, 'KEY_' + name))


class Lat:
    def __init__(self, rom, left=False):
        self.e = Emu(rom)
        self.left = left
        self.e.run(200)
        if left:                                             # HAND row of the options screen (the list never mirrors)
            self.e.tap(86, 170); self.e.run(30)
            self.e.tap(130, 32 + 13 * 3 + 6); self.e.run(10)
            self.e.tap(86, 161); self.e.run(30)
            self.e.set_hand(True)
        self.e.emu.savestate.save(2)                         # slot 2: the title, as booted
        self.px = self.e.bx

    def boot(self):
        self.e.emu.savestate.load(2)
        self.e.emu.input.keypad_update(0); self.e.emu.input.touch_release(); self.e.run(2)

    # ---- helpers for setups
    def to_play(self):
        e = self.e
        e.run(30); e.tap(e.bx + 52, 147); e.run(150)
        e.set_tutor(1); e.press('Y', settle=5)
        e.set_heat(0); e.set_burn(0, 99); e.set_turn(0)
        g = e.state()
        for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
        return e.state()

    def pick_word(self, g, n=4):
        ws = [w for w in self.e.find_words(g) if len(w[0]) >= n] or self.e.find_words(g)
        return min(ws, key=lambda w: len(w[0]))

    # ---- trial machinery
    def shots(self, ctx, act, n=WINDOW):
        e = self.e
        e.emu.savestate.load(1)
        e.emu.input.keypad_update(0); e.emu.input.touch_release()
        ctx.get('held', lambda: None)()
        e.run(3)
        base = e.pixels()
        if act: act()
        frames = []
        for k in range(1, n + 1):
            e.run(1)
            frames.append(e.shot_array())
        return base, frames

    def sig(self):
        m = self.e.music()
        return (m['sfxCount'], m['wordCount'], m['fanfare'], m['failPhase'], m['mix'])

    def measure(self, ctx, act, want):
        ctrl_b, ctrl = self.shots(ctx, None)
        e = self.e
        e.emu.savestate.load(1)
        e.emu.input.keypad_update(0); e.emu.input.touch_release()
        ctx.get('held', lambda: None)()
        e.run(3)
        sfx0 = self.sig(); st0 = e.state()['state']
        act()
        res = {}
        bright = []
        for k in range(1, WINDOW + 1):
            e.run(1)
            im = e.shot_array()
            c = ctrl[k - 1]
            if 'touch' in want and 'touch' not in res and (im[192:] != c[192:]).any(): res['touch'] = k
            if 'top' in want and 'top' not in res and (im[:192] != c[:192]).any(): res['top'] = k
            if 'sfx' in want and 'sfx' not in res and self.sig() != sfx0: res['sfx'] = k
            if 'state' in want and 'state' not in res and e.state()['state'] != st0: res['state'] = k
            if 'done' in want: bright.append(float(im[192:].mean()))
        if 'done' in want:
            fin = bright[-1]
            res['done'] = next((i + 1 for i, b in enumerate(bright) if abs(b - fin) <= 0.03 * max(fin, 1) and all(abs(x - fin) <= 0.03 * max(fin, 1) for x in bright[i:])), None)
        return res


def build_scenarios(L):
    e = L.e
    P = lambda dx, y: (e.bx + dx, y)
    panel = 4 if L.left else 176
    S = []

    def scen(name, setup, act, want, held=None):
        S.append((name, setup, act, want, held))

    def touch(x, y): return lambda: e.emu.input.touch_set_pos(x, y)
    def press(k): return lambda: e.emu.input.keypad_add_key(key(k))

    # ---------------- play
    def s_play():
        g = L.to_play(); return g
    st = {}

    def setup_gem():
        g = L.to_play(); w, path = L.pick_word(g); st['path'] = path; st['word'] = w
    scen('touch down on a gem', setup_gem, lambda: e.emu.input.touch_set_pos(*e.center(*st['path'][0])), ['touch', 'top', 'sfx'])

    def setup_ext():
        setup_gem()
    def held_ext():
        e.emu.input.touch_set_pos(*e.center(*st['path'][0])); e.run(4)
    scen('drag onto the next gem', setup_ext, lambda: e.emu.input.touch_set_pos(*e.center(*st['path'][1])), ['touch', 'top', 'sfx'], held_ext)

    def setup_chain():
        setup_gem(); e.chain(st['path'])                     # a chained word, stylus lifted
    scen('tail tap (submit)', setup_chain, lambda: e.emu.input.touch_set_pos(*e.center(*st['path'][-1])), ['touch', 'top', 'sfx'])
    scen('A button (submit)', setup_chain, press('A'), ['touch', 'top', 'sfx'])
    scen('SUBMIT button tap', setup_chain, touch(panel + 38, 130), ['touch', 'top', 'sfx'])
    scen('CLEAR button tap', setup_chain, touch(panel + 38, 156), ['touch', 'top'])

    def setup_peel():
        g = L.to_play(); st['sec'] = e.secret_cells(g)[1]
    def held_peel():
        e.emu.input.keypad_add_key(key('L')); e.run(4)
        e.emu.input.touch_set_pos(*e.center(*st['sec'])); e.run(4)
    scen('peel (stylus lift)', setup_peel, lambda: e.emu.input.touch_release(), ['touch', 'top', 'sfx'], held_peel)
    scen('peek arm (touch in peek)', setup_peel, lambda: e.emu.input.touch_set_pos(*e.center(*st['sec'])), ['touch', 'sfx'],
         lambda: (e.emu.input.keypad_add_key(key('L')), e.run(4)))
    scen('PEEK button tap', setup_peel, touch(panel + 38, 182), ['touch', 'top', 'sfx'])
    scen('X latch', setup_peel, press('X'), ['touch', 'top', 'sfx'])
    scen('L held (peek)', setup_peel, press('L'), ['touch', 'top', 'sfx'])
    scen('d-pad cursor move', setup_peel, press('RIGHT'), ['touch', 'top'])
    scen('pause button tap', setup_peel, touch(panel + 70, 12), ['touch', 'top', 'state'])
    scen('START (pause)', setup_peel, press('START'), ['touch', 'top', 'state'])

    def setup_last():
        g = L.to_play(); n = g['passLen']
        for i in range(1, n):
            e.set_heat(0); e.set_burn(0, 99)
            g = e.state()
            for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
            c, r = e.secret_cells(g)[i]; e.peel(c, r)
        g = e.state(); st['sec'] = e.secret_cells(g)[n]
        assert g['state'] == ST_PLAY
    scen('last pass letter peel -> results', setup_last, lambda: e.emu.input.touch_release(), ['touch', 'top', 'sfx', 'state', 'done'], held_peel)

    def setup_fail():
        g = L.to_play(); w, path = L.pick_word(g, 3); st['path'] = path
        # a flame on the bottom row of a column the word does not use, the fire stepping on the next word
        col = next(c for c in range(COLS) if all(p[0] != c for p in path))
        e.set_cell(col, ROWS - 1, BURN, 'e'); e.set_burn(0, 1); e.set_heat(0)
        e.chain(path)
    scen('fire reaches the bottom -> fail card', setup_fail, press('A'), ['touch', 'top', 'sfx', 'state', 'done'])

    def setup_pause():
        L.to_play(); e.press('START', settle=20)
    scen('pause menu: d-pad DOWN', setup_pause, press('DOWN'), ['touch', 'sfx'])
    scen('pause menu: resume (START)', setup_pause, press('START'), ['touch', 'state', 'done'])

    # ---------------- menus
    def title(): pass
    scen('title: CLASSIC tap -> play', title, touch(e.bx + 52, 147), ['touch', 'top', 'sfx', 'state', 'done'])
    scen('title: A (start)', title, press('A'), ['touch', 'top', 'sfx', 'state', 'done'])
    scen('title: OPTIONS tap', title, touch(e.bx + 86, 170), ['touch', 'top', 'sfx', 'state'])
    scen('title: LEDGER tap', title, touch(e.bx + 86, 185), ['touch', 'top', 'sfx', 'state'])
    scen('title: GUIDE tap', title, touch(e.bx + 140, 15), ['touch', 'top', 'sfx', 'state'])
    scen('title: language tag tap', title, touch(e.bx + 40, 15), ['touch', 'top', 'sfx', 'state'])
    scen('title: tip card flip (RIGHT)', title, press('RIGHT'), ['touch', 'top', 'sfx'])

    def setup_opts():
        e.run(30); e.tap(e.bx + 86, 170); e.run(40)
    scen('options: value row tap (THEME)', setup_opts, touch(130, 38), ['touch', 'top', 'sfx'])
    scen('options: d-pad DOWN', setup_opts, press('DOWN'), ['touch', 'sfx'])
    scen('options: BACK tap', setup_opts, touch(86, 161), ['touch', 'top', 'sfx', 'state'])

    def setup_guide():
        e.run(30); e.tap(e.bx + 140, 15); e.run(40)
    scen('guide: NEXT tap (page flip)', setup_guide, touch(236, 178), ['touch', 'top', 'sfx'])
    scen('guide: RIGHT (page flip)', setup_guide, press('RIGHT'), ['touch', 'top', 'sfx'])

    def setup_pick():
        e.run(30); e.tap(e.bx + 40, 15); e.run(40)
    scen('language picker: row tap (DEUTSCH) -> title', setup_pick, touch(86, 22 + 13 * 3 + 5), ['touch', 'top', 'state', 'done'])
    return S


def table(before, after=None):
    """Markdown table of a latency run (a json written by --json), with the previous run beside it when given."""
    cols = ['touch', 'top', 'sfx', 'state', 'done']
    head = '| action | ' + ' | '.join(cols) + ' |' if not after else '| action | ' + ' | '.join(c + ' before > after' for c in cols) + ' |'
    out = [head, '|---|' + '---|' * len(cols)]
    for name, r in (after or before).items():
        b = before.get(name, {})
        cells = []
        for k in cols:
            if after is None: cells.append(str(r.get(k, '')) if k in r else '')
            elif k in r or k in b: cells.append(f"{b.get(k, '-')} > {r.get(k, '-')}")
            else: cells.append('')
        out.append(f'| {name} | ' + ' | '.join(cells) + ' |')
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom', nargs='?'); ap.add_argument('--left', action='store_true')
    ap.add_argument('--only', default=''); ap.add_argument('--fps', action='store_true', help='also check Emu.fps() == 120 after each action'); ap.add_argument('--json', default='')
    ap.add_argument('--table', nargs='+', metavar='JSON', help='print a markdown table from one json, or before and after (no emulator)')
    a = ap.parse_args()
    if a.table:
        js = [json.load(open(f)) for f in a.table]
        print(table(js[0], js[1] if len(js) > 1 else None))
        return
    L = Lat(a.rom, a.left)
    e = L.e
    if not hasattr(e, 'shot_array'):
        import numpy as np
        def shot_array(self=e):
            return np.array(self.emu.screenshot().convert('RGB'))
        e.shot_array = shot_array
    out = {}
    names = [n for n in a.only.split('|') if n]
    for name, setup, act, want, held in build_scenarios(L):
        if names and not any(n in name for n in names): continue
        try:
            L.boot(); setup(); L.e.run(10)
            L.e.emu.input.keypad_update(0); L.e.emu.input.touch_release(); L.e.run(10)
            L.e.emu.savestate.save(1)
            r = L.measure({'held': held or (lambda: None)}, act, want)
            if a.fps:
                L.e.emu.savestate.load(1); L.e.emu.input.keypad_update(0); L.e.emu.input.touch_release()
                (held or (lambda: None))(); L.e.run(3); act(); L.e.run(4)
                r['fps'] = min(L.e.fps(120) for _ in range(2))
        except Exception as ex:
            r = {'error': repr(ex)}
        out[name] = r
        print('LAT %-48s %s' % (name, '  '.join(f'{k}={r.get(k, "-")}' for k in ['touch', 'top', 'sfx', 'state', 'done', 'fps'] if k in want or k in r)), flush=True)
    if a.json: json.dump(out, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
