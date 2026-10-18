#!/usr/bin/env python3
"""trace_summary.py: boil the sanitised extracts down to one small JSON of counts and names,
the file that is committed as docs/findings/30-proton-trace.json.

usage: trace_summary.py GAMEJSON SIZESJSON PROCSJSON OUTJSON

Inputs are made by trace_game.py, trace_sizes.py and trace_procs.py (all on the machine that
holds the log).  Nothing in the output is a value from the log: paths (user directory hidden),
registry key and value names, environment variable names, switch names, module names, counts
and times only.
"""
import json
import re
import sys
from collections import Counter, defaultdict

G = json.load(open(sys.argv[1]))
S = json.load(open(sys.argv[2]))
P = json.load(open(sys.argv[3]))
OUT = sys.argv[4]


def ext(p):
    m = re.search(r'\.([A-Za-z0-9_]+)$', p)
    return m.group(1).lower() if m else ''


def kind(p):
    low = p.lower()
    e = ext(p)
    if '\\dxvk\\' in low:
        return 'DXVK shader cache (Proton, not the game)'
    if low.startswith('c:\\programdata\\ea desktop') or 'electronic arts' in low:
        return 'EA overlay (log / cache)'
    if low.startswith('/home') or low.startswith('z:\\home'):
        return 'DXVK log (Proton, not the game)'
    if '\\documents\\respawn\\' in low:
        if low.endswith('.sav'):
            return 'save data'
        return 'user configuration'
    if '\\appdata\\local\\temp\\' in low:
        return 'temp'
    if low.startswith('c:\\windows') or low.startswith('s:\\usr') or low.startswith('c:/'):
        if e in ('ttf',):
            return 'fonts'
        return 'system files'
    if low.startswith('<install>'):
        if e == 'dll' or e == 'exe' or e == 'drv':
            return 'game executables / DLLs'
        if e == 'rpak':
            return 'rpak'
        if e == 'starpak':
            return 'starpak'
        if e == 'vpk':
            return 'vpk'
        if e in ('mstr', 'mbnk', 'mprj'):
            return 'audio (Miles)'
        if e == 'bik':
            return 'video (Bink)'
        if e == 'stbsp':
            return 'streaming map (stbsp)'
        if e == 'ttf':
            return 'fonts'
        if e == 'log':
            return 'logs'
        if e in ('cfg', 'txt', 'bin', 'ico', 'manifest', 'conf', 'cnf', 'zip'):
            return 'config / text / small data'
        return 'other install'
    return 'other'


def fam(name):
    """Family of a pak name: strip the (NN) patch number."""
    n = name.rsplit('\\', 1)[-1]
    return re.sub(r'\(\d+\)', '', n)


out = {}
# ---- processes
procs = P['procs']
cnt = Counter(q['exe'].rsplit('\\', 1)[-1] for q in procs)
rows = []
for q in procs:
    img = q['exe'].rsplit('\\', 1)[-1]
    if img == 'EACefSubProcess.exe':
        continue
    rows.append({'pid': q['pid'], 'image': img, 'parent': q['parent'], 'first_s': q['first'], 'last_s': q['last'],
                 'threads_seen': q['threads'], 'log_lines': q['lines'], 'args': q['args']})
out['processes'] = {'count': len(procs), 'by_image': dict(cnt), 'rows': rows,
                    'exit': [(q['pid'], q['terminate']) for q in procs if q['pid'] in ('0a98', '0020')]}
# ---- game process timeline anchors
m = G['meta']
out['game'] = {'pid': m['pid'], 'first_line_s': round(m['first'], 3), 'last_line_s': round(m['last'], 3),
               'lines': sum(v[2] for v in G['tids'].values()), 'threads_seen': len(G['tids'])}
# ---- modules
mods = G['modules']
inst = [x for x in mods if x['path'].startswith('<install>')]
out['modules'] = {
    'total_loaded': len(mods), 'unloaded': len(G['unloads']),
    'game': [{'t': x['t'], 'name': x['path'].replace('<install>\\', '').lower(), 'kind': x['kind']} for x in inst],
    'system_and_other': [{'t': x['t'], 'name': x['path'].rsplit('\\', 1)[-1], 'kind': x['kind']} for x in mods if not x['path'].startswith('<install>')],
    'unloads': [{'t': x['t'], 'name': x['path'].rsplit('\\', 1)[-1]} for x in G['unloads']],
}
# ---- files
op = Counter()
for o in G['opens']:
    acc = 'write' if 'WRITE' in o['access'] and 'READ' not in o['access'] else ('read+write' if 'WRITE' in o['access'] else 'read/query')
    op[(kind(o['path']), acc, o['creation'])] += 1
