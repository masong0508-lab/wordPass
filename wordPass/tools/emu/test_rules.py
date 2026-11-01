"""Deterministic rule tests on the real ROM through the poke channel (source/debug.c).
    python3 tools/emu/test_rules.py wordPass.nds [outdir]
Prints one PASS line per scenario and exits 0; a failing scenario prints FAIL and the
exit status is the number of failures. Screenshots land in outdir (default /tmp).
"""
import sys, os, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wp import Emu, COLS, ROWS, EMPTY, TILE, BURN, HOLE, SECRET, ST_TITLE, ST_PLAY, ST_PASSED, ST_FAIL, ST_PAUSE, ST_OPTIONS, \
    FAIL_FIRE, FAIL_SCORCH, FAIL_WORDS, Keys, keymask, load_words, POKE_SPELL, ST_STATS

rom = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else '/tmp'
e = Emu(rom)
fails = 0
C = 3   # the column every board scenario pokes

ONLY = [k for k in os.environ.get('WP_ONLY', '').split('|') if k]   # development filter: run scenarios whose name has one of these

def scenario(name):
    def deco(fn):
        global fails
        if ONLY and not any(k in name for k in ONLY): return
        try:
            fn(); print('PASS', name)
        except Exception as ex:
            fails += 1; print('FAIL', name, '-', ex); traceback.print_exc()
        sys.stdout.flush()
    return deco

def ensure_play():
    """Back to a calm play state from wherever the last scenario left the game (a fail with no
    bookmark left lands on the title, so this may take two steps)."""
    for _ in range(3):
        g = e.state()
        if g['state'] == ST_PLAY: break
        if g['state'] == ST_FAIL: e.run(60); e.tap(e.bx + 100, 100); e.run(150)
        elif g['state'] == ST_PASSED: e.run(40); e.tap(e.bx + 100, 100); e.run(200)
        elif g['state'] == ST_TITLE: e.start_classic()
        elif g['state'] == ST_PAUSE: e.press('START', settle=30)
        elif g['state'] == ST_OPTIONS: e.press('B', settle=30)
    g = e.state(); assert g['state'] == ST_PLAY, f'not in play: state {g["state"]}'
    e.press('Y', settle=5)
    e.set_heat(0); e.set_burn(0, 99)          # no spontaneous ignition, no fire advance on submits
    e.set_turn(0)                             # no overtime heat
    for c in range(COLS):
        for r in range(ROWS):
            if g['cell'][c][r][1] == BURN: e.set_cell(c, r, TILE, 'e')

def to_title():
    """Pause + B from play (or whatever state) back to the title screen."""
    ensure_play()
    e.press('START', settle=10); e.press('B', settle=30)
    g = e.state(); assert g['state'] == ST_TITLE, f'pause + B did not quit: state {g["state"]}'

def fresh_shelf():
    """Pause + SELECT: restart the shelf on a fresh random board (no holes, nothing revealed),
    with the shelf's own fire settings. A restart costs a bookmark, so the run is topped up first."""
    ensure_play()
    e.set_lives(3)
    e.press('START', settle=10); e.press('SELECT', settle=150)
    g = e.state(); assert g['state'] == ST_PLAY and g['turn'] == 0, f'restart: state {g["state"]} turn {g["turn"]}'
    return g

def clear_shelf():
    """Peel every pass letter in order (a perfect pass) with the fire held off, then tap through
    the results card. Returns the state at the start of the next shelf."""
    g = e.state(); assert g['state'] == ST_PLAY, f'clear_shelf: state {g["state"]}'
    shelf, n = g['shelf'], g['passLen']
    for i in range(1, n + 1):
        e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
        g = e.state()
        for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
        c, r = e.secret_cells(g)[i]
        assert g['cell'][c][r][1] == TILE, f'shelf {shelf} secret {i} at {(c, r)} is {g["cell"][c][r]}'
        e.peel(c, r)
        g = e.state(); assert g['state'] in (ST_PLAY, ST_PASSED), f'shelf {shelf} peel {i}: state {g["state"]} fail {g["fail"]}'
    assert g['state'] == ST_PASSED and g['found'] == [1] * n, f'shelf {shelf} not cleared: {g["state"]} {g["found"]}'
    e.run(40); e.tap(e.bx + 100, 100); e.run(20)
    g = e.state(); assert g['state'] == ST_PLAY and g['shelf'] == shelf + 1, f'after shelf {shelf}: state {g["state"]} shelf {g["shelf"]}'
    return g

def secret_rows(g): return [r for c in range(COLS) for r in range(ROWS) if g['back'][c][r]]

def col(g, c): return [g['cell'][c][r][1] for r in range(ROWS)]

def topmost_tile_row(g, c):
    for r in range(ROWS):
        if g['cell'][c][r][1] == TILE: return r
    return -1

def play_turn(pick):
    """One word chosen by pick(words, g); returns (g_before, word, path, g_after) or None."""
    g0 = e.state()
    ws = e.find_words(g0)
    if not ws: return None
    word, path = pick(ws, g0)
    e.chain(path); e.press('A')
    g1 = e.state()
    assert g1['lastWord'] == word or g1['state'] != ST_PLAY, f'{word} did not score: last {g1["lastWord"]!r}'
    return g0, word, path, g1

def shortest(ws, g): return min(ws, key=lambda w: len(w[0]))
def longest_fire_aware(ws, g):
    """Longest word, but a word through the lowest flame first when the shelf is burning."""
    burning = e.burn_cells(g)
    if burning:
        low = max(r for _, r in burning)
        through = [w for w in ws if any((c, r) in w[1] for (c, r) in burning if r == low)]
        if not through: through = [w for w in ws if any(cell in w[1] for cell in burning)]
        if through: return max(through, key=lambda w: len(w[0]))
    return max(ws, key=lambda w: len(w[0]))

# ---- title and startup -------------------------------------------------------------
@scenario('title ignores a tap outside the buttons and CLASSIC still starts')
def _():
    e.run(200)
    e.tap(160, 180); e.run(20)
    assert e.state()['state'] == ST_TITLE, 'a stray title tap started a game'
    e.tap(52, 147); e.run(150)
    assert e.state()['state'] == ST_PLAY, 'CLASSIC did not start'

@scenario('play runs at 60 fps with the poke channel in debugSync')
def _():
    fps = e.fps(); assert fps == 120, f'{fps}/120'

# ---- fire ------------------------------------------------------------------------------
@scenario('fire falls through a hole onto a tile')
def _():
    ensure_play()
    e.set_cell(C, 0, BURN, 'a'); e.set_cell(C, 1, HOLE); e.set_cell(C, 2, TILE, 'b')
    e.fire_step(); g = e.state()
    assert g['cell'][C][2] == (1, BURN), f'flame did not land on the tile: {col(g, C)}'
    assert g['cell'][C][1][1] == HOLE, 'the hole was overwritten'
    assert g['cell'][C][0][1] == TILE, 'the vacated top cell did not refill'
    assert g['fail'] == 0
    e.shot(f'{out}/rules_chimney.png')

@scenario('fire blocked by a diamond stays above it (through a hole)')
def _():
    ensure_play()
    e.set_cell(C, 0, TILE, 'e'); e.set_cell(C, 1, BURN, 'a'); e.set_cell(C, 2, HOLE); e.set_cell(C, 3, SECRET, 'b')
    e.fire_step(); g = e.state()
    assert col(g, C)[:4] == [TILE, BURN, HOLE, SECRET], f'column changed: {col(g, C)}'
    assert g['scorched'][C][2] == 0 and g['fail'] == 0
    e.set_cell(C, 1, TILE, 'e'); e.set_cell(C, 2, TILE, 'e'); e.set_cell(C, 3, TILE, 'e')

@scenario('a column poked to B,H,S keeps the hole at state 3 after a word is submitted')
def _():
    ensure_play()
    e.set_cell(C, 0, BURN, 'a'); e.set_cell(C, 1, HOLE); e.set_cell(C, 2, SECRET, 'b')
    e.set_burn(0, 1)                                   # the fire advances on this turn
    g = e.state(); ws = e.find_words(g, avoid={(C, r) for r in range(ROWS)})
    assert ws, 'no word away from the poked column'
    e.chain(ws[0][1]); before = g['score']; e.press('A')
    g = e.state(); assert g['score'] > before, f'{ws[0][0]} did not score'
    assert g['cell'][C][1][1] == HOLE, f'hole lost: {col(g, C)}'
    assert g['cell'][C][0][1] == BURN and g['cell'][C][2][1] == SECRET, f'column changed: {col(g, C)}'
    e.set_burn(0, 99)
    for r in range(3): e.set_cell(C, r, TILE, 'e')

