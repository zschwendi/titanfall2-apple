#!/usr/bin/env python3
"""trace_game.py: the one pass over the log that follows a single process (the game) and writes a
sanitised JSON of everything the questions need.  Run once, then read the JSON with
trace_report.py.  Nothing here prints a value: paths have the user directory hidden, registry
entries are key and value NAMES only (the log carries no data), environment variables are NAMES
only, command lines are switch names only, debug strings are scrubbed and cut short.

usage: trace_game.py LOG OUTJSON PID_HEX [LIMIT_BYTES]

Handles: the log prints `CreateFileW "name" ...` and, a few lines later on the same thread,
`CreateFileW returning <handle>`; that pair is how a read on a handle is tied to a file name.
Files opened some other way (NtCreateFile from the loader or from FindFirstFile) have no handle
printed, so reads on them show up under <unmapped>.  CloseHandle is not logged, so a handle
number that is reused is simply re-bound at the next `returning`.
"""
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_lib as T

QS = r'L?"((?:[^"\\]|\\.)*)"'
INSTALL_RE = re.compile(r'^(?:[sS]:\\steamapps\\common\\[tT]itanfall2|[zZ]:\\mnt\\1tb_nvme\\SteamLibrary\\steamapps\\common\\[tT]itanfall2)(?:\\|$)')
GUID_RE = T.GUID_RE
NUM9_RE = re.compile(r'\b\d{9,}\b')


def norm_path(p):
    """Printable, comparable form of a Windows path from the log."""
    p = T.redact_path(p)                  # unescapes, hides the user directory and ids
    for pre in ('\\??\\', '\\\\?\\', '\\\\.\\'):
        if p.startswith(pre):
            p = p[len(pre):]
    p = T.HEXRUN_RE.sub('<hex>', p)
    p = GUID_RE.sub('<guid>', p)
    p = NUM9_RE.sub('<num>', p)
    if p.startswith('/'):                 # a unix path (DXVK's own log files)
        return p
    m = INSTALL_RE.match(p)
    if m:
        p = '<install>\\' + p[m.end():]
    elif not re.match(r'^[A-Za-z]:\\', p) and not p.startswith('\\'):
        p = '<install>\\' + p      # relative: the working directory is the install directory
    return p.rstrip('\\') if len(p) > 3 else p


def pkey(p):
    return p.lower()


def hx(tok):
    tok = tok.strip()
    if tok in ('(nil)', '(null)', ''):
        return 0
    return int(tok, 16)


def reg_name(raw, rootpath):
    s = T.unescape_path(raw)
    s = re.sub(r'(?i)^\\Registry\\User\\S-1-[\d-]+', 'HKCU', s)
    s = re.sub(r'(?i)^\\Registry\\Machine', 'HKLM', s)
    s = re.sub(r'(?i)^\\Registry\\User\\\.Default', 'HKU\\.Default', s)
    if rootpath is not None and not s.startswith(('HK', '\\Registry')):
        s = rootpath + '\\' + s
    return T.scrub_text(s, 220)


PREDEF = {0x80000000: 'HKCR', 0x80000001: 'HKCU', 0x80000002: 'HKLM', 0x80000003: 'HKU', 0x80000005: 'HKCC'}


class G:
    pass


