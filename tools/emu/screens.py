#!/usr/bin/env python3
"""Regenerates every picture in docs/img/ for docs/GUIDE.md by driving the real ROM in the headless emulator.

    python3 tools/emu/screens.py [wordPass.nds]              # all shots, four at a time
    python3 tools/emu/screens.py wordPass.nds --list         # the shots and the files each writes
    python3 tools/emu/screens.py wordPass.nds --only peek    # shots whose function name contains "peek"
    python3 tools/emu/screens.py wordPass.nds --jobs 1       # one at a time (easier to read the log)

How it works
  * Every picture comes from a NAMED shot function below, registered with `@shot('file.png', ...)`.
    tools/check_guide.py reads those decorators, so a picture the guide shows that no shot declares fails
    the check, and so does a declared picture the guide never shows.
  * Each shot runs in its own child process with a freshly booted ROM, so a shot never depends on what
    another one left behind and the shots can run in parallel. The ROM is deterministic (the DS clock
    is pinned through the poke channel, boards are crafted with pokes), so a re-run reproduces the files.
  * To add a picture: write a function, decorate it with the file names it saves, call `save` / `montage`,
    add the image to docs/GUIDE.md (with alt text and an italic caption), run `make guide`.
  * Annotations are drawn on the pictures with PIL (numbered markers, arrows, rings) by the helpers below,
    so the sources of every annotated figure live in this file: there is no binary-only art.

Requires py-desmume, Pillow and (for the symbol lookup) the toolchain image, like the other tools in tools/emu.
"""
import os, sys, subprocess, argparse, traceback, concurrent.futures, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault('SDL_VIDEODRIVER', 'dummy'); os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
from PIL import Image, ImageDraw, ImageFont
import wp
from wp import (Emu, COLS, ROWS, EMPTY, TILE, BURN, HOLE, SECRET, ST_TITLE, ST_PLAY, ST_PASSED, ST_FAIL, ST_PAUSE,
                ST_OPTIONS, ST_STATS, ST_GUIDE, FAIL_FIRE, FAIL_SCORCH, FAIL_WORDS)

REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(REPO, 'docs', 'img')
CFG = {'rom': os.path.join(REPO, 'wordPass.nds')}

# ---------------------------------------------------------------------------------------------
# registry
# ---------------------------------------------------------------------------------------------
SHOTS = {}          # function name -> (files, function)
def shot(*files):
    def deco(fn):
        SHOTS[fn.__name__] = (files, fn)
        return fn
    return deco

# ---------------------------------------------------------------------------------------------
# picture helpers (PIL)
# ---------------------------------------------------------------------------------------------
GOLD = (255, 205, 60); WHITE = (255, 255, 255); RED = (255, 70, 60); CYAN = (60, 230, 230); BLACKISH = (8, 8, 14)

def _font(size, bold=True):
    for name in ('DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf', 'LiberationSans-Bold.ttf', 'Arial.ttf'):
        try: return ImageFont.truetype(name, size)
        except OSError: pass
    return ImageFont.load_default(size)

def grab(e):
    """The 256x384 screenshot (top screen above, touch screen below) as an RGB image."""
    return e.emu.screenshot().convert('RGB')

def top(img): return img.crop((0, 0, 256, 192))
def touch(img): return img.crop((0, 192, 256, 384))

def zoom(img, k):
    return img if k == 1 else img.resize((img.width * k, img.height * k), Image.NEAREST)

def save(img, name):
    """PNG into docs/img with an optimised palette (the game draws with few colours, so this is lossless in practice)."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name)
    q = img.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q.save(path, optimize=True)
    return path

def ring(d, x, y, r, col=GOLD, w=3):
    d.ellipse((x - r, y - r, x + r, y + r), outline=col, width=w)

def marker(d, x, y, n, col=GOLD, r=11):
    """A numbered disc with its centre at (x, y)."""
    d.ellipse((x - r, y - r, x + r, y + r), fill=col, outline=BLACKISH, width=2)
    f = _font(int(r * 1.35))
    d.text((x, y + 1), str(n), fill=BLACKISH, font=f, anchor='mm')

def arrow(d, x0, y0, x1, y1, col=GOLD, w=3):
    import math
    d.line((x0, y0, x1, y1), fill=col, width=w)
    a = math.atan2(y1 - y0, x1 - x0); L = 11
    for s in (-0.45, 0.45):
        d.line((x1, y1, x1 - L * math.cos(a + s), y1 - L * math.sin(a + s)), fill=col, width=w)

def annotate(img, k, items, margin=0):
    """Draw annotations on a zoomed copy. `items` use SOURCE pixel coordinates of `img` (before zoom):
       ('n', x, y, number)                      numbered disc at a point
       ('ring', x, y, radius_px)                a ring around a point
       ('box', x0, y0, x1, y1)                  a rectangle
       ('arrow', x0, y0, x1, y1)                an arrow
       ('label', x, y, text)                    a text tag (white on dark)
       ('n>', x, y, number, tx, ty)             a disc at (x, y) with an arrow to (tx, ty)
       ('m', number, tx, ty, side[, my])        a disc in the left ('L') or right ('R') margin (at height my, default ty) with a
                                                leader line to the point (tx, ty); `margin` output pixels are added on both sides"""
    base = zoom(img, k)
    out = Image.new('RGB', (base.width + 2 * margin, base.height), (14, 14, 22)); out.paste(base, (margin, 0)); d = ImageDraw.Draw(out)
    X = lambda v: v * k + margin
    for it in items:
        t = it[0]
        if t == 'n': marker(d, X(it[1]), it[2] * k, it[3])
        elif t == 'ring': ring(d, X(it[1]), it[2] * k, it[3] * k)
        elif t == 'box': d.rectangle((X(it[1]), it[2] * k, X(it[3]), it[4] * k), outline=GOLD, width=3)
        elif t == 'arrow': arrow(d, X(it[1]), it[2] * k, X(it[3]), it[4] * k)
        elif t == 'label':
            f = _font(15); tw = d.textlength(it[3], font=f)
            d.rectangle((X(it[1]) - 4, it[2] * k - 3, X(it[1]) + tw + 4, it[2] * k + 18), fill=BLACKISH)
            d.text((X(it[1]), it[2] * k), it[3], fill=WHITE, font=f)
        elif t == 'n>':
            arrow(d, X(it[1]), it[2] * k, X(it[4]), it[5] * k); marker(d, X(it[1]), it[2] * k, it[3])
        elif t == 'm':
            n, tx, ty, side = it[1:5]; my = it[5] if len(it) > 5 else ty
            mx = margin // 2 if side == 'L' else out.width - margin // 2
            d.line((mx, my * k, X(tx), ty * k), fill=GOLD, width=2)
            d.ellipse((X(tx) - 4, ty * k - 4, X(tx) + 4, ty * k + 4), fill=GOLD)
            marker(d, mx, my * k, n)
    return out

def montage(panels, cols, labels=None, gap=10, pad=12, k=1, title=None):
    """Panels (PIL images, same size is best) in a grid on a dark ground, each with a caption under it."""
    panels = [zoom(p, k) for p in panels]
    w = max(p.width for p in panels); h = max(p.height for p in panels)
    rows = (len(panels) + cols - 1) // cols
    f = _font(15)
    def wrap(txt):
        words, lines, cur = txt.split(), [], ''
        probe = ImageDraw.Draw(Image.new('RGB', (4, 4)))
        for wd in words:
            t = (cur + ' ' + wd).strip()
            if probe.textlength(t, font=f) > w - 8 and cur: lines.append(cur); cur = wd
            else: cur = t
        return lines + [cur]
    wrapped = [wrap(t) for t in labels] if labels else None
    lab = (8 + 19 * max(len(x) for x in wrapped)) if labels else 0
    top_ = 30 if title else 0
    img = Image.new('RGB', (pad * 2 + cols * w + (cols - 1) * gap, pad * 2 + top_ + rows * (h + lab) + (rows - 1) * gap), (14, 14, 22))
    d = ImageDraw.Draw(img)
    if title: d.text((pad, pad - 2), title, fill=GOLD, font=_font(17))
    for i, p in enumerate(panels):
        x = pad + (i % cols) * (w + gap); y = pad + top_ + (i // cols) * (h + lab + gap)
        img.paste(p, (x, y))
        if labels:
            for j, line in enumerate(wrapped[i]): d.text((x + w // 2, y + h + 4 + 19 * j), line, fill=WHITE, font=f, anchor='ma')
    return img

# ---------------------------------------------------------------------------------------------
# driving the game
# ---------------------------------------------------------------------------------------------
DAY = 20270310          # the date the title is pinned to when a picture does not care

def daily_mod(d): return (((d * 2654435761) & 0xFFFFFFFF) >> 28) & 3
def date_with_mod(mod, start=20270401):
    d = start
    while daily_mod(d) != mod: d += 1
    return d

def boot(left=False, tutor_done=True, clock=DAY):
    """A freshly booted ROM on the title screen, the DS clock pinned, the guided first shelf off unless asked."""
    e = Emu(CFG['rom'])
    e.run(10)
    e.set_tutor(1 if tutor_done else 0)
    e.set_clock(clock)
    e.run(150)
    if left: set_hand(e, True)
    return e

def set_hand(e, left):
    """Flip HAND through the real options screen (the list never mirrors, so the taps are the same)."""
    e.tap(e.bx + 86, 170); e.run(10)
    e.tap(130, 32 + 13 * 3 + 6); e.run(10)
    e.tap(86, 161); e.run(20)
    assert bool(e.state()['settings']['hand']) == left, 'HAND did not flip'
    e.set_hand(left)

def options_row(e, row, taps=0):
    """Open OPTIONS and light `row`; step it forward `taps` times with the d-pad's RIGHT."""
    for _ in range(row): e.press('DOWN', settle=3)
    for _ in range(taps): e.press('RIGHT', settle=3)

