#!/usr/bin/env python3
"""First look at a relay log: size, Call lines per function, per process, per calling
module, per 5 seconds.  usage: relay_hist.py <steam-1237970.log> [top-N]"""
import bisect, collections, re, sys

path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 80
call = re.compile(rb'^(\d+)\.\d+:([0-9a-f]+):([0-9a-f]+):Call ([^( ]+)\(')
ret = re.compile(rb' ret=([0-9a-f]+)\s*$')
loaded = re.compile(rb'^(?:\d+\.\d+:)?([0-9a-f]+):[0-9a-f]+:trace:loaddll:\S+ Loaded L"([^"]*)" at ([0-9a-fA-F]+)')
funcs = collections.Counter(); pids = collections.Counter(); secs = collections.Counter()
callers = collections.Counter(); first_exe = {}; bases = collections.defaultdict(list)
lines = size = calls = 0
with open(path, 'rb') as f:
    for line in f:
        lines += 1; size += len(line)
        if b':Call ' in line:
            m = call.match(line)
            if not m:
                continue
            calls += 1
            t, pid = int(m.group(1)), m.group(2)
            funcs[m.group(4)] += 1; pids[pid] += 1; secs[t // 5 * 5] += 1
            r = ret.search(line)
            if r:
                a = int(r.group(1), 16); b = bases[pid]
                k = bisect.bisect_right(b, (a, b'\xff')) - 1
                callers[(pid, b[k][1] if k >= 0 else b'?')] += 1
        elif b'loaddll' in line:
            m = loaded.match(line)
            if m:
                pid, name = m.group(1), m.group(2).split(b'\\')[-1]
                first_exe.setdefault(pid, name)
                bisect.insort(bases[pid], (int(m.group(3), 16), name))
print('%s: %d lines, %d bytes, %d Call lines' % (path, lines, size, calls))
print('\nCall lines per process (first module each loaded):')
for pid, n in pids.most_common(20):
    print('  %8d  pid %s  %s' % (n, pid.decode(), first_exe.get(pid, b'?').decode('latin-1')))
print('\nCall lines per calling module (by return address, nearest module base below it):')
for (pid, name), n in callers.most_common(40):
    print('  %8d  pid %s  %s' % (n, pid.decode(), name.decode('latin-1')))
print('\nCall lines per function, top %d of %d:' % (top, len(funcs)))
for fn, n in funcs.most_common(top):
    print('  %8d  %5.1f%%  %s' % (n, 100.0 * n / max(calls, 1), fn.decode('latin-1')))
print('\nCall lines per 5 s of Wine time:')
for t in sorted(secs):
    print('  %6d s  %8d' % (t, secs[t]))
