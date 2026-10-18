#!/usr/bin/env python3
"""What each module did while it loaded, from the runtime's log of one `gate3 load`.

    RT_LOG=all:debug RT_LOG_FILE=<log> build/gate3/gate3 load --in-process <modules...>
    .venv/bin/python tools/gate3/loadlog.py <log>                 every module, short
    .venv/bin/python tools/gate3/loadlog.py <log> -m engine       one module, with its call sites
    .venv/bin/python tools/gate3/loadlog.py <log> --common        the calls every C runtime start-up makes

Reads the log only (no game file).  A module's part of the log runs from the loader's line
"<module>: image built" to "<module>: loaded at" (or to the end of the log when the load
stopped); `--in-process` puts each load in the log once.  The trace channel gives one line
per call into a Windows function (runtime/core/trace.c), the import channel one line per
GetProcAddress, the other channels what their shims report.

For each module it prints the calls that say what the load asked the system for: modules
by name, GetProcAddress names, files, registry, threads, memory, kernel objects, the
environment, and every call into a library other than kernel32.  The rest is a histogram,
shown as the difference from the profile that most modules share (the Visual Studio C
runtime's start-up), so that fourteen identical start-ups do not hide the one that differs.
"""
import argparse
import collections
import re
import sys

CALL = re.compile(r'^trace: ([0-9a-f]+):Call ([A-Za-z0-9_]+)\.([A-Za-z0-9_@?$]+)\((.*)\) ret=(\S+)$')
RET = re.compile(r'^trace: ([0-9a-f]+):Ret  ([A-Za-z0-9_]+)\.([A-Za-z0-9_@?$]+)\(\) retval=(\S+) ret=(\S+)$')
BUILT = re.compile(r'^loader: (\S+): image built at (0x[0-9a-f]+)')
LOADED = re.compile(r'^loader: (\S+): loaded at ')
GPA = re.compile(r'^import: GetProcAddress\((\S+), (\S+)\) = (\S+)')

# Groups of calls that are listed one by one (with arguments and result), by what they ask for.
GROUPS = [
    ('modules by name', re.compile(r'^(LoadLibrary|GetModuleHandle|FreeLibrary|LoadPackagedLibrary)')),
    ('files', re.compile(r'^(CreateFile|FindFirst|FindNext|GetFileAttributes|ReadFile|WriteFile|DeleteFile|CreateDirectory|'
                         r'RemoveDirectory|SetCurrentDirectory|GetCurrentDirectory|GetFullPathName|SetFilePointer|GetFileSize|'
                         r'MoveFile|CopyFile|GetTempPath|GetTempFileName|SearchPath|GetLongPathName|GetShortPathName|'
                         r'GetDriveType|GetDiskFreeSpace|GetVolumeInformation|GetLogicalDrive|MapViewOfFile|OpenFile|_lopen|'
                         r'GetPrivateProfile|WritePrivateProfile|SHGet)')),
    ('registry', re.compile(r'^(Reg[A-Z])')),
    ('threads', re.compile(r'^(CreateThread|CreateRemoteThread|ExitThread|TerminateThread|SetThreadPriority|SetThreadAffinityMask|'
                           r'SetThreadIdealProcessor|SetProcessAffinityMask|GetProcessAffinityMask|ResumeThread|SuspendThread|'
                           r'QueueUserAPC|CreateTimerQueue|SetThreadName|SetThreadDescription)')),
    ('memory', re.compile(r'^(VirtualAlloc|VirtualFree|VirtualProtect|VirtualQuery|HeapCreate|HeapDestroy|HeapSetInformation|'
                          r'GlobalAlloc|LocalAlloc|GlobalMemoryStatus|CreateFileMapping)')),
    ('kernel objects', re.compile(r'^(CreateEvent|CreateSemaphore|CreateMutex|CreateWaitableTimer|OpenEvent|OpenMutex|OpenSemaphore|'
                                  r'CreateIoCompletionPort|CreatePipe|CreateNamedPipe|DuplicateHandle|WaitFor|SignalObjectAndWait|'
                                  r'MsgWaitFor|Sleep|SetEvent|ResetEvent|ReleaseSemaphore|ReleaseMutex|CloseHandle)')),
    ('environment and identity', re.compile(r'^(GetEnvironmentVariable|SetEnvironmentVariable|ExpandEnvironmentStrings|GetCommandLine|'
                                            r'GetModuleFileName|GetComputerName|GetUserName|GetVersion|VerifyVersionInfo|GetSystemInfo|'
                                            r'GetNativeSystemInfo|GetLogicalProcessorInformation|IsProcessorFeaturePresent|'
                                            r'IsDebuggerPresent|GetSystemDirectory|GetWindowsDirectory|GetUserDefault|GetSystemDefault|'
                                            r'GetTimeZoneInformation|SetUnhandledExceptionFilter|UnhandledExceptionFilter|'
                                            r'AddVectoredExceptionHandler|SetErrorMode|RaiseException|OutputDebugString|GetStdHandle|'
                                            r'SetConsole|AllocConsole|GetConsole|SetProcessDPIAware|CoInitialize|CoCreate)')),
    ('thread and fiber slots', re.compile(r'^(TlsAlloc|TlsFree|FlsAlloc|FlsFree)')),
]
OTHER_LIB = 'other libraries'
CHANNELS = ('file', 'registry', 'thread', 'window', 'net', 'guest.debug', 'trap')


