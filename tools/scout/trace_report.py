#!/usr/bin/env python3
"""trace_report.py: print one section of the sanitised game.json made by trace_game.py.

usage: trace_report.py GAMEJSON SECTION [ARGS...]
sections: summary modules unloads exceptions threads debug env procs other opens ntopens notfound
          probes enums reads writes ioctl regkeys regvals regwrites timeline
"""
import bisect
import json
import re
import sys
from collections import Counter, defaultdict

G = json.load(open(sys.argv[1]))
SEC = sys.argv[2]
ARGS = sys.argv[3:]


def short(p):
    return p


def ext(p):
    m = re.search(r'\.([A-Za-z0-9_]+)$', p)
    return m.group(1).lower() if m else '(none)'


def module_for(addr):
    mods = sorted(G['modules'], key=lambda m: m['base'])
    bases = [m['base'] for m in mods]
    i = bisect.bisect_right(bases, addr) - 1
    if i < 0:
        return None
    m = mods[i]
    off = addr - m['base']
    return m['path'].rsplit('\\', 1)[-1], off


def group_of(p):
    """File group for the tables: install-relative directory (two levels) and extension."""
    q = p.replace('/', '\\')
    low = q.lower()
    if low.startswith('<install>\\'):
        parts = q.split('\\')[1:]
        d = '\\'.join(parts[:-1][:2]) if len(parts) > 1 else '(root)'
        return '<install>\\' + d + '  .' + ext(p)
    if '\\dxvk\\' in low:
        return 'user-profile dxvk cache  .' + ext(p)
    if low.startswith('c:\\windows\\'):
        return 'C:\\windows  .' + ext(p)
    m = re.match(r'(?i)^c:\\users\\steamuser\\([^\\]+\\[^\\]+)', q)
    if m:
        return 'C:\\users\\steamuser\\' + m.group(1) + '\\...  .' + ext(p)
    return (q[:40]) + '  .' + ext(p)