@scenario('scorching a secret: fail 2, msg SCORCHED, PASS DESTROYED card')
def _():
    ensure_play()
    e.set_cell(C, 0, BURN, 'a'); e.set_cell(C, 1, TILE, 'b'); e.set_back(C, 1, 1)
    # frame-exact peel: L held, stylus down on the gem, step until the state flips
    e.hold('L', True); e.emu.input.touch_set_pos(*e.center(C, 1)); e.run(3)
    e.emu.input.touch_release()          # peel on release
    seen = None
    for i in range(10):
        e.run(1); g = e.state()
        if g['state'] == ST_FAIL: seen = g; break
    e.hold('L', False)
    assert seen, 'peel did not end the run'
    assert seen['fail'] == FAIL_SCORCH, f'fail {seen["fail"]}'
    assert seen['msg'] == 'SCORCHED', f'msg {seen["msg"]!r} on the fail frame'
    assert seen['cell'][C][1][1] == HOLE and seen['scorched'][C][1] == 1
    e.run(40); e.shot(f'{out}/rules_scorched.png')

# ---- words -----------------------------------------------------------------------------
@scenario('OUT OF WORDS: all-x board, one plain peel gives fail 3')
def _():
    ensure_play()
    for c in range(COLS):
        for r in range(ROWS): e.set_cell(c, r, TILE, 'x')
    g = e.state()
    plain = [(c, r) for c in range(COLS) for r in range(ROWS) if not g['back'][c][r]]
    c, r = plain[len(plain) // 2]
    e.peel(c, r); g = e.state()
    assert g['fail'] == FAIL_WORDS, f'fail {g["fail"]} msg {g["msg"]!r}'
    assert g['state'] == ST_FAIL
    assert all(g['cell'][cc][rr][0] == 23 for cc in range(COLS) for rr in range(ROWS) if (cc, rr) != (c, r)), 'letters refilled'
    e.shot(f'{out}/rules_outofwords.png')

@scenario('Qu + I + Z scores quiz')
def _():
    ensure_play()
    e.set_cell(C, 0, TILE, 'q'); e.set_cell(C, 1, TILE, 'i'); e.set_cell(C, 2, TILE, 'z')
    e.run(10)
    path = [(C, 0), (C, 1), (C, 2)]
    e.chain(path); g = e.state()
    assert g['chain'] == path, f'chain {g["chain"]}'
    before = g['score']; e.press('A'); g = e.state()
    assert g['lastWord'] == 'quiz' and g['score'] > before, f'{g["lastWord"]!r} {g["score"]}'

@scenario('chain backtracking')
def _():
    ensure_play()
    for r in range(4): e.set_cell(C, r, TILE, 'e')
    e.run(10)
    e.chain([(C, 0), (C, 1), (C, 2)], release=False)
    assert e.state()['chainLen'] == 3
    e.chain([(C, 1)], release=False); g = e.state()
    assert g['chain'] == [(C, 0), (C, 1)], f'backtrack: {g["chain"]}'
    e.chain([(C, 0)], release=False); g = e.state()
    assert g['chain'] == [(C, 0)], f'second backtrack: {g["chain"]}'
    e.chain([(C, 1), (C, 2), (C, 3)], release=False)
    e.chain([(C, 1)], release=False); g = e.state()          # a held jump two gems back retraces the gems it crosses
    assert g['chain'] == [(C, 0), (C, 1)], f'retrace: {g["chain"]}'
    e.emu.input.touch_release(); e.run(3); e.press('Y', settle=5)

@scenario('shatter shards after an all-vowel word are ruby-dominant')
def _():
    ensure_play()
    e.set_cell(C, 0, TILE, 'e'); e.set_cell(C, 1, TILE, 'a'); e.set_cell(C, 2, TILE, 'u')
    e.run(30)
    e.chain([(C, 0), (C, 1), (C, 2)])
    assert e.state()['chainLen'] == 3
    x0, y0, x1, y1 = e.cell_rect(C, 2)       # the bottom gem: its slot stays free of refills for ~10 frames
    def sample():
        px = e.pixels(); ruby = blue = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                R, G, B = [v >> 3 for v in px[x, y]]
                if R >= 27 and B <= 20 and G <= 16: ruby += 1          # ruby shard
                elif B >= 24 and R <= 16 and G <= 22: blue += 1        # sapphire
        return ruby, blue
    k = keymask(Keys.KEY_A); e.emu.input.keypad_add_key(k)
    best = (0, 0)
    for f in range(6):
        e.run(1)
        if f == 1: e.emu.input.keypad_rm_key(k)
        s = sample()
        if s[0] > best[0]: best = s; e.shot(f'{out}/rules_shards.png')
    e.run(40)
    g = e.state(); assert g['lastWord'] == 'eau', f'eau did not score: {g["lastWord"]!r}'
    assert best[0] >= 16 and best[0] > 3 * best[1], f'ruby {best[0]} blue {best[1]} pixels'

# ---- turns, heat and refills (item 06) ---------------------------------------------------
@scenario('overtime: a 7-letter word at turn 30 still heats the shelf')
def _():
    ensure_play()
    for r, ch in enumerate('letters'): e.set_cell(C, r, TILE, ch)
    e.run(10); e.set_turn(30)
    h0 = e.state()['heat']
    e.chain([(C, r) for r in range(7)]); e.press('A'); g = e.state()
    assert g['lastWord'] == 'letters', f'last word {g["lastWord"]!r}'
    assert g['turn'] == 31 and g['overtime'] == 1, f'turn {g["turn"]} overtime {g["overtime"]}'
    assert g['heat'] >= h0 + 1, f'heat {h0} -> {g["heat"]}: no overtime heat'
    e.set_turn(0)

@scenario('fire spawns on the topmost gem: holes across row 0, the flame lands in row 1, heat resets')
def _():
    ensure_play()
    for c in range(COLS): e.set_cell(c, 0, HOLE)
    e.set_cell(C, 1, TILE, 'b'); e.set_cell(C, 2, TILE, 'e'); e.set_cell(C, 3, TILE, 'd')
    g = e.state(); e.set_heat(g['heatThreshold'] - 1); e.run(5)
    e.chain([(C, 1), (C, 2), (C, 3)]); e.press('A'); g = e.state()
    assert g['lastWord'] == 'bed' and g['state'] == ST_PLAY, f'{g["lastWord"]!r} state {g["state"]} fail {g["fail"]}'
    burns = e.burn_cells(g)
    want = 2 if g['lockTier'] >= 2 else 1
    assert len(burns) == want and all(r == 1 for _, r in burns), f'flames {burns} (want {want} in row 1)'
    assert g['heat'] == 0, f'heat {g["heat"]} after the spawn'
    assert all(g['cell'][c][0][1] == HOLE for c in range(COLS)), 'a hole was overwritten'
    e.shot(f'{out}/rules_spawn_row1.png')
    for c in range(COLS): e.set_cell(c, 0, TILE, 'e')
    for c, r in burns: e.set_cell(c, r, TILE, 'e')

@scenario('gems refill under a hole and never paint over it while falling')
def _():
    ensure_play()
    e.set_back(C, 0, 0)
    for r in range(4): e.set_cell(C, r, TILE, 'e')
    e.run(5); e.peel(C, 0); g = e.state()
    assert g['cell'][C][0][1] == HOLE and g['turn'] == 1, f'peel: {col(g, C)} turn {g["turn"]}'
    e.set_cell(C, 1, TILE, 'b'); e.set_cell(C, 2, TILE, 'e'); e.set_cell(C, 3, TILE, 'd'); e.run(20)
    x0, y0, x1, y1 = e.cell_rect(C, 0)
    def grab():
        px = e.pixels(); return {(x, y): px[x, y] for y in range(y0, y1) for x in range(x0, x1)}
    ref = grab()
    def longest_run(cur):
        worst = 0
        for y in range(y0, y1):
            run = 0
            for x in range(x0, x1):
                digit = x0 + 9 <= x <= x0 + 16 and y0 + 7 <= y <= y0 + 16   # the blinking back number
                if cur[x, y] != ref[x, y] and not digit: run += 1; worst = max(worst, run)
                else: run = 0
        return worst
    e.chain([(C, 1), (C, 2), (C, 3)])
    k = keymask(Keys.KEY_A); e.emu.input.keypad_add_key(k)
    worst = (0, -1)
    for f in range(16):
        e.run(1)
        if f == 1: e.emu.input.keypad_rm_key(k)
        if f == 2: e.shot(f'{out}/rules_hole_refill.png')
        run = longest_run(grab())
        if run > worst[0]: worst = (run, f)
    e.run(60); g = e.state()
    assert g['lastWord'] == 'bed', f'bed did not score: {g["lastWord"]!r}'
    assert col(g, C)[:4] == [HOLE, TILE, TILE, TILE], f'column after the refill: {col(g, C)}'
    assert worst[0] < 12, f'a gem painted over the hole: {worst[0]} px run on frame {worst[1]}'

@scenario('ten plain peels: ten turns, ten heat (or the flame it lit)')
def _():
    ensure_play()
    g = e.state(); thr = g['heatThreshold']
    done = 0
    for c in range(COLS):
        for r in (ROWS - 1, ROWS - 2):
            if done >= 10: break
            g = e.state()
            if g['cell'][c][r][1] != TILE or g['back'][c][r]: continue
            e.peel(c, r); done += 1
            g = e.state(); assert g['state'] == ST_PLAY, f'peel {done} ended the run: fail {g["fail"]}'
    g = e.state()
    assert done == 10 and g['turn'] == 10, f'{done} peels, turn {g["turn"]}'
    assert g['heat'] >= min(10, thr) - 1 or e.burn_cells(g), f'heat {g["heat"]} and no flame after ten peels'
    e.shot(f'{out}/rules_ten_peels.png')

@scenario('short words burn the shelf down within 60 turns; flames light the topmost gem and creep one row per step')
def _():
    fresh_shelf()
    e.set_burn(0, 99)                          # phase 1: still fire, check where new flames appear
    spawns = 0
    for t in range(24):
        res = play_turn(shortest)
        assert res, 'no words'
        g0, word, path, g1 = res
        if g1['state'] != ST_PLAY: break
        if g1['heat'] == 0:                    # a word adds heat, so 0 means a flame was lit
            new = [cell for cell in e.burn_cells(g1) if cell not in e.burn_cells(g0)]
            assert new, f'heat reset without a new flame on turn {g1["turn"]}'
            for c, r in new:
                above = [g1['cell'][c][rr][1] for rr in range(r)]
                assert TILE not in above, f'flame at ({c},{r}) is not the topmost gem: {above}'
            spawns += 1
    assert spawns >= 2, f'only {spawns} spawns in 24 short words'
    g = e.state(); assert g['state'] == ST_PLAY, f'phase 1 ended the run: fail {g["fail"]}'
    # phase 2: the shelf's own pace from a clean board; the lowest flame drops at most one row a turn
    g = fresh_shelf(); e.set_burn(0, g['burnEvery'])
    log = []
    for t in range(60):
        res = play_turn(shortest)
        if not res: break
        g0, word, path, g1 = res
        low0, low1 = e.lowest_fire_row(g0), e.lowest_fire_row(g1)
        log.append((g1['turn'], word, g1['heat'], low1))
        assert low1 <= low0 + 1, f'turn {g1["turn"]}: lowest flame {low0} -> {low1} ({word})'
        if g1['state'] != ST_PLAY: break
    g = e.state()
    print('   burn log:', ' '.join(f'{t}:{w}/h{h}/f{l}' for t, w, h, l in log))
    assert g['state'] == ST_FAIL and g['fail'] == FAIL_FIRE, f'state {g["state"]} fail {g["fail"]} after {g["turn"]} turns'
    assert g['turn'] <= 60
    e.shot(f'{out}/rules_burned_down.png')

@scenario('long words through the flames survive 30 turns')
def _():
    g = fresh_shelf(); e.set_burn(0, g['burnEvery'])
    for t in range(30):
        res = play_turn(longest_fire_aware)
        assert res, f'no words on turn {t}'
        g = res[3]
        assert g['state'] == ST_PLAY, f'turn {g["turn"]}: fail {g["fail"]} after {res[1]}'
    g = e.state()
    assert g['turn'] >= 30 and g['state'] == ST_PLAY, f'turn {g["turn"]} state {g["state"]}'
    print(f'   survived: turn {g["turn"]} heat {g["heat"]} flames {e.burn_cells(g)} score {g["score"]}')
    e.shot(f'{out}/rules_survived.png')

# ---- scoring (item 07) --------------------------------------------------------------------
def calm():
    """A fresh shelf with the fire held still (fresh_shelf restores the shelf's own settings)."""
    g = fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); return g

