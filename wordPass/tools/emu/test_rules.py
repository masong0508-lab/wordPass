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
e.run(10)                                 # a first frame, so the DBG mirror is valid even when WP_ONLY skips the early scenarios
e.set_tutor(1)                             # no guided first shelf: its prompts own the message line (the tutorial scenario switches it on)
fails = 0
C = 3   # the column every board scenario pokes
def opt_row_y(i): return 34 + 15 * i + 7        # a touch inside row i of the options list (fixed x: it never mirrors)
OPT_BACK_Y = 166

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

def pause_rows():
    """The pause menu's rows for the current mode (Daily has no RESTART)."""
    return ['RESUME', 'QUIT'] if e.state()['mode'] == 1 else ['RESUME', 'RESTART', 'QUIT']

def pause_do(name, settle=30):
    """From play: open the pause menu, walk the d-pad to the row `name` and press A (twice for RESTART and
    QUIT, which ask for a second activation)."""
    e.press('START', settle=10)
    for _ in range(pause_rows().index(name)): e.press('DOWN', settle=3)
    if name == 'RESUME': e.press('A', settle=settle); return
    e.press('A', settle=3); e.press('A', settle=settle)

def to_title():
    """Pause menu, QUIT TO TITLE (from play, or whatever state) back to the title screen."""
    ensure_play()
    pause_do('QUIT')
    g = e.state(); assert g['state'] == ST_TITLE, f'pause menu QUIT did not quit: state {g["state"]}'

def fresh_shelf():
    """Pause menu, RESTART SHELF: a fresh random board (no holes, nothing revealed),
    with the shelf's own fire settings. A restart costs a bookmark, so the run is topped up first."""
    ensure_play()
    e.set_lives(3)
    pause_do('RESTART', settle=150)
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
        pause_do('RESTART', settle=150)
        g = e.state(); assert g['state'] == ST_PLAY and g['lives'] == want and g['turn'] == 0, f'restart: state {g["state"]} lives {g["lives"]}'
    e.press('START', settle=10); e.press('DOWN', settle=3); e.press('A', settle=20)
    g = e.state(); assert g['state'] == ST_PAUSE and g['lives'] == 1 and g['msg'] == 'LAST BOOKMARK' and g['pauseArm'] == 0, f'refusal: state {g["state"]} lives {g["lives"]} msg {g["msg"]!r}'
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
    e.press('START', settle=10)
    g = e.state(); assert g['state'] == ST_PAUSE and pause_rows() == ['RESUME', 'QUIT'], f'Daily pause: state {g["state"]} rows {pause_rows()}'
    e.press('DOWN', settle=3); assert e.state()['pauseRow'] == 1, 'Daily pause: DOWN goes to QUIT'
    e.press('DOWN', settle=3); assert e.state()['pauseRow'] == 0, 'Daily pause has no RESTART row: DOWN wraps to RESUME'
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
    pause_do('QUIT')
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
    pause_do('QUIT')
    assert e.state()['state'] == ST_TITLE, 'the pause menu did not quit to the title'
    e.tap(86, 170); e.run(10)
    g = e.state(); assert g['state'] == ST_OPTIONS and g['optRow'] == 0, f'options: {g["state"]} {g["optRow"]}'
    e.tap(130, opt_row_y(3)); e.run(10)           # rows are 15 px apart from y 34: HAND is row 3 (the right half steps forward)
    g = e.state(); assert g['optRow'] == 3, f'optRow {g["optRow"]}'
    assert g['settings']['hand'] == 1, 'HAND did not flip'
    e.set_hand(True)
    e.tap(86, OPT_BACK_Y); e.run(30)              # BACK: the options list never mirrors, so it is where it was
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
    pause_do('QUIT')
    assert e.state()['state'] == ST_TITLE, 'the pause menu did not quit to the title'
    e.tap(e.bx + 86, 170); e.run(10)
    g = e.state(); assert g['state'] == ST_OPTIONS and g['spelling'] == 0, f'options: {g["state"]} spelling {g["spelling"]}'
    seen = []
    for i in range(3):
        e.tap(130, opt_row_y(7)); e.run(10)              # SPELLING is row 7; the right half steps forward
        g = e.state(); assert g['optRow'] == 7, f'optRow {g["optRow"]}'
        seen.append(g['spelling'])
        e.shot(f'{out}/rules_options_spelling_{g["spelling"]}.png')
    assert seen == [1, 2, 0], f'spelling cycle {seen}'
    e.tap(40, opt_row_y(7)); e.run(10)                    # the left half steps back: BOTH -> BRITISH
    assert e.state()['spelling'] == 2, 'the left half of the row should step back'
    e.tap(40, opt_row_y(7)); e.run(10); assert e.state()['spelling'] == 1
    e.tap(40, opt_row_y(7)); e.run(10); assert e.state()['spelling'] == 0
    e.tap(86, OPT_BACK_Y); e.run(30)
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
        e.tap(130, opt_row_y(3)); e.run(10)           # HAND row (row 3)
        e.tap(86, OPT_BACK_Y); e.run(20)              # BACK (the options list is fixed: the hand does not move it)
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

# ---- panel and HUD (item 12) ------------------------------------------------------------
def px5(px, x, y): return tuple(v >> 3 for v in px[x, y])
def count_color(px, x0, y0, x1, y1, rgb, tol=2):
    """Pixels of the screenshot rectangle within `tol` (5-bit units) of the colour `rgb`."""
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = px5(px, x, y)
            if abs(c[0] - rgb[0]) <= tol and abs(c[1] - rgb[1]) <= tol and abs(c[2] - rgb[2]) <= tol: n += 1
    return n
FIRE1_C, FIRE2_C, GOLD_LT_C, ACCENT1_C = (31, 12, 2), (31, 24, 4), (31, 29, 18), (6, 28, 24)
def panel_x(): return 4 if e.bx else 176
def lit_outside_panel(px, y0=192, y1=384, thr=60):
    """Lit pixels in the margins around the panel text: the 76 px panel is x 176..251 right-handed, 4..79 left-handed."""
    if e.bx: xs = list(range(0, 4)) + [80, 81]
    else: xs = [252, 253, 254, 255]
    return [(x, y) for y in range(y0, y1) for x in xs if sum(px[x, y]) > thr]

def spell_cells(word, c=C, r0=0):
    for i, ch in enumerate(word): e.set_cell(c, r0 + i, TILE, ch)
    e.run(3); return [(c, r0 + i) for i in range(len(word))]