class Segment:
    def __init__(self, name, base):
        self.name, self.base = name, base
        self.finished = False
        self.calls = collections.Counter()          # 'lib.Function' -> count
        self.callers = collections.Counter()        # module of the call site -> count
        self.listed = collections.defaultdict(list)  # group -> [(function, args, result, site)]
        self.gpa = []                               # (library, name, answer)
        self.channel = collections.defaultdict(list)
        self.vm = collections.Counter()
        self.open = {}                              # thread -> stack of listed entries waiting for a result
        self.last = collections.deque(maxlen=6)


def group_of(lib, fn):
    if lib != 'kernel32':
        return OTHER_LIB
    for title, rx in GROUPS:
        if rx.match(fn):
            return title
    return None


def parse(path):
    segs, cur, before = [], None, Segment('(before any load)', '')
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('trace: '):
                seg = cur or before
                m = CALL.match(line)
                if m:
                    tid, lib, fn, args, site = m.groups()
                    seg.calls[lib + '.' + fn] += 1
                    seg.callers[site.split('+')[0]] += 1
                    seg.last.append(line[7:])
                    g = group_of(lib, fn)
                    entry = [fn if lib == 'kernel32' else lib + '.' + fn, args, None, site] if g else None
                    if entry:
                        seg.listed[g].append(entry)
                    seg.open.setdefault(tid, []).append(entry)
                    continue
                m = RET.match(line)
                if m:
                    tid, lib, fn, val, site = m.groups()
                    stack = seg.open.get(tid)
                    if stack:
                        entry = stack.pop()
                        if entry:
                            entry[2] = val
                    continue
                continue
            m = BUILT.match(line)
            if m:
                cur = Segment(m.group(1), m.group(2))
                segs.append(cur)
                continue
            m = LOADED.match(line)
            if m and cur and cur.name == m.group(1):
                cur.finished = True
                cur = None
                continue
            seg = cur or before
            m = GPA.match(line)
            if m:
                seg.gpa.append(m.groups())
                continue
            ch = line.split(':', 1)[0]
            if ch == 'vm':
                seg.vm[' '.join(line.split()[1:4]).split(' 0x')[0]] += 1
            elif ch in CHANNELS:
                seg.channel[ch].append(line)
    return before, segs


def common_profile(segs):
    """function -> the count that most finished modules share (when at least half share one)."""
    done = [s for s in segs if s.finished]
    prof = {}
    names = set()
    for s in done:
        names.update(s.calls)
    for fn in names:
        counts = collections.Counter(s.calls.get(fn, 0) for s in done)
        n, k = counts.most_common(1)[0]
        if k * 2 > len(done) and n:
            prof[fn] = n
    return prof


def short(args, n=110):
    return args if len(args) <= n else args[:n] + '...'


