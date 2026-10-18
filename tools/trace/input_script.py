#!/usr/bin/env python3
"""Write an input script (runtime/include/rt/input.h) from the recording, for a time range.

    tools/trace/input_script.py mission1 --t 21:30 --anchor cursor-shown --anchor-time 18.467
    tools/trace/input_script.py mission1 --t 46:60 --anchor 'present 1' --anchor-time 46.0 --out build/trace/play.script
    tools/trace/input_script.py selftest

The recording is read through tools/trace/cut.py (one `calls` cut of the window procedure's
messages and of SetCursorPos in the range; read-only, nothing written there).  THE SCRIPT IS
DERIVED FROM THE RECORDING AND HOLDS THE OWNER'S OWN INPUT: it is written under build/trace/
(git-ignored) and nowhere else, and this tool prints counts, never an event.

What becomes what (docs/trace/input.md; the recording has the messages, not the raw packets):
  WM_MOUSEMOVE          `moveto X Y` with the message's point, while the game leaves the cursor
                        alone (the menu).  Once a SetCursorPos was seen in the range (play: the
                        game puts the cursor back every frame) `move DX DY`, the point minus
                        where the cursor was last put or seen.  That is a guess at the mouse's
                        own movement: the points are after the pointer speed of the recording's
                        machine, and four packets on average were merged into one point.
  a button message      `moveto` to its point if the pointer is not there, then `button ... down|up`
  WM_KEYDOWN, WM_KEYUP  `key down|up VK SCAN [ext]` from wParam and bits 16 to 24 of lParam; a
                        repeated WM_KEYDOWN is written as it came (the runtime sets the repeat bit)
  WM_CHAR               nothing: TranslateMessage makes it (the runtime's does not yet: it stops)
  WM_INPUT, hit tests   nothing: the runtime posts them for each event
THE POINTS.  A script's `moveto` is a point of the SCREEN; a message's point is in the window's
client area.  The recording was fullscreen (client area at the screen's corner), so its points
are both.  Our windowed run's window stands at (0, 0) with its frame inside the screen, so its
client area starts at (5, 28) (the CreateWindowEx line of a run's log: 1930 x 1113 at (0, 0),
frame 5 and caption 28): pass --client-origin 5,28 to have the same client points hit.
Times are milliseconds after --anchor-time, the second of the recording at which the anchor's
event happened there (18.467: the ShowCursor(TRUE) of the menu; docs/trace/input.md section 4).
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT_DIR = os.path.join(ROOT, 'build', 'trace')

WND = re.compile(r'^\s*([\d.]+)\s+\w+\s+>engine\+0x[0-9a-f]+ \(wndproc\)\(hwnd=\w+,msg=(\w+),wp=([0-9a-f]+),lp=([0-9a-f]+)\)')
SETPOS = re.compile(r'^\s*([\d.]+)\s+\w+\s+\S+ user32\.SetCursorPos\(([0-9a-f]+),([0-9a-f]+)\)')
BUTTONS = {'WM_LBUTTONDOWN': ('left', 'down'), 'WM_LBUTTONUP': ('left', 'up'), 'WM_RBUTTONDOWN': ('right', 'down'),
           'WM_RBUTTONUP': ('right', 'up'), 'WM_MBUTTONDOWN': ('middle', 'down'), 'WM_MBUTTONUP': ('middle', 'up')}


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def convert(lines, t0, origin=(0, 0)):
    """The script's event lines for the cut's lines, and counts by kind.  `origin`: where the run's client area starts on its screen."""
    out, counts = [], {}
    cursor, recentred = None, False

    def emit(t, text, kind):
        ms = int(round((t - t0) * 1000))
        if ms < 0:
            raise SystemExit('input_script: an event at %.3f s is before --anchor-time %.3f' % (t, t0))
        out.append('%d %s' % (ms, text))
        counts[kind] = counts.get(kind, 0) + 1

    def point_to(t, x, y):
        nonlocal cursor
        if cursor == (x, y):
            return
        if recentred and cursor is not None:
            emit(t, 'move %d %d' % (x - cursor[0], y - cursor[1]), 'move')
        else:
            emit(t, 'moveto %d %d' % (x + origin[0], y + origin[1]), 'moveto')
        cursor = (x, y)

    for line in lines:
        m = SETPOS.match(line)
        if m:
            cursor, recentred = (int(m.group(2), 16) & 0xFFFFFFFF, int(m.group(3), 16) & 0xFFFFFFFF), True
            counts['SetCursorPos seen'] = counts.get('SetCursorPos seen', 0) + 1
            continue
        m = WND.match(line)
        if not m:
            continue
        t, msg, wp, lp = float(m.group(1)), m.group(2), int(m.group(3), 16), int(m.group(4), 16)
        if msg == 'WM_MOUSEMOVE':
            point_to(t, s16(lp), s16(lp >> 16))
        elif msg in BUTTONS:
            point_to(t, s16(lp), s16(lp >> 16))
            emit(t, 'button %s %s' % BUTTONS[msg], 'button')
        elif msg in ('WM_KEYDOWN', 'WM_KEYUP'):
            emit(t, 'key %s 0x%02x 0x%02x%s' % ('down' if msg == 'WM_KEYDOWN' else 'up', wp & 0xFF, lp >> 16 & 0xFF,
                                                ' ext' if lp >> 24 & 1 else ''), 'key')
        else:
            counts['not written: ' + msg] = counts.get('not written: ' + msg, 0) + 1
    return out, counts