def check_panel(tag):
    g = calm(); X = panel_x()
    # the pause button: a tap pauses (and only inside the button), START resumes
    e.tap(X + 50, 12, settle=10); assert e.state()['state'] == ST_PLAY, f'{tag}: a tap beside the pause button paused'
    e.tap(X + 70, 12, settle=10); g = e.state(); assert g['state'] == ST_PAUSE, f'{tag}: the pause button did not pause: {g["state"]}'
    e.press('START', settle=20); assert e.state()['state'] == ST_PLAY
    # worst-case text: a six-digit score, a nine-letter pass with some letters found, a long turn count
    e.set_score(999999); e.set_pass(9, 0b101010101); e.set_turn(150); e.run(10)
    e.shot(f'{out}/rules_panel_worst_{tag}.png'); px = e.pixels()
    bad = lit_outside_panel(px); assert not bad, f'{tag}: panel text reaches the margin: {len(bad)} lit px, first {bad[:3]}'
    # the tracker: found slots are filled diamonds (white), the others gold outlines
    found_px = count_color(px, X, 192 + 42, X + 72, 192 + 52, (31, 31, 31), 1)
    outline_px = count_color(px, X, 192 + 42, X + 72, 192 + 52, (16, 11, 2), 1)
    assert found_px > 5 * 30 and outline_px > 4 * 20, f'{tag}: tracker found {found_px} outline {outline_px}'
    assert count_color(px, X + 72, 192 + 42, X + 76, 192 + 52, (31, 31, 31), 4) == 0, f'{tag}: the tracker runs past x+72'
    # a valid chain previews its value, multiplier and heat
    e.set_score(0); e.set_turn(0); e.set_pass(4, 0); e.press('Y', settle=5)
    path = spell_cells('bread'); e.chain(path); e.run(8)
    g = e.state(); assert g['chainScore'] == 160 and g['chainLen'] == 5, f'{tag}: bread {g["chainScore"]}'
    px = e.pixels()
    assert count_color(px, X, 192 + 84, X + 40, 192 + 91, GOLD_LT_C, 2) > 15, f'{tag}: no +160 line'
    assert count_color(px, X + 50, 192 + 84, X + 76, 192 + 91, ACCENT1_C, 2) > 8, f'{tag}: no X2 multiplier'
    assert count_color(px, X + 28, 192 + 94, X + 76, 192 + 101, FIRE2_C, 2) > 20, f'{tag}: no +1 HEAT preview'
    e.shot(f'{out}/rules_panel_preview_{tag}.png')
    e.press('Y', settle=5)
    path = spell_cells('letters'); e.chain(path); e.run(8)
    px = e.pixels()
    assert count_color(px, X + 28, 192 + 94, X + 76, 192 + 101, ACCENT1_C, 2) > 20, f'{tag}: a 7-letter word should preview NO HEAT'
    assert not lit_outside_panel(px), f'{tag}: preview text reaches the margin'
    e.press('Y', settle=5)
    # fire: pips for the turns to the next fire step, and the label blinks FIRE / NEXT on the last one
    e.set_cell(C, 6, BURN, 'a'); e.set_burn(0, 3); e.run(6)
    px = e.pixels()
    assert count_color(px, X + 30, 192 + 112, X + 76, 192 + 117, FIRE1_C, 1) >= 3 * 25, f'{tag}: three filled pips expected'
    e.set_burn(2, 3); e.run(6); px = e.pixels()
    filled = count_color(px, X + 30, 192 + 112, X + 50, 192 + 117, FIRE1_C, 1)
    assert 20 <= filled <= 26, f'{tag}: one turn left should leave one filled pip, got {filled} px'
    seen = set()
    for i in range(24):
        e.run(2); px = e.pixels()
        seen.add(count_color(px, X, 192 + 111, X + 24, 192 + 118, FIRE1_C, 1) > 5)   # NEXT is FIRE1, FIRE is FIRE2
    assert seen == {True, False}, f'{tag}: the last turn must blink NEXT, saw {seen}'
    e.shot(f'{out}/rules_panel_fire_{tag}.png')
    e.set_burn(0, 99); e.set_cell(C, 6, TILE, 'e')
    # OVERTIME: past turn 24 the label blinks and a valid chain previews the number only
    e.set_turn(40)
    path = spell_cells('bed'); e.chain(path); e.press('A', settle=30)
    g = e.state(); assert g['overtime'] == 1 and g['turn'] == 41, f'{tag}: turn {g["turn"]} overtime {g["overtime"]}'
    e.set_heat(0); path = spell_cells('bead', r0=3); e.chain(path); e.run(4)
    lit_seen = set()
    for i in range(24):
        e.run(2); px = e.pixels()
        lit_seen.add(count_color(px, X, 192 + 94, X + 48, 192 + 101, FIRE1_C, 1) > 15)
    assert True in lit_seen, f'{tag}: no OVERTIME label'
    px = e.pixels(); assert not lit_outside_panel(px), f'{tag}: OVERTIME with a heat preview reaches the margin'
    assert count_color(px, X + 56, 192 + 94, X + 76, 192 + 101, FIRE2_C, 2) > 8, f'{tag}: the short heat number is missing next to OVERTIME'
    e.shot(f'{out}/rules_panel_overtime_{tag}.png')
    e.press('Y', settle=5); e.set_turn(0)
    # pressed buttons look pressed, then let go
    e.tap(X + 38, 130, hold=4, settle=0); assert e.state()['pressBits'] & 1, f'{tag}: SUBMIT not pressed'
    e.run(12); e.tap(X + 38, 158, hold=4, settle=0); assert e.state()['pressBits'] & 2, f'{tag}: CLEAR not pressed'
    e.run(12); e.tap(X + 38, 182, hold=4, settle=0); assert e.state()['pressBits'] & 4, f'{tag}: PEEK not pressed'
    e.run(12); e.tap(X + 38, 182, settle=0)  # unlatch the peek again
    e.run(20); assert e.state()['pressBits'] == 0, f'{tag}: a button stayed pressed'
    assert e.fps() == 120, f'{tag}: panel fps'

@scenario('panel HUD (left hand): pause button, tracker, chain value, heat preview, fire pips, OVERTIME; nothing past the margins')
def _():
    set_hand(True); e.start_classic()
    check_panel('left')

@scenario('panel HUD (right hand): pause button, tracker, chain value, heat preview, fire pips, OVERTIME; nothing past the margins')
def _():
    set_hand(False); e.start_classic()
    check_panel('right')

def check_callouts(tag):
    g = calm()
    # a five-letter word: GOOD! pops over the board, settles and goes; the message stays for the top screen
    path = spell_cells('bread'); e.chain(path); e.press('A', settle=3)
    g = e.state(); assert g['msg'] == 'GOOD!' and g['callout'] == 'GOOD!' and g['calloutLife'] > 30, f'{tag}: msg {g["msg"]!r} callout {g["callout"]!r} life {g["calloutLife"]}'
    px = e.pixels()
    assert count_color(px, e.bx + 20, 192 + 70, e.bx + 150, 192 + 100, GOLD_LT_C, 2) > 60, f'{tag}: no gold callout over the board'
    e.shot(f'{out}/rules_callout_good_{tag}.png')
    e.run(50); assert e.state()['calloutLife'] == 0, f'{tag}: the callout never ended'
    # a word that is no word: red rims on the chained gems and the callout; the full reason stays in msg
    e.press('Y', settle=5)
    path = spell_cells('xxx'); e.chain(path); e.press('A', settle=2)
    g = e.state(); assert g['msg'] == 'NOT IN THE LIBRARY' and g['callout'] == 'NOT A WORD', f'{tag}: msg {g["msg"]!r} callout {g["callout"]!r}'
    px = e.pixels(); x0, y0, x1, y1 = e.cell_rect(C, 1)
    rim = count_color(px, x0, y0 - 2, x1, y1, FIRE1_C, 3)
    assert rim > 10, f'{tag}: the rejected chain should flash red ({rim} px)'
    e.shot(f'{out}/rules_callout_invalid_{tag}.png')
    e.run(60); assert e.state()['calloutLife'] == 0
    e.press('Y', settle=5)
    # peeling a pass letter calls it out in accent
    e.set_back(C, 1, 1); e.set_cell(C, 1, TILE, 'e'); e.set_cell(C, 0, TILE, 'e'); e.set_heat(0); e.set_burn(0, 99); e.run(5)
    e.hold('L', False); e.press('X', settle=3)
    x, y = e.center(C, 1); e.tap(x, y, hold=5, settle=2)
    g = e.state(); assert g['callout'] == 'PASS LETTER!' and g['found'][0] == 1, f'{tag}: callout {g["callout"]!r} found {g["found"]}'
    e.run(6); e.shot(f'{out}/rules_callout_pass_{tag}.png')
    e.run(50)
    # scorch: SCORCHED pops at 3x in fire colours
    e.set_cell(C, 4, BURN, 'a'); e.set_cell(C, 5, TILE, 'b'); e.set_back(C, 5, 0); e.set_back(C, 1, 0)
    e.press('X', settle=3); x, y = e.center(C, 5); e.tap(x, y, hold=5, settle=2)
    g = e.state()
    if g['state'] == ST_PLAY:
        assert g['callout'] == 'SCORCHED', f'{tag}: callout {g["callout"]!r}'
        e.run(4); e.shot(f'{out}/rules_callout_scorched_{tag}.png')

@scenario('board callouts (right hand): length tags, NOT A WORD with red rims, PASS LETTER!, SCORCHED')
def _():
    set_hand(False); e.start_classic()
    check_callouts('right')

@scenario('board callouts (left hand): the same, mirrored strip')
def _():
    set_hand(True); e.start_classic()
    check_callouts('left')

