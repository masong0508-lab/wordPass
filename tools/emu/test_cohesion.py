#!/usr/bin/env python3
"""Word → clue → password regressions on the real retail ROM.

Usage: python3 tools/emu/test_cohesion.py wordPass.nds [output-directory]
The matching unstripped ELF supplies Game offsets; the pinned debug mirror is
not enlarged just for these tests. Fixtures use the existing debug poke channel.
"""
import argparse
from pathlib import Path
import sys
import traceback

from elf_abi import member_offsets
from wp import (Emu, COLS, ROWS, TILE, HOLE, SECRET, BURN, ST_TITLE, ST_PLAY,
                ST_FAIL, ST_GUIDE, ST_PAUSE, ST_OPTIONS, symbol)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom')
    parser.add_argument('output', nargs='?', type=Path, default=Path('/tmp/wordpass-cohesion'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    e = Emu(args.rom)
    address = symbol(e.rom_elf, 'G')
    offsets = member_offsets(e.rom_elf, 'G', ('wordClue', 'wordClueC', 'wordClueR', 'hintC', 'hintR'))
    failures = 0
    passed = 0

    def read(field, length):
        start = address + offsets[field]
        return bytes(e.mem.unsigned[start:start + length])

    def clues():
        return read('wordClue', COLS * ROWS)

    def focus():
        return read('wordClueC', 1)[0], read('wordClueR', 1)[0]

    def hint():
        length = e.state()['hintLen']
        assert 0 <= length <= 20, f'invalid hint length {length}'
        return list(zip(read('hintC', length), read('hintR', length)))

    def resources():
        g = e.state()
        return {key: g[key] for key in ('turn', 'heat', 'peels', 'score', 'hints', 'shuffles', 'found')}

    def restart(disable_tutorial=True):
        e.emu.input.keypad_update(0)
        e.emu.input.touch_release()
        e.run(5)
        state = e.state()['state']
        if state == ST_GUIDE:
            e.press('B', settle=20)
            state = e.state()['state']
        if state == ST_PAUSE:
            e.press('START', settle=20)
            state = e.state()['state']
        if state == ST_FAIL:
            e.set_lives(3)
            e.run(70)
            e.tap(e.bx + 100, 100, settle=160)
        elif state == ST_TITLE:
            e.start_classic()
        else:
            assert state == ST_PLAY, f'cannot restart state {state}'
            e.set_lives(3)
            e.press('START', settle=5)
            e.press('DOWN', settle=3)
            e.press('A', settle=3)
            e.press('A', settle=160)
        assert e.state()['state'] == ST_PLAY, 'restart did not enter play'
        if disable_tutorial:
            e.set_tutor(1)
        e.set_heat(0)
        e.set_burn(0, 99)
        e.set_turn(0)
        e.set_features(shuffles=1, hints=2)

    def fixture(word='cat', column=0, background=HOLE, keep_word=False, background_letter='x'):
        restart()
        e.set_pass(4)
        for c in range(COLS):
            for r in range(ROWS):
                e.set_cell(c, r, background, background_letter)
                e.set_back(c, r, 0)
        for r, letter in enumerate(word):
            e.set_cell(column, r, TILE, letter)
        if keep_word:
            for r, letter in enumerate('bed', 5):
                e.set_cell(6, r, TILE, letter)
        e.run(8)
        return [(column, r) for r in range(len(word))]

    def submit(path):
        e.chain(path)
        before = e.state()
        assert before['chainLen'] == len(path), f'could not follow hint {path}: length {before["chainLen"]}'
        assert before['chainScore'] > 0, 'hint does not spell an accepted word'
        e.press('A', settle=45)
        after = e.state()
        assert after['score'] - before['score'] == before['chainScore'], 'hint score differs from its preview'
        return after

    def select_from_peek():
        path = fixture(keep_word=True)
        e.press('X', settle=4)
        assert e.state()['peek'] == 1, 'X did not latch PEEK'
        before = resources()
        e.press('SELECT', settle=4)
        g = e.state()
        assert g['hints'] == before['hints'] - 1 and g['hintReq'] == 1 and not g['peek'], 'SELECT did not hand PEEK over to chaining'
        assert hint() == path, f'unexpected CAT hint {hint()}'
        e.run(32)
        assert e.state()['cueAge'] >= 32, 'hint did not advance'
        e.press('SELECT', settle=4)
        g = e.state()
        assert g['hints'] == 0 and g['hintReq'] and g['cueAge'] < 12, 'a new paid hint reused an almost-expired ghost'
        after = submit(path)
        assert after['lastWord'] == 'cat' and after['peels'] == before['peels'], 'following the word hint peeled a gem'
        for shoulder in ('L', 'R'):
            e.set_features(hints=2)
            e.hold(shoulder)
            e.press('SELECT', settle=4)
            g = e.state()
            assert g['peek'] and g['hints'] == 2 and not g['hintReq'], f'{shoulder}+SELECT spent a hint while still peeling'
            assert g['msg'].startswith('RELEASE L/R'), f'{shoulder}+SELECT did not explain how to follow a hint'
            e.hold(shoulder, False)
        e.shot(str(args.output / f'hint_peek_{e.bx}.png'))

    def focus_visible_clue_with_select():
        # A word discovers a visible number; a nearby cell has a password
        # location. Existing exposed holes can be selected too: neither
        # counts as secret information or a free peel.
        path = fixture(column=2, background=HOLE, keep_word=True)
        e.set_cell(2, 3, TILE, 'x')
        e.set_back(2, 3, 1)
        after = submit(path)
        assert after['state'] == ST_PLAY and clues()[2 * ROWS + 2] > 0, 'word did not earn a real clue'
        before = resources()
        e.hold('L')
        e.press('SELECT', settle=4)
        g = e.state()
        c, r = g['curC'], g['curR']
        visible_number = clues()[c * ROWS + r] or g['cell'][c][r][1] == HOLE
        assert g['state'] == ST_PLAY and g['peek'] and g['curOn'] and visible_number, \
            f'shoulder+SELECT did not focus a numbered cell: {(c, r, g["peek"], g["curOn"])}'
        assert resources() == before and not g['hintReq'] and g['chainLen'] == 0, \
            'clue scan spent a hint/turn, peeled, or started a chain'
        assert 'CLUE' in g['msg'], f'clue scan did not explain the focused evidence: {g["msg"]!r}'
        neighbors = list(e.neighbors(c, r))
        assert any(g['cell'][nc][nr][1] == TILE and
                   (nr == 0 or g['cell'][nc][nr-1][1] != BURN)
                   for nc, nr in neighbors), 'clue scan focused only unsafe/used neighbors'
        e.shot(str(args.output / f'clue_scan_{e.bx}.png'))
        e.press('SELECT', settle=4)
        g = e.state()
        assert g['curOn'] and resources() == before and g['peek'], \
            'cycling clues altered game state or unexpectedly left PEEK'
        e.hold('L', False)

    def diamond_hint():
        path = fixture(keep_word=True)
        e.set_pass(4, 1 << 2)          # C is the third letter in the debug password ABCD
        e.set_back(0, 0, 3)
        e.set_cell(0, 0, SECRET, 'c')
        e.press('SELECT', settle=4)
        assert hint() == path, f'hint skipped the diamond start: {hint()}'
        before = e.state()
        after = submit(path)
        assert after['lastWord'] == 'cat' and after['cell'][0][0] == (2, SECRET), 'diamond hint did not preserve its letter anchor'
        assert after['found'] == before['found'] and after['peels'] == before['peels'], 'word play altered password discovery'

    def word_cues_respect_peek():
        e.set_tutor(0)
        restart(disable_tutorial=False)
        for _ in range(240):
            if e.state()['msgTimer'] == -1:
                break
            e.run(1)
        assert e.state()['msgTimer'] == -1 and e.state()['msg'].startswith('SPELL A WORD'), 'fresh tutorial did not explain spelling'
        e.press('X', settle=8)
        assert e.state()['msgTimer'] == -1 and e.state()['msg'].startswith('HOLD '), 'latched PEEK kept instructing the player to spell'
        e.press('X', settle=8)
        assert e.state()['msgTimer'] == -1 and e.state()['msg'].startswith('SPELL A WORD'), 'leaving PEEK did not restore the word prompt'
        for via_goal in (False, True):
            e.press('X', settle=8)
            if via_goal:
                e.tap((4 if e.bx else 176) + 24, 55, settle=8)
                assert e.state()['state'] == ST_GUIDE, 'goal tap did not open the guide'
                e.press('B', settle=8)
            else:
                e.press('START', settle=8)
            assert e.state()['state'] == ST_PAUSE and not e.state()['peek'], 'pause kept peeling mode active'
            e.press('START', settle=8)
            assert e.state()['state'] == ST_PLAY and e.state()['msg'].startswith('SPELL A WORD'), 'resuming from PEEK kept the peeling prompt'
        path = fixture(keep_word=True)
        e.set_tutor(0)
        before = resources()
        e.press('X', settle=4)
        assert e.state()['peek'], 'X did not enter PEEK'
        for _ in range(200):
            e.run(1)
            assert e.state()['cue'] not in (1, 2), 'tutorial offered a word gesture while PEEK would peel it'
        assert resources() == before, 'reading PEEK spent a tutorial resource'
        e.press('X', settle=4)
        e.run(180)
        assert e.state()['cue'] == 1 and hint() == path, 'word tutorial did not resume after leaving PEEK'
        # Interrupting the automatic word demonstration must stop it safely,
        # then allow it to return after the player finishes inspecting clues.
        e.hold('R')
        assert e.state()['peek'] and e.state()['cue'] not in (1, 2), 'held R kept the automatic word cue visible'
        e.run(200)
        e.hold('R', False)
        e.run(180)
        assert e.state()['cue'] == 1 and resources() == before, 'interrupted word tutorial did not return for free'
        e.chain(path)
        assert e.state()['cue'] == 2 and e.state()['chain'] == path, 'tutorial did not point to the selected word tail'
        e.press('X', settle=4)
        assert e.state()['peek'] and e.state()['cue'] not in (1, 2), 'tail-submit cue remained visible in peeling mode'
        e.press('X', settle=4)
        after = submit(path)
        assert after['lastWord'] == 'cat' and after['peels'] == before['peels'], 'following the resumed tutorial peeled a gem'
        e.set_tutor(1)
        # A requested hint also stops if the player deliberately switches
        # back to peeling; entering PEEK never spends another hint.
        for shoulder in ('X', 'L', 'R'):
            e.set_features(hints=2)
            e.press('SELECT', settle=4)
            assert e.state()['hintReq'] and e.state()['cue'] == 1, 'requested hint did not start'
            charged = resources()
            if shoulder == 'X':
                e.press('X', settle=4)
            else:
                e.hold(shoulder)
            g = e.state()
            assert g['peek'] and not g['hintReq'] and g['cue'] not in (1, 2), f'{shoulder} left a word hint visible in PEEK'
            assert resources() == charged, f'{shoulder} spent a resource while stopping a hint'
            if shoulder == 'X':
                e.press('X', settle=4)
            else:
                e.hold(shoulder, False)
        e.shot(str(args.output / f'word_cue_modes_{e.bx}.png'))

    def submit_affordances_respect_peek():
        path = fixture(keep_word=True)
        e.chain(path)
        e.run(8)
        before = resources()
        x, y, _, _ = e.cell_rect(*path[-1])
        points = ((x, y - 2), (x + 23, y - 2), (x, y + 19), (x + 23, y + 19))

        def rims():
            pixels = e.pixels()
            return [pixels[point] for point in points]

        ready = rims()
        assert len(set(ready)) == 1 and ready[0][0] >= 230 and ready[0][1] >= 180, f'valid word lacks its gold tail ring: {ready}'
        e.press('X', settle=8)
        assert e.state()['peek'] and e.state()['chain'] == path, 'PEEK discarded the selected word'
        e.shot(str(args.output / f'selected_word_peek_{e.bx}.png'))
        assert all(color != ready[0] for color in rims()), 'PEEK still advertises a destructive tail tap as submit'
        # The submit button is inactive in PEEK. Its bright ready bevel must
        # disappear along with the tail's ready ring, then return on exit.
        panel_x = 4 if e.bx else 176
        assert e.pixels()[panel_x + 10, 192 + 122] != ready[0], 'SUBMIT still looks ready in PEEK'
        assert resources() == before, 'changing the input mode spent a resource'
        e.press('X', settle=8)
        assert rims() == ready and e.pixels()[panel_x + 10, 192 + 122] == ready[0], 'returning to chaining did not restore submit affordances'
        e.tap(*e.center(*path[-1]), settle=45)
        g = e.state()
        assert g['lastWord'] == 'cat' and g['score'] > before['score'] and g['peels'] == before['peels'], 'restored tail affordance did not submit CAT safely'

    def longer_word():
        path = fixture('rhythm')
        e.set_cell(6, 7, TILE, 'x')
        e.peel(6, 7)
        g = e.state()
        assert g['state'] == ST_PLAY and not g['fail'] and g['shuffles'] == 1, 'RHYTHM-only shelf was lost or needlessly shuffled'
        e.press('SELECT', settle=4)
        assert hint() == path and e.state()['hintLen'] == 6, f'longer-word hint missing: {hint()}'
        # Retain an unrelated word after the demonstration is submitted.
        for r, letter in enumerate('bed', 5):
            e.set_cell(6, r, TILE, letter)
        after = submit(path)
        assert after['lastWord'] == 'rhythm' and after['state'] == ST_PLAY, 'six-gem hint could not be used'

    def word_clue():
        path = fixture(column=2, background=TILE, keep_word=True)
        tail = path[-1]
        # The tail itself hides D. Its two secret neighbors include a found
        # B diamond, so a successful word must record 2 while finding nothing.
        e.set_back(*tail, 4)
        e.set_back(1, 1, 2)
        e.set_cell(1, 1, SECRET, 'b')
        e.set_back(3, 2, 1)
        e.set_pass(4, 1 << 1)
        before = e.state()
        after = submit(path)
        learned = clues()
        assert learned[tail[0] * ROWS + tail[1]] == 3 and sum(v != 0 for v in learned) == 1, f'wrong tail clue {list(learned)}'
        assert focus() == tail and after['back'] == before['back'] and after['found'] == before['found'], 'word revealed or moved password information'
        assert after['turn'] == before['turn'] + 1 and after['peels'] == before['peels'], 'earning a clue added a peel or extra turn'
        stable = resources()
        e.hold('L')
        e.run(25)
        assert e.state()['peek'] and not e.state()['armed'] and resources() == stable and clues() == learned, 'reading the latest clue spent a resource'
        # All six outlined neighbors have the same safe rim color; the tail
        # itself is excluded. This verifies the explanation is actually drawn.
        px = e.pixels()
        neighbors = list(e.neighbors(*tail))
        rims = [px[e.cell_rect(c, r)[0], e.cell_rect(c, r)[1]] for c, r in neighbors]
        assert len(neighbors) == 6 and len(set(rims)) == 1 and sum(rims[0]) > 300, f'neighbor explanation missing: {rims}'
        e.shot(str(args.output / f'word_clue_{e.bx}.png'))
        # A surveyed gem that catches fire must retain its warning after the
        # compact clue plate is painted. The always-red inner rim is distinct
        # from the animated outer flame; the exclamation flashes inside it.
        letter = e.state()['cell'][tail[0]][tail[1]][0]
        e.set_cell(*tail, BURN, letter)
        try:
            e.run(6)
            x, y, _, _ = e.cell_rect(*tail)
            px = e.pixels()
            warning_rim = [px[x + dx, y + dy] for dx, dy in ((1, 1), (22, 1), (1, 20), (22, 20))]
            e.shot(str(args.output / f'burning_word_clue_{e.bx}.png'))
            assert all(r >= 230 and 80 <= g <= 120 and b <= 30 for r, g, b in warning_rim), f'burning clue lost its red warning rim: {warning_rim}'
            blink = set()
            for _ in range(32):
                e.run(1)
                px = e.pixels()
                warning = any(r >= 230 and 180 <= g <= 210 and 20 <= b <= 45
                              for r, g, b in (px[x + dx, y + dy] for dx in range(17, 22) for dy in range(3, 10)))
                blink.add(warning)
            assert blink == {False, True}, 'compact clue plate erased the blinking scorch warning'
        finally:
            e.set_cell(*tail, TILE, letter)
        assert resources() == stable and clues() == learned, 'warning display changed the shelf'
        e.hold('L', False)
        # The goal tracker opens the password explanation without a turn.
        e.tap((4 if e.bx else 176) + 24, 55, settle=8)
        assert e.state()['state'] == ST_GUIDE and e.state()['guidePage'] == 0, 'goal tracker did not open the goal-first guide'
        e.press('B', settle=20)
        assert resources() == stable and clues() == learned and e.state()['state'] == ST_PAUSE, 'goal help changed the shelf or its pause return'
        e.press('START', settle=20)
        assert e.state()['state'] == ST_PLAY and resources() == stable, 'resuming goal help changed the shelf'
        e.peel(*tail)
        after = e.state()
        assert after['cell'][tail[0]][tail[1]][1] == SECRET and after['found'][3] and clues() == learned, 'covered clue could not reveal its own hidden letter'

    def clue_deduction():
        fixture(word='', background=HOLE, keep_word=True)
        path = [(2, 2), (2, 3), (2, 4)]
        tail = path[-1]
        for (c, r), letter in zip(path, 'cat'):
            e.set_cell(c, r, TILE, letter)
        e.set_back(*tail, 4)
        e.set_cell(2, 5, TILE, 'x')
        e.set_back(2, 5, 1)
        after_word = submit(path)
        assert clues()[2 * ROWS + 4] == 2 and not any(after_word['found']), 'word did not earn clue 1 without revealing its center'
        e.peel(2, 3)
        assert e.state()['cell'][2][3][1] == HOLE, 'preparation peel did not leave an empty neighbor'
        before = resources()
        e.hold('L')
        e.run(25)

        # Only information available to the player enters the decision: the
        # displayed clue, neighbor geometry, visible holes and known diamonds.
        # In particular, neither the back map nor a preselected secret is read.
        shown_count = clues()[tail[0] * ROWS + tail[1]] - 1
        # Check the player's numeral too: a correct stored count is not enough
        # if its badge is blank or draws the encoded count instead of 1.
        x, y, _, _ = e.cell_rect(*tail)
        pixels = e.pixels()
        badge = [sum((1 << (4 - dx)) for dx in range(5)
                     if max(pixels[x + 16 + dx, y + 12 + dy]) < 32)
                 for dy in range(7)]
        assert badge == [4, 12, 4, 4, 4, 4, 14], f'clue badge does not visibly read 1: {badge}'
        visible = e.state()['cell']
        neighbors = e.neighbors(*tail)
        known = sum(visible[c][r][1] == SECRET for c, r in neighbors)
        covered = [(c, r) for c, r in neighbors if visible[c][r][1] in (TILE, BURN)]
        empty = sum(visible[c][r][1] == HOLE for c, r in neighbors)
        assert empty == 5 and shown_count - known == len(covered) == 1, 'visible clue does not force exactly one covered neighbor'
        inferred = covered[0]
        # A visibly forced password neighbor gets the gold '+' badge.
        ix, iy, _, _ = e.cell_rect(*inferred)
        marker = [pixels[ix + dx, iy + dy] for dx in range(3, 8) for dy in range(2, 9)]
        assert any(r >= 215 and g >= 155 and 65 <= b <= 220 for r, g, b in marker), f'forced password neighbor lacks gold + badge: {marker}'
        assert resources() == before, 'inspecting the clue spent a resource'
        e.shot(str(args.output / f'clue_deduction_before_{e.bx}.png'))
        e.hold('L', False)
        e.peel(*inferred)
        after = e.state()
        assert after['cell'][inferred[0]][inferred[1]][1] == SECRET and after['found'][0], 'the visibly deduced peel did not reveal a password letter'
        assert sum(after['found']) == sum(before['found']) + 1, 'deduced reveal did not advance FOUND by exactly one'
        assert after['cell'][tail[0]][tail[1]][1] == TILE and not after['found'][3], 'neighbor deduction incorrectly uncovered the clue center'
        e.shot(str(args.output / f'clue_deduction_after_{e.bx}.png'))

    def safe_clue():
        # One displayed password neighbor is already a diamond, so a one
        # clue makes every other covered neighbor safe from the password.
        path = fixture(column=2, background=HOLE, keep_word=True)
        e.set_back(1, 1, 1)          # cell 1,1 borders the word's tail (2,2)
        e.set_cell(1, 1, SECRET, 'a')
        e.set_pass(4, 1 << 0)
        e.set_cell(2, 3, TILE, 'x')
        after = submit(path)
        tail = path[-1]
        assert clues()[tail[0] * ROWS + tail[1]] == 2 and after['found'][0], 'safe-clue setup lacks the visible one and diamond'
        stable = resources()
        e.hold('L')
        e.run(12)
        pixels = e.pixels()
        x, y, _, _ = e.cell_rect(2, 3)
        marker = [pixels[x + dx, y + dy] for dx in range(3, 8) for dy in range(2, 9)]
        assert any(b >= 165 and g >= 165 and b > r + 45 for r, g, b in marker), f'satisfied clue lacks blue - badge: {marker}'
        assert resources() == stable and e.state()['cell'][2][3][1] == TILE, 'safe deduction spent a turn or auto-peeled a gem'
        e.shot(str(args.output / f'clue_safe_{e.bx}.png'))
        e.hold('L', False)

    def tutorial_retry():
        path = fixture(column=2, background=TILE, keep_word=True)
        e.set_tutor(0)
        submit(path)
        assert e.state()['tutorStep'] == 1 and any(clues()), 'first word did not teach the earned clue'
        e.set_back(3, 5, 1)
        e.set_cell(3, 4, BURN, 'x')
        e.set_cell(3, 5, TILE, 'x')
        e.peel(3, 5)
        assert e.state()['state'] == ST_FAIL and not e.state()['tutorDone'], 'fixture did not fail the unfinished tutorial'
        restart(disable_tutorial=False)
        g = e.state()
        assert g['tutorStep'] == 0 and not g['tutorDone'] and not any(clues()) and focus() == (0, 0), 'retry kept stale clues or skipped word instruction'
        e.set_tutor(1)

    def repetitive_refill(refill):
        # A valid tail tap must finish its dead-board rescue promptly, even
        # when many geometric routes repeat the same impossible prefixes.
        path = fixture('bed', column=3, background=TILE, background_letter='e')
        bag_address = symbol(e.rom_elf, 'bag')
        position_address = symbol(e.rom_elf, 'bagPos')
        size_address = symbol(e.rom_elf, 'bagN')
        size = int.from_bytes(bytes(e.mem.unsigned[size_address:size_address + 4]), 'little')
        assert 3 <= size <= 100, f'invalid letter bag size {size}'
        bag = list(e.mem.unsigned[bag_address:bag_address + size])
        # Reorder real bag entries rather than inventing a different letter
        # distribution. The remaining bag also supplies the rescue re-deal.
        for i, letter in enumerate(refill):
            index = bag.index(ord(letter) - ord('a'), i)
            bag[i], bag[index] = bag[index], bag[i]
        for i, letter in enumerate(bag):
            e.mem.write_byte(bag_address + i, letter)
        e.mem.write_long(position_address, 0)
        e.chain(path)
        before = e.state()
        assert before['chain'] == path and before['chainScore'] == 60, 'BED fixture is not a valid selected word'
        front_address = symbol(e.rom_elf, 'vramFront')

        def front():
            return bytes(e.mem.unsigned[front_address:front_address + 4])

        completed = presented = None
        completed_front = None
        e.emu.input.touch_set_pos(*e.center(*path[-1]))
        try:
            for scan in range(1, 13):
                e.run(1)
                g = e.state()
                if completed is None and g['lastWord'] == 'bed' and g['score'] == before['score'] + 60:
                    completed, completed_front = scan, front()
                elif completed is not None and presented is None and front() != completed_front:
                    presented = scan
            e.shot(str(args.output / f'repetitive_refill_{refill}_{e.bx}.png'))
        finally:
            e.emu.input.touch_release()
            # Restore normal input even when the old ROM is still blocked.
            # All latency assertions use the state captured at scan 12.
            e.run(45)
        assert completed is not None and presented is not None, f'{refill.upper()} refill stalled: completion={completed}, presentation={presented} within 12 scans'
        assert g['state'] == ST_PLAY and not g['chainLen'] and g['turn'] == before['turn'] + 1, 'rescue altered the word turn or restarted its held stroke'
        assert before['shuffles'] == 1 and g['shuffles'] == 0 and g['peels'] == before['peels'], 'fixture did not exercise the normal dead-board rescue'
        print(f'LATENCY {refill.upper()} hand={e.bx}: submit {completed}, next presentation {presented} scans', flush=True)

    def choose_hand(left):
        if e.state()['state'] != ST_TITLE:
            restart()
            e.press('START', settle=4)
            for _ in range(3):
                e.press('DOWN', settle=2)
            e.press('A', settle=3)
            e.press('A', settle=30)
        assert e.state()['state'] == ST_TITLE
        if bool(e.state()['settings']['hand']) != left:
            e.tap(e.bx + 86, 170, settle=15)
            assert e.state()['state'] == ST_OPTIONS
            e.tap(130, 32 + 13 * 3 + 6, settle=8)
            e.tap(86, 161, settle=20)
        e.set_hand(left)
        assert bool(e.state()['settings']['hand']) == left

    scenarios = (('SELECT leaves PEEK safely', select_from_peek),
                 ('held-shoulder SELECT cycles useful visible clues freely', focus_visible_clue_with_select),
                 ('submit ring and button match chaining or PEEK mode', submit_affordances_respect_peek),
                 ('word cues stay coherent when entering and leaving PEEK', word_cues_respect_peek),
                 ('diamond starts a usable word hint', diamond_hint),
                 ('six-gem-only shelf survives and offers a hint', longer_word),
                 ('word earns a visible fixed clue and goal help is free', word_clue),
                 ('earned clue yields a reveal using only visible information', clue_deduction),
                 ('satisfied clue visibly marks safe neighbors without peeling', safe_clue),
                 ('unfinished tutorial retry clears clues and teaches words again', tutorial_retry),
                 ('EEE refill rescues the board promptly after a tail tap', lambda: repetitive_refill('eee')),
                 ('OAE refill rescues the board promptly after a tail tap', lambda: repetitive_refill('oae')))
    for left in (False, True):
        choose_hand(left)
        for name, scenario in scenarios:
            tag = f'{"left" if left else "right"}: {name}'
            try:
                scenario()
                passed += 1
                print('PASS', tag, flush=True)
            except Exception:
                failures += 1
                print('FAIL', tag, flush=True)
                traceback.print_exc()
                e.shot(str(args.output / f'failure_{failures}.png'))
    print(f'COHESION: {passed} passed, {failures} failed', flush=True)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