def main():
    log, out, pid = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else None
    t0 = None
    tfirst = None
    tlast = None
    tid_first, tid_last, tid_lines = {}, {}, Counter()
    modules, unloads = [], []
    loaddll_other = Counter()
    # file side
    handle_name = {}
    pend_create = {}                 # tid -> dict
    opens = []                       # CreateFileW calls
    nt_opens = []                    # NtCreateFile lines that were not CreateFileW's
    notfound = []
    probes = []
    enum_calls = []
    dir_of_tid = {}
    dir_entries = Counter()
    misc_ops = []
    reads = {}
    writes = {}
    unmapped = Counter()
    unmapped_bytes = Counter()
    pend_read = {}
    pend_write = {}
    last_api = {}
    read_api = Counter()
    read_status = Counter()
    read_sizes = Counter()
    seeks = Counter()
    seek_by_file = Counter()
    ioctl = Counter()
    ioctl_tids = defaultdict(Counter)
    ovl_results = 0
    ovl_fn = Counter()
    readfile_ovl = [0]
    api_by_file = defaultdict(Counter)
    # registry
    keymap = {}
    pend_key = {}
    keyopens = defaultdict(lambda: [0, 0, 0])   # path -> [open ok, open failed, create]
    keyfirst = {}
    qv = Counter()
    qv_first = {}
    writes_reg = Counter()
    reg_apis = Counter()
    # process
    env_get, env_set, expand = Counter(), Counter(), Counter()
    procs = []
    # seh, names, debug
    excs = []
    cur_exc = None
    seh_other = Counter()
    tnames = []
    dstr = []
    other = Counter()
    other_ex = {}

    for n, ts, p, tid, cls, ch, fn, msg in T.records(log, limit):
        if ts is None:
            continue
        if t0 is None:
            t0 = ts
        if p != pid:
            continue
        t = ts - t0
        if tfirst is None:
            tfirst = t
        tlast = t
        if tid not in tid_first:
            tid_first[tid] = t
        tid_last[tid] = t
        tid_lines[tid] += 1

        if ch == 'file':
            if fn == 'NtReadFile':
                if msg[:1] == b'(':
                    a = msg.rstrip(b' )').lstrip(b'(').split(b',')
                    h = int(a[0], 16)
                    ln = int(a[6], 16)
                    api = last_api.pop(tid, 'NtReadFile')
                    read_api[api] += 1
                    nm = handle_name.get(h)
                    if nm:
                        api_by_file[pkey(nm)][api] += 1
                    pend_read[tid] = (h, ln, t)
                else:
                    m = re.match(rb'= (\w+) \((\d+)\)', msg)
                    pr = pend_read.pop(tid, None)
                    if not m or pr is None:
                        continue
                    st, got = m.group(1).decode(), int(m.group(2))
                    read_status[st] += 1
                    h, ln, t_in = pr
                    name = handle_name.get(h)
                    key = pkey(name) if name else None
                    if key is None:
                        unmapped[h] += 1
                        unmapped_bytes[h] += got if st == 'SUCCESS' else 0
                        continue
                    r = reads.get(key)
                    if r is None:
                        r = reads[key] = {'name': name, 'n': 0, 'req': 0, 'ret': 0, 'pending': 0, 'pending_req': 0,
                                          'first': t_in, 'last': t_in, 'bins': {}, 'sizes': Counter(), 'tids': set()}
                    r['n'] += 1
                    r['req'] += ln
                    r['last'] = t_in
                    r['tids'].add(tid)
                    r['sizes'][min(ln.bit_length(), 30)] += 1
                    read_sizes[min(ln.bit_length(), 30)] += 1
                    if st == 'SUCCESS':
                        r['ret'] += got
                        b = r['bins'].setdefault(int(t_in), [0, 0])
                        b[0] += 1
                        b[1] += got
                    else:
                        r['pending'] += 1
                        r['pending_req'] += ln
                        b = r['bins'].setdefault(int(t_in), [0, 0])
                        b[0] += 1
                        b[1] += ln
            elif fn == 'ReadFile' or fn == 'ReadFileEx':
                last_api[tid] = fn
                if fn == 'ReadFileEx':
                    m = re.search(rb'ovl_fn=([0-9A-Fa-f]+)', msg)
                    if m:
                        ovl_fn[int(m.group(1), 16)] += 1
                else:
                    a = msg.split()
                    if len(a) >= 5 and int(a[4], 16) != 0:
                        readfile_ovl[0] += 1
            elif fn == 'NtWriteFile':
                # asynchronous writes print no result line, so count at entry (requested bytes)
                if msg[:1] == b'(':
                    a = msg.rstrip(b' )').lstrip(b'(').split(b',')
                    h, ln = int(a[0], 16), int(a[6], 16)
                    name = handle_name.get(h)
                    key = pkey(name) if name else '<unmapped:%x>' % h
                    w = writes.get(key)
                    if w is None:
                        w = writes[key] = {'name': name or '<unmapped handle>', 'n': 0, 'bytes': 0, 'first': t, 'last': t, 'tids': set()}
                    w['n'] += 1
                    w['bytes'] += ln
                    w['last'] = t
                    w['tids'].add(tid)
            elif fn == 'NtSetInformationFile':
                a = msg.rstrip(b' )').lstrip(b'(').split(b',')
                if len(a) >= 5:
                    cl = int(a[4], 16)
                    seeks[cl] += 1
                    if cl == 14:
                        nm = handle_name.get(int(a[0], 16))
                        seek_by_file[pkey(nm) if nm else '<unmapped>'] += 1
            elif fn == 'NtDeviceIoControlFile':
                a = msg.rstrip(b' )').lstrip(b'(').split(b',')
                if len(a) >= 6:
                    code = int(a[5], 16)
                    ioctl[code] += 1
                    ioctl_tids[code][tid] += 1
            elif fn == 'GetOverlappedResult':
                ovl_results += 1
            elif fn == 'CreateFileW':
                s = msg.decode('utf-8', 'replace')
                if s.startswith('returning'):
                    pc = pend_create.pop(tid, None)
                    h = hx(s.split()[1])
                    if pc is not None:
                        pc['handle_ok'] = h not in (0, 0xFFFFFFFFFFFFFFFF)
                        pc['t_ret'] = t
                        if pc['handle_ok']:
                            handle_name[h] = pc['path']
                elif s.startswith('Unable to create'):
                    m = re.search(QS + r' \(status ([0-9a-f]+)\)', s)
                    if m:
                        notfound.append([round(t, 3), 'CreateFileW', norm_path(m.group(1)), m.group(2)])
                    pend_create.pop(tid, None)
                else:
                    m = re.match(QS + r' (.*?)\s+creation (\d+) attributes (0x[0-9a-f]+)', s)
                    if m:
                        o = {'t': round(t, 3), 'tid': tid, 'path': norm_path(m.group(1)), 'access': m.group(2).strip(),
                             'creation': int(m.group(3)), 'attrs': m.group(4)}
                        opens.append(o)
                        pend_create[tid] = o
            elif fn == 'NtCreateFile':
                s = msg.decode('utf-8', 'replace')
                if s.startswith('handle='):
                    m = re.search(r'access=([0-9a-f]+) name=' + QS + r'.*? disp=(\d+) options=([0-9a-f]+)', s)
                    if m:
                        opt = int(m.group(4), 16)
                        rec = {'t': round(t, 3), 'tid': tid, 'path': norm_path(m.group(2)), 'access': m.group(1),
                               'disp': int(m.group(3)), 'options': m.group(4)}
                        nt_opens.append(rec)
                        if opt & 1:
                            dir_of_tid[tid] = rec['path']
                elif 'not found' in s:
                    m = re.match(QS + r' not found \(([0-9a-f]+)\)', s)
                    if m:
                        notfound.append([round(t, 3), 'NtCreateFile', norm_path(m.group(1)), m.group(2)])
            elif fn in ('NtQueryFullAttributesFile', 'NtQueryAttributesFile'):
                m = re.match(QS + r' not found \(([0-9a-f]+)\)', msg.decode('utf-8', 'replace'))
                if m:
                    notfound.append([round(t, 3), fn, norm_path(m.group(1)), m.group(2)])
            elif fn in ('GetFileAttributesW', 'GetFileAttributesExW', 'GetLongPathNameW', 'GetShortPathNameW',
                        'CreateDirectoryW', 'DeleteFileW', 'SetFileAttributesW', 'GetTempFileNameW', 'GetTempPathW',
                        'copy_file', 'MoveFileWithProgressW', 'ReplaceFileW', 'FindFirstFileExW', 'RtlSetCurrentDirectory_U'):
                s = msg.decode('utf-8', 'replace')
                strs = re.findall(QS, s)
                rec = [round(t, 3), tid, fn] + [norm_path(x) for x in strs[:2]]
                if fn == 'FindFirstFileExW':
                    enum_calls.append(rec)
                elif fn.startswith('GetFileAttr') or fn.startswith('Get'):
                    probes.append(rec)
                else:
                    misc_ops.append(rec)
            elif fn == 'append_entry':
                d = dir_of_tid.get(tid, '?')
                dir_entries[d] += 1
            elif fn == 'init_cached_dir_data':
                m = re.search(r'found (\d+) files', msg.decode('utf-8', 'replace'))
                if m:
                    enum_calls.append([round(t, 3), tid, 'dir_cache', dir_of_tid.get(tid, '?'), int(m.group(1))])
            else:
                k = (cls, ch, fn)
                other[k] += 1

        elif ch == 'loaddll':
            s = msg.decode('utf-8', 'replace')
            m = re.match(r'Loaded ' + QS + r' at ([0-9A-Fa-f]+): (\w+)', s)
            if m and fn in ('build_module', 'build_ntdll_module'):
                modules.append({'t': round(t, 3), 'tid': tid, 'path': norm_path(m.group(1)), 'base': int(m.group(2), 16), 'kind': m.group(3)})
            else:
                m = re.match(r'Unloaded module ' + QS + r' : (\w+)', s)
                if m:
                    unloads.append({'t': round(t, 3), 'tid': tid, 'path': norm_path(m.group(1)), 'kind': m.group(2)})
                else:
                    loaddll_other[(cls, fn)] += 1

        elif ch == 'reg':
            s = msg.decode('utf-8', 'replace')
            if fn in ('NtOpenKeyEx', 'NtCreateKey'):
                if s.startswith('<-'):
                    h = hx(s[2:])
                    pk = pend_key.pop(tid, None)
                    if pk is not None:
                        path, kind = pk
                        if h:
                            keymap[h] = path
                            keyopens[path][2 if kind == 'create' else 0] += 1
                        else:
                            keyopens[path][1] += 1
                        keyfirst.setdefault(path, round(t, 3))
                else:
                    m = re.match(r'\(([^,]*),' + QS, s)
                    if m:
                        root = hx(m.group(1))
                        rp = keymap.get(root) if root else None
                        if root and rp is None:
                            rp = PREDEF.get(root, '?')
                        pend_key[tid] = (reg_name(m.group(2), rp), 'create' if fn == 'NtCreateKey' else 'open')
            elif fn == 'NtQueryValueKey':
                m = re.match(r'\(([^,]*),' + QS, s)
                if m:
                    kp = keymap.get(hx(m.group(1)), '?')
                    vn = T.scrub_raw(m.group(2), 120)
                    qv[(kp, vn)] += 1
                    qv_first.setdefault((kp, vn), round(t, 3))
            elif fn in ('NtSetValueKey', 'NtDeleteValueKey'):
                m = re.match(r'\(([^,]*),' + QS, s)
                if m:
                    kp = keymap.get(hx(m.group(1)), '?')
                    writes_reg[(fn, kp, T.scrub_raw(m.group(2), 120))] += 1
            elif fn == 'NtDeleteKey':
                m = re.match(r'\(([^)]*)\)', s)
                writes_reg[(fn, keymap.get(hx(m.group(1)), '?') if m else '?', '')] += 1
            else:
                reg_apis[fn] += 1

        elif ch == 'process':
            s = msg.decode('utf-8', 'replace')
            if fn == 'GetEnvironmentVariableW':
                m = re.match(r'\(' + QS, s)
                if m:
                    env_get[T.scrub_raw(m.group(1), 80)] += 1
            elif fn == 'SetEnvironmentVariableW':
                m = re.match(r'\(' + QS, s)
                if m:
                    env_set[T.scrub_raw(m.group(1), 80)] += 1
            elif fn == 'ExpandEnvironmentStringsW':
                m = re.match(r'\(' + QS, s)
                if m:
                    expand[T.scrub_raw(m.group(1), 80)] += 1
            elif fn == 'CreateProcessInternalW':
                m = re.match(r'app (?:' + QS + r'|\(null\)) cmdline (?:' + QS + r'|\(null\))', s)
                if m:
                    procs.append({'t': round(t, 3), 'tid': tid, 'api': fn, 'app': norm_path(m.group(1)) if m.group(1) else None,
                                  'args': T.cmdline_names(m.group(2)) if m.group(2) else None})
            elif fn == 'NtCreateUserProcess':
                m = re.match(QS + r' image ' + QS + r' cmdline ' + QS, s)
                if m:
                    procs.append({'t': round(t, 3), 'tid': tid, 'api': fn, 'app': norm_path(m.group(2)), 'args': T.cmdline_names(m.group(3))})
            elif fn == 'NtTerminateProcess':
                procs.append({'t': round(t, 3), 'tid': tid, 'api': fn, 'what': re.sub(r'0x[0-9a-f]{6,}', '<h>', s)[:80]})
            else:
                other[(cls, ch, fn)] += 1

        elif ch == 'seh':
            s = msg.decode('utf-8', 'replace')
            if fn == 'dispatch_exception':
                if cls == 'warn':      # the debug string being dispatched
                    dstr.append({'t': round(t, 3), 'tid': tid, 'via': 'exception', 'text': T.scrub_raw(s.strip().strip('"'))})
                    continue
                m = re.match(r'code=([0-9a-f]+)(?: \(([^)]*)\))? flags=([0-9a-f]+) addr=([0-9A-Fa-f]+)', s)
                if m:
                    cur_exc = {'t': round(t, 3), 'tid': tid, 'code': m.group(1).lower(), 'name': m.group(2), 'flags': m.group(3),
                               'addr': int(m.group(4), 16), 'info': [], 'rip': None}
                    excs.append(cur_exc)
                    continue
                m = re.match(r' info\[(\d+)\]=([0-9a-f]+)', s)
                if m and cur_exc is not None and cur_exc['tid'] == tid:
                    cur_exc['info'].append(int(m.group(2), 16))
                    continue
                m = re.match(r'rip=([0-9a-f]+)', s)
                if m and cur_exc is not None and cur_exc['tid'] == tid and cur_exc['rip'] is None:
                    cur_exc['rip'] = int(m.group(1), 16)
                    continue
                seh_other[fn] += 1
            else:
                seh_other[fn] += 1

        elif ch == 'threadname':
            m = re.search(r'renamed to ' + QS, msg.decode('utf-8', 'replace'))
            tnames.append({'t': round(t, 3), 'tid': tid, 'fn': fn, 'name': T.scrub_raw(m.group(1), 60) if m else '?'})

        elif ch == 'debugstr':
            s = msg.decode('utf-8', 'replace')
            m = re.match(QS, s)
            dstr.append({'t': round(t, 3), 'tid': tid, 'via': fn, 'text': T.scrub_raw(m.group(1) if m else s)})

        else:
            k = (cls, ch, fn)
            other[k] += 1
            if k not in other_ex:
                other_ex[k] = re.sub(r'\d+', 'N', re.sub(QS, '"s"', msg.decode('utf-8', 'replace')))[:80]

    # ---- serialise
    def key_counts(c):
        return [[k, v] for k, v in c.most_common()]

    res = {
        'meta': {'t0': t0, 'first': tfirst, 'last': tlast, 'pid': '%04x' % pid},
        'tids': {'%04x' % k: [round(tid_first[k], 3), round(tid_last[k], 3), tid_lines[k]] for k in tid_first},
        'modules': modules, 'unloads': unloads, 'loaddll_other': [[list(k), v] for k, v in loaddll_other.items()],
        'opens': opens, 'nt_opens': nt_opens, 'notfound': notfound, 'probes': probes, 'enum_calls': enum_calls,
        'dir_entries': key_counts(dir_entries), 'misc_ops': misc_ops,
        'reads': [dict(name=r['name'], n=r['n'], req=r['req'], ret=r['ret'], pending=r['pending'], pending_req=r['pending_req'],
                       first=round(r['first'], 3), last=round(r['last'], 3), bins=r['bins'],
                       sizes=dict(r['sizes']), tids=sorted('%04x' % x for x in r['tids'])) for r in reads.values()],
        'writes': [dict(w, tids=sorted('%04x' % x for x in w['tids'])) for w in writes.values()],
        'unmapped_reads': {'handles': len(unmapped), 'reads': sum(unmapped.values()), 'bytes': sum(unmapped_bytes.values())},
        'read_api': dict(read_api), 'read_status': dict(read_status), 'read_sizes_log2': {str(k): v for k, v in sorted(read_sizes.items())},
        'seek_classes': {str(k): v for k, v in seeks.items()}, 'seek_by_file': key_counts(seek_by_file)[:40],
        'ioctl': [[hex(k), v, {'%04x' % t: c for t, c in ioctl_tids[k].most_common(4)}] for k, v in ioctl.most_common(30)],
        'overlapped_results': ovl_results, 'readfileex_callbacks': [[hex(k), v] for k, v in ovl_fn.most_common(8)],
        'readfile_with_overlapped_arg': readfile_ovl[0], 'api_by_file': {k: dict(v) for k, v in api_by_file.items()},
        'keyopens': [[k, v, keyfirst.get(k)] for k, v in sorted(keyopens.items(), key=lambda kv: keyfirst.get(kv[0], 0))],
        'value_queries': [[k[0], k[1], v, qv_first[k]] for k, v in sorted(qv.items(), key=lambda kv: qv_first[kv[0]])],
        'reg_writes': [[list(k), v] for k, v in writes_reg.items()], 'reg_other_apis': key_counts(reg_apis),
        'env_get': key_counts(env_get), 'env_set': key_counts(env_set), 'expand': key_counts(expand), 'procs': procs,
        'exceptions': excs, 'seh_other': key_counts(seh_other), 'threadnames': tnames, 'debugstr': dstr,
        'other': [[list(k), v, other_ex.get(k)] for k, v in other.most_common()],
    }
    json.dump(res, open(out, 'w'), indent=0, default=list)
    print('written', out, 'lines in game', sum(tid_lines.values()), 'modules', len(modules), 'opens', len(opens),
          'nt_opens', len(nt_opens), 'reads files', len(reads), 'exceptions', len(excs))


main()