out['file_opens_createfilew'] = [[list(k), v] for k, v in sorted(op.items(), key=lambda kv: -kv[1])]
ntk = Counter()
for o in G['nt_opens']:
    d = 'directory' if int(o['options'], 16) & 1 else 'file'
    ntk[(kind(o['path']), d)] += 1
out['file_opens_ntcreatefile'] = [[list(k), v] for k, v in sorted(ntk.items(), key=lambda kv: -kv[1])]
nf = Counter()
for o in G['notfound']:
    nf[(kind(o[2]), o[1], o[3])] += 1
out['not_found'] = [[list(k), v] for k, v in sorted(nf.items(), key=lambda kv: -kv[1])]
rd = defaultdict(lambda: [0, 0, 0, 0])
for r in G['reads']:
    a = rd[kind(r['name'])]
    a[0] += 1
    a[1] += r['n']
    a[2] += r['ret'] + r['pending_req']
out['reads_by_kind'] = {k: {'files': v[0], 'reads': v[1], 'bytes': v[2]} for k, v in rd.items()}
out['read_api'] = G['read_api']
out['read_size_log2_histogram_all'] = G['read_sizes_log2']
# reads excluding the DXVK shader cache
hist = Counter()
for r in G['reads']:
    if kind(r['name']).startswith('DXVK'):
        continue
    for b, c in r['sizes'].items():
        hist[b] += c
out['read_size_log2_histogram_no_dxvk'] = dict(sorted(hist.items(), key=lambda kv: int(kv[0])))
out['seek_calls_by_class'] = G['seek_classes']
wr = []
for w in G['writes']:
    wr.append({'kind': kind(w['name']), 'name': (w['name'] if not w['name'].startswith('<unmapped') else '<handle without a logged open>').replace('/home/~/tf2-trace/a2/', '<capture dir>/'),
               'calls': w['n'], 'bytes': w['bytes'], 'first_s': round(w['first'], 2), 'last_s': round(w['last'], 2)})
out['writes'] = sorted(wr, key=lambda x: -x['bytes'])
out['overlapped_results'] = G['overlapped_results']
out['directory_enumerations'] = {
    'entries_returned_by_directory': G['dir_entries'][:12],
    'find_first_file_calls_in_install': sum(1 for x in G['enum_calls'] if x[2] == 'FindFirstFileExW' and x[3].startswith('<install>')),
    'directories_listed_in_install': sorted({x[3] for x in G['enum_calls'] if x[2] == 'dir_cache' and x[3].startswith('<install>')})}
# windows
B = S['windows']
names = ['W%d' % i for i in range(len(B) + 1)]
out['windows'] = {'boundaries_s': B, 'files': []}
by_win = {}
for wi in range(len(B) + 1):
    lo = B[wi - 1] if wi else 0.0
    hi = B[wi] if wi < len(B) else None
    used = [f for f in S['files'] if f['per'][wi][0]]
    new = [f for f in S['files'] if lo <= f['first'] and (hi is None or f['first'] < hi)]
    agg = defaultdict(lambda: [0, 0, 0])
    for f in used:
        e = kind(f['name'])
        agg[e][0] += 1
        agg[e][1] += f['per'][wi][1]
        agg[e][2] += f['size'] or 0
    by_win[names[wi]] = {'from_s': lo, 'to_s': hi, 'files_read': len(used), 'bytes_read': sum(f['per'][wi][1] for f in used),
                         'size_of_those_files': sum(f['size'] or 0 for f in used), 'first_read_here': len(new),
                         'size_of_first_read_here': sum(f['size'] or 0 for f in new),
                         'by_kind': {k: {'files': v[0], 'bytes_read': v[1], 'file_sizes': v[2]} for k, v in agg.items()}}
