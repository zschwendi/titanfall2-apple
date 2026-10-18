#!/usr/bin/env python3
"""What our own run does between two Presents, frame by frame, from its log.

    GATE3_PRESENTS=300 build/gate3/gate3 run > /dev/null 2>&1     (writes build/gate3/run.log)
    tools/trace/frames_run.py [build/gate3/run.log] [--show 0,1,2,100,299] [--tid 1004]
                              [--ref-census docs/trace/d3d-frame-menu.md]

A frame is numbered as finding 40 numbers the recording's: frame N is every call after the
N-th `IDXGISwapChain::Present` up to and including the next one, so frame 0 is the start-up
to the first Present.  What is counted:

  device calls    every `trace: <tid>:Call` line whose function has "::" (the interface
                  layer's methods), on any thread, by method name;
  Windows calls   every other `Call` line of thread --tid (the main thread, 1004), by function;
                  the message pump's (PeekMessage, GetMessage, TranslateMessage, DispatchMessage)
                  with their arguments; the waits (Sleep*, Wait*, MsgWait*) with theirs;
  time, memory    the driver's line of each present (gate3/run.c, GATE3_PRESENTS).

It prints one line per frame of --show, the runs of frames whose device tables are the same
(so one sees whether the frame settles and what still changes), the tables of the frames of
--show, and with --ref-census the recorded frame's census beside the last frame shown.
Reads nothing but the log; prints method and function names and counts.
"""
import argparse
import collections
import re
import sys

CALL = re.compile(r'^trace: (\w+):Call (\S+?)\((.*)\) ret=(\S+)\s*$')
PRESENT = re.compile(r'gate3 run: present (\d+): ([\d.]+) ms after .*?, ([\d.]+) ms into the run, (\d+) bytes in use')
PUMP = ('PeekMessage', 'GetMessage', 'TranslateMessage', 'DispatchMessage')
WAIT = ('Sleep', 'WaitFor', 'MsgWaitFor', 'SleepConditionVariable', 'SleepEx')
CENSUS = re.compile(r'^\s+(\d+)\s+(\w+)\s*$')


class Frame:
    def __init__(self):
        self.dev = collections.Counter()
        self.win = collections.Counter()
        self.pump = collections.Counter()
        self.wait = collections.Counter()
        self.ms = None
        self.mem = None


def read(path, tid):
    frames, cur, by_thread = [], Frame(), collections.Counter()
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('trace: '):
                m = CALL.match(line)
                if not m:
                    continue
                t, fn, args, ret = m.groups()
                name = fn.split('.', 1)[-1]
                if '::' in name:
                    cur.dev[name] += 1
                    if name == 'IDXGISwapChain::Present':
                        by_thread[t] += 1
                        frames.append(cur)
                        cur = Frame()
                elif t == tid:
                    cur.win[name] += 1
                    if name.startswith(PUMP):
                        cur.pump['%s(%s) from %s' % (name, args, ret)] += 1
                    elif name.startswith(WAIT):
                        cur.wait['%s(%s) from %s' % (name, args, ret)] += 1
            elif 'gate3 run: present ' in line:
                m = PRESENT.search(line)
                if m:
                    n = int(m.group(1))
                    # the line is written inside present n, before its Call line: it closes frame n-1
                    while len(frames) < n - 1:
                        frames.append(Frame())
                    target = cur if len(frames) == n - 1 else frames[n - 1]
                    target.ms, target.mem = float(m.group(2)), int(m.group(4))
    return frames, cur, by_thread