def to_play(e, tutor_done=True):
    e.set_tutor(1 if tutor_done else 0)
    e.start_classic()
    g = e.state(); assert g['state'] == ST_PLAY, f'not in play: {g["state"]}'
    return g

def calm(e):
    """No spontaneous ignition, no fire advance on submits, no overtime heat."""
    e.set_heat(0); e.set_burn(0, 99); e.set_turn(0)
    e.press('Y', settle=3)

def msg_off(e):
    """Let the shelf's introductory message line run out so it does not cover the picture."""
    e.run(260)

def craft(e, rows, diamonds=None, back=None, holes=None, found=None):
    """Put a whole board down. `rows` is eight strings of seven characters, top row first, one per column:
       a-z a plain gem (q is the Qu gem), A-Z a burning gem, '#' a hole, '.' leave the cell alone.
       `diamonds` {(c, r): letter} revealed pass letters, `back` {(c, r): pass index starting at 1}
       sets the back layer (every other back cell is cleared first), `holes` unused (use '#')."""
    assert len(rows) == ROWS and all(len(r) == COLS for r in rows), 'craft: eight rows of seven'
    for r, line in enumerate(rows):
        for c, ch in enumerate(line):
            if ch == '.': continue
            if ch == '#': e.set_cell(c, r, HOLE, 0)
            elif ch.isupper(): e.set_cell(c, r, BURN, ch.lower())
            else: e.set_cell(c, r, TILE, ch)
    for (c, r), ch in (diamonds or {}).items(): e.set_cell(c, r, SECRET, ch)
    if back is not None:
        for c in range(COLS):
            for r in range(ROWS): e.set_back(c, r, 0)
        for (c, r), i in back.items(): e.set_back(c, r, i)
    e.run(40)       # let gems settle (yoff animation) before a picture is taken

FILL = ['seatrol', 'tonerdi', 'ratelon', 'sioncal', 'eldarti', 'noisera', 'taleorn', 'raseint']   # an ordinary-looking board of lower case letters

def plain_board(e):
    craft(e, FILL)

def peek(e, on=True):
    e.hold('L', on); e.run(8)

def pause_row_y(i): return 88 + 16 * i + 6

# ---------------------------------------------------------------------------------------------
# the shots
# ---------------------------------------------------------------------------------------------
@shot('title.png', 'title_zoom.png')
def title():
    e = boot(tutor_done=False)
    e.run(100)
    img = grab(e)
    save(zoom(img, 2), 'title.png')
    save(annotate(touch(img).crop((0, 0, 172, 192)), 3, [('m', 1, 52, 145, 'L'), ('m', 2, 120, 143, 'R'), ('m', 3, 86, 166, 'L', 170), ('m', 4, 137, 15, 'R'), ('m', 5, 35, 184, 'L', 186), ('m', 6, 60, 15, 'L')], margin=40), 'title_zoom.png')

@shot('title_daily_states.png')
def title_daily_states():
    panels, labels = [], []
    e = boot()
    for mod, nm in enumerate(('STANDARD', 'EMBER', 'DEEP', 'LONG')):
        d = date_with_mod(mod)
        e.set_clock(d); e.set_daily(0, 0, 0); e.run(80)
        panels.append(touch(grab(e)).crop((8, 128, 168, 192))); labels.append(f'fresh day, modifier {nm}')
    d = date_with_mod(0)
    e.set_clock(d); e.set_daily(d, d - 1 if d % 100 > 1 else d, 0); e.run(80)
    panels.append(touch(grab(e)).crop((8, 128, 168, 192))); labels.append('today already played: PRACTICE')
    e.set_clock(d); e.set_daily(0, d - 1, 4); e.run(80)
    panels.append(touch(grab(e)).crop((8, 128, 168, 192))); labels.append('streak of 4 alive: STREAK 4')
    e.set_clock(20000101); e.run(80)
    panels.append(touch(grab(e)).crop((8, 128, 168, 192))); labels.append('clock never set: SET CLOCK')
    save(montage(panels, 2, labels, k=3), 'title_daily_states.png')