@scenario('HUD top row: mode label, a 20-letter chain stays left of the fire meter, long words and the last word fit')
def _():
    set_hand(False); e.start_classic()
    g = calm(); e.set_pass(9, 0b110011001); e.run(40)             # the found letters pop for 24 frames
    px = e.pixels()
    assert count_color(px, 16, 36, 60, 43, ACCENT1_C, 2) > 25, 'no CLASSIC label under the logo'
    # the plaque of nine letters ends before the meter column at x 222
    assert count_color(px, 221, 60, 236, 87, (31, 31, 31), 1) == 0, 'the nine-letter pass runs into the fire meter'
    e.shot(f'{out}/rules_top_label.png')
    for word in ('lophophores', 'livingnesses', 'kindheartednesses', 'lymphogranulomatoses'):
        e.press('Y', settle=5)
        path = [(0, r) for r in range(8)] + [(1, r) for r in range(7, -1, -1)] + [(2, r) for r in range(8)]
        for (c, r), ch in zip(path, word): e.set_cell(c, r, TILE, ch)
        e.run(3); e.chain(path[:len(word)]); e.run(10)
        g = e.state(); assert g['chainLen'] == len(word) and g['chainScore'] > 0, f'{word}: {g["chainLen"]} {g["chainScore"]}'
        e.shot(f'{out}/rules_top_chain_{len(word)}.png'); px = e.pixels()
        hot = [(x, y) for y in range(100, 122) for x in range(222, 242) if sum(px[x, y]) > 300]
        assert not hot, f'{word}: the chain word reaches the fire meter column, first {hot[:3]}'
        left = [(x, y) for y in range(100, 122) for x in range(10, 12) if sum(px[x, y]) > 300]   # just inside the plaque border (x 8..9)
        assert not left, f'{word}: the chain word reaches the plaque border, first {left[:3]}'
        assert count_color(px, 60, 122, 200, 136, (31, 24, 4), 5) > 30 or count_color(px, 60, 122, 200, 136, (31, 28, 14), 6) > 30, f'{word}: no +value under the word'
        e.press('A', settle=70)
        px = e.pixels()
        hot = [(x, y) for y in range(100, 116) for x in range(222, 242) if sum(px[x, y]) > 300]
        assert not hot, f'{word}: the last word reaches the fire meter column'
    e.press('Y', settle=5)

# ---- overlays: pause menu, results tally, fail card, title (item 13) -----------------------
def pause_row_y(i): return 88 + 16 * i + 6          # a touch inside the i-th 16 px row of the pause menu

def check_pause(tag):
    fresh_shelf(); e.set_lives(3)
    x = e.bx + 86
    # a held chain is dropped when the menu opens, and the menu starts on RESUME
    spell_cells('bed'); e.chain([(C, 0), (C, 1), (C, 2)]); assert e.state()['chainLen'] == 3
    e.press('START', settle=10); g = e.state()
    assert g['state'] == ST_PAUSE and g['chainLen'] == 0 and g['pauseRow'] == 0, f'{tag}: pause: state {g["state"]} chain {g["chainLen"]} row {g["pauseRow"]}'
    e.shot(f'{out}/rules_pause_{tag}.png')
    # START and B both resume; SELECT does nothing any more
    e.press('B', settle=20); assert e.state()['state'] == ST_PLAY, f'{tag}: B did not resume'
    e.press('START', settle=10); e.press('SELECT', settle=10); assert e.state()['state'] == ST_PAUSE and e.state()['lives'] == 3, f'{tag}: SELECT must do nothing in the pause menu'
    e.press('START', settle=20); assert e.state()['state'] == ST_PLAY, f'{tag}: START did not resume'
    # the d-pad walks the rows and wraps
    e.press('START', settle=10); rows = []
    for k in 'DOWN', 'DOWN', 'DOWN', 'UP': e.press(k, settle=3); rows.append(e.state()['pauseRow'])
    assert rows == [1, 2, 0, 2], f'{tag}: pause rows {rows}'
    # a tap on another row only highlights it; a tap on the highlighted row arms QUIT, a second one within 90 frames quits
    e.tap(x, pause_row_y(0), settle=6); g = e.state()
    assert g['state'] == ST_PAUSE and g['pauseRow'] == 0, f'{tag}: a tap on RESUME while QUIT was lit must only highlight: {g["state"]} {g["pauseRow"]}'
    e.tap(x, pause_row_y(2), settle=6); g = e.state()
    assert g['state'] == ST_PAUSE and g['pauseRow'] == 2 and g['pauseArm'] == 0, f'{tag}: tap on QUIT highlights it: row {g["pauseRow"]} arm {g["pauseArm"]}'
    e.tap(x, pause_row_y(2), settle=6); g = e.state()
    assert g['state'] == ST_PAUSE and g['pauseArm'] > 60, f'{tag}: the first QUIT activation must only arm: state {g["state"]} arm {g["pauseArm"]}'
    e.shot(f'{out}/rules_pause_quit_{tag}.png')
    e.run(95); assert e.state()['pauseArm'] == 0, f'{tag}: the confirmation never expired'
    e.tap(x, pause_row_y(2), settle=6); assert e.state()['state'] == ST_PAUSE, f'{tag}: after the timeout one tap must arm again, not quit'
    e.press('UP', settle=3); assert e.state()['pauseArm'] == 0, f'{tag}: moving the highlight must disarm'
    e.press('DOWN', settle=3); e.press('A', settle=3); e.press('A', settle=30)
    g = e.state(); assert g['state'] == ST_TITLE, f'{tag}: the second activation did not quit: {g["state"]}'
    e.start_classic(); e.set_lives(3)
    # RESTART costs a bookmark and is guarded too; a tap on the highlighted RESUME resumes
    e.press('START', settle=10); e.press('DOWN', settle=3); e.press('A', settle=3)
    g = e.state(); assert g['state'] == ST_PAUSE and g['lives'] == 3 and g['pauseArm'] > 0, f'{tag}: RESTART must ask first: lives {g["lives"]} arm {g["pauseArm"]}'
    e.shot(f'{out}/rules_pause_restart_{tag}.png')
    e.press('A', settle=150); g = e.state()
    assert g['state'] == ST_PLAY and g['lives'] == 2 and g['turn'] == 0, f'{tag}: restart: state {g["state"]} lives {g["lives"]}'
    e.press('START', settle=10); e.tap(x, pause_row_y(0), settle=20)
    assert e.state()['state'] == ST_PLAY, f'{tag}: a tap on the lit RESUME row did not resume'
    # the last bookmark cannot be spent
    e.set_lives(1); e.press('START', settle=10); e.press('DOWN', settle=3); e.press('A', settle=10)
    g = e.state(); assert g['state'] == ST_PAUSE and g['msg'] == 'LAST BOOKMARK' and g['lives'] == 1 and g['pauseArm'] == 0, f'{tag}: last bookmark: {g["msg"]!r} lives {g["lives"]}'
    e.shot(f'{out}/rules_pause_last_{tag}.png')
    e.press('START', settle=20)
    assert e.fps() == 120, f'{tag}: pause fps'

@scenario('pause menu (right hand): rows, d-pad and taps, guarded QUIT and RESTART, START and B resume, the chain is dropped')
def _():
    set_hand(False); e.start_classic()
    check_pause('right')

@scenario('pause menu (left hand): the same, mirrored')
def _():
    set_hand(True); e.start_classic()
    check_pause('left')

def pass_by_peeling():
    """Peel the pass in order with the fire held off; the last peel is a stylus lift whose frame we stop on."""
    g = e.state(); n = g['passLen']
    for i in range(1, n + 1):
        e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
        g = e.state()
        for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
        c, r = e.secret_cells(g)[i]
        if i < n: e.peel(c, r); continue
        e.hold('L', True); e.emu.input.touch_set_pos(*e.center(c, r)); e.run(4); e.emu.input.touch_release()
        for _ in range(8):
            e.run(1)
            if e.state()['state'] == ST_PASSED: break
    e.hold('L', False)
    g = e.state(); assert g['state'] == ST_PASSED, f'pass_by_peeling: state {g["state"]} fail {g["fail"]}'
    return g

def long_word_path(word):
    path = [(0, r) for r in range(8)] + [(1, r) for r in range(7, -1, -1)] + [(2, r) for r in range(8)]
    for (c, r), ch in zip(path, word): e.set_cell(c, r, TILE, ch)
    e.run(3); return path[:len(word)]

def card_text_inside(tag, px, cy, what):
    """No lit pixel on the two columns just inside the card's inner border, over the rows of the card."""
    bad = []
    for y in range(cy + 6, cy + 148):
        for x in (e.bx + 14, e.bx + 157):
            if sum(px[x, 192 + y]) > 150: bad.append((x, y))
    assert not bad, f'{tag}: {what}: text reaches the card border, first {bad[:3]}'

