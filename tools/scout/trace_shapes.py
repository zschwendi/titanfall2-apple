#!/usr/bin/env python3
"""trace_shapes.py: first look at the log.  One pass; prints, for each process id, its first and
last line time and line counts per channel, and for each (channel, function) its count with a
heavily redacted example of the message's SHAPE (every quoted string and every number replaced),
so nothing from the log's values is printed.

usage: trace_shapes.py LOG OUTJSON [LIMIT_BYTES [PID_HEX]]
"""
import json
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, __file__.rsplit('/', 1)[0] if '/' in __file__ else '.')
import trace_lib as T


def shape(msg):
    s = msg.decode('utf-8', 'replace')
    s = re.sub(r'L?"(?:[^"\\]|\\.)*"', '"<s>"', s)
    s = re.sub(r'0x[0-9a-fA-F]+', '<h>', s)
    s = re.sub(r'\b\d+\b', '<n>', s)
    return s[:110]


def main():
    log, out = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else None
    only = int(sys.argv[4], 16) if len(sys.argv) > 4 else None
    first = {}
    last = {}
    chan = defaultdict(Counter)
    funcs = Counter()
    ex = {}
    cont = 0
    lines = 0
    t0 = None
    tlast = None
    for n, ts, pid, tid, cls, ch, fn, msg in T.records(log, limit):
        lines = n
        if ts is None:
            cont += 1
            continue
        if t0 is None:
            t0 = ts
        if only is not None and pid != only:
            continue
        tlast = ts
        first.setdefault(pid, ts)
        last[pid] = ts
        chan[pid][ch] += 1
        k = (cls, ch, fn)
        funcs[k] += 1
        if k not in ex:
            ex[k] = shape(msg)
    print('lines', lines, 'continuations', cont, 'span_s', round((tlast or 0) - (t0 or 0), 1))
    print('== processes (pid first last lines, top channels)')
    for pid in sorted(first, key=lambda p: first[p]):
        top = ', '.join('%s=%d' % kv for kv in chan[pid].most_common(6))
        print('%04x %9.3f %9.3f %9d  %s' % (pid, first[pid] - t0, last[pid] - t0, sum(chan[pid].values()), top))
    print('== functions by count (class:channel:function count shape)')
    for k, c in funcs.most_common(400 if only is not None else 150):
        print('%8d %s:%s:%s  %s' % (c, k[0], k[1], k[2], ex[k]))
    json.dump({'t0': t0, 'lines': lines, 'funcs': {'%s:%s:%s' % k: [c, ex[k]] for k, c in funcs.items()}},
              open(out, 'w'))


main()