def to_title(e):
    """Pause menu, QUIT TO TITLE (classic: RESUME, RESTART, GUIDE, QUIT)."""
    e.press('START', settle=10)
    for _ in range(3): e.press('DOWN', settle=3)
    e.press('A', settle=3); e.press('A', settle=30)
    assert e.state()['state'] == ST_TITLE

def open_options(e):
    e.press('Y', settle=10)
    assert e.state()['state'] == ST_OPTIONS

@shot('options_screen.png', 'options_rows.png', 'options_themes.png', 'options_frames.png', 'options_accents.png')
def options():
    e = boot()
    open_options(e)
    for _ in range(8): e.press('DOWN', settle=3)      # LANGUAGE, the ninth row
    e.run(20)
    img = grab(e)
    save(zoom(img, 2), 'options_screen.png')
    # one panel per row: the sample board and the help line of the lit row, from the top screen (nine DOWN presses wrap back to row 0)
    e.press('B', settle=20); open_options(e)
    panels, labels = [], []
    names = ['THEME', 'FRAME', 'ACCENT', 'HAND', 'FIRE', 'MUSIC', 'LIBRARY', 'SPELLING', 'LANGUAGE']
    for row in range(9):
        e.run(10)
        panels.append(top(grab(e)).crop((18, 62, 238, 156))); labels.append(f'row {row + 1}: {names[row]}')
        e.press('DOWN', settle=3)
    save(montage(panels, 2, labels, k=2), 'options_rows.png')
    # THEME: the sample board in each of the five themes (CLEAR also adds its own second help line)
    panels, labels = [], []
    for i, nm in enumerate(('JEWEL', 'INK', 'CANDY', 'EMBER', 'CLEAR')):
        e.run(10)
        panels.append(top(grab(e)).crop((18, 62, 238, 156))); labels.append(nm)
        e.press('RIGHT', settle=3)
    save(montage(panels, 2, labels, k=2), 'options_themes.png')
    e.press('DOWN', settle=3)      # FRAME
    panels, labels = [], []
    for nm in ('GOLD', 'SILVER', 'ACCENT'):
        e.run(10)
        panels.append(top(grab(e)).crop((18, 62, 238, 156))); labels.append(nm)
        e.press('RIGHT', settle=3)
    save(montage(panels, 3, labels, k=2), 'options_frames.png')
    e.press('DOWN', settle=3)      # ACCENT
    panels, labels = [], []
    for nm in ('AUTO', 'TEAL', 'MAGENTA', 'LIME', 'ORANGE', 'SKY', 'VIOLET', 'PINK', 'MINT'):
        e.run(10)
        panels.append(top(grab(e)).crop((50, 64, 206, 130))); labels.append(nm)
        e.press('RIGHT', settle=3)
    save(montage(panels, 3, labels, k=2), 'options_accents.png')

@shot('guide_1.png', 'guide_2.png', 'guide_3.png', 'guide_4.png', 'guide_5.png', 'guide_6.png', 'guide_7.png', 'guide_8.png')
def guide_pages():
    e = boot()
    e.tap(137, 15); e.run(150)
    assert e.state()['state'] == ST_GUIDE
    for p in range(8):
        assert e.state()['guidePage'] == p
        e.run(100)
        save(zoom(grab(e), 2), f'guide_{p + 1}.png')
        e.press('RIGHT', settle=60)

def hit_path(e, word, c0=3, r0=0):
    """Lay `word` down a column (each gem touches the next) and return its path."""
    path = [(c0, r0 + i) for i in range(len(word))]
    for (c, r), ch in zip(path, word): e.set_cell(c, r, TILE, ch)
    e.run(30)
    return path

def hold_chain(e, path):
    e.chain(path, release=False); e.run(6)

def release(e):
    e.emu.input.touch_release(); e.run(4)

LAYOUT_BOARD = ['zlnoebi', 'rtdsaum', 'ecaorlp', 'dsistne', 'nuiolag', 'tahcrse', 'ebrdint', 'gaolems']

@shot('play_layout.png')
def play_layout():
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD)
    g = e.state()
    # a word for the ledger, then the first pass letter, then a chain held for the panel to price
    path = hit_path(e, 'stone', 3, 2); e.chain(path); e.press('A', settle=30)
    craft(e, LAYOUT_BOARD)
    calm(e)
    sec = e.secret_cells(e.state())
    c, r = sec[1]; e.peel(c, r); calm(e)
    e.set_heat(4); e.set_turn(7)
    e.run(260)
    path = hit_path(e, 'stare', 5, 0) if False else None
    w = [(1, 0), (1, 1), (1, 2), (1, 3), (1, 4)]
    for (cc, rr), ch in zip(w, 'blame'): e.set_cell(cc, rr, TILE, ch)
    e.run(30); hold_chain(e, w)
    img = grab(e)
    items = [('m', 1, 40, 36, 'L'), ('m', 2, 78, 73, 'L'), ('m', 3, 70, 108, 'L'),
             ('m', 4, 200, 19, 'R'), ('m', 5, 190, 30, 'R', 42), ('m', 6, 226, 42, 'R', 66), ('m', 7, 230, 95, 'R'), ('m', 8, 215, 138, 'R'), ('m', 9, 200, 154, 'R', 164),
             ('m', 10, 88, 192 + 96, 'L'),
             ('m', 11, 242, 192 + 12, 'R'), ('m', 12, 190, 192 + 30, 'R', 192 + 34), ('m', 13, 196, 192 + 47, 'R', 192 + 56), ('m', 14, 200, 192 + 60, 'R', 192 + 78),
             ('m', 15, 190, 192 + 79, 'R', 192 + 100), ('m', 16, 200, 192 + 98, 'R', 192 + 122), ('m', 17, 214, 192 + 132, 'R', 192 + 140), ('m', 18, 214, 192 + 158, 'R', 192 + 162), ('m', 19, 214, 192 + 182, 'R', 192 + 181)]
    save(annotate(img, 2, items, margin=40), 'play_layout.png')


def board_crop(img, k=3, box=(0, 0, 172, 192)):
    return zoom(touch(img).crop(box), k)

def spell_board(e, rows=None):
    craft(e, rows or LAYOUT_BOARD)