def check_results(tag):
    calm(); e.set_lives(3)
    # a 20-letter best word, so the card is checked at its widest
    e.chain(long_word_path('lymphogranulomatoses')); e.press('A', settle=60)
    e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
    assert e.state()['lastWord'].startswith('lymphogranulo'), f'{tag}: the long word did not score'
    g = pass_by_peeling()
    total = 1000 * g['shelf'] + g['speedBonus'] + g['peelBonus']
    entry = g['score'] - total
    assert g['shownScore'] == entry, f'{tag}: the HUD must show the score before the bonuses on entry: {g["shownScore"]} want {entry}'
    series = [g['shownScore']]; flash = set(); shots = {}
    for f in range(110):
        e.run(1); g = e.state(); series.append(g['shownScore'])
        px = e.pixels(); flash.add(px5(px, e.bx + 11 + 40, 192 + 22)[2] >= 30)    # the card's top border: gold, white for four frames at the end
        if f in (8, 20, 35, 100): e.shot(f'{out}/rules_results_{tag}_{f}.png')
    assert all(b >= a for a, b in zip(series, series[1:])), f'{tag}: the tally went backwards: {series[:40]}'
    assert series[3] == entry, f'{tag}: the count must wait for the diamonds (frame 18): {series[:22]}'
    assert series[-1] == g['score'] == entry + total, f'{tag}: the tally did not arrive: {series[-1]} want {g["score"]}'
    start = next(i for i, v in enumerate(series) if v > entry); end = next(i for i, v in enumerate(series) if v == g['score'])
    assert 10 <= end - start <= 80, f'{tag}: the tally took {end - start} frames'
    assert True in flash and False in flash, f'{tag}: the card border should flash white when the tally ends'
    card_text_inside(tag, px, 22, 'results card')            # the last frame of the loop: card settled, tally done
    # a tap before the tally ends advances and snaps the HUD to the real score
    g = e.state(); assert g['state'] == ST_PASSED
    e.tap(e.bx + 100, 100); g = e.state()
    assert g['state'] == ST_PLAY and g['shelf'] == 2 and g['shownScore'] == g['score'], f'{tag}: after the tap: state {g["state"]} shown {g["shownScore"]} score {g["score"]}'
    # and one that lands mid-count
    e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
    pass_by_peeling(); e.run(31)                          # a tap only counts after frame 30, and a shelf-2 tally runs to about frame 50
    g = e.state(); assert g['shownScore'] < g['score'], f'{tag}: still counting at frame ~32: {g["shownScore"]} / {g["score"]}'
    e.tap(e.bx + 100, 100, settle=4); g = e.state()
    assert g['state'] == ST_PLAY and g['shownScore'] == g['score'], f'{tag}: a mid-count tap must land on the real score: {g["shownScore"]} / {g["score"]}'
    assert e.fps() == 120, f'{tag}: results fps'

@scenario('results card (right hand): the score counts up behind the bonus rows, diamonds light one by one, a tap snaps it')
def _():
    set_hand(False); e.start_classic()
    check_results('right')

@scenario('results card (left hand): the same, mirrored')
def _():
    set_hand(True); e.start_classic()
    check_results('left')

def check_fail(tag):
    calm(); e.set_lives(3)
    e.chain(long_word_path('lymphogranulomatoses')); e.press('A', settle=60)
    e.set_heat(0); e.set_turn(0); e.set_burn(0, 99); e.set_score(1234567)
    g = force_fail()                                     # OUT OF WORDS, 60 frames in
    assert g['state'] == ST_FAIL and g['lives'] == 2, f'{tag}: fail: state {g["state"]} lives {g["lives"]}'
    e.run(80); e.shot(f'{out}/rules_fail_{tag}.png'); px = e.pixels()
    card_text_inside(tag, px, 22, 'classic fail card')
    assert count_color(px, e.bx + 14, 192 + 22 + 100, e.bx + 150, 192 + 22 + 125, ACCENT1_C, 2) > 80, f'{tag}: no RETRY FROM line in accent'
    # a tap before 45 frames does nothing; after it retries on the same shelf
    # (force_fail already waited 60 + 80 frames)
    e.tap(e.bx + 100, 100); g = e.state()
    assert g['state'] == ST_PLAY and g['lives'] == 2 and g['shelf'] == 1, f'{tag}: retry: state {g["state"]} lives {g["lives"]}'
    # the run over: last bookmark, FINAL and RUN OVER, a tap goes to the title
    e.set_lives(1); e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
    force_fail(); e.run(60); e.shot(f'{out}/rules_fail_over_{tag}.png'); px = e.pixels()
    card_text_inside(tag, px, 22, 'run-over card')
    assert count_color(px, e.bx + 14, 192 + 22 + 105, e.bx + 150, 192 + 22 + 125, FIRE2_C, 2) > 30, f'{tag}: no RUN OVER in fire colours'
    e.tap(e.bx + 100, 100); e.run(20)
    assert e.state()['state'] == ST_TITLE, f'{tag}: the run-over tap did not reach the title'
    e.start_classic()

@scenario('fail card (right hand): reason, the pass revealed, stats with a 20-letter best word, bookmarks, RUN OVER; nothing past the border')
def _():
    set_hand(False); e.start_classic()
    check_fail('right')

@scenario('fail card (left hand): the same, mirrored')
def _():
    set_hand(True); e.start_classic()
    check_fail('left')

def check_title(tag):
    if e.state()['state'] == ST_OPTIONS: e.press('B', settle=30)     # set_hand's BACK tap can miss when the hand just flipped
    assert e.state()['state'] == ST_TITLE, f'{tag}: not on the title'
    e.run(200)
    e.shot(f'{out}/rules_title_{tag}.png')
    black = (1, 1, 2)
    rain = 0; band_bad = []
    for k in range(40):
        e.run(13); px = e.pixels()
        # the band is solid black around the logo gems: rows 31..37 and 64..72 hold nothing, whatever rains behind it
        for y in list(range(31, 37)) + list(range(65, 72)):
            for x in range(e.bx + 10, e.bx + 165):
                if px5(px, x, 192 + y) != black: band_bad.append((x, y))
        # rain: lit pixels in the strip above the band and in the side gutters
        for y in range(3, 29):
            for x in range(e.bx + 4, e.bx + 170):
                if sum(px[x, 192 + y]) > 200: rain += 1
    assert not band_bad, f'{tag}: the gem rain or something else crosses the band: {len(band_bad)} px, first {band_bad[:3]}'
    assert rain > 30, f'{tag}: no gem rain above the band ({rain} lit px in 40 samples)'
    px = e.pixels()
    assert count_color(px, e.bx + 8, 192 + 30, e.bx + 165, 192 + 31, (16, 11, 2), 1) > 100 and count_color(px, e.bx + 8, 192 + 73, e.bx + 165, 192 + 74, (16, 11, 2), 1) > 100, f'{tag}: the gold rules at y 30 and 73 are missing'
    assert e.fps() == 120, f'{tag}: title fps with the gem rain'
    # the hit rects did not move
    e.tap(e.bx + 52, 147); e.run(150); assert e.state()['state'] == ST_PLAY, f'{tag}: CLASSIC no longer starts'

@scenario('title (right hand): logo band with gold rules, gem rain behind it, buttons where they were')
def _():
    set_hand(False); check_title('right')

@scenario('title (left hand): the same, mirrored')
def _():
    set_hand(True); check_title('left')

# ---- options, onboarding and accessibility (item 14) ---------------------------------------
def open_options():
    """From the title (whatever the hand): the options screen, optRow 0."""
    e.tap(e.bx + 86, 170); e.run(10)
    g = e.state(); assert g['state'] == ST_OPTIONS, f'OPTIONS did not open: {g["state"]}'

def region_diff(a, b, x0, y0, x1, y1):
    return [(x, y) for y in range(y0, y1) for x in range(x0, x1) if a[x, y] != b[x, y]]

