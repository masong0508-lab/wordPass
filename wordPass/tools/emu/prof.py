"""Per-stage render timings from a -DWP_PROFILE build (add it to CFLAGS in a copy of the tree).
    python3 tools/emu/prof.py path/to/profile-tree [--fire]
Prints dbgT[] (cpu timer ticks at 33.5 MHz; a 60 fps frame is ~560k) for an idle board, a
chained word and a cascade; with --fire, spells short words until a flame sits on row >= 4.
Stages: 0 backdrop, 1 board, 2 particles+popups+danger glow, 3 panel, 4 overlay, 7 top HUD slice.
"""
import sys, os, struct
tree = os.path.abspath(sys.argv[1])
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wp import Emu, symbol, COLS, ROWS
rom = os.path.join(tree, 'wordPass.nds'); elf = os.path.join(tree, 'wordPass.elf')
e = Emu(rom, elf)
t = symbol(elf, 'dbgT')
def dbgT(): return list(struct.unpack('<10I', bytes(e.mem.unsigned[t:t + 40])))
e.start_classic(); e.run(300)
print('idle     fps', e.fps(), 'dbgT', dbgT())
g = e.state(); word, path = e.find_words(g)[0]
for (c, r) in path:
    e.emu.input.touch_set_pos(*e.center(c, r)); e.run(5)
e.run(20); print('chained  fps', e.fps(), 'dbgT', dbgT(), word)
e.emu.input.touch_release(); e.run(3); e.press('A'); e.run(10)
print('cascade  fps', e.fps(), 'dbgT', dbgT())
if '--fire' in sys.argv:
    for turn in range(80):
        g = e.state()
        if g['state'] != 1 or any(g['cell'][c][r][1] == 2 and r >= 4 for c in range(COLS) for r in range(ROWS)): break
        ws = e.find_words(g); ws.sort(key=lambda w: len(w[0]))
        if not ws: break
        e.chain(ws[0][1]); e.press('A')
    e.run(60); g = e.state()
    flames = [(c, r) for c in range(COLS) for r in range(ROWS) if g['cell'][c][r][1] == 2]
    print('fire     fps', e.fps(), 'dbgT', dbgT(), 'state', g['state'], 'flames', flames)
