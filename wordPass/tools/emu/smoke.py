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
# peek one plain cell, then all secrets
g = e.state(); plain = [(c, r) for c in range(COLS) for r in range(ROWS) if g['cell'][c][r][1] == 1 and not g['back'][c][r]]
e.peel(*plain[len(plain)//2]); g = e.state(); assert g['peels'] == 1, 'peel not counted'
for i in range(1, g['passLen'] + 1):
    g = e.state()
    if g['state'] != 1: break
    c, r = e.secret_cells(g)[i]
    if g['cell'][c][r][1] in (1,):
        e.peel(c, r)
g = e.state(); assert g['state'] == 2, f'pass not completed: state {g["state"]} fail {g["fail"]} found {g["found"]}'
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