def check_options(tag):
    to_title(); open_options()
    # the list is fixed: flipping HAND mirrors the title and the board, never the options (the row you just tapped stays under the stylus)
    e.tap(130, opt_row_y(3)); e.run(12)
    g = e.state(); hand0 = g['settings']['hand']; assert g['optRow'] == 3
    e.set_hand(bool(hand0)); e.shot(f'{out}/rules_options_hand{hand0}_{tag}.png'); a = e.pixels()
    e.tap(130, opt_row_y(3)); e.run(12)
    g = e.state(); assert g['settings']['hand'] == 1 - hand0, f'{tag}: HAND did not flip back'
    e.set_hand(bool(1 - hand0)); e.shot(f'{out}/rules_options_hand{1 - hand0}_{tag}.png'); b = e.pixels()
    moved = region_diff(a, b, 0, 196, 73, 384)            # labels, highlight, frame: x 0..73 (the value column differs by its text)
    assert not moved, f'{tag}: the options list moved when HAND flipped: {len(moved)} px differ, first {moved[:3]}'
    lit = [(x, y) for y in range(192, 384) for x in range(174, 256) if sum(a[x, y]) > 90 or sum(b[x, y]) > 90]
    assert not lit, f'{tag}: something is drawn right of the options strip: {lit[:3]}'
    # left half steps back, right half steps forward, whatever the hand; THEME wraps onto CLEAR
    e.tap(40, opt_row_y(0)); e.run(8)
    g = e.state(); assert g['settings']['theme'] == 4 and g['optRow'] == 0, f'{tag}: left half of THEME: {g["settings"]}'
    e.tap(130, opt_row_y(0)); e.run(8); assert e.state()['settings']['theme'] == 0, f'{tag}: right half of THEME'
    # the chevron on the tapped side flashes white for six frames, then settles to the accent colour
    y = opt_row_y(1) - 4
    def flash(side_x, other_x, tap_x):
        """Tap row 1 on the given side and watch its chevrons for the next ten frames: the tapped side flashes white, the other never does."""
        e.tap(tap_x, opt_row_y(1), hold=2, settle=0)
        seen = other = 0
        for _ in range(10):
            px = e.pixels()
            if count_color(px, side_x, 192 + y, side_x + 7, 192 + y + 9, (31, 31, 31), 1) >= 4: seen += 1
            if count_color(px, other_x, 192 + y, other_x + 7, 192 + y + 9, (31, 31, 31), 1) >= 4: other += 1
            e.run(1)
        e.run(8); px = e.pixels()
        assert count_color(px, side_x, 192 + y, side_x + 7, 192 + y + 9, (31, 31, 31), 1) == 0, f'{tag}: the chevron flash never ended'
        return seen, other
    seen, other = flash(148, 75, 130)
    assert 2 <= seen <= 7 and other == 0, f'{tag}: the right chevron should flash white for about six frames (right {seen} left {other})'
    seen, other = flash(75, 148, 40)
    assert 2 <= seen <= 7 and other == 0, f'{tag}: the left chevron should flash white on a left tap (left {seen} right {other})'
    # d-pad: UP and DOWN move the row, LEFT and RIGHT step the value, A steps forward
    e.press('DOWN', settle=4); assert e.state()['optRow'] == 2
    e.press('RIGHT', settle=4); assert e.state()['settings']['theme'] == 0 and e.state()['optRow'] == 2
    for i in range(OPT_ROWS_N):
        while e.state()['optRow'] != i: e.press('DOWN', settle=4)
        e.run(8); px = e.pixels()
        over = [(x, y) for y in range(128, 150) for x in list(range(0, 8)) + list(range(249, 256)) if sum(px[x, y]) > 250]   # the accent dot grid is dimmer than any text
        assert not over, f'{tag}: help text for row {i} reaches the plaque border: {over[:3]}'
        if i == 0:
            e.shot(f'{out}/rules_options_row0_{tag}.png')
        if i in (2, 7): e.shot(f'{out}/rules_options_row{i}_{tag}.png')
    while e.state()['optRow'] != 2: e.press('DOWN', settle=4)
    e.press('LEFT', settle=4); assert e.state()['settings'] is not None and e.state()['optRow'] == 2
    e.press('DOWN', settle=4); e.press('DOWN', settle=4)
    # the sample board: a ruby gem, the numbered hole, the burning gem's light plate, the lit chain rim, the ghost
    e.press('DOWN', settle=4); e.run(8); px = e.pixels()
    bx0, by0 = 56, 70
    ruby = px5(px, bx0 + 19, by0 + 12)
    assert ruby[0] >= 14 and ruby[0] > 2 * max(ruby[1], 1) and ruby[2] < ruby[0], f'{tag}: the first sample gem should be a ruby, got {ruby}'
    assert count_color(px, bx0, by0 + 22, bx0 + 24, by0 + 44, (6, 28, 24), 2) > 20, f'{tag}: the numbered hole (accent rim) is missing'
    assert count_color(px, bx0 + 24, by0 + 22 + 11, bx0 + 48, by0 + 22 + 11 + 22, (31, 22, 4), 1) > 60, f'{tag}: the burning gem has no light plate'
    assert count_color(px, bx0 + 48, by0 + 20, bx0 + 72, by0 + 22 + 22, (31, 31, 31), 1) > 40, f'{tag}: the chained gem has no white rim'
    # CLEAR gets its own line of help, still inside the plaque
    while e.state()['optRow'] != 0: e.press('DOWN', settle=4)
    e.tap(40, opt_row_y(0)); e.run(8); assert e.state()['settings']['theme'] == 4
    e.run(10); px = e.pixels(); e.shot(f'{out}/rules_options_clear_{tag}.png')
    assert count_color(px, 40, 141, 216, 148, (6, 28, 24), 2) > 40, f'{tag}: no CLEAR: COLOUR-BLIND SAFE line'
    over = [(x, y) for y in range(128, 150) for x in list(range(0, 8)) + list(range(249, 256)) if sum(px[x, y]) > 250]   # the accent dot grid is dimmer than any text
    assert not over, f'{tag}: the CLEAR help reaches the border'
    e.tap(130, opt_row_y(0)); e.run(8); assert e.state()['settings']['theme'] == 0
    assert e.fps() == 120, f'{tag}: options fps (the sample board is drawn in the top slices)'
    e.tap(86, OPT_BACK_Y); e.run(30)
    assert e.state()['state'] == ST_TITLE, f'{tag}: BACK did not leave'

OPT_ROWS_N = 8

@scenario('options (right hand): a fixed list, left/right halves, chevron flash, the sample board and help inside the plaque')
def _():
    set_hand(False); check_options('right')

@scenario('options (left hand): the same list, in the same place')
def _():
    set_hand(True); check_options('left')

def set_theme(n):
    """Pick a theme through the options screen (from the title or play)."""
    to_title(); open_options()
    while e.state()['settings']['theme'] != n: e.tap(130, opt_row_y(0)); e.run(6)
    e.tap(86, OPT_BACK_Y); e.run(30)

@scenario('CLEAR theme: yellow vowels, blue consonants, magenta rare letters; burning gems keep a light plate and a dark letter')
def _():
    set_hand(False); set_theme(4); e.start_classic()
    calm()
    for c in range(COLS):
        for r in range(ROWS): e.set_cell(c, r, TILE, 'a')
    e.set_cell(0, 0, TILE, 'r'); e.set_cell(0, 1, TILE, 'j'); e.set_cell(0, 2, TILE, 'b'); e.set_cell(0, 3, TILE, 'g')
    e.set_cell(2, 5, BURN, 'm'); e.set_cell(4, 3, BURN, 'q'); e.run(10)
    e.shot(f'{out}/rules_clear_play.png'); px = e.pixels()
    def body(c, r):
        x0, y0, x1, y1 = e.cell_rect(c, r); return px5(px, x0 + 5, y0 + 11)
    v, cons, rare, soft, other = body(3, 3), body(0, 0), body(0, 1), body(0, 2), body(0, 3)
    assert v[0] >= 20 and v[1] >= 16 and v[2] <= 12, f'CLEAR vowel should be yellow: {v}'
    assert cons[2] >= 20 and cons[0] <= 6, f'CLEAR consonant should be blue: {cons}'
    assert rare[0] >= 14 and rare[2] >= 14 and rare[1] <= 14, f'CLEAR rare letter should be magenta: {rare}'
    assert soft[1] >= 8 and soft[0] <= 4 and soft[2] >= 6, f'CLEAR soft letter should be teal: {soft}'
    assert max(other) <= 14 and abs(other[0] - other[2]) <= 4, f'CLEAR other letter should be grey: {other}'
    # the fire cue: the plate behind a burning letter is the light colour, the glyph is dark, a FIRE2 bar caps the gem
    for (c, r) in ((2, 5), (4, 3)):
        x0, y0, x1, y1 = e.cell_rect(c, r)
        assert count_color(px, x0 + 4, y0 + 3, x0 + 20, y0 + 20, (31, 22, 4), 1) > 90, f'burning ({c},{r}): no light plate'
        assert count_color(px, x0 + 4, y0 + 3, x0 + 20, y0 + 20, (10, 0, 0), 1) > 15, f'burning ({c},{r}): no dark glyph'
        assert count_color(px, x0 + 3, y0 + 1, x0 + 21, y0 + 3, (31, 24, 4), 1) > 20, f'burning ({c},{r}): no bar across the top'
    set_theme(0)