@shot('chain_valid.png', 'chain_invalid.png', 'chain_submit.png')
def chain_shots():
    e = boot(); to_play(e); calm(e); craft(e, LAYOUT_BOARD)
    msg_off(e)
    # a valid chain: STONE down column 3; the gold ring on the tail, the rope in the accent colour, the panel prices it
    w = hit_path(e, 'stone', 3, 1)
    hold_chain(e, w); e.run(10)
    g = e.state(); assert g['chainLen'] == 5 and g['chainScore'] == 100, (g['chainLen'], g['chainScore'])
    img = grab(e)
    tx, ty = e.center(3, 5)
    save(annotate(img, 2, [('m', 1, tx, 192 + ty, 'L'), ('m', 2, tx, 192 + ty - 44, 'L', 192 + ty - 60), ('m', 3, 200, 192 + 79, 'R'), ('m', 4, 244, 192 + 84, 'R', 192 + 100), ('m', 5, 232, 192 + 94, 'R', 192 + 122), ('m', 6, 128, 108, 'L'), ('m', 7, 214, 192 + 132, 'R', 192 + 150)], margin=40), 'chain_valid.png')
    release(e)
    e.press('Y', settle=4)
    # an invalid chain: the same gems with the last one swapped for a letter that spells nothing
    e.set_cell(3, 5, TILE, 'x'); e.run(20)
    hold_chain(e, w); e.run(8)
    g = e.state(); assert g['chainLen'] == 5 and g['chainScore'] == 0
    img = grab(e)
    tx, ty = e.center(3, 5)
    save(annotate(img, 2, [('m', 1, 186, 192 + 84, 'R'), ('m', 2, 214, 192 + 132, 'R'), ('m', 3, tx, 192 + ty, 'L'), ('m', 4, 128, 108, 'L')], margin=40), 'chain_invalid.png')
    release(e); e.press('Y', settle=4)
    # the submit moment: a few frames after the tap, shards in the air and the +score over the chain
    e.set_cell(3, 5, TILE, 'e'); e.run(20)
    hold_chain(e, w); release(e)
    e.press('A', hold=2, settle=3)
    img = grab(e)
    save(annotate(img, 2, [('m', 1, 88, 192 + 100, 'L'), ('m', 2, 60, 192 + 75, 'L', 192 + 40), ('m', 3, 128, 108, 'L'), ('m', 4, 214, 192 + 132, 'R'), ('m', 5, 190, 30, 'R')], margin=40), 'chain_submit.png')

@shot('peel_flow.png')
def peel_flow():
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD, back={(1, 2): 1, (2, 2): 2, (2, 3): 3, (3, 3): 4})
    e.run(40)
    # 1: hold L, touch a gem: it is only armed (double rim and a PEEL label)
    e.hold('L', True); e.run(6)
    e.emu.input.touch_set_pos(*e.center(2, 4)); e.run(8)
    p1 = board_crop(grab(e), 2)
    # 2: lift the stylus: the gem is peeled, the cell shows how many of its six neighbours hide pass letters
    e.emu.input.touch_release(); e.run(20)
    p2 = board_crop(grab(e), 2)
    # 3: a gem with a pass letter behind it: a diamond, +100
    e.emu.input.touch_set_pos(*e.center(2, 3)); e.run(6); e.emu.input.touch_release(); e.run(10)
    p3 = board_crop(grab(e), 2)
    e.hold('L', False)
    save(montage([p1, p2, p3], 3, ['1 touch to arm', '2 lift to peel: a 2', '3 a pass letter: diamond'], k=1), 'peel_flow.png')

# ---------------------------------------------------------------------------------------------
def full_touch(e, k=2):
    return zoom(touch(grab(e)), k)

def neighbours(c, r):
    return Emu.neighbors(c, r)

@shot('peek_digits.png')
def peek_digits():
    """The same peeled cell with 0..6 of its six neighbours hiding a pass letter (the rings are added for the picture)."""
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD, back={})
    panels, labels = [], []
    hole = (3, 3)
    ring_cells = neighbours(*hole)
    assert len(ring_cells) == 6
    order = [0, 3, 1, 4, 2, 5]      # which neighbours get a letter first: spread around the ring
    for n in range(7):
        picked = {ring_cells[i]: 1 + j for j, i in enumerate(order[:n])}
        craft(e, ['.' * 7] * 8, back=picked)
        e.set_cell(3, 3, HOLE, 0)
        peek(e); e.run(20)
        im = touch(grab(e)).crop((28, 20, 148, 150))
        d = ImageDraw.Draw(im)
        for (c, r) in picked:
            x, y = e.cell_rect(c, r)[:2]; y -= 192
            ring(d, x + 12 - 28, y + 11 - 20, 13, CYAN, 2)
        panels.append(im); labels.append(f'shows {n}: {n} of the six')
        peek(e, False)
    save(montage(panels, 4, labels, k=2), 'peek_digits.png')

@shot('peek_digits_edge.png')
def peek_digits_edge():
    """A peeled corner cell has only two neighbours, so its number can never pass 2; an edge cell has three or four."""
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD, back={(1, 0): 1, (0, 1): 2, (5, 7): 3, (6, 6): 4})
    e.set_cell(0, 0, HOLE, 0); e.set_cell(6, 7, HOLE, 0)
    peek(e); e.run(20)
    save(zoom(touch(grab(e)), 2), 'peek_digits_edge.png')

@shot('scorch_risk.png')
def scorch_risk():
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD, back={(1, 5): 1, (5, 6): 2, (4, 6): 3, (2, 6): 4})
    e.set_cell(2, 1, BURN, 'e'); e.set_cell(5, 3, BURN, 't'); e.set_cell(3, 3, HOLE, 0)
    e.set_burn(0, 3); e.set_heat(4); e.run(30)
    peek(e); e.run(24)
    save(zoom(touch(grab(e)), 2), 'scorch_risk.png')
    peek(e, False)

@shot('fire_midshelf.png', 'heat_bar.png')
def fire_midshelf():
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD, back={})
    e.set_cell(2, 0, BURN, 'o'); e.set_cell(5, 2, BURN, 'a')
    e.set_burn(1, 2); e.set_heat(5); e.set_turn(9); e.run(260)
    save(zoom(grab(e), 2), 'fire_midshelf.png')
    # the heat bar and the fire pips in their states
    panels, labels = [], []
    for c, r in e.burn_cells(e.state()): e.set_cell(c, r, TILE, 'e')
    states = [(0, 'heat 0 of 10'), (4, 'heat 4'), (7, 'heat 7'), (9, 'heat 9: nearly full')]
    for h, lab in states:
        e.set_heat(h); e.set_burn(0, 99); e.run(24)
        panels.append(touch(grab(e)).crop((174, 88, 254, 122))); labels.append(lab)
    e.set_cell(2, 0, BURN, 'o'); e.set_heat(3); e.set_burn(0, 3); e.run(30)
    panels.append(touch(grab(e)).crop((174, 88, 254, 122))); labels.append('a flame, fire every 3 turns')
    e.set_burn(2, 3); e.run(8)
    best = None
    for _ in range(12):       # NEXT blinks: wait for the lit half of the blink
        im = touch(grab(e)).crop((174, 88, 254, 122)); e.run(3)
        if sum(im.crop((0, 18, 28, 28)).convert('L').getdata()) > 2200: best = im; break
    panels.append(best or im); labels.append('one turn left: NEXT')
    e.set_cell(2, 0, TILE, 'o'); e.set_burn(0, 99); e.set_turn(30)
    w = hit_path(e, 'tone', 3, 1); e.chain(w); e.press('A', settle=30)      # a turn past the limit: the label changes
    e.set_heat(2); e.set_burn(0, 99); e.run(20)
    assert e.state()['overtime'] == 1
    panels.append(touch(grab(e)).crop((174, 88, 254, 122))); labels.append('past turn 24: OVERTIME')
    save(montage(panels, 4, labels, k=3), 'heat_bar.png')