def main():
    if SEC == 'groups':
        which = ARGS[0]
        agg = defaultdict(lambda: [0, 0, 0, 0])
        if which == 'reads':
            for r in G['reads']:
                a = agg[group_of(r['name'])]
                a[0] += 1; a[1] += r['n']; a[2] += r['ret'] + r['pending_req']
        elif which == 'opens':
            for o in G['opens']:
                agg[group_of(o['path'])][0] += 1
        elif which == 'ntopens':
            seen = set()
            for o in G['nt_opens']:
                agg[group_of(o['path'])][0] += 1
        elif which == 'notfound':
            for o in G['notfound']:
                agg[group_of(o[2])][0] += 1
        elif which == 'writes':
            for w in G['writes']:
                a = agg[group_of(w['name'])]
                a[0] += 1; a[1] += w['n']; a[2] += w['bytes']
        for k, v in sorted(agg.items(), key=lambda kv: -kv[1][2] if which in ('reads', 'writes') else -kv[1][0]):
            print('%6d files/calls  %9d reads %14d bytes  %s' % (v[0], v[1], v[2], k))
        return
    if SEC == 'summary':
        m = G['meta']
        print('process', m['pid'], 'first %.3f last %.3f' % (m['first'], m['last']))
        print('threads seen', len(G['tids']), 'modules loaded', len(G['modules']), 'unloaded', len(G['unloads']))
        print('CreateFileW calls', len(G['opens']), 'NtCreateFile lines', len(G['nt_opens']), 'not-found', len(G['notfound']))
        print('files read', len(G['reads']), 'files written', len(G['writes']))
        print('read api', G['read_api'], 'read status', G['read_status'])
        print('unmapped', G['unmapped_reads'])
        print('seek classes', G['seek_classes'])
        print('read size log2 histogram', G['read_sizes_log2'])
        tot = sum(r['ret'] for r in G['reads'])
        totreq = sum(r['req'] for r in G['reads'])
        print('bytes returned %d requested %d; pending reqs %d' % (tot, totreq, sum(r['pending'] for r in G['reads'])))
    elif SEC == 'modules':
        for m in G['modules']:
            print('%8.3f %s %-6s %016x %s' % (m['t'], '%04x' % m['tid'], m['kind'], m['base'], m['path']))
    elif SEC == 'unloads':
        for m in G['unloads']:
            print('%8.3f %04x %-8s %s' % (m['t'], m['tid'], m['kind'], m['path']))
    elif SEC == 'exceptions':
        for e in G['exceptions']:
            mf = module_for(e['rip'] or e['addr'])
            print(e['t'], '%04x' % e['tid'], e['code'], e['name'], 'flags', e['flags'], 'addr', hex(e['addr']), 'rip', hex(e['rip'] or 0),
                  'in', mf and '%s+0x%x' % mf, 'nparams', len(e['info']), 'info0', e['info'][:1])
        print('seh other', G['seh_other'])
    elif SEC == 'threads':
        for k, (a, b, n) in sorted(G['tids'].items(), key=lambda kv: kv[1][0]):
            print(k, '%8.3f %8.3f %8d' % (a, b, n))
        for t in G['threadnames']:
            print(t)
    elif SEC == 'debug':
        for d in G['debugstr']:
            print(d['t'], '%04x' % d['tid'], d['via'], repr(d['text']))
    elif SEC == 'env':
        print('GET', G['env_get'])
        print('SET', G['env_set'])
        print('EXPAND', G['expand'])
    elif SEC == 'procs':
        for p in G['procs']:
            print(p)
    elif SEC == 'other':
        for k, v, ex in G['other'][:int(ARGS[0]) if ARGS else 80]:
            print(v, ':'.join(k), ex or '')
    elif SEC == 'timeline':
        bw = float(ARGS[0]) if ARGS else 5.0
        bins = defaultdict(Counter)
        for r in G['reads']:
            g = group_of(r['name'])
            if g.startswith('user-profile dxvk'):
                g = 'dxvk-cache'
            elif g.startswith('<install>'):
                g = g.split('  ')[1]
            else:
                g = 'other'
            for sec, (n, b) in r['bins'].items():
                bins[int(int(sec) // bw)][g] += b
        for k in sorted(bins):
            print('%6.1f' % (k * bw), ' '.join('%s=%.1fM' % (g, v / 1e6) for g, v in bins[k].most_common(8)))
    elif SEC == 'opens':
        for o in G['opens']:
            print(o['t'], '%04x' % o['tid'], o['creation'], o['access'], o['attrs'], o['path'])
    elif SEC == 'ntopens':
        for o in G['nt_opens']:
            print(o['t'], '%04x' % o['tid'], o['disp'], o['options'], o['access'], o['path'])
    elif SEC == 'notfound':
        for o in G['notfound']:
            print(*o)
    elif SEC == 'probes':
        for o in G['probes']:
            print(*o)
    elif SEC == 'enums':
        for o in G['enum_calls']:
            print(*o)
        print('dir entries', G['dir_entries'])
    elif SEC == 'reads':
        rs = sorted(G['reads'], key=lambda r: -r['ret'] - r['pending_req'])
        for r in rs[:int(ARGS[0]) if ARGS else 60]:
            print('%10d ret %10d req %7d reads %7.2f..%7.2f pend %5d  %s' % (r['ret'] + r['pending_req'], r['ret'], r['n'], r['first'], r['last'], r['pending'], r['name']))
    elif SEC == 'writes':
        for w in sorted(G['writes'], key=lambda w: -w['bytes']):
            print(w['n'], w['bytes'], '%.2f..%.2f' % (w['first'], w['last']), w['name'])
    elif SEC == 'ioctl':
        for x in G['ioctl']:
            print(x)
    elif SEC == 'regkeys':
        for k, v, t in G['keyopens']:
            print('%8.3f ok %d fail %d create %d  %s' % (t or 0, v[0], v[1], v[2], k))
    elif SEC == 'regvals':
        for k, vn, c, t in G['value_queries']:
            print('%8.3f %5d %s :: %s' % (t, c, k, vn))
    elif SEC == 'regwrites':
        for k, v in G['reg_writes']:
            print(v, k)
        print(G['reg_other_apis'])
    else:
        sys.exit('unknown section')


main()