def msg_of(prefix): return e.state()['msg'].startswith(prefix)

def check_tutor(tag):
    """The guided first shelf on a fresh Classic game: sticky prompts, overridden by callouts, ended by the closing tip."""
    left = e.bx == 82
    peek = 'HOLD R OR TAP PEEK' if left else 'HOLD L OR TAP PEEK'
    e.set_tutor(0); e.start_classic(); e.set_lives(3)
    e.run(260)                                              # the shelf intro (180 frames) runs out, then the prompt stays
    g = e.state(); assert g['tutorStep'] == 0 and g['msgTimer'] == -1 and g['msg'].startswith('DRAG 3 TOUCHING GEMS'), f'{tag}: step 0: {g["tutorStep"]} {g["msgTimer"]} {g["msg"]!r}'
    e.run(300); assert msg_of('DRAG 3 TOUCHING GEMS') and e.state()['msgTimer'] == -1, f'{tag}: the prompt must be sticky'
    e.shot(f'{out}/rules_tutor_0_{tag}.png')
    e.set_heat(0); e.set_burn(0, 99); e.set_turn(0)
    path = spell_cells('bed'); e.chain(path); e.press('A', settle=10)
    g = e.state(); assert g['tutorStep'] == 1 and g['msg'].startswith(peek) and g['msgTimer'] == -1, f'{tag}: after the first word: step {g["tutorStep"]} {g["msg"]!r}'
    e.shot(f'{out}/rules_tutor_1_{tag}.png')
    # a callout takes the line for its moment, then the prompt returns
    e.set_heat(0); path = spell_cells('bread'); e.chain(path); e.press('A', settle=4)
    g = e.state(); assert g['msg'] == 'GOOD!' and g['msgTimer'] > 0, f'{tag}: the callout should override the prompt: {g["msg"]!r}'
    e.run(80); g = e.state(); assert g['msg'].startswith(peek) and g['msgTimer'] == -1, f'{tag}: the prompt did not come back: {g["msg"]!r} {g["msgTimer"]}'
    # the first peel: a hole says what its number means (zero the back layer, give (C, 3) exactly one pass-letter neighbour)
    e.set_heat(0); e.set_burn(0, 99)
    for c in range(COLS):
        for r in range(ROWS): e.set_back(c, r, 0)
    nc, nr = e.neighbors(C, 3)[0]; e.set_back(nc, nr, 1)
    target = (C, 3); e.set_cell(C, 3, TILE, 'e'); e.run(5)
    e.peel(*target); g = e.state()
    assert g['peels'] == 1 and g['tutorStep'] == 2 and g['msgTimer'] > 0 and g['msg'].startswith('1: A PASS LETTER TOUCHES'), f'{tag}: hole message: step {g["tutorStep"]} {g["msg"]!r}'
    e.shot(f'{out}/rules_tutor_hole_{tag}.png')
    e.run(250); g = e.state(); assert g['tutorStep'] == 2 and g['msg'].startswith(peek) and g['msgTimer'] == -1, f'{tag}: after the hole message: {g["msg"]!r}'
    # the first pass letter found: the closing tip, which ends the guide when it runs out
    e.set_back(C, 6, 1); e.set_cell(C, 6, TILE, 'e'); e.set_heat(0); e.set_burn(0, 99); e.run(5)
    e.peel(C, 6); g = e.state()
    assert g['found'][0] == 1 and g['tutorStep'] == 3 and g['msg'].startswith('FIND THE REST.'), f'{tag}: closing tip: step {g["tutorStep"]} found {g["found"]} {g["msg"]!r}'
    assert g['tutorDone'] == 0, f'{tag}: the guide must not be marked done before the tip has run'
    e.shot(f'{out}/rules_tutor_rest_{tag}.png')
    e.run(250); g = e.state()
    assert g['tutorStep'] == 4 and g['tutorDone'] == 1 and g['msg'] == '' and g['msgTimer'] == 0, f'{tag}: the guide should end: step {g["tutorStep"]} done {g["tutorDone"]} {g["msg"]!r}'
    # once done, a new game has no prompts, and the old nudge is back as a fallback
    to_title(); e.start_classic(); e.run(260)
    g = e.state(); assert g['tutorStep'] == 4 and g['msg'] == '', f'{tag}: a finished guide must stay off: {g["tutorStep"]} {g["msg"]!r}'

@scenario('guided first shelf (right hand): drag, peek, the first hole, the first pass letter, then off for good')
def _():
    set_hand(False); check_tutor('right')

@scenario('guided first shelf (left hand): the same, with HOLD R in the prompts')
def _():
    set_hand(True); check_tutor('left')

@scenario('guided first shelf: a first peel that finds a pass letter, and a shelf cleared mid-guide ends it')
def _():
    set_hand(False)
    e.set_tutor(0); e.start_classic(); e.set_lives(3); e.run(260); e.set_heat(0); e.set_burn(0, 99); e.set_turn(0)
    path = spell_cells('bed'); e.chain(path); e.press('A', settle=10); assert e.state()['tutorStep'] == 1
    g = e.state(); c, r = e.secret_cells(g)[1]
    for cc, rr in e.burn_cells(g): e.set_cell(cc, rr, TILE, 'e')
    e.peel(c, r); g = e.state()
    assert g['found'][0] == 1 and g['msg'] == 'A PASS LETTER! PEEL AROUND IT' and g['tutorStep'] == 2, f'secret first: step {g["tutorStep"]} {g["msg"]!r}'
    e.run(250); g = e.state()
    assert g['tutorStep'] == 3 and g['msg'].startswith('FIND THE REST.'), f'after the first-peel message the closing tip follows: {g["tutorStep"]} {g["msg"]!r}'
    # clear the shelf before the tip has run out: the guide is done anyway
    for i in range(2, g['passLen'] + 1):
        e.set_heat(0); e.set_turn(0); e.set_burn(0, 99)
        g = e.state()
        for cc, rr in e.burn_cells(g): e.set_cell(cc, rr, TILE, 'e')
        c, r = e.secret_cells(g)[i]; e.peel(c, r)
    g = e.state(); assert g['state'] == ST_PASSED and g['tutorDone'] == 1 and g['tutorStep'] == 4, f'cleared: {g["state"]} done {g["tutorDone"]} step {g["tutorStep"]}'
    assert g['msgTimer'] != -1, 'no sticky prompt may survive into the results card'
    e.run(40); e.tap(e.bx + 100, 100); e.run(20)
    e.set_tutor(1)

@scenario('title tip cards: LEFT/RIGHT and L/R flip them, the dots follow, they turn by themselves every 420 frames')
def _():
    set_hand(False); to_title(); e.run(10)
    i0 = e.state()['tipIndex']
    e.press('RIGHT', settle=8); assert e.state()['tipIndex'] == (i0 + 1) % 3, 'RIGHT should show the next card'
    e.press('LEFT', settle=8); e.press('LEFT', settle=8); assert e.state()['tipIndex'] == (i0 + 2) % 3, 'LEFT twice wraps backwards'
    e.press('R', settle=8); e.press('L', settle=8); e.press('L', settle=8); assert e.state()['tipIndex'] == (i0 + 1) % 3, 'L and R flip as well'
    shots = []
    for k in range(3):
        i = e.state()['tipIndex']; e.run(12); e.shot(f'{out}/rules_tip_{i}.png'); px = e.pixels()
        for j in range(3):                                 # the dot of the card on show is a filled gold square, the others outlines
            lit = sum(1 for yy in (171, 172) for xx in (118 + j * 8 + 1, 118 + j * 8 + 2) if sum(px[xx, yy]) > 300)   # the frame colour varies: judge by brightness
            assert (lit >= 4) == (j == i), f'tip card {i}: dot {j} lit {lit}'
        over = [(x, y) for y in range(40, 168) for x in list(range(0, 11)) + list(range(246, 256)) if sum(px[x, y]) > 300 and not (x in (8, 9, 246, 247))]
        assert not over, f'tip card {i}: text reaches the plaque border, first {over[:3]}'
        e.press('RIGHT', settle=8)
    j = e.state()['tipIndex']
    e.run(430); assert e.state()['tipIndex'] == (j + 1) % 3, f'the card should turn by itself after about 420 frames: {j} -> {e.state()["tipIndex"]}'
    e.press('RIGHT', settle=8); k = e.state()['tipIndex']; e.run(300); e.press('RIGHT', settle=8)
    assert e.state()['tipIndex'] == (k + 1) % 3, 'a flip restarts the 420 frame timer'

