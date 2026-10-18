#!/usr/bin/env python3
"""A files table of the reference trace held against the lists of game data copied to this Mac.

    tools/trace/cut.py mission1 files --commit-safe --bins 6.862,21.189,29.559,46.0 > build/trace/mission1.files.tsv
    tools/trace/files_vs_lists.py build/trace/mission1.files.tsv [--from T] [--to T] [--all]

Reads the table `relay.py files` printed (one line per path, in order of first use, the bytes
asked per bin) and docs/handoff/game-data-w1.list and game-data-small.list (paths relative to
the install directory).  Nothing of game/ is opened.  For the paths first used in [--from, --to):

  - install files the original found (an open, a load or a query that succeeded), with the
    list that holds each or "NOT COPIED", the reads and the bytes asked in the window's bins;
  - install paths asked for and not found in the original either (a miss is part of the
    behaviour: the same question must get the same "no");
  - paths outside the install (the user's documents, Windows' own files), as given.

A relative path (the game sets its working directory to the install) is taken as an install
path.  The totals line counts the bytes asked of the files that are not in either list.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def read_list(name):
    out = set()
    with open(os.path.join(ROOT, 'docs', 'handoff', name)) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                out.add(line.split()[0].replace('\\', '/').lower())
    return out


def install_path(path):
    """The path relative to the install with forward slashes, lower case; None when outside it."""
    p = path.replace('/', '\\')
    low = p.lower()
    if low.startswith('<game>\\'):
        return low[7:].replace('\\', '/')
    if len(low) > 1 and (low[1] == ':' or low.startswith('\\\\')):
        return None
    if low.startswith('<redacted'):
        return None
    return low.lstrip('.\\').replace('\\', '/')


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('table')
    p.add_argument('--from', dest='lo', type=float, default=0.0)
    p.add_argument('--to', dest='hi', type=float, default=1e9)
    p.add_argument('--all', action='store_true', help='also list the files that are in a list')
    a = p.parse_args(argv)
    w1, small = read_list('game-data-w1.list'), read_list('game-data-small.list')
    found, missed, outside = [], [], []
    bins = ''
    with open(a.table) as f:
        for line in f:
            if line.startswith('# files:'):
                bins = line.strip().split('bins ')[-1]
            if line.startswith('#') or not line.strip():
                continue
            c = line.rstrip('\n').split('\t')
            if len(c) < 12:
                continue
            t = float(c[0])
            if not (a.lo <= t < a.hi):
                continue
            path, ops, reads, asked = c[1], c[2], int(c[7]), [int(x) for x in c[8].split('/')]
            rel = install_path(path)
            ok = any(not o.split(':')[0].endswith('-FAIL') for o in ops.split())
            row = (t, rel if rel is not None else path, ops, reads, asked, c[11])
            if rel is None:
                outside.append(row)
            elif ok:
                found.append(row)
            else:
                missed.append(row)
    print('# files_vs_lists: %s; paths first used in [%s, %s); bins of the bytes: %s' % (
        a.table, a.lo, a.hi if a.hi < 1e9 else 'end', bins))
    n_not = b_not = 0
    lines = []
    for t, rel, ops, reads, asked, mods in found:
        where = 'w1' if rel in w1 else 'small' if rel in small else 'NOT COPIED'
        if where == 'NOT COPIED':
            n_not += 1
            b_not += sum(asked)
        if a.all or where == 'NOT COPIED':
            lines.append('%.3f\t%s\t%s\t%s\t%d\t%s\t%s' % (t, where, rel, ops, reads, '/'.join(map(str, asked)), mods))
    print('# install files the original found: %d; in game-data-w1.list %d, in game-data-small.list %d, in neither '
          '%d (bytes asked of those: %d)' % (len(found), sum(1 for r in found if r[1] in w1),
                                           sum(1 for r in found if r[1] in small and r[1] not in w1), n_not, b_not))
    print('# first_time\tlist\tpath\toperations\treads\tbytes_asked per bin\tmodules')
    for line in lines:
        print(line)
    print('# install paths asked for and not found in the original: %d' % len(missed))
    for t, rel, ops, reads, asked, mods in missed:
        print('%.3f\tmiss\t%s\t%s\t%s' % (t, rel, ops, mods))
    print('# paths outside the install: %d' % len(outside))
    for t, path, ops, reads, asked, mods in outside:
        print('%.3f\toutside\t%s\t%s\t%d\t%s\t%s' % (t, path, ops, reads, '/'.join(map(str, asked)), mods))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
