#!/usr/bin/env python3
"""trace_procs.py: question 1.  The process tree of the Wine prefix.

For every process id: first and last line time (relative to the first log line), line count,
distinct thread count, main executable (from the first `loaddll` line that names a .exe),
exit codes seen on `NtTerminateProcess`.  For every `NtCreateUserProcess` line: creation time,
creating process, image path (user directory hidden), command line reduced to switch names
(values become <redacted>, bare arguments become <arg>).  The child's process id is not on that
line; it is matched by image name and the first later process whose first line follows it.

usage: trace_procs.py LOG OUTJSON [LIMIT_BYTES]
"""
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_lib as T

QS = r'L?"((?:[^"\\]|\\.)*)"'
CREATE_RE = re.compile(QS + r' image ' + QS + r' cmdline ' + QS + r' parent (\S+) machine (\d+)')
LOADED_RE = re.compile(r'Loaded ' + QS)
TERM_RE = re.compile(r'handle (\S+), exit_code (\d+), process_exiting (\d+)')


def main():
    log, out = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else None
    t0 = None
    first, last, lines = {}, {}, Counter()
    tids = defaultdict(set)
    exe = {}
    term = defaultdict(list)
    creates = []
    for n, ts, pid, tid, cls, ch, fn, msg in T.records(log, limit):
        if ts is None:
            continue
        if t0 is None:
            t0 = ts
        first.setdefault(pid, ts)
        last[pid] = ts
        lines[pid] += 1
        tids[pid].add(tid)
        if ch == 'loaddll' and fn == 'build_module' and pid not in exe:
            m = LOADED_RE.search(msg.decode('utf-8', 'replace'))
            if m and m.group(1).lower().endswith('.exe'):
                exe[pid] = T.redact_path(m.group(1))
        elif ch == 'process' and fn == 'NtTerminateProcess':
            m = TERM_RE.search(msg.decode('utf-8', 'replace'))
            if m:
                term[pid].append((round(ts - t0, 3), m.group(1), int(m.group(2)), int(m.group(3))))
        elif ch == 'process' and fn == 'NtCreateUserProcess':
            m = CREATE_RE.search(msg.decode('utf-8', 'replace'))
            if m:
                creates.append({'t': round(ts - t0, 3), 'parent': pid, 'image': T.redact_path(m.group(2)),
                                'args': T.cmdline_names(m.group(3)), 'machine': int(m.group(5))})

    # match each create to the first later process with that image and no creator yet
    claimed = set()
    for c in creates:
        base = c['image'].replace('\\', '/').rsplit('/', 1)[-1].lower()
        cand = [p for p in first if p not in claimed and first[p] - t0 >= c['t'] - 0.001
                and exe.get(p, '').replace('\\', '/').rsplit('/', 1)[-1].lower() == base]
        cand.sort(key=lambda p: first[p])
        c['child'] = None
        if cand:
            c['child'] = cand[0]
            claimed.add(cand[0])

    procs = []
    for p in sorted(first, key=lambda p: first[p]):
        creator = None
        for c in creates:
            if c['child'] == p:
                creator = c
        procs.append({'pid': '%04x' % p, 'first': round(first[p] - t0, 3), 'last': round(last[p] - t0, 3),
                      'lines': lines[p], 'threads': len(tids[p]), 'exe': exe.get(p, '?'),
                      'parent': '%04x' % creator['parent'] if creator else None,
                      'args': creator['args'] if creator else None,
                      'terminate': term.get(p, [])[-3:]})
    for q in procs:
        print('%s <- %s  %8.3f..%8.3f  lines %7d thr %3d  %s' % (
            q['pid'], q['parent'] or '----', q['first'], q['last'], q['lines'], q['threads'],
            q['exe'].rsplit('\\', 1)[-1]))
    json.dump({'t0': t0, 'procs': procs, 'creates': creates}, open(out, 'w'), indent=1)


main()