@scenario('side-aware hints: HOLD L on the right-handed panel and title, HOLD R when left-handed')
def _():
    for left in (False, True):
        set_hand(left); e.start_classic(); calm(); e.run(8)
        X = panel_x(); px = e.pixels()
        # the sixth glyph of "HOLD ?: PEEK" is L or R at panel x + 30: R has a pixel on the right of its second row, L has none
        lit = sum(px[X + 34, 192 + 74 + 1]) > 100
        assert lit == left, f'{"left" if left else "right"}-handed panel hint: glyph at the third column lit={lit}'
        assert count_color(px, X, 192 + 74, X + 24, 192 + 81, (13, 13, 16), 2) > 20, 'the hint text is missing'
        e.shot(f'{out}/rules_hint_{"left" if left else "right"}.png')
        to_title(); e.run(20); e.shot(f'{out}/rules_title_hint_{"left" if left else "right"}.png')
        px = e.pixels()
        # "DRAG SPELLS. L PEEKS": the L or R is the 14th character, at x BX(86 - 59 + 78); R has a pixel in the right column of its second row, L none
        lit = sum(px[e.bx + 109, 192 + 112 + 1]) > 100
        assert lit == left, f'title tagline for the {"left" if left else "right"} hand: lit={lit}'
    set_hand(False)

# ---- music: the sequencer through the debug mirror (nothing here is judged by ear) -------------
SFX_TAP, SFX_UNTAP, SFX_INVALID, SFX_PEEL, SFX_SCORCH, SFX_IGNITE, SFX_FIRESTEP, SFX_MENU = range(8)
HOOK_C = 523                      # shelf 1 is in C: the hook's first note is C5

@scenario('music mirror: a new shelf fades out, plays one pickup bar of bass and pulse, then the hook enters on step 16 at 64 on a 12.5% pulse')
def _():
    to_title(); e.run(60)
    assert e.state()['settings']['music'] == 1, 'MUSIC defaults to ON'
    e.tap(e.bx + 52, 147, settle=0)                       # CLASSIC
    fade = bass = False; m = e.music()
    for _ in range(400):
        e.run(1); m = e.music()
        if m['fade'] > 0:
            fade = True; assert m['step'] == 255, f'step {m["step"]} during the fade'
        if m['pickup'] == 1 and m['step'] < 16:
            assert not (m['vol'][0] or m['vol'][3] or m['vol'][6] or m['vol'][7] or m['vol'][4]), f'pickup bar: only bass and pulse may play, volumes {m["vol"]}'
            bass |= m['vol'][2] > 0
        if m['step'] == 16: break
    assert fade and bass, f'fade {fade} bass in the pickup bar {bass}'
    assert m['tempo'] == 8 and m['mix'] == 0, f'tempo {m["tempo"]} mix {m["mix"]}'
    assert m['hz'][0] == HOOK_C and m['vol'][0] == 64 and m['duty'][0] == 0, f'hook at step 16: {m["hz"][0]} Hz vol {m["vol"][0]} duty {m["duty"][0]}'
    seen = []
    for _ in range(9):
        e.run(1); seen.append(e.music())
    assert [x['duty'][0] for x in seen[:3]] == [0, 0, 3], 'the hook goes from 12.5% to 50% duty on the third frame'
    assert seen[6]['vol'][0] == 44, f'the hook settles at 44 after seven frames, read {seen[6]["vol"][0]}'
    assert seen[7]['step'] == 17 and seen[6]['step'] == 16, 'the next step arrives eight frames later'

@scenario('music: the MUSIC option silences every channel, effects included, and ON brings the hook back (attract mix: 60 on a 50% pulse)')
def _():
    to_title(); e.run(5)
    e.tap(e.bx + 86, 170); e.run(10)
    assert e.state()['state'] == ST_OPTIONS
    e.tap(130, opt_row_y(5)); e.run(4)                      # MUSIC: any half steps a two-valued row
    assert e.state()['settings']['music'] == 0
    m = e.music(); assert not any(m['vol']), f'MUSIC OFF but a channel is at {m["vol"]}'
    e.tap(130, opt_row_y(0)); e.run(2)                      # a menu blip while off stays silent too
    assert not any(e.music()['vol']), 'effects ignore the MUSIC option'
    e.tap(130, opt_row_y(5)); e.run(4)
    assert e.state()['settings']['music'] == 1
    m = e.wait_step(0)
    assert m['mix'] == 1 and m['vol'][0] == 60 and m['duty'][0] == 3, f'attract mix hook: vol {m["vol"][0]} duty {m["duty"][0]} mix {m["mix"]}'
    e.press('B', settle=20)

@scenario('music effects from play: taps climb, an untap steps back, a bad word, peels, scorch, ignite, firestep')
def _():
    ensure_play(); fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(40)
    g = e.state(); ws = e.find_words(g, minlen=4, maxlen=5)
    assert ws, 'no word on the board'
    word, path = ws[0]
    c0 = e.music()['sfxCount']
    e.chain(path, release=False)
    m = e.music()
    assert m['sfxCount'] - c0 == len(path) and m['lastSfx'] == SFX_TAP, f'{len(path)} taps expected, got {m["sfxCount"] - c0} (last {m["lastSfx"]})'
    c1 = m['sfxCount']
    e.emu.input.touch_release(); e.run(3)
    e.press('B', settle=3)                                  # B steps the chain back one gem
    m = e.music(); assert m['sfxCount'] == c1 + 1 and m['lastSfx'] == SFX_UNTAP, f'untap: count {m["sfxCount"] - c1} last {m["lastSfx"]}'
    e.press('Y', settle=5)
    # a three-gem chain that is not a word: INVALID
    for r in (3, 4, 5): e.set_cell(C, r, TILE, 'x')
    e.run(20); c1 = e.music()['sfxCount']
    e.chain([(C, 3), (C, 4), (C, 5)]); e.press('A', settle=3)
    m = e.music(); assert m['lastSfx'] == SFX_INVALID, f'a non-word should fire INVALID, last {m["lastSfx"]}'
    e.press('Y', settle=5); e.run(30)
    # a peel that finds nothing: PEEL
    g = e.state()
    plain = [(c, r) for c in range(COLS) for r in range(ROWS) if not g['back'][c][r] and g['cell'][c][r][1] == TILE and (c, r) != (C, 3)]
    pc, pr = plain[0]; e.peel(pc, pr)
    assert e.music()['lastSfx'] == SFX_PEEL, f'a peel that finds nothing fires PEEL, last {e.music()["lastSfx"]}'
    e.run(30)
    # a peel under a flame scorches: SCORCH
    g = e.state()
    sc = [(c, r) for c in range(COLS) for r in range(2, ROWS) if not g['back'][c][r] and (c, r) != (pc, pr) and g['cell'][c][r][1] == TILE and c != C]
    qc, qr = sc[0]; e.set_cell(qc, qr - 1, BURN, 'a'); e.peel(qc, qr)
    assert e.music()['lastSfx'] == SFX_SCORCH, f'peeling under a flame fires SCORCH, last {e.music()["lastSfx"]}'
    e.run(60); ensure_play(); e.run(20)
    # a word with the meter full lights a flame: IGNITE
    e.set_burn(0, 99); g = e.state()
    for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
    e.set_heat(g['heatThreshold']); g = e.state(); ws = e.find_words(g, minlen=4, maxlen=5)
    e.chain(ws[0][1]); e.press('A', settle=2)
    assert e.music()['lastSfx'] == SFX_IGNITE, f'a flame lighting fires IGNITE, last {e.music()["lastSfx"]}'
    e.run(40)
    # the fire moving: FIRESTEP
    c1 = e.music()['sfxCount']; e.fire_step(); m = e.music()
    assert m['lastSfx'] == SFX_FIRESTEP and m['sfxCount'] == c1 + 1, f'a flame moving fires FIRESTEP once, last {m["lastSfx"]}'
    e.set_heat(0); e.run(30)