def spell(path, settle=50):
    """Chain `path`, read the preview, press A; returns (preview, gain, g_after)."""
    e.chain(path); g0 = e.state()
    assert g0['chainLen'] == len(path), f'chain {g0["chain"]} for {path}'
    preview = g0['chainScore']
    e.press('A', settle=settle); g1 = e.state()
    return preview, g1['score'] - g0['score'], g1

@scenario('chainScore previews the exact gain for five words (one through a flame); ledger line stays in rows 137..143')
def _():
    calm()
    e.set_cell(C, 0, BURN, 'b'); e.set_cell(C, 1, TILE, 'e'); e.set_cell(C, 2, TILE, 'd'); e.run(5)
    spelled = []
    for i in range(5):
        e.set_heat(0)
        g = e.state(); ws = e.find_words(g)
        if i == 0: ws = [w for w in ws if (C, 0) in w[1]]
        assert ws, f'no word on turn {i}'
        word, path = ws[0]
        burn = any(g['cell'][c][r][1] == BURN for c, r in path)
        diamonds = sum(g['cell'][c][r][1] == SECRET for c, r in path)
        want = e.word_score(word, burn=burn, diamonds=diamonds, repeat=word in spelled[-8:])
        preview, gain, g1 = spell(path)
        assert g1['lastWord'] == word, f'{word} did not score: {g1["lastWord"]!r}'
        assert preview == gain == want, f'{word}: preview {preview} gain {gain} want {want} (burn {burn} diamonds {diamonds})'
        if i == 0: assert burn and g1['msg'] != 'AGAIN', 'first word did not go through the flame'
        spelled.append(word)
    g = e.state(); assert g['ledgerN'] == 5, f'ledgerN {g["ledgerN"]}'
    assert g['chainScore'] == 0, 'preview with no chain'
    e.shot(f'{out}/rules_ledger.png')
    px = e.pixels(); bad = []; lit = 0
    for y in range(126, 150):
        for x in range(10, 225):
            if x % 8 == 4 and y % 8 == 4: continue           # the accent dot grid
            if sum(px[x, y]) > 60:
                if 137 <= y <= 143: lit += 1
                else: bad.append((x, y))
    assert lit > 40, f'no ledger text in rows 137..143 ({lit} px)'
    assert not bad, f'{len(bad)} lit pixels outside rows 137..143 near the ledger, first {bad[:3]}'

@scenario('a word through a revealed pass letter: PASS LINK! and x1.5')
def _():
    g = calm()
    e.set_back(C, 0, 1); e.peel(C, 0); g = e.state()
    assert g['cell'][C][0][1] == SECRET and g['found'][0] == 1, f'peel: {col(g, C)} found {g["found"]}'
    gem = e.letter(g['cell'][C][0])
    if e.words is None: e.words = load_words()
    word = next(w for w in sorted(e.words) if w.startswith(gem) and len(w) == len(gem) + 2 and 'q' not in w[len(gem):])
    e.set_cell(C, 1, TILE, word[len(gem)]); e.set_cell(C, 2, TILE, word[len(gem) + 1]); e.run(5)
    preview, gain, g1 = spell([(C, 0), (C, 1), (C, 2)])
    want = e.word_score(word, diamonds=1)
    assert g1['lastWord'] == word, f'{word} did not score: {g1["lastWord"]!r}'
    assert preview == gain == want == e.word_score(word) * 3 // 2, f'{word}: preview {preview} gain {gain} want {want}'
    assert g1['msg'] == 'PASS LINK!', f'msg {g1["msg"]!r}'
    assert g1['cell'][C][0][1] == SECRET, 'the diamond was cleared by the word'