@shot('fire_mechanics.png')
def fire_mechanics():
    """Four steps of the same board: a flame creeps down through gems, falls through a hole, and stops on a diamond."""
    e = boot(); to_play(e); calm(e)
    rows = ['ejkrtsl', 'ranodtn', 'dlsaeiu', 'gitenrh', 'moblcas', 'ptiduer', 'cnaslho', 'hrgotmi']
    craft(e, rows, back={})
    e.set_cell(1, 0, BURN, 'j'); e.set_cell(3, 0, BURN, 'r'); e.set_cell(5, 0, BURN, 's')
    e.set_cell(3, 3, HOLE, 0); e.set_cell(3, 4, HOLE, 0)
    e.set_cell(5, 3, SECRET, 'k')
    e.set_burn(0, 99); e.run(30)
    panels, labels = [], []
    labs = ['0: three flames on the top row', '1: one fire step', '2: the middle flame meets the holes', '3: the right flame meets a diamond']
    for i in range(4):
        e.run(14)
        panels.append(board_crop(grab(e), 1))
        labels.append(labs[i])
        e.fire_step(); e.run(20)
    save(montage(panels, 2, labels, k=2), 'fire_mechanics.png')

def pass_by_peeling(e, keep_turn=False):
    """Peel the pass in order with the fire held off (a perfect pass), stopping on the results card.
    keep_turn leaves the turn counter alone (the speed bonus then pays for the turns really taken)."""
    n = e.state()['passLen']
    for i in range(1, n + 1):
        e.set_heat(0); e.set_burn(0, 99)
        if not keep_turn: e.set_turn(0)
        g = e.state()
        for c, r in e.burn_cells(g): e.set_cell(c, r, TILE, 'e')
        c, r = e.secret_cells(g)[i]
        e.peel(c, r)
    g = e.state(); assert g['state'] == ST_PASSED, f'pass_by_peeling: {g["state"]} fail {g["fail"]}'
    return g

