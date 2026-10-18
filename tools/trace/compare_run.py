#!/usr/bin/env python3
"""Hold our own run's log against the reference trace, call by call, on one thread.

    tools/trace/cut.py mission1 calls --t 2.1:4.75 --width 400 > build/trace/mission1.start.calls.txt
    build/gate3/gate3 run > /dev/null 2>&1          (writes build/gate3/run.log)
    tools/trace/compare_run.py build/trace/mission1.start.calls.txt build/gate3/run.log \
        [--ref-tid 04ec] [--our-tid 1004] [--blocks 12] [--drop-site module+0xrva,...] [--commit-safe]

Both sides are reduced to the sequence of (call site as module+rva, function) of one thread, and
the two sequences are walked together.  Where they part, the walk looks up to --look calls ahead
on each side for the place where they agree again on --sync calls in a row, and prints what each
side had in between as one difference.  For the calls that pair up it also prints the first ones
whose answer differs in kind (zero or "invalid" on one side and not on the other) and the first
ones whose quoted string arguments differ once each side's game directory is taken off.

What is taken out before the walk, because one side cannot have it:
  - from our log, the functions the capture excluded (tools/trace/capture/exclude-1.list:
    GetLastError, the thread-local getters, the pointer encoders, the locks...);
  - from both, calls from a module the other side's log has no call from at all (the reference
    has the executable's own calls and our entry point is not the executable; our log has no
    calls of DLLs we do not lift);
  - from the reference, calls into a module (window procedures, DllMain): our log has none;
  - from our log, the four functions NOT_RELAYED (the game calls them thousands of times and
    the capture has none in 267 s: Wine does not relay them) and the interface methods our own
    layer logs (names with "::": Direct3D is a native DLL in the capture).
The heap functions are compared under the names the game imports (HeapAlloc, not the
ntdll.RtlAllocateHeap that Wine ran).

The reference file must come from `cut.py ... calls` WITHOUT --commit-safe and without
--collapse (it is a cache file under build/, never committed); this tool's own output is
checked by relay.py's rules when --commit-safe is given.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import relay  # noqa: E402
from census_runtime import FORWARDS  # noqa: E402

NOT_RELAYED = ('GetCurrentThreadId', 'GetCurrentProcessId', 'GetCurrentThread', 'GetCurrentProcess')
REF = re.compile(r'^\s*(\d+\.\d+) (\w+) +(>?)(\S+?)\+0x([0-9a-f]+) (\S+?)\((.*)\) = (.*)$')
OUR_CALL = re.compile(r'^trace: (\w+):Call (\S+?)\((.*)\) ret=(\S+?)\+0x([0-9a-f]+)\s*$')
OUR_RET = re.compile(r'^trace: (\w+):Ret  (\S+?)\(\) retval=(\S+) ret=')
STRINGS = re.compile(r'L?"((?:[^"\\]|\\.)*)"')


def canon(fn):
    name = fn.split('.', 1)[-1]
    name = FORWARDS.get(name, name)
    if name.startswith('Rtl') and name[3:] in ('InitializeSListHead', 'QueryDepthSList', 'InterlockedFlushSList',
                                                 'WakeConditionVariable', 'WakeAllConditionVariable',
                                                 'InitializeConditionVariable', 'InitializeSRWLock',
                                                 'TryAcquireSRWLockExclusive', 'TryAcquireSRWLockShared'):
        name = name[3:]
    return name


def module(name):
    name = name.lower()
    return name[:-4] if name.endswith('.dll') else name


def excluded():
    names = set()
    with open(os.path.join(HERE, 'capture', 'exclude-1.list')) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#') and '*' not in line:
                names.add(line.split('.')[-1])
    return names


def read_ref(path, tid):
    out = []
    with open(path) as f:
        for line in f:
            m = REF.match(line)
            if not m or m.group(2) != tid or m.group(3):
                continue
            out.append(dict(t=float(m.group(1)), mod=module(m.group(4)), rva=int(m.group(5), 16),
                            fn=canon(m.group(6)), args=m.group(7), res=m.group(8).strip()))
    return out


def read_ours(path, tid, drop):
    out, stack, dropped = [], [], 0
    with open(path, errors='replace') as f:
        for line in f:
            if not line.startswith('trace: '):
                continue
            m = OUR_CALL.match(line)
            if m:
                if m.group(1) != tid:
                    continue
                e = dict(t=None, mod=module(m.group(4)), rva=int(m.group(5), 16), fn=canon(m.group(2)),
                         args=m.group(3), res='... (not returned)', raw=m.group(2))
                stack.append(e)
                if e['fn'] in drop or e['fn'] in NOT_RELAYED or '::' in e['fn']:
                    dropped += 1
                else:
                    out.append(e)
                continue
            m = OUR_RET.match(line)
            if m and m.group(1) == tid:
                for i in range(len(stack) - 1, -1, -1):
                    if stack[i]['raw'] == m.group(2):
                        stack[i]['res'] = m.group(3)
                        del stack[i:]
                        break
    return out, dropped


def kind(res):
    r = res.lower().split()[0] if res else ''
    if r in ('void', '...'):
        return 'none'
    if r.strip('f') == '' and len(r) >= 8:
        return 'invalid'
    return 'zero' if r.strip('0') == '' else 'value'


def strings(args, root):
    out = []
    for s in STRINGS.findall(args):
        s = relay.unescape(s).lower().replace('/', '\\')
        if root and s.startswith(root):
            s = '<game>' + s[len(root):]
        out.append(s)
    return out


def find_root(seq):
    for e in seq:
        if e['fn'] == 'SetCurrentDirectoryA':
            s = STRINGS.findall(e['args'])
            if s:
                return relay.unescape(s[0]).lower().rstrip('\\')
    return None


def walk(a, b, look, sync):
    """Yield ('same', i, j) and ('diff', i0, i1, j0, j1) over two lists of keys."""
    i = j = 0
    na, nb = len(a), len(b)
    while i < na and j < nb:
        if a[i] == b[j]:
            yield ('same', i, j)
            i += 1
            j += 1
            continue
        best = None
        for d in range(1, 2 * look):
            for di in range(max(0, d - look), min(d, look) + 1):
                dj = d - di
                if i + di + sync <= na and j + dj + sync <= nb and a[i + di:i + di + sync] == b[j + dj:j + dj + sync]:
                    best = (di, dj)
                    break
            if best:
                break
        if not best:
            yield ('diff', i, na, j, nb)
            return
        yield ('diff', i, i + best[0], j, j + best[1])
        i += best[0]
        j += best[1]
    if i < na or j < nb:
        yield ('diff', i, na, j, nb)


def main(argv):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('ref')
    p.add_argument('ours')
    p.add_argument('--ref-tid', default='04ec')
    p.add_argument('--our-tid', default='1004')
    p.add_argument('--look', type=int, default=1500)
    p.add_argument('--sync', type=int, default=4)
    p.add_argument('--blocks', type=int, default=12, help='differences printed in full')
    p.add_argument('--lines', type=int, default=10, help='lines of each side printed per difference')
    p.add_argument('--pairs', type=int, default=40, help='paired calls printed in each of the two lists')
    p.add_argument('--drop-site', default='', help='comma list of module+0xrva: calls from these call sites are '
                   'taken out of both lists before the walk (a polling loop whose turns depend on the clock)')
    p.add_argument('--commit-safe', action='store_true')
    p.add_argument('--redact', default='')
    a = p.parse_args(argv)
    safe = relay.Safe(a.commit_safe, False, a.redact.split(','))
    ref = read_ref(a.ref, a.ref_tid)
    ours, dropped = read_ours(a.ours, a.our_tid, excluded())
    ref_mods = set(e['mod'] for e in ref)
    our_mods = set(e['mod'] for e in ours)
    only_ref = sorted(ref_mods - our_mods)
    only_ours = sorted(our_mods - ref_mods)
    n_ref, n_ours = len(ref), len(ours)
    ref = [e for e in ref if e['mod'] in our_mods]
    ours = [e for e in ours if e['mod'] in ref_mods]
    n_ref_mods, n_ours_mods = len(ref), len(ours)
    drop_sites = set()
    for site in filter(None, a.drop_site.split(',')):
        mod, _, rva = site.partition('+')
        drop_sites.add((module(mod), int(rva, 16)))
    if drop_sites:
        n_a, n_b = len(ref), len(ours)
        ref = [e for e in ref if (e['mod'], e['rva']) not in drop_sites]
        ours = [e for e in ours if (e['mod'], e['rva']) not in drop_sites]
        dropped_sites = (n_a - len(ref), n_b - len(ours))
    ref_root, our_root = find_root(ref), find_root(ours)

    def show(e):
        args = e['args'] if len(e['args']) <= 200 else e['args'][:200] + '...'
        when = '%9.3f ' % e['t'] if e['t'] is not None else ''
        return '%s%s+0x%x %s(%s) = %s' % (when, e['mod'], e['rva'], e['fn'], safe.text(e['fn'], args), e['res'])

    ka = [(e['mod'], e['rva'], e['fn']) for e in ref]
    kb = [(e['mod'], e['rva'], e['fn']) for e in ours]
    same = 0
    diffs = []
    res_diff = []
    str_diff = []
    for step in walk(ka, kb, a.look, a.sync):
        if step[0] == 'same':
            same += 1
            x, y = ref[step[1]], ours[step[2]]
            kx, ky = kind(x['res']), kind(y['res'])
            if kx != ky and 'none' not in (kx, ky):
                res_diff.append((x, y))
            elif strings(x['args'], ref_root) != strings(y['args'], our_root):
                str_diff.append((x, y))
        else:
            diffs.append(step[1:])
    print('# compare_run: reference %s thread %s against %s thread %s' % (a.ref, a.ref_tid, a.ours, a.our_tid))
    print('# reference: %d calls, %d after leaving out modules our log has no call from (%s)' % (
        n_ref, n_ref_mods, ', '.join(only_ref) or 'none'))
    print('# ours: %d calls after dropping %d calls of functions the capture excluded or cannot show; %d after leaving out '
          'modules the reference window has no call from (%s)' % (n_ours, dropped, n_ours_mods, ', '.join(only_ours) or 'none'))
    if drop_sites:
        print('# --drop-site %s: %d reference calls and %d of ours taken out before the walk' % (
            a.drop_site, dropped_sites[0], dropped_sites[1]))
    print('# game directory: reference %s, ours %s' % (ref_root, our_root))
    print('# %d calls pair up (same call site, same function, same order); %d differences; of the pairs, %d differ '
          'in the kind of answer and %d in a string argument' % (same, len(diffs), len(res_diff), len(str_diff)))
    last_i = last_j = 0
    for n, (i0, i1, j0, j1) in enumerate(diffs):
        if n >= a.blocks:
            print('\n... %d more differences not printed' % (len(diffs) - n))
            break
        before = ref[i0 - 1] if i0 else None
        print('\n== difference %d: after %d paired calls; reference has %d calls ours lacks, ours has %d the '
              'reference lacks' % (n + 1, sum(1 for _ in range(0)) + i0 - sum(d[1] - d[0] for d in diffs[:n]),
                                   i1 - i0, j1 - j0))
        if before is not None:
            print('   last pair before it: ' + show(before))
        for label, seq, lo, hi in (('reference only', ref, i0, i1), ('ours only', ours, j0, j1)):
            for e in seq[lo:min(hi, lo + a.lines)]:
                print('   %-14s %s' % (label, show(e)))
            if hi - lo > a.lines:
                print('   %-14s ... %d more, the last: %s' % (label, hi - lo - a.lines, show(seq[hi - 1])))
        last_i, last_j = i1, j1
    if diffs and diffs[-1][1] == len(ref) and diffs[-1][3] == len(ours):
        print('\n# the last difference runs to the end of both lists: the walk found no further agreement')
    for title, rows in (('paired calls whose answer differs in kind (zero / invalid / a value)', res_diff),
                        ('paired calls whose string arguments differ', str_diff)):
        print('\n== %s: %d, the first %d' % (title, len(rows), min(len(rows), a.pairs)))
        for x, y in rows[:a.pairs]:
            print('   reference %s' % show(x))
            print('   ours      %s' % show(y))
    print(safe.report())
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
