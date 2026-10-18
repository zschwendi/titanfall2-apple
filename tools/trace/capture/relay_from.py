#!/usr/bin/env python3
"""Call lines of one process whose caller (by return address) is a module of a list.
usage: relay_from.py <log> <pid> <from.list> [top-N]   (reads the log from stdin if <log> is -)"""
import bisect, collections, re, sys
src = sys.stdin.buffer if sys.argv[1] == '-' else open(sys.argv[1], 'rb')
pid = sys.argv[2].encode()
want = set(l.strip().lower().encode() for l in open(sys.argv[3]) if l.strip())
top = int(sys.argv[4]) if len(sys.argv) > 4 else 70
call = re.compile(rb'^(\d+)\.\d+:([0-9a-f]+):([0-9a-f]+):Call ([^( ]+)\(')
ret = re.compile(rb' ret=([0-9a-f]+)\s*$')
loaded = re.compile(rb'^(?:\d+\.\d+:)?([0-9a-f]+):[0-9a-f]+:trace:loaddll:\S+ Loaded L"([^"]*)" at ([0-9a-fA-F]+)')
bases = []; funcs = collections.Counter(); mods = collections.Counter(); secs = collections.Counter()
other = 0; t0 = None
for line in src:
    if b':Call ' in line:
        m = call.match(line)
        if not m or m.group(2) != pid:
            continue
        r = ret.search(line)
        if not r:
            continue
        a = int(r.group(1), 16)
        k = bisect.bisect_right(bases, (a, b'\xff')) - 1
        name = bases[k][1] if k >= 0 else b'?'
        if name.lower() in want:
            funcs[m.group(4)] += 1; mods[name] += 1; secs[int(m.group(1)) // 5 * 5] += 1
        else:
            other += 1
    elif b'loaddll' in line:
        m = loaded.match(line)
        if m and m.group(1) == pid:
            bisect.insort(bases, (int(m.group(3), 16), m.group(2).split(b'\\')[-1]))
n = sum(funcs.values())
print('pid %s: %d Call lines from listed modules, %d from other modules' % (pid.decode(), n, other))
for name, c in mods.most_common():
    print('  %8d  %s' % (c, name.decode('latin-1')))
print('per function, top %d of %d:' % (top, len(funcs)))
for fn, c in funcs.most_common(top):
    print('  %8d  %5.1f%%  %s' % (c, 100.0 * c / max(n, 1), fn.decode('latin-1')))
print('per 5 s:', ' '.join('%d:%d' % (t, secs[t]) for t in sorted(secs)))