def selftest():
    """Made-up lines in the shape cut.py prints (no value is the recording's)."""
    lines = [
        '# calls: made up',
        '   10.100 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_INPUT,wp=00000000,lp=00000803) = 00000000',
        '   10.100 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_NCHITTEST,wp=00000000,lp=00c80064) = 00000001',
        '   10.101 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_MOUSEMOVE,wp=00000000,lp=00c80064) = 00000000',
        '   10.250 05d4   >inputsystem+0x79d0 (wndproc)(hwnd=000000000002012E,msg=WM_LBUTTONDOWN,wp=00000001,lp=00c90064) = 00000000',
        '   10.250 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_LBUTTONDOWN,wp=00000001,lp=00c90064) = 00000000',
        '   10.330 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_LBUTTONUP,wp=00000000,lp=00c90064) = 00000000',
        '   11.000 04ec inputsystem+0x84db user32.SetCursorPos(000003c0,0000021c) = 00000001',
        '   11.010 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_MOUSEMOVE,wp=00000000,lp=021a03c5) = 00000000',
        '   11.020 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_KEYDOWN,wp=00000051,lp=00100001) = 00000000',
        '   11.020 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_CHAR,wp=00000071,lp=00100001) = 00000000',
        '   11.120 05d4       >engine+0x1cedf0 (wndproc)(hwnd=000000000002012E,msg=WM_KEYUP,wp=00000027,lp=c14d0001) = 00000000',
    ]
    out, counts = convert(lines, 10.0)
    want = ['101 moveto 100 200', '250 moveto 100 201', '250 button left down', '330 button left up', '1010 move 5 -2',
            '1020 key down 0x51 0x10', '1120 key up 0x27 0x4d ext']
    assert out == want, out
    assert counts['not written: WM_CHAR'] == 1 and counts['not written: WM_INPUT'] == 1 and counts['SetCursorPos seen'] == 1, counts
    assert convert(lines, 10.0, (5, 28))[0][:2] == ['101 moveto 105 228', '250 moveto 105 229']
    try:
        convert(lines, 10.2)
    except SystemExit:
        pass
    else:
        raise AssertionError('an event before the anchor time was accepted')
    try:
        out_path(os.path.join(ROOT, 'docs', 'x.script'))
    except SystemExit:
        pass
    else:
        raise AssertionError('a path outside build/trace was accepted')
    print('input_script selftest: ok')


def out_path(path):
    full = os.path.realpath(path)
    if os.path.commonpath([full, os.path.realpath(OUT_DIR)]) != os.path.realpath(OUT_DIR):
        raise SystemExit('input_script: %s is not under build/trace/: a script made from the recording stays there' % path)
    return full


def main(argv):
    if argv[:1] == ['selftest']:
        selftest()
        return 0
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('capture')
    p.add_argument('--t', required=True, help='A:B, seconds of the recording')
    p.add_argument('--anchor', required=True, help="the script's anchor line without the word: 'cursor-shown', 'present 1', 'start'")
    p.add_argument('--anchor-time', type=float, required=True, help='the second of the recording at which that anchor happened there')
    p.add_argument('--client-origin', default='0,0', help="X,Y: where the run's client area starts on its screen (5,28 for today's windowed run)")
    p.add_argument('--out', help='default build/trace/<capture>-<A>-<B>.script')
    a = p.parse_args(argv)
    lo, hi = a.t.split(':')
    path = out_path(a.out or os.path.join(OUT_DIR, '%s-%s-%s.script' % (a.capture, lo, hi)))
    cut = subprocess.run([sys.executable, os.path.join(HERE, 'cut.py'), a.capture, 'calls', '--from', 'engine,inputsystem',
                          '--fn', 'wndproc|SetCursorPos', '--t', a.t, '--width', '160'], stdout=subprocess.PIPE)
    if cut.returncode != 0:
        raise SystemExit('input_script: cut.py ended with status %d' % cut.returncode)
    origin = tuple(int(v) for v in a.client_origin.split(','))
    out, counts = convert(cut.stdout.decode('latin-1').splitlines(), a.anchor_time, origin)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write('# Written by tools/trace/input_script.py from the capture %s, %s to %s s.\n' % (a.capture, lo, hi))
        f.write('# DERIVED FROM THE RECORDING (the owner\'s own input): stays under build/trace/, never committed.\n')
        f.write('# Times: milliseconds after %.3f s of the recording.  Points: those of the recording, moved by (%d, %d).\n' % (
            (a.anchor_time,) + origin))
        f.write('anchor %s\n' % a.anchor)
        f.write('\n'.join(out) + '\n')
    print('input_script: %s: %d event(s): %s' % (os.path.relpath(path, ROOT), len(out),
                                                 ', '.join('%s %d' % kv for kv in sorted(counts.items()))))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