def table(c, limit=None):
    rows = sorted(c.items(), key=lambda kv: (-kv[1], kv[0]))
    more = 0
    if limit and len(rows) > limit:
        more, rows = len(rows) - limit, rows[:limit]
    out = ['    %7d  %s' % (n, k) for k, n in rows]
    if more:
        out.append('             ... %d more' % more)
    return out


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('log', nargs='?', default='build/gate3/run.log')
    p.add_argument('--show', default='0,1,2')
    p.add_argument('--tid', default='1004')
    p.add_argument('--ref-census', default='')
    p.add_argument('--win-rows', type=int, default=25)
    a = p.parse_args(argv)
    frames, tail, by_thread = read(a.log, a.tid)
    n = len(frames)
    print('# frames_run: %s: %d presents (%s); %d device calls in whole frames, %d after the last present' % (
        a.log, n, ', '.join('%d on thread %s' % (c, t) for t, c in sorted(by_thread.items())) or 'none',
        sum(sum(f.dev.values()) for f in frames), sum(tail.dev.values())))
    if not n:
        print('# no Present in the log: nothing to report (the device calls so far: %d of %d methods)' % (
            sum(tail.dev.values()), len(tail.dev)))
        return 0
    show = [int(x) for x in a.show.split(',') if x.strip() and int(x) < n]

    print('\n== per frame: device calls, methods, draws; thread %s Windows calls; ms since the present before; MiB in use' % a.tid)
    for k in show:
        f = frames[k]
        draws = sum(c for m, c in f.dev.items() if '::Draw' in m)
        print('   frame %4d  %6d calls of %3d methods, %4d draws   %6d Windows calls   %s   %s' % (
            k, sum(f.dev.values()), len(f.dev), draws, sum(f.win.values()),
            '%9.3f ms' % f.ms if f.ms is not None else '        ? ms',
            '%8.1f MiB' % (f.mem / 1048576.0) if f.mem is not None else '       ? MiB'))

    print('\n== runs of frames with the same device table (method by method)')
    start = 0
    runs = []
    for k in range(1, n + 1):
        if k == n or frames[k].dev != frames[start].dev:
            runs.append((start, k - 1))
            start = k
    for lo, hi in runs[:40]:
        f = frames[lo]
        print('   frames %4d to %4d (%4d): %6d calls of %3d methods' % (lo, hi, hi - lo + 1, sum(f.dev.values()), len(f.dev)))
    if len(runs) > 40:
        print('   ... %d more runs' % (len(runs) - 40))
    half = frames[n // 2:]
    if len(half) > 1:
        methods = set().union(*[set(f.dev) for f in half])
        varying = sorted(m for m in methods if len(set(f.dev.get(m, 0) for f in half)) > 1)
        print('   in frames %d to %d: %d methods are called, %d of them not the same number of times in every frame%s' % (
            n // 2, n - 1, len(methods), len(varying), ': ' + ', '.join(
                '%s %d..%d' % (m.split('::')[-1], min(f.dev.get(m, 0) for f in half), max(f.dev.get(m, 0) for f in half))
                for m in varying[:30]) if varying else ''))
    timed = [f.ms for f in frames[1:] if f.ms is not None]
    if timed:
        s = sorted(timed)
        print('   wall time per frame, frames 1 to %d: median %.3f ms, mean %.3f ms, least %.3f, most %.3f' % (
            n - 1, s[len(s) // 2], sum(s) / len(s), s[0], s[-1]))

    for k in show:
        f = frames[k]
        print('\n== frame %d: device calls (%d)' % (k, sum(f.dev.values())))
        print('\n'.join(table(f.dev)))
        print('   thread %s between the two presents: %d Windows calls of %d functions' % (a.tid, sum(f.win.values()), len(f.win)))
        print('\n'.join(table(f.win, a.win_rows)))
        print('   the message pump:%s' % ('' if f.pump else ' no call'))
        if f.pump:
            print('\n'.join(table(f.pump, 12)))
        print('   waits:%s' % ('' if f.wait else ' no call'))
        if f.wait:
            print('\n'.join(table(f.wait, 12)))

    if a.ref_census and show:
        ref = collections.Counter()
        seen = False
        with open(a.ref_census) as f:
            for line in f:
                if 'census of this range' in line:
                    seen = True
                    continue
                m = CENSUS.match(line) if seen else None
                if m:
                    ref[m.group(2)] += int(m.group(1))
                elif seen and ref and line.strip():
                    break
        ours = collections.Counter()
        for m, c in frames[show[-1]].dev.items():
            ours[m.split('::')[-1]] += c
        print('\n== frame %d against the recorded census of %s (memcpy is the recorder\'s own line, not a call)' % (show[-1], a.ref_census))
        print('   %-34s %8s %8s' % ('method', 'recorded', 'ours'))
        for m in sorted(set(ref) | set(ours), key=lambda m: (-ref.get(m, 0), m)):
            if m == 'memcpy':
                continue
            mark = '' if ref.get(m, 0) == ours.get(m, 0) else '   <'
            print('   %-34s %8d %8d%s' % (m, ref.get(m, 0), ours.get(m, 0), mark))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