def force_fail_words(e):
    e.set_features(shuffles=0)          # the shelf's one re-deal is spent, so a dead board ends it
    for c in range(COLS):
        for r in range(ROWS): e.set_cell(c, r, TILE, 'x')
    g = e.state()
    plain = [(c, r) for c in range(COLS) for r in range(ROWS) if not g['back'][c][r]]
    e.peel(*plain[len(plain) // 2])
    g = e.state(); assert g['state'] == ST_FAIL and g['fail'] == FAIL_WORDS, (g['state'], g['fail'])

@shot('results_card.png', 'fail_cards.png', 'pause_menu.png', 'ledger.png')
def cards():
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD)
    for word, c0 in (('stone', 3), ('garden', 0), ('planets', 6)):
        path = hit_path(e, word, c0, 0)
        e.chain(path); e.press('A', settle=40); e.set_heat(0); e.set_burn(0, 99)
        craft(e, LAYOUT_BOARD)
    e.set_lives(3); e.set_heat(0)
    # pause menu
    e.press('START', settle=20)
    save(zoom(touch(grab(e)), 2), 'pause_menu.png')
    e.press('START', settle=20)
    # results card, after the tally has finished (4065: the words 840 with their combo bonus, the four letters 400 and their streak bonus 600, the shelf 1000, speed 825, perfect 400)
    g = pass_by_peeling(e, keep_turn=True)
    assert g['score'] == 4065 and g['turn'] == 7 and g['speedBonus'] == 825 and g['peelBonus'] == 400, (g['score'], g['turn'], g['speedBonus'], g['peelBonus'])
    e.run(110)
    save(zoom(touch(grab(e)), 2), 'results_card.png')
    e.tap(100, 100); e.run(200)
    assert e.state()['shelf'] == 2
    # three fails
    panels = []; labels = ['LIBRARY LOST', 'PASS DESTROYED', 'OUT OF WORDS']
    e.set_lives(3); calm(e)
    craft(e, LAYOUT_BOARD, back={(3, 5): 1, (1, 6): 2, (5, 6): 3, (2, 6): 4})
    for c, r in e.burn_cells(e.state()): e.set_cell(c, r, TILE, 'e')
    e.set_cell(4, 7, BURN, 'q'); w = hit_path(e, 'tone', 0, 0); e.set_burn(0, 1); e.chain(w); e.press('A', settle=3); e.run(150)
    g = e.state(); assert g['state'] == ST_FAIL and g['fail'] == FAIL_FIRE, (g['state'], g['fail'])
    panels.append(touch(grab(e)))
    e.tap(100, 100); e.run(200)
    calm(e)
    craft(e, LAYOUT_BOARD, back={(3, 4): 1, (1, 6): 2, (5, 6): 3, (2, 6): 4})
    e.set_cell(3, 3, BURN, 'a'); e.set_burn(0, 99); e.run(10)
    e.peel(3, 4); e.run(150)
    g = e.state(); assert g['state'] == ST_FAIL and g['fail'] == FAIL_SCORCH, (g['state'], g['fail'])
    panels.append(touch(grab(e)))
    e.tap(100, 100); e.run(200)
    calm(e); force_fail_words(e); e.run(150)
    panels.append(touch(grab(e)))
    save(montage(panels, 3, labels, k=2), 'fail_cards.png')
    e.tap(100, 100); e.run(200)
    # the ledger
    g = e.state()
    if g['state'] == ST_PLAY: to_title(e)
    e.press('X', settle=30)
    assert e.state()['state'] == ST_STATS, e.state()['state']
    save(zoom(touch(grab(e)), 2), 'ledger.png')

@shot('hex_adjacency.png')
def hex_adjacency():
    """Drawn, not photographed: the board's own tile geometry (24 x 22, odd columns half a tile lower) with the
    six neighbours of an even-column cell and of an odd-column cell, labelled with their (dx, dy) offsets."""
    k = 4; TW, TH, ODD = 24, 22, 11
    cols, rows = 5, 4
    panels = []
    for centre in ((2, 1), (3, 1)):
        im = Image.new('RGB', (cols * TW * k + 8, (rows * TH + ODD) * k + 8), (10, 10, 18))
        d = ImageDraw.Draw(im)
        nbs = Emu.neighbors(*centre)
        for c in range(cols):
            for r in range(rows):
                x = 4 + c * TW * k; y = 4 + (r * TH + (ODD if c & 1 else 0)) * k
                fill, outl = (28, 30, 46), (60, 64, 90)
                if (c, r) == centre: fill, outl = (150, 110, 20), GOLD
                elif (c, r) in nbs: fill, outl = (20, 90, 100), CYAN
                d.rectangle((x + 3, y + 3, x + TW * k - 4, y + TH * k - 4), fill=fill, outline=outl, width=3)
                label = f'{c},{r}'
                d.text((x + TW * k // 2, y + TH * k // 2 - 14), label, fill=(150, 150, 170), font=_font(15, False), anchor='mm')
                if (c, r) in nbs:
                    d.text((x + TW * k // 2, y + TH * k // 2 + 14), f'{c - centre[0]:+d},{r - centre[1]:+d}', fill=WHITE, font=_font(19), anchor='mm')
                if (c, r) == centre:
                    d.text((x + TW * k // 2, y + TH * k // 2 + 14), 'this gem', fill=BLACKISH, font=_font(17), anchor='mm')
        panels.append(im)
    save(montage(panels, 1, ['an even column (0, 2, 4, 6): the neighbours sit at dy -1 and 0 in the side columns',
                             'an odd column (1, 3, 5): the neighbours sit at dy 0 and +1 in the side columns']), 'hex_adjacency.png')

# ---- peek puzzles: the solver proves every deduction the guide writes out
def solve_puzzle(holes, digits):
    """All placements of pass letters on the unpeeled cells next to the holes that give every hole its number.
    Returns (cells that must hold a letter, cells that cannot, number of placements)."""
    front = sorted({n for h in holes for n in neighbours(*h)} - set(holes))
    sols = []
    for mask in range(1 << len(front)):
        S = {front[i] for i in range(len(front)) if mask >> i & 1}
        if all(sum(1 for n in neighbours(*h) if n in S) == digits[h] for h in holes): sols.append(S)
    forced = set.intersection(*sols); anyc = set.union(*sols)
    return forced, set(front) - anyc, len(sols)

PUZZLES = {
    # name: (holes, secrets)
    'one':   ([(3, 3), (3, 2), (3, 4), (2, 3), (2, 4), (4, 3)], [(4, 4)]),
    'zero':  ([(3, 3), (4, 3), (2, 4)], [(3, 2), (4, 4)]),
    'count': ([(3, 3), (2, 4), (3, 2), (4, 4)], [(2, 3), (3, 4), (4, 3)]),
}

def puzzle_panels(e, holes, secrets, solved):
    digits = {h: sum(1 for n in neighbours(*h) if n in secrets) for h in holes}
    forced, safe, nsol = solve_puzzle(holes, digits)
    assert nsol == 1 and forced == set(secrets), (holes, secrets, forced, nsol)
    craft(e, LAYOUT_BOARD, back={s: i + 1 for i, s in enumerate(secrets)})
    for h in holes: e.set_cell(h[0], h[1], HOLE, 0)
    peek(e); e.run(20)
    im = touch(grab(e)).crop((16, 12, 152, 152))
    if solved:
        d = ImageDraw.Draw(im)
        for (c, r) in secrets:
            x, y = e.cell_rect(c, r)[:2]; y -= 192
            ring(d, x + 12 - 16, y + 11 - 12, 13, CYAN, 2)
        for (c, r) in safe:
            x, y = e.cell_rect(c, r)[:2]; y -= 192
            cx, cy = x + 12 - 16, y + 11 - 12
            d.line((cx - 7, cy - 7, cx + 7, cy + 7), fill=RED, width=2); d.line((cx - 7, cy + 7, cx + 7, cy - 7), fill=RED, width=2)
    peek(e, False)
    return im, digits

@shot('peek_puzzles.png')
def peek_puzzles():
    e = boot(); to_play(e); calm(e)
    craft(e, LAYOUT_BOARD, back={})
    panels, labels = [], []
    for name, (holes, secrets) in PUZZLES.items():
        for solved in (False, True):
            im, digits = puzzle_panels(e, holes, set(secrets), solved)
            panels.append(im); labels.append(f'puzzle {name}: ' + ('solved (ring = letter, X = none)' if solved else 'as you see it'))
    save(montage(panels, 2, labels, k=2), 'peek_puzzles.png')

# ---- worked scoring examples, held on the board so the panel prices them
def example_panel(e, rows, path, diamonds=None, burn=None, expect=None):
    craft(e, rows, diamonds=diamonds, back={})
    for (c, r), ch in (burn or {}).items(): e.set_cell(c, r, BURN, ch)
    e.run(40); hold_chain(e, path); e.run(10)
    g = e.state()
    assert g['chainScore'] == expect, f'example: panel prices {g["chainScore"]}, want {expect}'
    img = grab(e)
    release(e); e.press('Y', settle=4)
    return img

@shot('worked_examples.png')
def worked_examples():
    e = boot(); to_play(e); calm(e); msg_off(e)
    blank = ['abcdefg'] * 8
    base = ['zlnoebi', 'rtdsaum', 'ecaorlp', 'dsistne', 'nuiolag', 'tahcrse', 'ebrdint', 'gaolems']
    imgs, labels = [], []
    # 1: I N K  = (1 + 1 + 5) x 1 x 10 = 70
    rows = [list(r) for r in base]
    rows[2][3] = 'i'; rows[3][3] = 'n'; rows[4][3] = 'k'
    imgs.append(example_panel(e, [''.join(r) for r in rows], [(3, 2), (3, 3), (3, 4)], expect=70)); labels.append('INK: 7 x 1 x 10 = +70')
    # 2: Qu I Z = (10 + 1 + 10) x 1 x 10 = 210 (a Qu gem is two letters, QUIZ is four, still x1)
    rows = [list(r) for r in base]
    rows[2][3] = 'q'; rows[3][3] = 'i'; rows[4][3] = 'z'
    imgs.append(example_panel(e, [''.join(r) for r in rows], [(3, 2), (3, 3), (3, 4)], expect=210)); labels.append('QU I Z: 21 x 1 x 10 = +210')
    # 3: LANTERN through a flame and a diamond: base 7, x3, +200 long word, +210 flame, +50% diamond = 930
    rows = [list(r) for r in base]
    for i, ch in enumerate('lantern'): rows[1 + i][3] = ch
    imgs.append(example_panel(e, [''.join(r) for r in rows], [(3, 1 + i) for i in range(7)], diamonds={(3, 2): 'a'}, burn={(3, 5): 'e'}, expect=930))
    labels.append('LANTERN with a flame and a diamond: +930')
    pan = [im for im in imgs]
    save(montage(pan, 3, labels, k=1), 'worked_examples.png')

@shot('repeat_again.png')
def repeat_again():
    e = boot(); to_play(e); calm(e); msg_off(e)
    craft(e, LAYOUT_BOARD, back={})
    w = hit_path(e, 'tone', 3, 1)
    e.chain(w); e.press('A', settle=70); calm(e)
    first = e.state()['score']
    craft(e, LAYOUT_BOARD, back={})
    w = hit_path(e, 'tone', 3, 1)
    e.chain(w); e.press('A', hold=2, settle=6)
    g = e.state(); assert (g['score'] - first) * 2 == first and g['callout'] == 'AGAIN', (first, g['score'], g['callout'])
    save(zoom(grab(e), 2), 'repeat_again.png')

@shot('refusals.png')
def refusals():
    e = boot()
    open_options(e)
    for _ in range(6): e.press('DOWN', settle=3)
    e.press('RIGHT', settle=3)                       # LIBRARY: COMMON
    e.press('B', settle=30)
    to_play(e); calm(e); msg_off(e)
    panels, labels = [], []
    def attempt(word, label, spelling=None):
        if spelling is not None: e.poke(wp.POKE_SPELL, spelling)
        craft(e, LAYOUT_BOARD, back={})
        w = hit_path(e, word, 3, 0)
        e.chain(w); e.press('A', hold=2, settle=6)
        img = grab(e)
        panels.append(stack([top(img).crop((8, 148, 248, 184)), touch(img).crop((0, 70, 172, 140))]))
        labels.append(label)
        e.run(80); e.press('Y', settle=4); calm(e)
    attempt('xqz', 'a string that spells nothing: NOT IN THE LIBRARY')
    attempt('zax', 'rare word with LIBRARY COMMON: TOO OBSCURE')
    e.poke(wp.POKE_SPELL, 2); attempt('color', 'BRITISH spelling set, American word: AMERICAN SPELLING', 2)
    attempt('colour', 'AMERICAN spelling set, British word: BRITISH SPELLING', 1)
    save(montage(panels, 2, labels, k=2), 'refusals.png')

def stack(imgs, gap=4):
    w = max(i.width for i in imgs); h = sum(i.height for i in imgs) + gap * (len(imgs) - 1)
    out = Image.new('RGB', (w, h), (14, 14, 22)); y = 0
    for i in imgs: out.paste(i, (0, y)); y += i.height + gap
    return out

LONG_PATH = [(0, r) for r in range(8)] + [(1, r) for r in range(7, -1, -1)] + [(2, r) for r in range(8)]

@shot('long_word.png')
def long_word():
    e = boot(); to_play(e); calm(e); msg_off(e)
    craft(e, LAYOUT_BOARD, back={})
    word = 'internationalism'      # 16 letters
    for (c, r), ch in zip(LONG_PATH, word): e.set_cell(c, r, TILE, ch)
    e.run(30)
    path = LONG_PATH[:len(word)]
    hold_chain(e, path); e.run(10)
    g = e.state(); want = Emu.word_score(word)
    assert g['chainScore'] == want, (g['chainScore'], want)
    save(zoom(grab(e), 2), 'long_word.png')
    release(e)

@shot('left_hand.png')
def left_hand():
    e = boot(left=True)
    t = grab(e)
    to_play(e); calm(e); craft(e, LAYOUT_BOARD, back={}); msg_off(e)
    w = hit_path(e, 'stone', 3, 1)
    hold_chain(e, w); e.run(8)
    p = grab(e)
    save(montage([t, p], 2, ['title, HAND LEFT', 'play, HAND LEFT: board and panel swap sides'], k=1), 'left_hand.png')

@shot('dpad_cursor.png')
def dpad_cursor():
    e = boot(); to_play(e); calm(e); craft(e, LAYOUT_BOARD, back={}); msg_off(e)
    w = hit_path(e, 'stone', 3, 1)
    for _ in range(2): e.press('UP', settle=3)             # the cursor starts at column 3 row 3: now row 1
    panels = []
    e.run(6); panels.append(touch(grab(e)))
    for i in range(5):
        e.press('A', settle=4)
        if i < 4: e.press('DOWN', settle=4)
    e.run(6); panels.append(touch(grab(e)))
    g = e.state(); assert g['chainLen'] == 5 and g['curOn'] == 1, (g['chainLen'], g['curOn'])
    save(montage(panels, 2, ['the cursor on the first gem, before A', 'A chained five gems; A again on the tail submits'], k=2), 'dpad_cursor.png')

def pick_theme(e, steps):
    open_options(e)
    for _ in range(steps): e.press('RIGHT', settle=3)
    e.press('B', settle=30)

@shot('themes_gallery.png', 'clear_vs_jewel.png')
def themes_gallery():
    e = boot()
    panels, labels = [], []
    for i, nm in enumerate(('JEWEL', 'INK', 'CANDY', 'EMBER', 'CLEAR')):
        if i: pick_theme(e, 1)
        to_play(e); calm(e)
        craft(e, LAYOUT_BOARD, back={}, diamonds={(5, 4): 'k'})
        e.set_cell(1, 2, BURN, 'a'); e.set_cell(4, 5, HOLE, 0); e.set_burn(1, 3)
        w = hit_path(e, 'stone', 2, 1)
        e.run(60); hold_chain(e, w); e.run(10)
        panels.append(touch(grab(e))); labels.append(nm)
        release(e); e.press('Y', settle=4)
        to_title(e)
    save(montage(panels, 3, labels, k=1), 'themes_gallery.png')
    save(montage([panels[0].crop((0, 0, 172, 192)), panels[4].crop((0, 0, 172, 192))], 2, ['JEWEL', 'CLEAR: yellow, blue, magenta, teal, grey'], k=2), 'clear_vs_jewel.png')

@shot('overtime.png')
def overtime():
    e = boot(); to_play(e); calm(e); msg_off(e)
    craft(e, LAYOUT_BOARD, back={})
    e.set_turn(30); w = hit_path(e, 'tone', 3, 1)
    e.chain(w); e.press('A', settle=40)
    craft(e, LAYOUT_BOARD, back={}); e.set_heat(2); e.set_burn(0, 99)
    w = hit_path(e, 'stone', 3, 1); hold_chain(e, w); e.run(10)
    g = e.state(); assert g['overtime'] == 1
    save(zoom(grab(e), 2), 'overtime.png')
    release(e)

def advance_shelves(e, n):
    """Clear n shelves with perfect passes (the fire held off), tapping through each results card."""
    for _ in range(n):
        pass_by_peeling(e)
        e.run(45); e.tap(100, 100); e.run(220)
        assert e.state()['state'] == ST_PLAY

@shot('lockdown_shelf.png')
def lockdown_shelf():
    e = boot(); to_play(e); calm(e)
    advance_shelves(e, 5)
    g = e.state(); assert g['shelf'] == 6 and g['lockdown'] == 1, (g['shelf'], g['lockdown'])
    e.run(260)
    e.set_heat(2); e.set_turn(4)
    craft(e, LAYOUT_BOARD, back=None)
    e.set_cell(5, 0, BURN, 'm'); e.set_burn(1, 1)
    e.run(40)
    save(zoom(grab(e), 2), 'lockdown_shelf.png')

@shot('twist_shelves.png')
def twist_shelves():
    e = boot(); to_play(e); calm(e)
    advance_shelves(e, 3)
    g = e.state(); assert g['shelf'] == 4
    panels, labels = [], []
    want = {wp.TWIST_INK: 'INK SPILL: three cells are already holes', wp.TWIST_DAMP: 'DAMP: slower fire, a shorter meter', wp.TWIST_BURIED: 'BURIED: a longer pass, buried deep'}
    for tw in (wp.TWIST_INK, wp.TWIST_DAMP, wp.TWIST_BURIED):
        for attempt in range(40):
            if e.state()['twist'] == tw: break
            e.set_lives(3)
            e.press('START', settle=10); e.press('DOWN', settle=3); e.press('A', settle=3); e.press('A', settle=120)
        g = e.state(); assert g['twist'] == tw, f'twist {tw} never came up in 40 restarts'
        e.run(30)
        img = grab(e)
        panels.append(stack([top(img).crop((8, 8, 248, 184)), touch(img).crop((0, 0, 172, 192))])); labels.append(want[tw])
        e.set_lives(3)
        e.press('START', settle=10); e.press('DOWN', settle=3); e.press('A', settle=3); e.press('A', settle=120)   # re-roll for the next one
    save(montage(panels, 3, labels, k=1), 'twist_shelves.png')

@shot('guided_first_shelf.png')
def guided_first_shelf():
    e = boot(tutor_done=False)
    e.start_classic(); e.run(60)
    panels, labels = [], []
    panels.append(grab(e)); labels.append('first prompt: drag a word')
    for _ in range(300):             # three idle seconds: a ghost stylus traces a real word
        e.run(2)
        if e.state()['cue'] == wp.CUE_GHOST: break
    assert e.state()['cue'] == wp.CUE_GHOST, 'the ghost drag never appeared'
    e.run(40)
    panels.append(grab(e)); labels.append('after three idle seconds: the ghost drag')
    g = e.state()
    ws = e.find_words(g, maxlen=5)
    e.chain(ws[0][1]); e.press('A', settle=200)
    e.press('Y', settle=4)
    panels.append(grab(e)); labels.append('after the first word: peek')
    save(montage(panels, 3, labels, k=1), 'guided_first_shelf.png')

class FirstBootEmu(Emu):
    """Emu without the constructor's habit of dismissing the first-boot language chooser."""
    def __init__(self, rom):
        rom = os.path.abspath(rom)
        self.emu = wp.DeSmuME(); self.emu.open(rom)
        self.emu.input.keypad_update(0); self.emu.input.touch_release(); self.emu.volume_set(0)
        self.mem = self.emu.memory
        self.dbg = wp.symbol(rom.replace('.nds', '.elf'), 'DBG')
        self.bx = 0; self.words = None
        self.run(60)

@shot('language_chooser.png')
def language_chooser():
    e = FirstBootEmu(CFG['rom'])
    assert e.state()['state'] == wp.ST_LANGPICK, 'the first boot should show the chooser'
    e.press('DOWN', settle=3); e.press('DOWN', settle=3); e.run(10)
    save(annotate(zoom(touch(grab(e)), 1).crop((0, 0, 172, 192)), 3, [('m', 1, 86, 28, 'L'), ('m', 2, 86, 60, 'R'), ('m', 3, 86, 172, 'L')], margin=40), 'language_chooser.png')

@shot('title_language.png')
def title_language():
    e = boot()
    panels, labels = [], []
    for idx, nm in ((0, 'English: a quiet tag'), (1, 'Espanol: the tag lights gold'), (3, 'Deutsch')):
        e.set_language(idx); e.run(60)
        panels.append(touch(grab(e)).crop((0, 0, 172, 192))); labels.append(nm)
    e.set_language(0)
    save(montage(panels, 3, labels, k=2), 'title_language.png')

@shot('language_shelves.png', 'language_options.png')
def language_shelves():
    e = boot()
    panels, labels = [], []
    for idx, nm in ((1, 'Espanol'), (3, 'Deutsch'), (10, 'Suomi')):
        e.set_language(idx); e.run(30)
        to_play(e); calm(e); e.run(280)
        img = grab(e)
        panels.append(stack([top(img).crop((8, 8, 248, 184)), touch(img).crop((0, 0, 172, 192))])); labels.append(nm)
        to_title(e)
    e.set_language(0)
    save(montage(panels, 3, labels, k=1), 'language_shelves.png')
    # the options with another language on: SPELLING is a note, not a choice
    e.set_language(2); e.run(20); open_options(e)
    for _ in range(7): e.press('DOWN', settle=3)
    e.run(20)
    img = grab(e)
    save(zoom(img, 2), 'language_options.png')

def run_one(name):
    files, fn = SHOTS[name]
    t = time.time()
    fn()
    for f in files:
        assert os.path.exists(os.path.join(OUT, f)), f'{name} did not write {f}'
    return time.time() - t

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', nargs='?', default=CFG['rom'])
    ap.add_argument('--only', default='', help='run the shots whose function name contains one of these words (| separated)')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--child', default='', help=argparse.SUPPRESS)
    a = ap.parse_args()
    ROM = CFG['rom'] = os.path.abspath(a.rom)
    if a.child:
        run_one(a.child); return 0
    names = [n for n in SHOTS if not a.only or any(k in n for k in a.only.split('|'))]
    if a.list:
        for n in names: print(f'{n}: {" ".join(SHOTS[n][0])}')
        return 0
    if not os.path.exists(ROM): raise SystemExit(f'no ROM at {ROM}: build it first (see TESTING.md)')
    t0 = time.time(); bad = []
    def job(n):
        r = subprocess.run([sys.executable, os.path.abspath(__file__), ROM, '--child', n], capture_output=True, text=True)
        return n, r
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        for n, r in ex.map(job, names):
            ok = r.returncode == 0
            print(('ok    ' if ok else 'FAIL  ') + n + '  ' + ' '.join(SHOTS[n][0]), flush=True)
            if not ok:
                bad.append(n); print(r.stdout[-1500:] + r.stderr[-2500:])
    print(f'{len(names) - len(bad)}/{len(names)} shots in {time.time() - t0:.0f}s')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