@scenario('the same word twice: AGAIN and exactly half')
def _():
    calm()
    for r, ch in enumerate('bed'): e.set_cell(C, 1 + r, TILE, ch)
    e.run(5)
    p1, gain1, g1 = spell([(C, 1), (C, 2), (C, 3)])
    assert g1['lastWord'] == 'bed' and p1 == gain1 == e.word_score('bed'), f'first bed: {p1} {gain1} {g1["lastWord"]!r}'
    e.set_heat(0)
    for r, ch in enumerate('bed'): e.set_cell(C, 1 + r, TILE, ch)
    e.run(5)
    p2, gain2, g2 = spell([(C, 1), (C, 2), (C, 3)], settle=10)
    assert g2['msg'] == 'AGAIN', f'msg {g2["msg"]!r}'
    assert p2 == gain2 == gain1 // 2, f'second bed: preview {p2} gain {gain2}, first {gain1}'
    assert g2['ledgerN'] == 2
    e.run(40)

@scenario('perfect pass: only the secrets peeled gives kind 2, +400 per shelf and the speed bonus')
def _():
    g = calm(); score0 = g['score']; shelf = g['shelf']; n = g['passLen']
    for i in range(1, n + 1):
        e.set_heat(0); g = e.state()
        c, r = e.secret_cells(g)[i]
        assert g['cell'][c][r][1] == TILE, f'secret {i} at {(c, r)} is {g["cell"][c][r]}'
        e.peel(c, r)
    g = e.state()
    assert g['state'] == ST_PASSED and g['peels'] == n, f'state {g["state"]} peels {g["peels"]} fail {g["fail"]}'
    shelfB, speed, peelB, kind = e.pass_bonus(shelf, g['turn'], g['peels'], n)
    assert kind == 2 and g['passBonusKind'] == 2 and g['peelBonus'] == 400 * shelf and g['speedBonus'] == speed, \
        f'kind {g["passBonusKind"]} peel {g["peelBonus"]} speed {g["speedBonus"]} (want {speed})'
    want = 100 * n * shelf + shelfB + speed + peelB
    assert g['score'] - score0 == want, f'pass paid {g["score"] - score0}, want {want}'
    e.run(40); e.shot(f'{out}/rules_perfect.png')
    fps = e.fps(); assert fps >= 118, f'results fps {fps}/120'

# ---- content and progression (item 08) ---------------------------------------------------
@scenario('eight CLASSIC taps at different title spots give at least two shelf-1 passes, all 4-letter BINDERY')
def _():
    passes = set()
    for i in range(8):
        to_title(); e.run(3 + 7 * i)
        e.tap(e.bx + 24 + i * 8, 136 + i * 2); e.run(150)
        g = e.state()
        assert g['state'] == ST_PLAY and g['mode'] == 0, f'tap {i}: state {g["state"]} mode {g["mode"]}'
        assert g['passTheme'] == 0 and g['passLen'] == 4 and g['lives'] == 3 and g['twist'] == 0, \
            f'tap {i}: pass {g["pass"]!r} theme {g["passTheme"]} lives {g["lives"]} twist {g["twist"]}'
        passes.add(g['pass'])
    print('   shelf-1 passes:', sorted(passes))
    assert len(passes) >= 2, f'eight starts gave one pass: {passes}'

@scenario('30 Classic boards: at least 25% vowels and at most three of J/Q/X/Z')
def _():
    worst_v, worst_r = 1.0, 0
    for i in range(30):
        g = fresh_shelf()
        v, rare, n = e.letter_mix(g)
        assert v * 4 >= n, f'board {i}: {v} vowels of {n} ({g["pass"]})'
        assert rare <= 3, f'board {i}: {rare} rare letters'
        worst_v = min(worst_v, v / n); worst_r = max(worst_r, rare)
    print(f'   worst vowel share {worst_v:.2f}, most rare letters {worst_r}')

@scenario('two Daily starts: identical letters, back layer and pass; no twist')
def _():
    boards = []
    for k in range(2):
        to_title(); e.start_daily()
        g = e.state(); assert g['state'] == ST_PLAY and g['mode'] == 1, f'daily {k}: state {g["state"]} mode {g["mode"]}'
        boards.append(g)
    a, b = boards
    assert a['pass'] == b['pass'] and a['back'] == b['back'], f'pass {a["pass"]}/{b["pass"]}'
    assert [[x[0] for x in col] for col in a['cell']] == [[x[0] for x in col] for col in b['cell']], 'letters differ'
    assert a['twist'] == 0 and 5 <= a['passLen'] <= 7 and a['dailySeed'] == b['dailySeed'], f'twist {a["twist"]} len {a["passLen"]}'
    print('   daily', a['dailySeed'], a['pass'], 'theme', a['passTheme'])

@scenario('Daily refills are identical whatever the frame gap between words (run(5) vs run(90))')
def _():
    def daily_run(gap):
        to_title(); e.start_daily()
        log = []
        for i in range(4):
            e.run(gap)
            g = e.state(); assert g['state'] == ST_PLAY, f'gap {gap} word {i}: state {g["state"]} fail {g["fail"]}'
            ws = e.find_words(g); assert ws, 'no words'
            word, path = ws[0]
            e.chain(path); e.press('A', settle=50)
            g = e.state(); assert g['lastWord'] == word, f'gap {gap}: {word} did not score ({g["lastWord"]!r})'
            log.append((word, [[x for x in col] for col in g['cell']], g['heat'], e.burn_cells(g)))
        return log
    a, b = daily_run(5), daily_run(90)
    for i, (wa, wb) in enumerate(zip(a, b)):
        assert wa[0] == wb[0], f'word {i}: {wa[0]} vs {wb[0]}'
        assert wa[1] == wb[1], f'refill after word {i} ({wa[0]}) differs'
        assert wa[2:] == wb[2:], f'heat/fire after word {i} differ: {wa[2:]} vs {wb[2:]}'
    print('   daily words:', [w for w, _, _, _ in a])

@scenario('shelf 4 twist (message, holes or depth), then shelf 12: Lockdown tier 2 with the pass in rows 5..7')
def _():
    to_title(); e.start_classic()
    g = e.state(); assert g['shelf'] == 1 and g['lives'] == 3
    for _ in range(3): g = clear_shelf()
    assert g['shelf'] == 4 and g['twist'] in (1, 2, 3) and g['turn'] == 0, f'shelf {g["shelf"]} twist {g["twist"]} turn {g["turn"]}'
    name = {1: 'INK SPILL', 2: 'DAMP', 3: 'BURIED'}[g['twist']]
    assert g['msg'] == f'SHELF 4: {name}', f'intro msg {g["msg"]!r}'
    assert g['passTheme'] == 3, f'shelf 4 theme {g["passTheme"]}'
    e.run(20); e.shot(f'{out}/rules_twist_shelf4.png')
    holes = [(c, r) for c in range(COLS) for r in range(ROWS) if g['cell'][c][r][1] == HOLE]
    if g['twist'] == 1:
        assert len(holes) == 3 and all(g['back'][c][r] == 0 and 2 <= r <= 6 for c, r in holes), f'INK SPILL holes {holes}'
        assert g['passLen'] == 5
    elif g['twist'] == 3:
        assert min(secret_rows(g)) >= 5 and g['passLen'] == 6, f'BURIED rows {secret_rows(g)} len {g["passLen"]}'
        assert not holes
    else:
        assert g['burnEvery'] == 2 and g['heatThreshold'] == 5 and g['passLen'] == 5 and not holes, f'DAMP {g["burnEvery"]} {g["heatThreshold"]}'
    e.run(140); g = e.state()
    assert g['msg'] == f'TALES: FIND THE {g["passLen"]} LETTER PASS', f'queued intro {g["msg"]!r}'
    print('   shelf 4:', name, g['pass'])
    twists = {}
    while g['shelf'] < 12:
        g = clear_shelf()
        if g['twist']: twists[g['shelf']] = g['twist']
        else: assert g['shelf'] % 3 != 1, f'shelf {g["shelf"]} has no twist'
    assert g['shelf'] == 12 and g['lockdown'] == 1 and g['lockTier'] == 2 and g['twist'] == 0, f'shelf 12: lock {g["lockdown"]} tier {g["lockTier"]} twist {g["twist"]}'
    assert min(secret_rows(g)) >= 5, f'shelf 12 secret rows {secret_rows(g)}'
    assert g['passTheme'] == 11 and g['passLen'] == 7, f'shelf 12 pass {g["pass"]!r} theme {g["passTheme"]}'
    assert g['lives'] == 3, f'lives {g["lives"]} after a clean climb'
    print('   twists on the way:', twists, 'shelf 12 pass', g['pass'])
    e.run(30); e.shot(f'{out}/rules_shelf12.png')
    fps = e.fps(); assert fps == 120, f'shelf 12 fps {fps}/120'

