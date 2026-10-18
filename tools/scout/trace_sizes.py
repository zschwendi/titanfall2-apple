#!/usr/bin/env python3
"""trace_sizes.py: join the files the game opened and read (game.json) with the sizes in an
`ls -laR` listing of the install directory (names and sizes only; nothing was opened).

usage: trace_sizes.py GAMEJSON LSFILE OUTJSON T1,T2,T3,...
       T1.. are window boundaries in seconds after the first log line.  Windows are
       [0,T1) [T1,T2) ... [Tn,end].  Prints, per window, the files read in it, their total size on
       disk, the bytes read, and the files whose first read is in it.
"""
import json
import re
import sys
from collections import defaultdict

G = json.load(open(sys.argv[1]))
LS = open(sys.argv[2], errors='replace').read().split('\n')
OUT = sys.argv[3]
B = [float(x) for x in sys.argv[4].split(',')]
NAMES = ['W%d' % i for i in range(len(B) + 1)]


def parse_ls(lines):
    sizes = {}
    cur = None
    root = None
    for ln in lines:
        if ln.endswith(':') and ln.startswith('/'):
            cur = ln[:-1]
            if root is None:
                root = cur
            continue
        m = re.match(r'^([-dlcbps])[rwxsStT-]{9}\.?\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\d+\s+(.*)$', ln)
        if m and cur is not None and m.group(1) == '-':
            rel = cur[len(root):].lstrip('/')
            full = (rel + '/' if rel else '') + m.group(3)
            sizes[full.replace('/', '\\').lower()] = int(m.group(2))
    return sizes


def win(t):
    for i, b in enumerate(B):
        if t < b:
            return i
    return len(B)


def main():
    sizes = parse_ls(LS)
    total = sum(sizes.values())
    print('install: %d files, %d bytes' % (len(sizes), total))
    byext = defaultdict(lambda: [0, 0])
    for k, v in sizes.items():
        e = k.rsplit('.', 1)[-1] if '.' in k.rsplit('\\', 1)[-1] else '(none)'
        byext[e][0] += 1
        byext[e][1] += v
    print('by extension:', ', '.join('%s %d files %.1f MB' % (e, a, b / 1e6) for e, (a, b) in sorted(byext.items(), key=lambda kv: -kv[1][1])[:14]))

    def rel(name):
        n = name.lower()
        return n[len('<install>\\'):] if n.startswith('<install>\\') else None

    opened = {}
    for o in G['opens'] + G['nt_opens']:
        r = rel(o['path'])
        if r is not None:
            opened.setdefault(r, o['t'])
    files = {}
    for r in G['reads']:
        k = rel(r['name'])
        if k is None:
            continue
        per = [[0, 0] for _ in NAMES]
        for sec, (n, b) in r['bins'].items():
            w = win(float(sec) + 0.5)
            per[w][0] += n
            per[w][1] += b
        files[k] = {'name': r['name'], 'size': sizes.get(k), 'reads': r['n'], 'bytes': r['ret'] + r['pending_req'],
                    'first': r['first'], 'last': r['last'], 'per': per, 'opened': opened.get(k)}
    missing = [f['name'] for f in files.values() if f['size'] is None]
    print('read files not found in the listing:', len(missing), missing[:6])
    out = {'windows': B, 'files': []}
    for wi, wn in enumerate(NAMES):
        lo = B[wi - 1] if wi else 0.0
        hi = B[wi] if wi < len(B) else 9999.0
        used = [f for f in files.values() if f['per'][wi][0]]
        new = [f for f in files.values() if lo <= f['first'] < hi]
        print('\n== %s [%.1f, %.1f): %d files read, %d bytes read, sizes of those files %d bytes; %d files first read here (sizes %d)' % (
            wn, lo, hi, len(used), sum(f['per'][wi][1] for f in used), sum(f['size'] or 0 for f in used), len(new), sum(f['size'] or 0 for f in new)))
        agg = defaultdict(lambda: [0, 0, 0])
        for f in used:
            e = f['name'].rsplit('.', 1)[-1].lower()
            agg[e][0] += 1
            agg[e][1] += f['per'][wi][1]
            agg[e][2] += f['size'] or 0
        for e, (n, b, s) in sorted(agg.items(), key=lambda kv: -kv[1][1]):
            print('   .%-8s %4d files  read %12d  file sizes %12d' % (e, n, b, s))
    # opened but never read
    nr = [(k, sizes.get(k)) for k in opened if k not in files and k in sizes]
    print('\nopened in the install directory but never read: %d files, %d bytes' % (len(nr), sum(s for _, s in nr)))
    agg = defaultdict(lambda: [0, 0])
    for k, s in nr:
        e = k.rsplit('.', 1)[-1] if '.' in k.rsplit('\\', 1)[-1] else '(none)'
        agg[e][0] += 1
        agg[e][1] += s
    for e, (n, s) in sorted(agg.items(), key=lambda kv: -kv[1][1]):
        print('   .%-8s %4d files %12d bytes' % (e, n, s))
    out['files'] = sorted(files.values(), key=lambda f: f['first'])
    out['opened_not_read'] = [[k, s] for k, s in nr]
    out['install_total'] = [len(sizes), total]
    json.dump(out, open(OUT, 'w'))


main()
