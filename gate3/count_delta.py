#!/usr/bin/env python3
"""What one frame is made of in a stretch of a `gate3 run`: the difference of two count tables.

    GATE3_COUNTS_AT=2000,2200 build/gate3/gate3 run          # the snapshots (gate3/run.c)
    gate3/count_delta.py build/gate3/run.log 2000 2200       # per Present and per engine frame
    gate3/count_delta.py build/gate3/run.log 2000 end        # `end`: the table every run ends with

Reads the run's log only: the lines "snapshot N: com: <count> <Interface::Method> ..." and the
line "gate3 run: count table at present N ...: T ms into the run, F frame(s) ...".  Prints, for
every method whose count changed, the calls between the two and the calls per Present, then
the totals, the Presents per second and the Presents per frame of the engine's main loop.
"""
import re
import sys

ROW = re.compile(r'com:\s+(\d+)\s+(\S+::\S+)')
HEAD = re.compile(r'count table at present (\d+) .*?: ([\d.]+) ms into the run, (\d+) frame')


def tables(path):
    snap, head, end, frames_end = {}, {}, {}, None
    with open(path, errors='replace') as fp:
        for line in fp:
            if line.startswith('snapshot '):
                n, rest = line[9:].split(':', 1)
                m = ROW.search(rest)
                if m:
                    snap.setdefault(n, {})[m.group(2)] = int(m.group(1))
                continue
            m = HEAD.search(line)
            if m:
                head[m.group(1)] = (float(m.group(2)), int(m.group(3)))
                continue
            if line.startswith('com:'):
                m = ROW.search(line)
                if m:
                    end[m.group(2)] = int(m.group(1))
            elif line.startswith('standins: counted:'):
                m = re.search(r'stand-in 5 answered (\d+)', line)
                frames_end = int(m.group(1)) if m else None
    return snap, head, end, frames_end


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    snap, head, end, frames_end = tables(sys.argv[1])
    a, b = sys.argv[2], sys.argv[3]
    if a not in snap or (b != 'end' and b not in snap):
        sys.exit('count_delta: the log has snapshots at: %s' % (', '.join(sorted(snap, key=int)) or 'none'))
    ta, tb = snap[a], (end if b == 'end' else snap[b])
    presents = tb.get('IDXGISwapChain::Present', 0) - ta.get('IDXGISwapChain::Present', 0)
    if presents <= 0:
        sys.exit('count_delta: no Present between the two tables')
    total = 0
    print('calls between present %s and %s (%d Presents), per method that changed:' % (a, b, presents))
    for name in sorted(set(ta) | set(tb), key=lambda k: -(tb.get(k, 0) - ta.get(k, 0))):
        d = tb.get(name, 0) - ta.get(name, 0)
        if d:
            total += d
            print('  %8d  %8.2f per Present  %s%s' % (d, d / presents, name, '' if name in ta else '   (not called before)'))
    print('total %d calls of %d methods: %.1f per Present' % (total, sum(1 for k in tb if tb[k] != ta.get(k, 0)), total / presents))
    if a in head and b in head:
        (t0, f0), (t1, f1) = head[a], head[b]
        print('%.1f s, %.1f Presents a second; %d frame(s) of the engine\'s main loop: %.2f Presents per frame, %.1f calls per frame'
              % ((t1 - t0) / 1000, presents / ((t1 - t0) / 1000), f1 - f0, presents / max(f1 - f0, 1), total / max(f1 - f0, 1)))
    elif a in head and frames_end is not None:
        f = frames_end - head[a][1]
        print('%d frame(s) of the engine\'s main loop: %.2f Presents per frame, %.1f calls per frame' % (f, presents / max(f, 1), total / max(f, 1)))


if __name__ == '__main__':
    main()