def show(seg, prof, sites, out):
    total = sum(seg.calls.values())
    state = 'loaded' if seg.finished else 'DID NOT FINISH (the log ends inside its load)'
    out.write(f'\n## {seg.name}: {state}; {total} calls into Windows functions, {len(seg.calls)} distinct\n')
    if seg.callers:
        out.write('call sites in: ' + ', '.join(f'{m} {n}' for m, n in seg.callers.most_common()) + '\n')
    for title, _ in GROUPS + [(OTHER_LIB, None)]:
        rows = seg.listed.get(title)
        if not rows:
            continue
        out.write(f'{title}:\n')
        merged = collections.OrderedDict()
        for fn, args, val, site in rows:
            key = (fn, args, val) if not sites else (fn, args, val, site)
            merged.setdefault(key, []).append(site)
        shown = 0
        for key, where in merged.items():
            fn, args, val = key[0], key[1], key[2]
            if shown == 40 and not sites:
                out.write(f'    ... and {len(merged) - shown} more distinct calls (use -m for all)\n')
                break
            times = f' x{len(where)}' if len(where) > 1 else ''
            at = where[0] if len(set(where)) == 1 else f'{len(set(where))} sites, first {where[0]}'
            out.write(f'    {fn}({short(args)}) = {val}{times}   [{at}]\n')
            shown += 1
    if seg.gpa:
        hit = [(lib, n, a) for lib, n, a in seg.gpa if a != 'NULL']
        miss = collections.Counter((lib, n) for lib, n, a in seg.gpa if a == 'NULL')
        out.write(f'GetProcAddress: {len(seg.gpa)} asked, {len(hit)} answered, {sum(miss.values())} NULL\n')
        if hit:
            out.write('    answered: ' + ', '.join(sorted({f'{lib}!{n}' for lib, n, a in hit})) + '\n')
        if miss:
            out.write('    NULL: ' + ', '.join(f'{lib}!{n}' + (f' x{k}' if k > 1 else '') for (lib, n), k in miss.items()) + '\n')
    for ch in CHANNELS:
        rows = seg.channel.get(ch)
        if rows:
            out.write(f'channel {ch}: {len(rows)} lines\n')
            for r in rows[:12 if not sites else 200]:
                out.write('    ' + short(r, 200) + '\n')
    if seg.vm:
        out.write('vm channel: ' + ', '.join(f'{k} x{n}' for k, n in seg.vm.most_common()) + '\n')
    diff = []
    for fn in sorted(set(seg.calls) | set(prof)):
        have, usual = seg.calls.get(fn, 0), prof.get(fn, 0)
        if have != usual:
            diff.append(f'{fn.replace("kernel32.", "")} {have}' + (f' (usual {usual})' if usual else ''))
    out.write('calls that differ from the common start-up: ' + ('; '.join(diff) if diff else 'none') + '\n')
    if not seg.finished:
        out.write('last calls before the log ends:\n')
        for l in seg.last:
            out.write('    ' + short(l, 200) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('log')
    ap.add_argument('-m', '--module', action='append', help='only this module (name with or without .dll); lists every call site')
    ap.add_argument('--common', action='store_true', help='print the profile most modules share')
    a = ap.parse_args()
    before, segs = parse(a.log)
    if not segs:
        sys.exit(f'{a.log}: no "image built" line: is the loader channel on (RT_LOG=all:debug)?')
    names = [s.name for s in segs]
    if len(set(names)) != len(names):
        sys.stdout.write('NOTE: a module is in this log more than once (not --in-process?); each load is shown.\n')
    prof = common_profile(segs)
    out = sys.stdout
    out.write(f'{a.log}: {len(segs)} loads, {sum(s.finished for s in segs)} finished, '
              f'{sum(sum(s.calls.values()) for s in segs)} calls into Windows functions\n')
    if a.common:
        out.write('\ncommon start-up profile (the count more than half of the finished modules share):\n')
        for fn, n in sorted(prof.items(), key=lambda kv: (-kv[1], kv[0])):
            out.write(f'    {n:6d} {fn}\n')
    want = None
    if a.module:
        want = {m.lower() if m.lower().endswith('.dll') else m.lower() + '.dll' for m in a.module}
    for s in segs:
        if want is None or s.name in want:
            show(s, prof, want is not None, out)


if __name__ == '__main__':
    main()