@scenario('bookmarks: a pause restart costs one and is refused on the last; three fails read 2, 1, 0 then the title')
def _():
    to_title(); e.start_classic()
    assert e.state()['lives'] == 3
    for want in (2, 1):
        e.press('START', settle=10); e.press('SELECT', settle=150)
        g = e.state(); assert g['state'] == ST_PLAY and g['lives'] == want and g['turn'] == 0, f'restart: state {g["state"]} lives {g["lives"]}'
    e.press('START', settle=10); e.press('SELECT', settle=20)
    g = e.state(); assert g['state'] == ST_PAUSE and g['lives'] == 1 and g['msg'] == 'LAST BOOKMARK', f'refusal: state {g["state"]} lives {g["lives"]} msg {g["msg"]!r}'
    e.shot(f'{out}/rules_last_bookmark.png')
    e.press('START', settle=30); assert e.state()['state'] == ST_PLAY
    to_title(); e.start_classic()
    games0 = e.state()['statGames']
    for want in (2, 1, 0):
        g = e.state(); assert g['state'] == ST_PLAY and g['shelf'] == 1 and g['lives'] == want + 1
        for c in range(COLS):
            for r in range(ROWS): e.set_cell(c, r, TILE, 'x')
        g = e.state()
        plain = [(c, r) for c in range(COLS) for r in range(ROWS) if not g['back'][c][r]]
        e.peel(*plain[len(plain) // 2]); g = e.state()
        assert g['state'] == ST_FAIL and g['fail'] == FAIL_WORDS and g['lives'] == want, f'fail: state {g["state"]} lives {g["lives"]}'
        assert g['statGames'] == games0 + (1 if want == 0 else 0), f'games played {g["statGames"]} (was {games0}) after fail with {want} left'
        e.run(60)
        if want == 1: e.shot(f'{out}/rules_fail_bookmarks.png')
        e.tap(e.bx + 100, 100); e.run(150)
        g = e.state()
        if want: assert g['state'] == ST_PLAY and g['lives'] == want, f'retry {want}: state {g["state"]} lives {g["lives"]}'
        else: assert g['state'] == ST_TITLE, f'third fail tap: state {g["state"]}'

# ---- persistence, Daily rules and the ledger (item 09) -------------------------------------
MODS = ['STANDARD', 'EMBER', 'DEEP', 'LONG']
def daily_mod(d): return (((d * 2654435761) & 0xFFFFFFFF) >> 28) & 3
def date_with_mod(mod, start=20270401):
    d = start
    while daily_mod(d) != mod: d += 1
    return d

def force_fail():
    """Every gem an x and one plain peel: OUT OF WORDS, deterministically."""
    for c in range(COLS):
        for r in range(ROWS): e.set_cell(c, r, TILE, 'x')
    g = e.state()
    plain = [(c, r) for c in range(COLS) for r in range(ROWS) if not g['back'][c][r]]
    e.peel(*plain[len(plain) // 2])
    g = e.state(); assert g['state'] == ST_FAIL and g['fail'] == FAIL_WORDS, f'force_fail: state {g["state"]} fail {g["fail"]}'
    e.run(60)
    return g

def force_pass():
    """Peel the pass in order with the fire held off: a perfect pass, stopping at the results card."""
    g = e.state(); n = g['passLen']
    for i in range(1, n + 1):
        e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
        g = e.state()
        for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
        c, r = e.secret_cells(g)[i]
        e.peel(c, r)
    g = e.state(); assert g['state'] == ST_PASSED, f'force_pass: state {g["state"]} fail {g["fail"]}'
    e.run(40)
    return g

def leave_result():
    e.tap(e.bx + 100, 100); e.run(30)
    g = e.state(); assert g['state'] == ST_TITLE, f'tap on a Daily result card: state {g["state"]}'
    return g

def daily_title(day, played=0, last=0, streak=0):
    """Title screen with the DS clock at `day` and the Daily records set; returns the state."""
    e.set_clock(day); e.set_daily(played, last, streak); e.run(70)
    g = e.state(); assert g['state'] == ST_TITLE and g['clockOk'] == 1 and g['dailySeed'] == day, f'daily_title: {g["state"]} {g["dailySeed"]}'
    return g

def tap_daily(wait=150):
    e.tap(e.bx + 120, 147); e.run(wait)

@scenario('Daily: one scored attempt (words counted, fail recorded), then PRACTICE records nothing')
def _():
    to_title()
    day = 20270310
    daily_title(day)
    e.shot(f'{out}/rules_daily_fresh_title.png')
    g0 = e.state(); words0, games0 = g0['statWords'], g0['statGames']
    tap_daily(30)
    g = e.state()
    assert g['state'] == ST_PLAY and g['mode'] == 1 and g['practice'] == 0 and g['dailySeed'] == day, f'daily start: {g["state"]} practice {g["practice"]}'
    assert g['dailyPlayed'] == day, f'the attempt is spent at once: dailyPlayed {g["dailyPlayed"]}'
    assert g['dailyMod'] == daily_mod(day), f'modifier {g["dailyMod"]} want {daily_mod(day)}'
    assert g['msg'] == f'DAILY: {MODS[daily_mod(day)]}', f'start msg {g["msg"]!r}'
    e.run(120)
    r = play_turn(shortest); assert r, 'no word to spell'
    g = e.state(); assert g['statWords'] == words0 + 1, f'scored Daily word counted: {g["statWords"]} (was {words0})'
    e.press('START', settle=10); e.press('SELECT', settle=20)
    g = e.state(); assert g['state'] == ST_PAUSE and g['msg'] == 'NO RESTART IN DAILY', f'Daily restart: state {g["state"]} msg {g["msg"]!r}'
    e.shot(f'{out}/rules_daily_pause.png')
    e.press('START', settle=30); assert e.state()['state'] == ST_PLAY
    score = e.state()['score']
    g = force_fail()
    assert g['statGames'] == games0 + 1 and g['streak'] == 1 and g['dailyLast'] == day, f'record: games {g["statGames"]} streak {g["streak"]} last {g["dailyLast"]}'
    assert g['dailyLastScore'] == score, f'last score {g["dailyLastScore"]} want {score}'
    leave_result()
    g = daily_title(day, played=day, last=day, streak=1)
    e.shot(f'{out}/rules_daily_practice_title.png')
    # the second run today is practice: same board, announced, never recorded
    words1, games1, last1 = g['statWords'], g['statGames'], g['dailyLastScore']
    tap_daily(30)
    g = e.state()
    assert g['state'] == ST_PLAY and g['practice'] == 1 and g['msg'] == 'PRACTICE RUN: NOT SCORED', f'practice: {g["state"]} {g["practice"]} {g["msg"]!r}'
    e.run(120)
    r = play_turn(shortest); assert r, 'no word to spell'
    g = e.state(); assert g['statWords'] == words1, f'a practice word was counted: {g["statWords"]} (was {words1})'
    e.shot(f'{out}/rules_daily_practice_play.png')
    force_fail(); leave_result()
    g = e.state()
    assert g['statGames'] == games1 and g['streak'] == 1 and g['dailyLastScore'] == last1 and g['dailyLast'] == day, \
        f'practice recorded: games {g["statGames"]} streak {g["streak"]} last score {g["dailyLastScore"]}'

@scenario('Daily streak: next day +1 (results card shows it), a skipped day or a date in the past resets to 1')
def _():
    to_title()
    d1, d2, d4 = 20270310, 20270311, 20270314
    daily_title(d2, played=d1, last=d1, streak=1)
    tap_daily()
    g = e.state(); assert g['practice'] == 0 and g['dailyPlayed'] == d2, f'next day starts scored: practice {g["practice"]}'
    games0 = g['statGames']
    g = force_pass()
    assert g['streak'] == 2 and g['dailyLast'] == d2 and g['statGames'] == games0 + 1, f'next day: streak {g["streak"]} games {g["statGames"]}'
    e.shot(f'{out}/rules_daily_result_streak.png')
    leave_result()
    daily_title(d2, played=d2, last=d2, streak=2); e.shot(f'{out}/rules_daily_title_streak.png')
    # a practice pass the same day: the card says so and nothing changes
    tap_daily()
    g = force_pass(); assert g['practice'] == 1
    e.shot(f'{out}/rules_daily_result_practice.png')
    leave_result()
    g = e.state(); assert g['streak'] == 2 and g['statGames'] == games0 + 1, f'practice pass recorded: streak {g["streak"]}'
    # two days missed
    daily_title(d4, played=d2, last=d2, streak=2); tap_daily(); force_fail(); leave_result()
    g = e.state(); assert g['streak'] == 1 and g['dailyLast'] == d4, f'missed days: streak {g["streak"]} last {g["dailyLast"]}'
    # the clock moves back before the last scored date: the streak counts as broken
    daily_title(d1, played=d2, last=d4, streak=5); tap_daily(); force_fail(); leave_result()
    g = e.state(); assert g['streak'] == 1 and g['dailyLast'] == d1, f'date in the past: streak {g["streak"]} last {g["dailyLast"]}'
    # walking away from a scored Daily forfeits it: the score so far stands and the attempt stays spent
    daily_title(d2, played=d1, last=d1, streak=1); tap_daily()
    assert e.state()['practice'] == 0
    e.press('START', settle=10); e.press('B', settle=30)
    g = e.state(); assert g['state'] == ST_TITLE and g['dailyPlayed'] == d2 and g['dailyLast'] == d2 and g['streak'] == 2, \
        f'quit Daily: {g["state"]} played {g["dailyPlayed"]} last {g["dailyLast"]} streak {g["streak"]}'

@scenario('Daily needs a real clock: tap and SELECT are ignored with SET THE DS CLOCK FOR DAILY')
def _():
    to_title()
    e.set_clock(19990101); e.run(70)
    g = e.state(); assert g['state'] == ST_TITLE and g['clockOk'] == 0, f'clock: {g["state"]} ok {g["clockOk"]}'
    e.shot(f'{out}/rules_daily_noclock_title.png')
    e.tap(e.bx + 120, 147, settle=10)
    g = e.state(); assert g['state'] == ST_TITLE and g['msg'] == 'SET THE DS CLOCK FOR DAILY', f'tap: {g["state"]} {g["msg"]!r}'
    e.run(6); e.shot(f'{out}/rules_daily_noclock_msg.png')
    e.run(150); assert e.state()['msg'] == '', 'the clock message should fade'
    e.press('SELECT', settle=10)
    g = e.state(); assert g['state'] == ST_TITLE and g['msg'] == 'SET THE DS CLOCK FOR DAILY', f'SELECT: {g["state"]} {g["msg"]!r}'
    g0 = e.state()
    e.run(200)
    e.tap(e.bx + 52, 147); e.run(150)
    assert e.state()['state'] == ST_PLAY and e.state()['mode'] == 0, 'Classic must not depend on the clock'
    to_title()
    e.set_clock(0); e.run(70)
    assert e.state()['clockOk'] == 1, 'the real clock should be accepted again'

@scenario('Daily modifiers: STANDARD, EMBER (meter 8, fire every turn), DEEP (pass in rows 5..7), LONG (7-letter pass)')
def _():
    to_title()
    for mod in range(4):
        day = date_with_mod(mod)
        daily_title(day)
        e.shot(f'{out}/rules_daily_mod_{MODS[mod].lower()}_title.png')
        e.set_daily(day, 0, 0)
        tap_daily()
        g = e.state()
        assert g['state'] == ST_PLAY and g['practice'] == 1 and g['dailyMod'] == mod, f'{MODS[mod]}: state {g["state"]} mod {g["dailyMod"]}'
        if mod == 0: assert g['heatThreshold'] == 7 and g['burnEvery'] == 2, f'STANDARD: {g["heatThreshold"]}/{g["burnEvery"]}'
        if mod == 1: assert g['heatThreshold'] == 8 and g['burnEvery'] == 1, f'EMBER: {g["heatThreshold"]}/{g["burnEvery"]}'
        if mod == 2: assert min(secret_rows(g)) >= 5 and g['heatThreshold'] == 7, f'DEEP rows {secret_rows(g)}'
        if mod == 3: assert 6 <= g['passLen'] <= 8, f'LONG pass {g["pass"]!r}'
        print(f'   {day} {MODS[mod]}: {g["pass"]} theme {g["passTheme"]} len {g["passLen"]}')
        e.run(40); e.shot(f'{out}/rules_daily_mod_{MODS[mod].lower()}_play.png')
        to_title()
    e.set_clock(0); e.set_daily(0, 0, 0)

@scenario('THE LEDGER: X and a tap on the BEST line open it, B / X / any tap close it; OPTIONS and CLASSIC unaffected')
def _():
    to_title()
    g = e.state(); assert g['statWords'] > 0 and g['statGames'] > 0, 'earlier scenarios should have left some stats'
    e.press('X', settle=20)
    g = e.state(); assert g['state'] == ST_STATS, f'X: state {g["state"]}'
    e.shot(f'{out}/rules_ledger.png')
    fps = e.fps(); assert fps == 120, f'ledger fps {fps}/120'
    e.press('B', settle=20); assert e.state()['state'] == ST_TITLE, 'B did not leave the ledger'
    e.press('X', settle=20); e.press('X', settle=20); assert e.state()['state'] == ST_TITLE, 'X did not leave the ledger'
    e.tap(e.bx + 86, 185); assert e.state()['state'] == ST_STATS, 'tap on the BEST line did not open the ledger'
    e.tap(e.bx + 20, 20); assert e.state()['state'] == ST_TITLE, 'a tap did not leave the ledger'
    e.tap(e.bx + 86, 150, settle=10); assert e.state()['state'] == ST_TITLE, 'a tap between the buttons opened something'
    e.tap(e.bx + 86, 178); g = e.state(); assert g['state'] == ST_OPTIONS, f'OPTIONS (to y 179) lost to the ledger: {g["state"]}'
    e.press('B', settle=30); assert e.state()['state'] == ST_TITLE
    e.tap(e.bx + 86, 181); assert e.state()['state'] == ST_STATS, 'y 181 should be the ledger'
    e.press('B', settle=20)
    e.tap(e.bx + 52, 147); e.run(150); assert e.state()['state'] == ST_PLAY, 'CLASSIC no longer starts'

# ---- options and the left-hand title --------------------------------------------------
@scenario('options HAND row: optRow 3, hand flips; left-hand title keeps the top screen inside its frame')
def _():
    ensure_play()
    e.press('START', settle=10); e.press('B', settle=30)
    assert e.state()['state'] == ST_TITLE, 'pause + B did not quit to the title'
    e.tap(86, 170); e.run(10)
    g = e.state(); assert g['state'] == ST_OPTIONS and g['optRow'] == 0, f'options: {g["state"]} {g["optRow"]}'
    e.tap(86, 78); e.run(10)                      # rows are 12 px apart from y 36: HAND is row 3
    g = e.state(); assert g['optRow'] == 3, f'optRow {g["optRow"]}'
    assert g['settings']['hand'] == 1, 'HAND did not flip'
    e.set_hand(True)
    e.tap(e.bx + 86, 170); e.run(30)              # BACK, now mirrored
    g = e.state(); assert g['state'] == ST_TITLE and g['optRow'] == 3, f'back: {g["state"]} {g["optRow"]}'
    e.shot(f'{out}/rules_lefthand_title.png')
    px = e.pixels()
    lit = [(y, px[250, y]) for y in range(60, 151) if sum(px[250, y]) > 48]
    assert not lit, f'top screen column 250 has {len(lit)} lit pixels, first {lit[0]}'
    e.tap(e.bx + 52, 147); e.run(150)
    assert e.state()['state'] == ST_PLAY, 'mirrored CLASSIC did not start'

@scenario('options SPELLING row: BOTH -> AMERICAN -> BRITISH -> BOTH, and the screen shows each')
def _():
    ensure_play()
    e.press('START', settle=10); e.press('B', settle=30)
    assert e.state()['state'] == ST_TITLE, 'pause + B did not quit to the title'
    e.tap(e.bx + 86, 170); e.run(10)
    g = e.state(); assert g['state'] == ST_OPTIONS and g['spelling'] == 0, f'options: {g["state"]} spelling {g["spelling"]}'
    seen = []
    for i in range(3):
        e.tap(e.bx + 86, 126); e.run(10)                 # SPELLING is row 7, y 120..132
        g = e.state(); assert g['optRow'] == 7, f'optRow {g["optRow"]}'
        seen.append(g['spelling'])
        e.shot(f'{out}/rules_options_spelling_{g["spelling"]}.png')
    assert seen == [1, 2, 0], f'spelling cycle {seen}'
    e.tap(e.bx + 86, 170); e.run(30)
    assert e.state()['state'] == ST_TITLE, 'BACK did not leave the options'

@scenario('poke channel sets SPELLING (the rejection rules themselves are in make check)')
def _():
    to_title(); e.start_classic()
    g = e.state(); assert g['state'] == ST_PLAY
    e.poke(POKE_SPELL, 1); e.run(4)
    assert e.state()['spelling'] == 1, 'POKE_SPELL did not set AMERICAN'
    e.poke(POKE_SPELL, 0); e.run(4)
    assert e.state()['spelling'] == 0


@scenario('left-hand layout: title with a Daily streak and THE LEDGER stay inside the board strip')
def _():
    assert e.bx == 82, 'the HAND scenario above leaves the game left-handed'
    to_title()
    daily_title(20270320, played=0, last=20270319, streak=3)
    e.shot(f'{out}/rules_left_title_streak.png')
    e.press('X', settle=20)
    assert e.state()['state'] == ST_STATS, 'X in the left-hand layout'
    e.run(20); e.shot(f'{out}/rules_left_ledger.png')
    px = e.pixels()
    lit = [(x, y) for y in range(192, 384) for x in range(0, 82) if sum(px[x, y]) > 120]
    assert not lit, f'ledger drew {len(lit)} lit pixels left of the mirrored strip, first {lit[0]}'
    e.tap(e.bx + 20, 20); assert e.state()['state'] == ST_TITLE
    daily_title(20270320, played=20270320, last=20270320, streak=3)
    e.shot(f'{out}/rules_left_title_practice.png')
    e.set_clock(0); e.set_daily(0, 0, 0)

# ---- chain input grammar (item 10) -------------------------------------------------------
def touch(x, y, frames=5):
    e.emu.input.touch_set_pos(x + e.bx, y); e.run(frames)
def untouch(frames=8):
    e.emu.input.touch_release(); e.run(frames)
def to_play_calm():
    if e.state()['state'] != ST_PLAY: to_title(); e.start_classic()
    return calm()
def set_hand(left):
    """Be on the title with the HAND option (and the harness mapping) set to `left`."""
    e.run(5)
    to_title()
    if bool(e.state()['settings']['hand']) != left:
        e.tap(e.bx + 86, 170); e.run(10)              # OPTIONS
        e.tap(e.bx + 86, 78); e.run(10)               # HAND row (row 3)
        e.tap(e.bx + 86, 170); e.run(20)              # BACK
        assert bool(e.state()['settings']['hand']) == left, 'HAND did not flip'
    e.set_hand(left)

def check_chain_input(tag):
    g = to_play_calm()
    for c in range(COLS):
        for r in range(ROWS): e.set_cell(c, r, TILE, 'e')
    for r, l in enumerate('bed'): e.set_cell(C, r, TILE, l)
    e.run(3)
    # a wider hit area: the old rectangle dead-zoned x 22..24 of a cell; the 12 px circle takes it
    ox, oy = e.center(0, 0)
    e.tap(ox + 10, oy - 1); g = e.state()
    assert g['chain'] == [(0, 0)], f'{tag}: a tap near the right edge of (0,0): {g["chain"]}'
    e.press('Y', settle=5)
    e.tap(30 + e.bx, 14); assert e.state()['chainLen'] == 0, f'{tag}: the gap between hexes must be dead'
    # a stroke down column 3, then tap an earlier gem: the chain is cut back to it
    for r in range(4):
        x, y = e.center(C, r); touch(x - e.bx, y)
    untouch()
    g = e.state(); assert g['chain'] == [(C, 0), (C, 1), (C, 2), (C, 3)], f'{tag}: stroke {g["chain"]}'
    x, y = e.center(C, 1); e.tap(x, y)
    g = e.state(); assert g['chain'] == [(C, 0), (C, 1)], f'{tag}: tap an earlier gem should cut back: {g["chain"]}'
    # a fast stylus: one jump over two gems still chains them, and never restarts mid-drag
    e.press('Y', settle=5)
    x, y = e.center(C, 0); touch(x - e.bx, y)
    x, y = e.center(C, 3); touch(x - e.bx, y)
    g = e.state(); assert g['chain'][:1] == [(C, 0)] and g['chainLen'] == 4, f'{tag}: fast drag {g["chain"]}'
    # past a hole the stylus has no gem to bridge through: the held chain is kept and the gem flashes red
    untouch(); e.press('Y', settle=5)
    e.set_cell(C, 6, HOLE, 'a'); e.run(3)
    x, y = e.center(C, 4); touch(x - e.bx, y)
    x, y = e.center(C, 7); touch(x - e.bx, y, 4)
    g = e.state(); assert g['chain'] == [(C, 4), (C, 5)], f'{tag}: a held gem past a hole must not restart or bridge: {g["chain"]}'
    e.shot(f'{out}/rules_nope_{tag}.png')
    px = e.pixels(); x0, y0, x1, y1 = e.cell_rect(C, 7)
    red = px[x0, (y0 + y1) // 2]
    assert red[0] > 200 and red[1] < 150 and red[2] < 80, f'{tag}: no red nope outline at {(x0, (y0 + y1) // 2)}: {red}'
    untouch(20)
    g = e.state(); assert g['chain'] == [(C, 4), (C, 5)], f'{tag}: chain lost on release {g["chain"]}'
    e.set_cell(C, 6, TILE, 'e')
    # a tap far away on a fresh stroke restarts
    e.tap(*e.center(6, 6))
    g = e.state(); assert g['chain'] == [(6, 6)], f'{tag}: a tap elsewhere should restart: {g["chain"]}'
    # a valid word: gold ring on the tail, tail tap submits, and the stylus does not start a new chain
    e.press('Y', settle=5)
    for r in range(3):
        x, y = e.center(C, r); touch(x - e.bx, y)
    untouch()
    g = e.state(); assert g['chainLen'] == 3 and g['chainScore'] > 0, f'{tag}: bed should be valid: {g["chain"]} {g["chainScore"]}'
    e.run(20); e.shot(f'{out}/rules_tailring_{tag}.png')
    sc = g['score']
    x, y = e.center(C, 2); e.tap(x, y, hold=8)
    g = e.state(); assert g['lastWord'] == 'bed' and g['score'] > sc, f'{tag}: tail tap did not submit: {g["lastWord"]!r} {g["score"]}'
    assert g['chainLen'] == 0, f'{tag}: a new chain started under the submit tap: {g["chain"]}'
    assert e.fps() == 120, 'fps'

@scenario('chain input (left hand): wide hit circles, cut back, bridge, no restart mid-drag, red nope, tail-tap submit')
def _():
    set_hand(True)
    e.start_classic()
    check_chain_input('left')

@scenario('chain input (right hand): the same grammar on the right-handed board')
def _():
    set_hand(False)
    e.start_classic()
    check_chain_input('right')

# ---- peek input and readability (item 11) -----------------------------------------------
def check_peek(tag):
    px0 = 42 if e.bx else 214                              # the PEEK button's centre x in this layout
    fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.set_turn(0)
    g = e.state()
    for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
    # plain gems (nothing hidden behind them) in rows 1..6 of column C and its neighbour: peeling them cannot finish or fail the shelf
    plain = [r for r in range(1, 7) if not g['back'][C][r]]
    assert len(plain) >= 3, f'{tag}: column {C} hides too much'
    ra, rb, rc = plain[0], plain[-1], plain[len(plain) // 2]
    x, y = e.center(C, ra)
    # arm, slide off, release: nothing peels
    e.hold('L', True); e.emu.input.touch_set_pos(x, y); e.run(4)
    g = e.state(); assert g['armed'] == 1 and (g['armC'], g['armR']) == (C, ra) and g['peek'] == 1, f'{tag}: arm {g["armed"]} {(g["armC"], g["armR"])}'
    e.shot(f'{out}/rules_armed_{tag}.png')
    e.emu.input.touch_set_pos(*e.center(C, rb)); e.run(3)
    assert e.state()['armed'] == 0, f'{tag}: sliding to another gem must disarm'
    e.emu.input.touch_release(); e.run(5)
    g = e.state(); assert g['peels'] == 0 and g['cell'][C][ra][1] == TILE, f'{tag}: a disarmed release peeled'
    # arm and let go of the shoulder: disarmed, nothing peels
    e.emu.input.touch_set_pos(x, y); e.run(4); e.hold('L', False)
    assert e.state()['armed'] == 0, f'{tag}: releasing the shoulder must disarm'
    e.emu.input.touch_release(); e.run(5)
    assert e.state()['peels'] == 0, f'{tag}: shoulder release peeled'
    # arm and release on the gem: it peels
    e.hold('L', True); e.emu.input.touch_set_pos(x, y); e.run(4); e.emu.input.touch_release(); e.run(6)
    g = e.state(); assert g['peels'] == 1 and g['cell'][C][ra][1] == HOLE and g['armed'] == 0, f'{tag}: release did not peel: peels {g["peels"]} armed {g["armed"]}'
    e.hold('L', False); e.run(20)
    # X latches peek for exactly one peel
    e.press('X', settle=5); assert e.state()['peek'] == 1, f'{tag}: X did not latch peek'
    e.press('X', settle=5); assert e.state()['peek'] == 0, f'{tag}: X did not unlatch'
    e.press('X', settle=5)
    x2, y2 = e.center(C, rc); e.tap(x2, y2, hold=5, settle=8)
    g = e.state(); assert g['peels'] == 2 and g['peek'] == 0, f'{tag}: latched peel should be one-shot: peels {g["peels"]} peek {g["peek"]}'
    # the panel button toggles it too
    e.tap(px0, 182); assert e.state()['peek'] == 1, f'{tag}: PEEK button did not latch'
    e.run(10); e.shot(f'{out}/rules_peekon_{tag}.png')
    e.tap(px0, 182); assert e.state()['peek'] == 0, f'{tag}: PEEK button did not unlatch'
    # readability: numbered holes and scorch-risk marks under a flame
    g = e.state()
    e.set_cell(0, 0, BURN, 'a'); e.set_cell(5, 2, BURN, 'a'); e.set_burn(0, 99); e.set_heat(0)
    e.hold('L', True); e.run(20)
    e.shot(f'{out}/rules_peek_read_{tag}.png')
    assert e.fps() == 120, f'{tag}: peek fps'
    e.hold('L', False); e.run(10)

@scenario('peek input (left hand): arm on touch, peel on release, latch by X and the button, readable layer')
def _():
    set_hand(True); e.start_classic()
    check_peek('left')

@scenario('peek input (right hand): arm on touch, peel on release, latch by X and the button, readable layer')
def _():
    set_hand(False); e.start_classic()
    check_peek('right')

# ---- button-only play (item 15) ----------------------------------------------------------
def goto(c, r):
    """Move the d-pad cursor to (c, r)."""
    for _ in range(20):
        g = e.state()
        if (g['curC'], g['curR']) == (c, r): return
        k = 'LEFT' if g['curC'] > c else 'RIGHT' if g['curC'] < c else 'UP' if g['curR'] > r else 'DOWN'
        e.press(k, hold=3, settle=3)
    raise AssertionError(f'cursor stuck at {(g["curC"], g["curR"])} going to {(c, r)}')

def check_dpad(tag):
    g = fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.set_turn(0)
    for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
    g = e.state(); assert (g['curC'], g['curR'], g['curOn']) == (3, 3, 0), f'{tag}: cursor init {(g["curC"], g["curR"], g["curOn"])}'
    e.press('RIGHT', settle=3); g = e.state()
    assert (g['curC'], g['curR'], g['curOn']) == (4, 3, 1), f'{tag}: RIGHT {(g["curC"], g["curR"], g["curOn"])}'
    for _ in range(8): e.press('LEFT', hold=3, settle=2)
    for _ in range(10): e.press('UP', hold=3, settle=2)
    g = e.state(); assert (g['curC'], g['curR']) == (0, 0), f'{tag}: the cursor must clamp at the corner: {(g["curC"], g["curR"])}'
    for _ in range(9): e.press('RIGHT', hold=3, settle=2)
    for _ in range(10): e.press('DOWN', hold=3, settle=2)
    g = e.state(); assert (g['curC'], g['curR']) == (COLS - 1, ROWS - 1), f'{tag}: far clamp {(g["curC"], g["curR"])}'
    for r, l in enumerate('bed'): e.set_cell(C, r, TILE, l)
    e.run(3)
    # A chains under the cursor, on the tail it submits
    goto(C, 0); e.press('A', settle=3)
    e.press('DOWN', settle=3); e.press('A', settle=3); e.press('DOWN', settle=3); e.press('A', settle=3)
    g = e.state(); assert g['chain'] == [(C, 0), (C, 1), (C, 2)], f'{tag}: A chain {g["chain"]}'
    e.run(10); e.shot(f'{out}/rules_cursor_{tag}.png')
    # B steps back one gem, a long hold clears, Y clears
    e.press('B', hold=3, settle=3); assert e.state()['chainLen'] == 2, f'{tag}: B should step back one'
    e.press('A', settle=3)                      # the cursor is on (C,2): not the tail now, adjacent: chains again
    assert e.state()['chainLen'] == 3, f'{tag}: A on the next gem'
    e.hold('B', True); e.run(30); e.hold('B', False); e.run(3)
    assert e.state()['chainLen'] == 0, f'{tag}: holding B should clear the chain'
    e.press('A', settle=3); e.press('UP', settle=3); e.press('A', settle=3); e.press('UP', settle=3); e.press('A', settle=3)
    assert e.state()['chainLen'] == 3
    e.press('Y', settle=3); assert e.state()['chainLen'] == 0, f'{tag}: Y should clear'
    # a word: chain it with A and submit it with A on the tail
    goto(C, 0); e.press('A', settle=3); e.press('DOWN', settle=3); e.press('A', settle=3); e.press('DOWN', settle=3); e.press('A', settle=3)
    sc = e.state()['score']
    e.press('A', settle=40)
    g = e.state(); assert g['lastWord'] == 'bed' and g['score'] > sc and g['chainLen'] == 0, f'{tag}: A on the tail should submit: {g["lastWord"]!r} {g["score"]}'
    # a far gem: not adjacent, the chain is kept
    goto(C, 4); e.press('A', settle=3)
    goto(0, 7); e.press('A', settle=3)
    g = e.state(); assert g['chain'] == [(C, 4)], f'{tag}: A on a far gem must not restart: {g["chain"]}'
    e.press('Y', settle=3)
    # peek: X latches, A on the cursor peels it at once
    g = e.state()
    plain = [r for r in range(1, 7) if not g['back'][C][r] and g['cell'][C][r][1] == TILE]
    goto(C, plain[0]); e.press('X', settle=3); assert e.state()['peek'] == 1
    e.run(8); e.shot(f'{out}/rules_cursor_peek_{tag}.png')
    e.press('A', settle=20)
    g = e.state(); assert g['peels'] == 1 and g['cell'][C][plain[0]][1] == HOLE and g['peek'] == 0, f'{tag}: A should peel the cursor gem: peels {g["peels"]} peek {g["peek"]}'
    # a touch hands the board back to the stylus
    e.tap(*e.center(0, 0)); assert e.state()['curOn'] == 0, f'{tag}: a touch should hide the cursor'
    e.press('Y', settle=3)
    assert e.fps() == 120, f'{tag}: fps'

@scenario('d-pad play (left hand): cursor moves and clamps, A chains and submits, B steps back, X + A peels')
def _():
    set_hand(True); e.start_classic()
    check_dpad('left')

@scenario('d-pad play (right hand): cursor moves and clamps, A chains and submits, B steps back, X + A peels')
def _():
    set_hand(False); e.start_classic()
    check_dpad('right')

print('RULES OK' if not fails else f'RULES FAILED: {fails}')
sys.exit(fails)