@scenario('music: menu taps blip (title buttons, options rows, pause rows)')
def _():
    to_title(); e.run(30); c0 = e.music()['sfxCount']
    e.tap(e.bx + 86, 170); e.run(4)
    assert e.music()['lastSfx'] == SFX_MENU and e.music()['sfxCount'] == c0 + 1, 'OPTIONS fires MENU'
    c0 = e.music()['sfxCount']; e.press('DOWN', settle=2)
    assert e.music()['sfxCount'] == c0 + 1, 'walking the options list fires MENU'
    e.press('B', settle=20)
    ensure_play(); e.press('START', settle=10); c0 = e.music()['sfxCount']
    e.press('DOWN', settle=2)
    assert e.music()['lastSfx'] == SFX_MENU and e.music()['sfxCount'] == c0 + 1, 'a pause row move fires MENU'
    e.press('START', settle=20)

@scenario('music composition: A A B A over four loops (section in the mirror), restarting with the shelf')
def _():
    to_title(); e.run(5)
    secs = []
    for _ in range(8):
        m = e.wait_step(0); secs.append(m['section'])
    cyc = [0, 0, 1, 0] * 3
    assert any(secs == cyc[k:k + 8] for k in range(4)), f'sections of eight loops {secs} are not a run of A A B A'
    ensure_play(); fresh_shelf(); e.run(5)
    m = e.wait_step(0); assert m['section'] == 0, 'a new shelf starts in section A'

@scenario('music under fire: alarm blips by the lowest flame, tempo lifts to 6 on row 5 and relaxes a bar after the fire is out')
def _():
    fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(5)
    e.wait_step(15); e.wait_step(0)
    assert e.music()['tempo'] == 8, 'a fire-free shelf plays at 8 frames a step'
    blips = 0
    for _ in range(64 * 8):                                  # a loop with no fire: the alarm is silent
        e.run(1); blips += e.music()['vol'][4] > 0
    assert blips == 0, f'alarm sounded {blips} frames with no fire'
    e.set_cell(C, 5, BURN, 'a')
    e.wait_step(15); m = e.wait_step(0)
    assert m['tempo'] == 6, f'tempo {m["tempo"]} with a flame on row 5'
    n = 0; vols = set(); duty = set()
    for _ in range(16 * 6):
        e.run(1); m = e.music()
        if m['vol'][4]: n += 1; vols.add(m['vol'][4]); duty.add(m['duty'][4])
    assert vols and vols <= {53}, f'row 5 alarm volume {vols}'     # min(60, 28 + 5 * 5)
    assert duty == {0}, f'the alarm is a 12.5% pulse: {duty}'
    assert 28 <= n <= 36, f'eight four-frame blips a bar: {n} frames lit'
    e.wait_step(20); e.set_cell(C, 5, TILE, 'e')
    seen = []
    for st in (32, 48):
        e.wait_step(st); seen.append(e.music()['tempo'])
    assert seen == [6, 8], f'the tempo should hold one bar after the fire is out, then relax: {seen}'

@scenario('music: a word brings in the bass riff and the groove (the drum level and the kick are visible in the mirror)')
def _():
    fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(5)
    g = e.state(); ws = e.find_words(g, minlen=5, maxlen=7)
    assert ws, 'no five-letter word on the board'
    e.chain(ws[0][1]); e.press('A', settle=2)
    assert e.music()['drumLevel'] >= 2, f'a five-letter word lifts the groove: level {e.music()["drumLevel"]}'
    kick = False; bass = False
    for _ in range(64 * 8 // 2):
        e.run(1); m = e.music()
        kick |= m['vol'][6] == 54 and m['hz'][6] == 2400
        bass |= m['vol'][2] > 0
    assert kick and bass, f'kick {kick} bass {bass}'

@scenario('music words: the chime follows the length of the word (C5 first, 64 for five or six letters, 72 for seven, 76 for eight and more)')
def _():
    fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(5)
    g = e.state(); ws = e.find_words(g, minlen=5, maxlen=7)
    assert ws, 'no five-letter word on the board'
    word, path = ws[0]
    w0 = e.music()['wordCount']
    e.chain(path); e.press('A', hold=2, settle=0)
    m = e.music()
    assert m['wordCount'] == w0 + 1, 'musicWord was not called'
    want = 64 if len(word) <= 6 else 72
    assert m['hz'][5] == HOOK_C and m['vol'][5] in (want, want + 8) and m['duty'][5] == 3, f'{word}: chime {m["hz"][5]} Hz vol {m["vol"][5]}'

@scenario('music: a failed shelf plays its stinger and settles into the quiet bed; the next shelf fades it out')
def _():
    fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(150)
    force_fail()
    m = e.music(); assert m['mix'] == 4 and m['failPhase'] in (1, 2), f'mix {m["mix"]} phase {m["failPhase"]} after the fail'
    e.run(120); m = e.music(); st = m['step']
    assert m['failPhase'] == 2, 'the stinger gives way to the bed'
    e.run(80); m = e.music()
    assert m['vol'][3] == 22 and m['hz'][3] == 311 and not (m['vol'][0] or m['vol'][1] or m['vol'][4] or m['vol'][6] or m['vol'][7]), f'bed: {m["vol"]}'
    assert m['step'] == st, 'the step sequencer stays stopped under the bed'
    e.tap(e.bx + 100, 100, hold=2, settle=0); e.run(2)
    m = e.music(); assert m['failPhase'] == 0 and m['fade'] > 0 and m['mix'] == 0, f'next shelf: phase {m["failPhase"]} fade {m["fade"]} mix {m["mix"]}'

@scenario('music: a cleared shelf plays the fanfare, then the octave-up hook with no drums; tapping on fades into the next shelf')
def _():
    fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(60)
    force_pass()
    m = e.music(); assert m['mix'] == 3 and m['fanfare'] in (1, 2, 3), f'mix {m["mix"]} fanfare {m["fanfare"]}'
    e.run(120); m = e.music()
    assert m['fanfare'] == 3 and m['drumLevel'] == 0, f'fanfare {m["fanfare"]} drum level {m["drumLevel"]}'
    top = 0
    for _ in range(300):
        e.run(1); m = e.music(); top = max(top, m['vol'][0])
        assert not (m['vol'][6] or m['vol'][7]) and m['vol'][2] == 0, 'the results loop has no drums and no bass'
    assert top == 89 and m['vol'][3] == 60, f'octave-up hook vol {top}, pad {m["vol"][3]}'
    e.tap(e.bx + 100, 100, hold=2, settle=0); e.run(2)
    m = e.music(); assert m['fanfare'] == 0 and m['fade'] > 0, f'next shelf: fanfare {m["fanfare"]} fade {m["fade"]}'
    ensure_play()

@scenario('music mixes: pause drops the music to a quarter, peek to a fifth; the title is the attract mix and starting a shelf drops out of it')
def _():
    ensure_play(); fresh_shelf(); e.set_heat(0); e.set_burn(0, 99); e.run(200)
    top = 0
    for _ in range(160):
        e.run(1); top = max(top, e.music()['vol'][0])
    assert top == 64, f'hook peak in play {top}'
    e.hold('L', True); e.run(30)
    peak = 0
    for _ in range(160):
        e.run(1); peak = max(peak, e.music()['vol'][0])
    e.hold('L', False)
    assert 0 < peak <= 64 // 5, f'peeking: hook peak {peak}'
    e.press('START', settle=30); m = e.music(); assert m['mix'] == 2
    pk = 0
    for _ in range(160):
        e.run(1); pk = max(pk, e.music()['vol'][0])
    assert 0 < pk <= 16, f'paused: hook peak {pk}'
    e.press('START', settle=20); assert e.music()['mix'] == 0
    to_title(); e.run(30); m = e.music(); assert m['mix'] == 1
    bass = hats = False
    for _ in range(300):
        e.run(1); m = e.music(); bass |= m['vol'][2] > 0; hats |= m['vol'][7] > 0
    assert bass and hats and m['drumLevel'] >= 1, f'attract: bass {bass} hats {hats} drums {m["drumLevel"]}'
    e.tap(e.bx + 52, 147); e.run(400); m = e.music()
    assert m['mix'] == 0 and m['drumLevel'] == 0, f'playing: mix {m["mix"]} drums {m["drumLevel"]}'

print('RULES OK' if not fails else f'RULES FAILED: {fails}')
sys.exit(fails)
