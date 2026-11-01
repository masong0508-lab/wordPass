"""Regression smoke test: boots the ROM, spells, triggers fire, peeks the pass to
completion, advances a shelf, then burns the library down. Exits non-zero on failure.
    python3 tools/emu/smoke.py wordPass.nds [outdir]
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wp import Emu, COLS, ROWS
rom = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else '/tmp'
e = Emu(rom)
e.run(150); e.shot(f'{out}/smoke_title.png')
fps = e.fps(); assert fps >= 118, f'title fps too low: {fps}/120 (expect 120)'
e.tap(52, 147); e.run(150)
g = e.state(); assert g['state'] == 1, f'not in play after CLASSIC tap: {g["state"]}'
fps = e.fps(); assert fps >= 110, f'play fps too low: {fps}/120 (expect 120)'
# pause dims the board strip in hardware; resuming must switch the effect back off
lit = e.strip_brightness()
e.press('START', settle=30); dim = e.strip_brightness(); e.shot(f'{out}/smoke_pause.png')
e.press('START', settle=30); back = e.strip_brightness()
assert e.state()['state'] == 1, 'START did not resume play'
assert dim < lit * 0.7, f'pause did not dim the board: {dim} vs {lit} lit'
assert back > lit * 0.9, f'board still dim after resume: {back} vs {lit} lit'
# spell three long words
for i in range(3):
    g = e.state(); ws = e.find_words(g); assert ws, 'solver found no words'
    word, path = ws[0]; e.chain(path)
    assert e.state()['chainLen'] == len(path), f'chain mismatch for {word}'
    before = g['score']; e.press('A')
    assert e.state()['score'] > before, f'word {word} did not score'
e.shot(f'{out}/smoke_words.png')
# peek one plain cell, then all secrets. Every peel is a turn and heats the shelf (three
# long words <= 3 heat + five peels = 8 < 10 on shelf 1), but if a flame does appear, put it
# out with a word through it before peeling anything under it: a peel below a flame scorches.
def douse(c, r):
    for _ in range(4):
        g = e.state()
        flames = [(fc, fr) for (fc, fr) in e.burn_cells(g) if fc == c and fr <= r]
        if not flames: return
        ws = [w for w in e.find_words(g) if any(f in w[1] for f in flames)]
        assert ws, f'no word through the flame(s) {flames}'
        e.chain(ws[0][1]); e.press('A')
g = e.state(); assert g['turn'] == 3, f'three words should be three turns, got {g["turn"]}'
plain = [(c, r) for c in range(COLS) for r in range(ROWS) if g['cell'][c][r][1] == 1 and not g['back'][c][r]]
c, r = plain[len(plain)//2]; douse(c, r)
e.peel(c, r); g = e.state(); assert g['peels'] == 1, 'peel not counted'
assert g['turn'] == 4 and g['cell'][c][r][1] == 3, f'peel is a turn: turn {g["turn"]} cell {g["cell"][c][r]}'
for i in range(1, g['passLen'] + 1):
    g = e.state()
    if g['state'] != 1: break
    c, r = e.secret_cells(g)[i]
    if g['cell'][c][r][1] in (1,):
        douse(c, r); e.peel(c, r)
g = e.state(); assert g['state'] == 2, f'pass not completed: state {g["state"]} fail {g["fail"]} found {g["found"]}'
print('shelf 1 cleared on turn', g['turn'], 'heat', g['heat'], 'flames', e.burn_cells(g))
e.shot(f'{out}/smoke_passed.png')
fps = e.fps(); assert fps >= 118, f'results fps too low: {fps}/120 (expect 120)'
e.tap(100, 100); e.run(200)
g = e.state(); assert g['shelf'] == 2 and g['state'] == 1, f'did not advance to shelf 2: {g["shelf"]} {g["state"]}'
# burn it down with short words
for t in range(60):
    g = e.state()
    if g['state'] != 1: break
    ws = e.find_words(g); ws.sort(key=lambda w: len(w[0]))
    if not ws: break
    e.chain(ws[0][1]); e.press('A')
g = e.state(); assert g['state'] == 3, f'expected a fail state, got {g["state"]}'
e.shot(f'{out}/smoke_fail.png')
fps = e.fps(); assert fps >= 118, f'fail-screen fps too low: {fps}/120 (expect 120)'
print('SMOKE OK: fail reason', g['fail'], 'turns', g['turn'])