out['windows']['summary'] = by_win
# files first read in the two windows that bracket the map load
for wi in (2, 3):
    lo = B[wi - 1]
    hi = B[wi]
    out['windows']['files'].append({'window': names[wi], 'from_s': lo, 'to_s': hi, 'files': [
        {'name': f['name'].replace('<install>\\', ''), 'size': f['size'], 'bytes_read': f['bytes'], 'reads': f['reads'], 'first_s': round(f['first'], 2)}
        for f in S['files'] if lo <= f['first'] < hi]})
out['windows']['all_install_files_read_total'] = {
    'files': len(S['files']), 'bytes_read': sum(f['bytes'] for f in S['files']), 'size_of_files': sum(f['size'] or 0 for f in S['files'])}
out['windows']['install_listing'] = {'files': S['install_total'][0], 'bytes': S['install_total'][1]}
out['windows']['opened_not_read'] = {'files': len(S['opened_not_read']), 'bytes': sum(x[1] for x in S['opened_not_read'])}
# biggest files read
big = sorted(S['files'], key=lambda f: -f['bytes'])[:25]
out['biggest_reads'] = [{'name': f['name'].replace('<install>\\', ''), 'size': f['size'], 'bytes_read': f['bytes'], 'reads': f['reads'],
                         'first_s': round(f['first'], 2), 'last_s': round(f['last'], 2)} for f in big]
# ---- registry
out['registry'] = {
    'keys_opened': len(G['keyopens']),
    'keys_missing': sum(1 for k in G['keyopens'] if k[1][1] and not k[1][0] and not k[1][2]),
    'value_queries': len(G['value_queries']),
    'value_query_calls': sum(v[2] for v in G['value_queries']),
    'writes': Counter(k[0][0] for k in G['reg_writes']).most_common(),
    'write_calls': {a: sum(v for k, v in G['reg_writes'] if k[0] == a) for a in ('NtSetValueKey', 'NtDeleteValueKey', 'NtDeleteKey')},
}
pat = re.compile(r'(?i)valve|origin|respawn|electronic|titanfall|directx|machineguid|\\classes\\\.|classes\\valve|classes\\applications|multimedia|cryptography\\|DEVICEMAP')
out['registry']['game_related_keys'] = [[k[0], k[1], k[2]] for k in
                                        [[x[0], x[1], x[2]] for x in G['keyopens']] if pat.search(k[0]) and 'DeviceClasses' not in k[0]][:40]
out['registry']['game_related_values'] = [[v[0], v[1], v[2]] for v in G['value_queries'] if pat.search(v[0]) and 'DeviceClasses' not in v[0] and 'DllOverrides' not in v[0]]
top = Counter()
for k in G['value_queries']:
    top['\\'.join(k[0].split('\\')[:3])] += k[2]
out['registry']['value_query_calls_by_top_key'] = top.most_common(14)
# ---- exceptions, threads, debug, env
out['exceptions'] = [{'t': e['t'], 'tid': '%04x' % e['tid'], 'code': e['code'], 'name': e['name'], 'flags': e['flags'],
                      'params': len(e['info'])} for e in G['exceptions']]
out['threads'] = {'seen': len(G['tids']), 'by_time': [], 'names': [{'t': x['t'], 'name': x['name']} for x in G['threadnames']]}
cum = sorted(v[0] for v in G['tids'].values())
for edge in (26, 28, 30, 32, 35, 40, 50, 60, 75, 100, 200, 288, 293):
    out['threads']['by_time'].append([edge, sum(1 for t in cum if t <= edge)])
out['threads']['lived_to_end'] = sum(1 for v in G['tids'].values() if v[1] >= 285)
out['debug_output'] = {'strings': len(G['debugstr']), 'via': Counter(d['via'] for d in G['debugstr']).most_common()}
out['environment_names_queried'] = [x[0] for x in G['env_get']]
out['environment_names_set'] = [x[0] for x in G['env_set']]
out['child_processes_of_game'] = [p for p in G['procs'] if p['api'] != 'NtTerminateProcess']
out['ioctl_codes'] = G['ioctl'][:10]
out['other_diagnostics'] = [[list(k), v] for k, v, _ in G['other'] if k[0] in ('fixme', 'err', 'warn')][:40]
json.dump(out, open(OUT, 'w'), indent=1, default=list)
print('summary written,', len(json.dumps(out, default=list)), 'bytes')
