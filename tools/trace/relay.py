#!/usr/bin/env python3
"""Cut a Wine relay log (WINEDEBUG=+timestamp,+pid,+tid,+relay,+loaddll,... and optionally +snoop).

One file, standard library only, so that it can travel on standard input to the machine that
holds the log (nothing is installed there, nothing is written there):

    ssh HOST nice python3 - calls LOG --pid 04e8 --dll user32 --t 5:9 < tools/trace/relay.py

tools/trace/cut.py does exactly that and keeps an index on this side, so that a time window is
reached by a seek instead of a pass.  Subcommands (each prints text; --help on each):

    index    one pass: processes, the game's process and its time zero, per-second file
             offsets, the module load events.  Its JSON is the --ctx of the other commands.
    calls    the calls of one process from a set of calling modules, between two times or
             two marker calls:   time thread caller+rva function(args) = result
    census   per function: count, calling modules, first time, first call site; by first call
    threads  every thread: creator, start address, name, priority and affinity calls, first
             and last call, counts, what it waits on and who wakes it
    files    every path opened, found or queried, in order: access, result, bytes asked of
             it, by which module
    phases   the times of the landmarks (module loads, the launcher's steps, the window...)
    exceptions  every exception dispatched: code, where raised, handlers called, where unwound to
    multi    several of the above in one pass:  multi LOG --pid P -- census -- threads -- ...

Lines of the log this reads (times are seconds; everything is printed relative to the first
line of the process, the "time zero" of the index):

    1467891.516:04e8:0550:Call KERNEL32.CreateThread(00000000,...) ret=6fffe9638660
    1467891.516:04e8:0550:Ret  KERNEL32.CreateThread() retval=00000014 ret=6fffe9638660
    ...:Call window proc 00006FFFD3E579D0 (hwnd=...,msg=WM_SHOWWINDOW,wp=..,lp=..)      a call INTO a module
    ...:Call PE DLL (proc=...,module=... L"ntdll.dll",reason=PROCESS_ATTACH,res=...)   likewise
    ...:CALL launcher.LauncherMain(...) ret=140001234                                  +snoop
    ...:trace:loaddll:build_module Loaded L"S:\\...\\engine.dll" at 00006FFFE0000000: native
    ...:trace:loaddll:free_modref Unloaded module L"..." : native
    ...:Starting thread proc 00006FFFE964B890 (arg=00000000041D52C0)
    ...:warn:threadname:NtSetInformationThread Thread renamed to L"dxvk-submit"

A return address becomes module+rva by the nearest module base below it (the load lines carry
no size; an address more than 256 MB above the nearest base is printed as ?+address).

Account data.  --commit-safe is for anything that will be committed: an argument list is
replaced by <redacted:reason> when the function's name or its arguments match
  name    auth, token, ticket, persona, passw (any case)
  email   an e-mail address
  run     24 or more characters of the base64 or hex alphabet in a row (with - and _).  A run
          of letters, underscores and hyphens only is let through (a symbol or a file name) unless
          --strict; the name given to GetProcAddress and the path given to LoadLibrary are
          symbols by their type and are let through as well.  Both are counted in the report.
  digits  10 or more decimal digits in a row inside a quoted string
  host    the name of the machine the tool runs on, or the user it runs as, or a --redact word
and the last line of the output says how many lines each rule caught.  Calls made BY a module
of EA's (OriginSDK, Activation, EADesktop, IGO, EALink...) are never printed in any mode: they
are counted per module.  Lines of the +process and +debugstr channels are never printed.
"""
import argparse
import base64
import bisect
import collections
import json
import os
import re
import socket
import sys
import zlib

RVA_LIMIT = 0x10000000
M32 = 0xffffffff     # a DWORD argument is printed as the whole register: the upper half is noise
EA_NAME = re.compile(r'^(origin|activation|eadesktop|igo|ealink|easteam|eabackground|ealocal|eacef|ealaunch|eadestager)', re.I)
EA_PATH = re.compile(r'\\(electronic arts|ea desktop|origin)\\', re.I)
GAME_PATH = re.compile(r'\\steamapps\\common\\titanfall2\\', re.I)

LOADED = re.compile(rb'^trace:loaddll:\S+ Loaded L"(.*)" at ([0-9a-fA-F]+): (\w+)')
UNLOADED = re.compile(rb'^trace:loaddll:\S+ Unloaded module L"(.*)" : (\w+)')
STARTING = re.compile(rb'^Starting thread proc ([0-9A-Fa-f]+) \(arg=([0-9A-Fa-f]+)\)')
RENAMED = re.compile(rb'^\w+:threadname:\S+ Thread renamed to L"(.*)"')
EXCEPTION = re.compile(rb'^trace:seh:dispatch_exception code=([0-9a-fA-F]+)')
PROC_ARG = re.compile(rb'proc=([0-9A-Fa-f]+)')
SPECIAL = ((b'window proc ', '(wndproc)'), (b'hook proc ', '(hookproc)'), (b'PE DLL ', '(dllmain)'),
           (b'TLS callback ', '(tls)'), (b'LDR notification callback ', '(ldrnotify)'))
QUOTED = re.compile(r'L?"((?:[^"\\]|\\.)*)"?')


def unescape(s):
    """The text of a string as Wine's debugstr printed it (backslashes doubled)."""
    return s.replace('\\\\', '\x00').replace('\\"', '"').replace('\x00', '\\')


def short_name(path):
    """engine.dll -> engine; Titanfall2.exe -> titanfall2.exe; GFSDK_SSAO.win64.dll -> gfsdk_ssao.win64."""
    name = path.replace('\\\\', '\\').split('\\')[-1].lower()
    return name[:-4] if name.endswith('.dll') else name


def mod_class(path):
    p = path.replace('\\\\', '\\')
    name = p.split('\\')[-1]
    if EA_NAME.match(name) or EA_PATH.search(p):
        return 'ea'
    if GAME_PATH.search(p):
        return 'game'
    return 'sys'


class Mods(object):
    """The modules of the process as the load lines give them: address -> (name, class, rva)."""

    def __init__(self):
        self.bases = []
        self.ents = []
        self.cache = {}

    def load(self, base, name, cls):
        i = bisect.bisect_left(self.bases, base)
        if i < len(self.bases) and self.bases[i] == base:
            self.ents[i] = (name, cls)
        else:
            self.bases.insert(i, base)
            self.ents.insert(i, (name, cls))
        self.cache.clear()

    def unload(self, name):
        for i in range(len(self.ents) - 1, -1, -1):
            if self.ents[i][0] == name:
                del self.ents[i]
                del self.bases[i]
        self.cache.clear()

    def find(self, addr):
        hit = self.cache.get(addr)
        if hit is None:
            k = bisect.bisect_right(self.bases, addr) - 1
            if k < 0 or addr - self.bases[k] >= RVA_LIMIT:
                hit = ('?', '?', addr)
            else:
                hit = (self.ents[k][0], self.ents[k][1], addr - self.bases[k])
            self.cache[addr] = hit
        return hit


class Rec(object):
    """One call.  mod/cls/rva: the caller (for a call into a module, kind != 'call': the callee)."""
    __slots__ = ('t', 'tid', 'fn', 'raw', 'args', 'ra', 'mod', 'cls', 'rva', 'retval', 't_ret', 'depth',
                 'kind', 'x')

    def site(self):
        s = '%s+0x%x' % (self.mod, self.rva)
        return s if self.kind in ('call', 'snoop') else '>' + s

    def argtext(self):
        return self.args.decode('latin-1')


def split_args(text):
    """Top-level comma split of a relay argument list; strings keep their commas."""
    out, cur, quote, esc = [], [], False, False
    for ch in text:
        if quote:
            cur.append(ch)
            if esc:
                esc = False
            elif ch == '\\':
                esc = True
            elif ch == '"':
                quote = False
        elif ch == '"':
            quote = True
            cur.append(ch)
        elif ch == ',':
            out.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append(''.join(cur))
    return out


def arg_string(arg):
    """The string printed after a pointer argument, unescaped; None when there is none."""
    m = QUOTED.search(arg)
    return unescape(m.group(1)) if m else None


def nth_int(args, n):
    """The n-th argument of a raw (bytes) argument list as a number, when the ones before it
    are plain values (no string holds a comma before it)."""
    parts = args.split(b',', n + 1)
    try:
        return int(parts[n].split()[0], 16)
    except (ValueError, IndexError):
        return None


def arg_int(arg):
    try:
        return int(arg.split()[0], 16)
    except (ValueError, IndexError):
        return None


class Safe(object):
    """The --commit-safe check (see the module text).  check() returns a reason or None."""
    EMAIL = re.compile(r'[\w.%+-]+@[\w-]+(\.[\w-]+)+')
    RUN = re.compile(r'[A-Za-z0-9+/=_-]{24,}')
    LETTERS = re.compile(r'^[A-Za-z_-]+$')
    DIGITS = re.compile(r'\d{10,}')
    NAME = re.compile(r'auth|token|ticket|persona|passw', re.I)
    SYMBOL_FN = re.compile(r'\.(GetProcAddress|LoadLibrary(Ex)?[AW]|GetModuleHandle(Ex)?[AW]|LdrGetProcedureAddress)$')

    def __init__(self, on, strict=False, words=()):
        self.on = on
        self.strict = strict
        ws = [w for w in words if w]
        if on:
            try:
                ws.append(socket.gethostname())
            except OSError:
                pass
            ws.append(os.environ.get('USER') or os.environ.get('LOGNAME') or '')
        ws = sorted(set(w.lower() for w in ws if len(w) >= 4))
        self.words = re.compile('|'.join(re.escape(w) for w in ws), re.I) if ws else None
        self.caught = collections.Counter()
        self.passed = collections.Counter()

    def check(self, fn, text):
        if not self.on:
            return None
        reason = self._check(fn, text)
        if reason:
            self.caught[reason] += 1
        return reason

    def _check(self, fn, text):
        if self.NAME.search(fn) or self.NAME.search(text):
            return 'name'
        if self.EMAIL.search(text):
            return 'email'
        if self.words is not None and self.words.search(text):
            return 'host'
        symbol = bool(self.SYMBOL_FN.search(fn))
        for m in self.RUN.finditer(text):
            run = m.group(0)
            if self.strict:
                return 'run'
            if self.LETTERS.match(run):
                self.passed['letters-only run'] += 1
            elif symbol:
                self.passed['symbol argument'] += 1
            else:
                return 'run'
        for m in QUOTED.finditer(text):
            if self.DIGITS.search(m.group(1)):
                return 'digits'
        return None

    def text(self, fn, text):
        """text, or <redacted:reason>."""
        reason = self.check(fn, text)
        return '<redacted:%s>' % reason if reason else text

    def report(self):
        if not self.on:
            return '# commit-safe: OFF (this output is not for committing)'
        got = ', '.join('%s %d' % kv for kv in sorted(self.caught.items())) or 'nothing'
        let = ', '.join('%s %d' % kv for kv in sorted(self.passed.items())) or 'none'
        return '# commit-safe: caught %s; let through: %s' % (got, let)


def parse_window(text):
    if not text:
        return None, None
    lo, _, hi = text.partition(':')
    return (float(lo) if lo else None), (float(hi) if hi else None)


class Coll(object):
    """A collector: sees every call, return and other line of the process, prints at the end."""
    needs_history = False
    name = '?'

    def __init__(self, a):
        self.a = a
        self.t_lo, self.t_hi = parse_window(getattr(a, 't', None))
        self.safe = Safe(getattr(a, 'commit_safe', False), getattr(a, 'strict', False),
                         (getattr(a, 'redact', '') or '').split(','))
        self.froms = self._froms(getattr(a, 'frm', None) or 'game')
        self.ea_from = collections.Counter()
        self.ea_into = collections.Counter()

    @staticmethod
    def _froms(text):
        return set(x.strip().lower() for x in text.split(',') if x.strip())

    def wanted(self, r):
        """Is the caller (or, for a call into a module, the callee) one of --from?  EA's never."""
        if r.cls == 'ea':
            (self.ea_from if r.kind in ('call', 'snoop') else self.ea_into)[r.mod] += 1
            return False
        f = self.froms
        return 'all' in f or r.cls in f or r.mod in f

    def inside(self, t):
        return (self.t_lo is None or t >= self.t_lo) and (self.t_hi is None or t < self.t_hi)

    def fn_filter(self, a):
        self.dlls = self._froms(a.dll) if a.dll else None
        self.fn = re.compile(a.fn) if a.fn else None
        self.not_fn = re.compile(a.not_fn) if a.not_fn else None
        self.fn_ok = {}

    def _fn_ok(self, fn):
        ok = self.fn_ok.get(fn)
        if ok is None:
            ok = True
            if self.dlls is not None and fn.split('.')[0] not in self.dlls:
                ok = False
            if ok and self.fn is not None and not self.fn.search(fn):
                ok = False
            if ok and self.not_fn is not None and self.not_fn.search(fn):
                ok = False
            self.fn_ok[fn] = ok
        return ok

    def finished(self, t):
        return False

    def call(self, r):
        pass

    def ret(self, r):
        pass

    def other(self, t, tid, rest):
        pass

    def loaded(self, t, tid, path, base, cls, how):
        pass

    def finish(self, out, env):
        pass

    def tail(self, out):
        if self.ea_from:
            out('# calls made by modules of EA\'s, counted and not shown: ' +
                ', '.join('%s %d' % kv for kv in self.ea_from.most_common()))
        if self.ea_into:
            out('# calls into modules of EA\'s (window, hook, DllMain, TLS), counted and not shown: ' +
                ', '.join('%s %d' % kv for kv in self.ea_into.most_common()))
        out(self.safe.report())


def common_args(p, window=True, fn=False):
    p.add_argument('--from', dest='frm', default='game',
                   help='calling modules: names (engine,launcher,titanfall2.exe), and/or the classes game '
                        '(a file of the install directory; the default), sys, ?, or all')
    if window:
        p.add_argument('--t', help='A:B, seconds after the process\'s first line; either side may be empty')
    if fn:
        p.add_argument('--dll', help='only calls into these DLLs (user32,gdi32,imm32)')
        p.add_argument('--fn', help='only functions matching this regex (on dll.Function, dll in lower case)')
        p.add_argument('--not-fn', help='drop functions matching this regex')
    p.add_argument('--commit-safe', action='store_true', help='redact as the module text says')
    p.add_argument('--strict', action='store_true', help='with --commit-safe: no exemption for letter-only runs or symbols')
    p.add_argument('--redact', default='', help='further words to redact, comma-separated (never committed)')


# ------------------------------------------------------------------------------------- calls
class Calls(Coll):
    name = 'calls'

    @staticmethod
    def parser(p):
        common_args(p, fn=True)
        p.add_argument('--grep', help='only lines whose function(args) match this regex')
        p.add_argument('--tid', help='only these threads (comma-separated ids)')
        p.add_argument('--after', help='start at the first call of the process whose "function(args)" matches')
        p.add_argument('--until', help='stop after the first call (past --after) whose "function(args)" matches')
        p.add_argument('--max', type=int, default=0, help='stop after this many printed calls')
        p.add_argument('--width', type=int, default=220, help='characters of the argument list kept')
        p.add_argument('--collapse', action='store_true',
                       help='fold consecutive calls of one function from one site by one thread')
        p.add_argument('--other', help='also print other lines of the process matching this regex '
                                       '(fixme:, threadname, loaddll...; never process or debugstr)')
        p.add_argument('--top', action='store_true', help='only calls not nested in another relayed call')

    def __init__(self, a):
        Coll.__init__(self, a)
        self.fn_filter(a)
        self.grep = re.compile(a.grep) if a.grep else None
        self.tids = self._froms(a.tid) if a.tid else None
        self.after = re.compile(a.after) if a.after else None
        self.until = re.compile(a.until) if a.until else None
        self.other_re = re.compile(a.other.encode()) if a.other else None
        self.started = self.after is None
        self.done = False
        self.done_t = None
        self.q = collections.deque()
        self.lines = []
        self.printed = 0
        self.run_key = None
        self.run_n = 0
        self.run_last = None

    def call(self, r):
        if self.done or not self.inside(r.t):
            return
        if not self.started or self.until is not None:
            text = '%s(%s)' % (r.fn, r.argtext())
            if not self.started:
                if not self.after.search(text):
                    return
                self.started = True
            elif self.until.search(text):
                self.done = True          # this call is still printed if the filters take it
        if not self.wanted(r) or not self._fn_ok(r.fn):
            return
        if self.tids is not None and r.tid not in self.tids:
            return
        if self.a.top and r.depth:
            return
        if self.grep is not None and not self.grep.search('%s(%s)' % (r.fn, r.argtext())):
            return
        r.x[self.ix] = True
        self.q.append(r)
        self._flush(False)

    def finished(self, t):
        if not self.done:
            return False
        if self.done_t is None:
            self.done_t = t
        return not self.q or t > self.done_t + 0.5

    def ret(self, r):
        if r.x[self.ix] is None:
            return
        if r.x[self.ix] == 'late':
            self._line('%9.3f %s   returned: %s = %s (called at %.3f)' % (r.t_ret, r.tid, r.fn, r.retval, r.t))
        else:
            self._flush(False)

    def other(self, t, tid, rest):
        if self.other_re is None or not self.started or self.done or not self.inside(t):
            return
        if rest.startswith(b'trace:process') or b':debugstr:' in rest[:40] or rest.startswith(b'warn:seh'):
            return
        if self.other_re.search(rest):
            self._flush(True)
            text = rest.decode('latin-1').rstrip()
            self._line('%9.3f %s | %s' % (t, tid, self.safe.text('', text[:self.a.width])))

    def _flush(self, force):
        q = self.q
        while q and (force or q[0].retval is not None or len(q) > 3000):
            self._emit(q.popleft())

    def _emit(self, r):
        if self.a.max and self.printed >= self.a.max:
            self.done = True
            return
        key = (r.tid, r.fn, r.mod, r.rva)
        if r.kind in ('(wndproc)', '(hookproc)'):
            key += (r.args.split(b',', 2)[1],)        # one run per message, not per procedure
        if self.a.collapse and key == self.run_key:
            self.run_n += 1
            self.run_last = r
            return
        self._close_run()
        self.run_key = key
        self.printed += 1
        self._line(self._fmt(r))

    def _fmt(self, r):
        args = r.argtext()
        if len(args) > self.a.width:
            args = args[:self.a.width] + '...'
        args = self.safe.text(r.fn, args)
        if r.retval is None:
            r.x[self.ix] = 'late'
            res = '... (not returned yet)'
        else:
            res = r.retval if r.retval != '' else 'void'
        return '%9.3f %s %s%s %s(%s) = %s' % (r.t, r.tid, '  ' * min(r.depth, 6), r.site(), r.fn, args, res)

    def _close_run(self):
        if self.run_n:
            r = self.run_last
            self._line('%9s %s   ... %d more of %s from the same site, the last at %.3f = %s'
                       % ('', r.tid, self.run_n, r.fn, r.t, r.retval if r.retval is not None else '...'))
        self.run_n = 0
        self.run_last = None

    def _line(self, text):
        self.lines.append(text)

    def finish(self, out, env):
        self._flush(True)
        self._close_run()
        out('# calls: %s; from %s; window %s; %d printed%s' % (
            env['log'], ','.join(sorted(self.froms)), self.a.t or 'whole run', self.printed,
            ' (stopped at --max)' if self.a.max and self.printed >= self.a.max else ''))
        out('# time thread caller+rva function(args) = result      (>module+rva: a call INTO that address)')
        for line in self.lines:
            out(line)
        self.tail(out)


# ------------------------------------------------------------------------------------ census
class Census(Coll):
    name = 'census'

    @staticmethod
    def parser(p):
        common_args(p, fn=True)
        p.add_argument('--sites', type=int, default=0, help='also list up to N call sites per function')
        p.add_argument('--key', help='a regex on the argument list: its groups are added to the function\'s '
                                     'name, so that one function is counted per value (msg=(\\w+) for a window procedure)')
        p.add_argument('--pow2', type=int, help='add the size class of argument N (0-based) to the name: '
                                                '{<2^k} holds values below 2^k')
        p.add_argument('--sum', type=int, help='add up argument N (0-based, read as a number) per row')
        p.add_argument('--dword', action='store_true', help='--pow2 and --sum read the low 32 bits (a DWORD '
                                                            'argument is printed with a noisy upper half)')
        p.add_argument('--by-module', action='store_true', help='one row per function and calling module')
        p.add_argument('--tid', help='only these threads (comma-separated ids)')

    def __init__(self, a):
        Coll.__init__(self, a)
        self.fn_filter(a)
        self.key = re.compile(a.key) if a.key else None
        self.tids = self._froms(a.tid) if a.tid else None
        self.f = {}
        self.others = collections.Counter()
        self.total = 0

    def call(self, r):
        if not self.inside(r.t):
            return
        if not self.wanted(r):
            if r.cls != 'ea':
                self.others[r.cls] += 1
            return
        if not self._fn_ok(r.fn) or (self.tids is not None and r.tid not in self.tids):
            return
        self.total += 1
        fn = r.fn
        a = self.a
        if a.by_module:
            fn = '%s@%s' % (fn, r.mod)
        if self.key is not None:
            m = self.key.search(r.argtext())
            if m:
                fn = '%s{%s}' % (fn, self.safe.text(r.fn, ','.join(g for g in m.groups() if g is not None) or m.group(0)))
        if a.pow2 is not None:
            v = nth_int(r.args, a.pow2)
            if v is not None:
                fn = '%s{<2^%d}' % (fn, (v & M32 if a.dword else v).bit_length())
        e = self.f.get(fn)
        if e is None:
            e = self.f[fn] = [0, r.t, r.site(), r.tid, collections.Counter(), collections.Counter(), 0]
        e[0] += 1
        e[4][r.mod] += 1
        if a.sites:
            e[5][r.site()] += 1
        if a.sum is not None:
            v = nth_int(r.args, a.sum) or 0
            e[6] += v & M32 if a.dword else v

    def finish(self, out, env):
        a = self.a
        out('# census: %s; from %s; window %s%s' % (env['log'], ','.join(sorted(self.froms)), a.t or 'whole run',
                                                  ''.join('; %s %s' % (k, v) for k, v in (
                                                      ('dll', a.dll), ('fn', a.fn), ('not-fn', a.not_fn),
                                                      ('key', a.key), ('pow2 of argument', a.pow2),
                                                      ('sum of argument', a.sum), ('threads', a.tid)) if v is not None)))
        out('# %d calls of %d distinct %s; calls from other modules, not listed: %s' % (
            self.total, len(self.f), 'rows' if (a.key or a.by_module or a.pow2 is not None) else 'functions',
            ', '.join('%s %d' % kv for kv in sorted(self.others.items())) or 'none'))
        out('# first_time\tcount\tfunction\tfirst_site\tfirst_thread\tcallers' + ('\tsum' if a.sum is not None else ''))
        for fn, e in sorted(self.f.items(), key=lambda kv: (kv[1][1], kv[0])):
            out('%.3f\t%d\t%s\t%s\t%s\t%s%s' % (e[1], e[0], fn, e[2], e[3],
                                               ' '.join('%s:%d' % kv for kv in e[4].most_common()),
                                               '\t%d' % e[6] if a.sum is not None else ''))
            for site, n in e[5].most_common(a.sites):
                out('\t\t\t%s\t\t%d' % (site, n))
        self.tail(out)


# ----------------------------------------------------------------------------------- threads
WAITS = {'WaitForSingleObject': 0, 'WaitForSingleObjectEx': 0, 'SignalObjectAndWait': 1,
         'SleepConditionVariableSRW': 0, 'SleepConditionVariableCS': 0, 'RtlSleepConditionVariableSRW': 0,
         'RtlSleepConditionVariableCS': 0, 'WaitOnAddress': 0, 'RtlWaitOnAddress': 0,
         'GetQueuedCompletionStatus': 0, 'GetQueuedCompletionStatusEx': 0,
         'WaitForMultipleObjects': None, 'WaitForMultipleObjectsEx': None,
         'MsgWaitForMultipleObjects': None, 'MsgWaitForMultipleObjectsEx': None,
         'Sleep': None, 'SleepEx': None, 'GetMessageA': None, 'GetMessageW': None, 'WaitMessage': None}
WAKES = {'SetEvent': 0, 'PulseEvent': 0, 'ResetEvent': 0, 'ReleaseSemaphore': 0, 'ReleaseMutex': 0,
         'RtlWakeConditionVariable': 0, 'RtlWakeAllConditionVariable': 0, 'WakeConditionVariable': 0,
         'WakeAllConditionVariable': 0, 'WakeByAddressSingle': 0, 'WakeByAddressAll': 0,
         'RtlWakeAddressSingle': 0, 'RtlWakeAddressAll': 0, 'PostQueuedCompletionStatus': 0,
         'SignalObjectAndWait': 0, 'QueueUserAPC': 1, 'ResumeThread': 0}
MAKERS = {'CreateEventA': 'event', 'CreateEventW': 'event', 'CreateEventExA': 'event', 'CreateEventExW': 'event',
          'OpenEventA': 'event', 'OpenEventW': 'event',
          'CreateSemaphoreA': 'sem', 'CreateSemaphoreW': 'sem', 'CreateSemaphoreExA': 'sem',
          'CreateSemaphoreExW': 'sem', 'CreateMutexA': 'mutex', 'CreateMutexW': 'mutex',
          'CreateMutexExA': 'mutex', 'CreateMutexExW': 'mutex', 'OpenMutexA': 'mutex', 'OpenMutexW': 'mutex',
          'CreateWaitableTimerA': 'timer', 'CreateWaitableTimerW': 'timer', 'CreateWaitableTimerExW': 'timer',
          'CreateIoCompletionPort': 'iocp', 'CreateThread': 'thread', 'OpenThread': 'thread',
          'CreateFileA': 'file', 'CreateFileW': 'file'}
THREAD_SET = ('SetThreadPriority', 'SetThreadAffinityMask', 'SetThreadIdealProcessor', 'SetThreadPriorityBoost',
              'SetThreadGroupAffinity', 'SetThreadIdealProcessorEx', 'SetThreadDescription',
              'SetThreadStackGuarantee', 'AvSetMmThreadCharacteristicsA', 'AvSetMmThreadCharacteristicsW',
              'AvSetMmThreadPriority', 'SuspendThread', 'TerminateThread')
ADDRESS_OBJ = ('SleepConditionVariableSRW', 'SleepConditionVariableCS', 'RtlSleepConditionVariableSRW',
               'RtlSleepConditionVariableCS', 'WaitOnAddress', 'RtlWaitOnAddress', 'RtlWakeConditionVariable',
               'RtlWakeAllConditionVariable', 'WakeConditionVariable', 'WakeAllConditionVariable',
               'WakeByAddressSingle', 'WakeByAddressAll', 'RtlWakeAddressSingle', 'RtlWakeAddressAll')
PSEUDO_THREAD = 0xfffffffffffffffe


class Threads(Coll):
    name = 'threads'
    needs_history = True

    @staticmethod
    def parser(p):
        common_args(p)
        p.add_argument('--objects', type=int, default=40, help='rows of the wait/wake table')
        p.add_argument('--topn', type=int, default=6, help='functions and wait objects listed per thread')

    def __init__(self, a):
        Coll.__init__(self, a)
        self.th = collections.OrderedDict()
        self.handles = {}          # handle value -> object record (while open)
        self.addr_objs = {}        # address -> object record (condition variables, wait addresses)
        self.objs = []
        self.pending_threads = []  # CreateThread calls seen, not yet matched to a "Starting thread" line
        self.thread_of_handle = {}

    def _th(self, tid, t):
        e = self.th.get(tid)
        if e is None:
            e = self.th[tid] = dict(tid=tid, first=t, last=t, lines=0, calls=0, wcalls=0, into=0, start=None,
                                    arg=None, creator=None, names=[], sets=[], first_call=None, last_call=None,
                                    fns=collections.Counter(), waits=collections.Counter(),
                                    blocked=collections.Counter(), wakes=collections.Counter(),
                                    mods=collections.Counter(), exc=collections.Counter())
        e['last'] = t
        e['lines'] += 1
        return e

    def _obj(self, kind, t, tid, site, fn, detail):
        o = dict(id='%s#%d' % (kind, len(self.objs) + 1), kind=kind, t=t, tid=tid, site=site, fn=fn,
                 detail=detail, waiters=collections.Counter(), wakers=collections.Counter(),
                 blocked=0.0)
        self.objs.append(o)
        return o

    def _lookup(self, fn, v):
        """The object a wait or wake names: by handle, or by address for condition variables."""
        if v is None:
            return None
        if fn in ADDRESS_OBJ:
            o = self.addr_objs.get(v)
            if o is None:
                o = self.addr_objs[v] = self._obj('cv' if 'Condition' in fn else 'addr', None, None, None, None, '')
            return o
        o = self.handles.get(v)
        if o is None:
            o = self.handles[v] = self._obj('handle', None, None, None, None, 'creator not in the log')
        return o

    def call(self, r):
        e = self._th(r.tid, r.t)
        if r.kind not in ('call', 'snoop'):
            e['into'] += 1
            return
        e['calls'] += 1
        if not self.wanted(r):
            return
        name = r.fn.split('.', 1)[-1]
        inside = self.inside(r.t)
        if inside:
            e['wcalls'] += 1
            e['fns'][r.fn] += 1
            e['mods'][r.mod] += 1
            if e['first_call'] is None:
                e['first_call'] = (r.t, r.site(), r.fn)
            e['last_call'] = (r.t, r.site(), r.fn)
        if name in WAITS:
            idx = WAITS[name]
            o = None
            if idx == 0:
                o = self._lookup(name, nth_int(r.args, 0))
            elif idx == 1:
                o = self.handles.get(nth_int(r.args, 1))
            key = o['id'] if o else name
            if o is None and 'Multiple' in name:
                key = '%s(n=%s)' % (name, nth_int(r.args, 0))
            r.x[self.ix] = (key, o)
            if inside:
                e['waits'][(name, key)] += 1
                if o:
                    o['waiters'][r.tid] += 1
        if name in WAKES:
            if inside:
                if name == 'QueueUserAPC':
                    o = self.handles.get(nth_int(r.args, 1))
                else:
                    o = self._lookup(name, nth_int(r.args, 0))
                if o:
                    o['wakers'][r.tid] += 1
                    e['wakes'][(name, o['id'])] += 1
        elif name in MAKERS or name in THREAD_SET or name == 'CloseHandle':
            p = None
            if name == 'CreateThread':
                args = split_args(r.argtext())
                if len(args) >= 6:
                    p = dict(t=r.t, tid=r.tid, site=r.site(), start=arg_int(args[2]), arg=arg_int(args[3]),
                             stack=args[1], flags=args[4], handle=None, child=None)
                    self.pending_threads.append(p)
            r.x[self.ix] = (None, p)

    def ret(self, r):
        if r.x[self.ix] is None:
            return
        name = r.fn.split('.', 1)[-1]
        key, o = r.x[self.ix]
        if key is not None:
            if self.inside(r.t):
                dt = r.t_ret - r.t
                self.th[r.tid]['blocked'][(name, key)] += dt
                if o:
                    o['blocked'] += dt
            return
        args = split_args(r.argtext())
        try:
            rv = int(r.retval, 16)
        except (TypeError, ValueError):
            rv = None
        if name == 'CloseHandle':
            v = arg_int(args[0]) if args else None
            self.handles.pop(v, None)
            self.thread_of_handle.pop(v, None)
        elif name in MAKERS:
            if rv in (None, 0, 0xffffffffffffffff, 0xffffffff):
                return
            kind = MAKERS[name]
            detail = ''
            if kind in ('event', 'sem', 'mutex', 'timer'):
                s = None
                for a in args:
                    s = arg_string(a) or s
                detail = 'args(%s)' % ','.join(x.split()[0] if x.split() else '' for x in args)
                if s:
                    detail += ' name "%s"' % self.safe.text(r.fn, s)
            elif kind == 'file':
                detail = self.safe.text(r.fn, arg_string(args[0]) or '') if args else ''
            self.handles[rv] = self._obj(kind, r.t, r.tid, r.site(), r.fn, detail)
            if name == 'CreateThread' and o is not None:
                o['handle'] = rv
                self.thread_of_handle[rv] = o
        elif name in THREAD_SET:
            v = arg_int(args[0]) if args else None
            target = None
            if v == PSEUDO_THREAD or name.startswith('AvSet'):
                target = r.tid
            elif v in self.thread_of_handle:
                target = self.thread_of_handle[v].get('child')
            rest = ','.join(self.safe.text(r.fn, x) for x in args[1:])
            entry = '%.3f %s(%s) = %s by %s at %s' % (r.t, name, rest, r.retval, r.tid, r.site())
            if target is None:
                p = self.thread_of_handle.get(v)
                if p is not None:
                    p.setdefault('early', []).append(entry)     # set before the thread's first line
                else:
                    self._th(r.tid, r.t)['sets'].append(entry + ' on a handle whose thread is not known')
            else:
                self._th(target, r.t)['sets'].append(entry)

    def other(self, t, tid, rest):
        e = self._th(tid, t)
        if rest.startswith(b'Starting thread'):
            m = STARTING.match(rest)
            if m:
                e['start'] = int(m.group(1), 16)
                e['arg'] = int(m.group(2), 16)
                e['start_mod'] = self.mods.find(e['start'])     # now: the module may be unloaded later
                for p in self.pending_threads:
                    if p['child'] is None and p['start'] == e['start'] and p['arg'] == e['arg']:
                        p['child'] = tid
                        e['creator'] = p
                        e['sets'].extend(p.get('early', []))
                        self.pending_threads.remove(p)
                        break
        elif b'threadname' in rest[:20]:
            m = RENAMED.match(rest)
            if m:
                e['names'].append(unescape(m.group(1).decode('latin-1')))
        elif rest.startswith(b'trace:seh:dispatch_exception code='):
            m = EXCEPTION.match(rest)
            if m:
                e['exc'][m.group(1).decode()] += 1

    def finish(self, out, env):
        n = self.a.topn
        ea = collections.Counter()
        ea_span = {}
        out('# threads: %s; calls counted from %s; window %s (creation and handles are followed from the start)'
            % (env['log'], ','.join(sorted(self.froms)), self.a.t or 'whole run'))
        out('# %d threads logged a line. "calls": relayed calls made by the thread from any module; "counted": '
            'those from the modules above inside the window; "into": window, hook, DllMain and TLS calls on it.'
            % len(self.th))
        for tid, e in self.th.items():
            m = e.get('start_mod')
            if m is not None and m[1] == 'ea':
                ea[m[0]] += 1
                lo, hi = ea_span.get(m[0], (e['first'], e['last']))
                ea_span[m[0]] = (min(lo, e['first']), max(hi, e['last']))
                continue
            name = ('"%s"' % '", "'.join(self.safe.text('', x) for x in e['names'])) if e['names'] else '-'
            out('')
            out('thread %s  first line %.3f  last line %.3f  calls %d  counted %d  into %d  name %s'
                % (tid, e['first'], e['last'], e['calls'], e['wcalls'], e['into'], name))
            if m is not None:
                out('  start: %s+0x%x (arg %x)' % (m[0], m[2], e['arg']))
            c = e['creator']
            if c:
                out('  created: %.3f by thread %s at %s, CreateThread(stack=%s, flags=%s) = handle %x'
                    % (c['t'], c['tid'], c['site'], c['stack'], c['flags'][-8:], c['handle'] or 0))
            elif e['start'] is not None:
                out('  created: the CreateThread call is not in the log (made by a module outside the caller list)')
            for s in e['sets'][:8]:
                out('  set: ' + s)
            if len(e['sets']) > 8:
                out('  set: ... %d more' % (len(e['sets']) - 8))
            if e['exc']:
                out('  exceptions: ' + ', '.join('%s x%d' % kv for kv in e['exc'].items()))
            if e['first_call']:
                out('  first call: %.3f %s %s' % e['first_call'])
                out('  last call:  %.3f %s %s' % e['last_call'])
                out('  by module: ' + ' '.join('%s:%d' % kv for kv in e['mods'].most_common(n)))
                out('  most called: ' + ', '.join('%s x%d' % kv for kv in e['fns'].most_common(n)))
            if e['waits']:
                out('  waits: ' + '; '.join('%s on %s x%d (%.1f s inside)' % (k[0], k[1], c, e['blocked'][k])
                                            for k, c in e['waits'].most_common(n)))
            if e['wakes']:
                out('  signals: ' + '; '.join('%s %s x%d' % (k[0], k[1], c) for k, c in e['wakes'].most_common(n)))
        out('')
        for mod, k in ea.most_common():
            out('# %d threads started inside %s (EA\'s): counted, not described; the first appears at %.3f, the last '
                'ends at %.3f' % (k, mod, ea_span[mod][0], ea_span[mod][1]))
        out('# objects waited on and signalled (the %d with most waits; cv = a condition variable, by address)'
            % self.a.objects)
        out('# id\tmade\tby\twaiters thread:count\tsignallers thread:count\tseconds waited\tdetail')
        busy = [o for o in self.objs if o['waiters'] or o['wakers']]
        busy.sort(key=lambda o: -(sum(o['waiters'].values()) + sum(o['wakers'].values())))
        for o in busy[:self.a.objects]:
            made = '%.3f %s %s' % (o['t'], o['fn'], o['site']) if o['t'] is not None else '-'
            out('%s\t%s\t%s\t%s\t%s\t%.1f\t%s' % (
                o['id'], made, o['tid'] or '-',
                ' '.join('%s:%d' % kv for kv in o['waiters'].most_common(6)) or '-',
                ' '.join('%s:%d' % kv for kv in o['wakers'].most_common(6)) or '-', o['blocked'], o['detail']))
        self.tail(out)


# ------------------------------------------------------------------------------------- files
F_OPEN = ('CreateFileA', 'CreateFileW', 'CreateFile2')
F_PATH = {'GetFileAttributesA': 'attr', 'GetFileAttributesW': 'attr', 'GetFileAttributesExA': 'attr',
          'GetFileAttributesExW': 'attr', 'FindFirstFileA': 'find', 'FindFirstFileW': 'find',
          'FindFirstFileExA': 'find', 'FindFirstFileExW': 'find', 'DeleteFileA': 'delete',
          'DeleteFileW': 'delete', 'CreateDirectoryA': 'mkdir', 'CreateDirectoryW': 'mkdir',
          'RemoveDirectoryA': 'rmdir', 'RemoveDirectoryW': 'rmdir', 'MoveFileA': 'move', 'MoveFileW': 'move',
          'MoveFileExA': 'move', 'MoveFileExW': 'move', 'CopyFileA': 'copy', 'CopyFileW': 'copy',
          'SetFileAttributesA': 'setattr', 'SetFileAttributesW': 'setattr', 'PathFileExistsA': 'attr',
          'PathFileExistsW': 'attr', 'SetCurrentDirectoryA': 'chdir', 'SetCurrentDirectoryW': 'chdir',
          'LoadLibraryA': 'load', 'LoadLibraryW': 'load', 'LoadLibraryExA': 'load', 'LoadLibraryExW': 'load',
          'GetDiskFreeSpaceExA': 'disk', 'GetDiskFreeSpaceExW': 'disk', 'GetDiskFreeSpaceA': 'disk',
          'GetDiskFreeSpaceW': 'disk', 'GetVolumeInformationA': 'disk', 'GetVolumeInformationW': 'disk',
          'GetDriveTypeA': 'disk', 'GetDriveTypeW': 'disk', 'SHCreateDirectoryExA': 'mkdir',
          'SHCreateDirectoryExW': 'mkdir', 'GetFileVersionInfoSizeA': 'version',
          'GetFileVersionInfoSizeW': 'version'}
F_HANDLE = ('ReadFile', 'ReadFileEx', 'WriteFile', 'GetFileSize', 'GetFileSizeEx', 'SetFilePointer',
            'SetFilePointerEx', 'CloseHandle', 'CreateFileMappingA', 'CreateFileMappingW', 'FindClose',
            'GetFileInformationByHandle', 'GetFileInformationByHandleEx', 'GetFileType', 'FlushFileBuffers',
            'SetEndOfFile', 'GetOverlappedResult', 'CancelIo', 'CancelIoEx', 'DeviceIoControl',
            'BindIoCompletionCallback', 'FindNextFileA', 'FindNextFileW', 'GetFileTime', 'SetFileTime')
FAIL = ('ffffffffffffffff', 'ffffffff')


class Files(Coll):
    name = 'files'
    needs_history = True

    @staticmethod
    def parser(p):
        common_args(p)
        p.add_argument('--bins', help='times T1,T2,...: the bytes asked of each path are given per interval '
                                      '(before T1, T1 to T2, ..., after the last)')
        p.add_argument('--events', action='store_true', help='print each event instead of one line per path')
        p.add_argument('--grep', help='only paths matching this regex (case-insensitive)')

    def __init__(self, a):
        Coll.__init__(self, a)
        self.bins = [float(x) for x in a.bins.split(',')] if a.bins else []
        self.paths = collections.OrderedDict()
        self.handles = {}
        self.events = []
        self.grep = re.compile(a.grep, re.I) if a.grep else None
        self.prefix = None
        self.unknown_reads = [0, 0]

    def loaded(self, t, tid, path, base, cls, how):
        if self.prefix is None and path.lower().endswith('titanfall2.exe'):
            self.prefix = path[:-len('titanfall2.exe')].lower()

    def _norm(self, s):
        s = s.replace('/', '\\')
        if self.prefix and s.lower().startswith(self.prefix):
            s = '<game>\\' + s[len(self.prefix):]
        return s

    def _entry(self, path, t):
        key = path.lower()
        e = self.paths.get(key)
        if e is None:
            e = self.paths[key] = dict(path=path, first=t, ops=collections.Counter(), access=0, flags=0,
                                       disp=set(), results=collections.Counter(), size=None, reads=0,
                                       rbytes=[0] * (len(self.bins) + 1), writes=0, wbytes=0,
                                       mods=collections.Counter(), shown=False)
        return e

    def call(self, r):
        if r.kind != 'call':
            return
        name = r.fn.split('.', 1)[-1]
        if name in F_OPEN or name in F_PATH or name in F_HANDLE:
            if name in F_HANDLE or self.wanted(r):
                r.x[self.ix] = True

    def ret(self, r):
        if r.x[self.ix] is None:
            return
        name = r.fn.split('.', 1)[-1]
        rv = (r.retval or '').lower()
        ok = rv not in FAIL and rv.strip('0') != ''
        if name in F_HANDLE:
            return self._handle_op(r, name, rv, ok)
        args = split_args(r.argtext())
        s = arg_string(args[0]) if args else None
        if s is None:
            return
        path = self._norm(s)
        kind = 'open' if name in F_OPEN else F_PATH[name]
        e = self._entry(path, r.t)
        if kind == 'open':
            if len(args) >= 6:
                e['access'] |= (arg_int(args[1]) or 0) & M32
                e['disp'].add((arg_int(args[4]) or 0) & M32)
                e['flags'] |= (arg_int(args[5]) or 0) & M32
            if ok:
                self.handles[int(rv, 16)] = e
        elif kind == 'find' and ok:
            self.handles[int(rv, 16)] = None
        if name in ('GetFileAttributesA', 'GetFileAttributesW'):
            ok = rv not in FAIL            # the answer is the attributes themselves
        if self.inside(r.t):
            e['ops'][kind + (':ok' if ok else ':fail')] += 1
            e['mods'][r.mod] += 1
            e['shown'] = True
            self._event(r, kind, path, rv)

    def _handle_op(self, r, name, rv, ok):
        h = nth_int(r.args, 0)
        e = self.handles.get(h)
        if name in ('CloseHandle', 'FindClose'):
            self.handles.pop(h, None)
            return
        if e is None:
            if name in ('ReadFile', 'ReadFileEx') and h not in self.handles and self.inside(r.t) and self.wanted(r):
                self.unknown_reads[0] += 1
                self.unknown_reads[1] += (nth_int(r.args, 2) or 0) & M32
            return
        if not self.inside(r.t):
            return
        e['shown'] = True
        if name in ('ReadFile', 'ReadFileEx'):
            n = (nth_int(r.args, 2) or 0) & M32
            e['reads'] += 1
            e['rbytes'][bisect.bisect_right(self.bins, r.t)] += n
            e['mods'][r.mod] += 1
            if self.a.events:
                self._event(r, 'read', e['path'], '%s bytes asked = %s' % (n, rv))
        elif name == 'WriteFile':
            e['writes'] += 1
            e['wbytes'] += (nth_int(r.args, 2) or 0) & M32
            e['mods'][r.mod] += 1
        elif name == 'GetFileSize' and ok:
            e['size'] = int(rv, 16)
        elif name in ('CreateFileMappingA', 'CreateFileMappingW'):
            e['ops']['map' + (':ok' if ok else ':fail')] += 1
        else:
            e['ops'][name] += 1

    def _event(self, r, kind, path, result):
        if self.a.events and (self.grep is None or self.grep.search(path)):
            self.events.append('%9.3f %s %s %s %s = %s' % (r.t, r.tid, r.site(), kind,
                                                          self.safe.text(r.fn, path), result))

    def finish(self, out, env):
        out('# files: %s; from %s; window %s; bins %s' % (env['log'], ','.join(sorted(self.froms)),
                                                        self.a.t or 'whole run', self.a.bins or 'none'))
        if self.a.events:
            out('# time thread caller+rva what path = result')
            for line in self.events:
                out(line)
            return self.tail(out)
        shown = [e for e in self.paths.values() if e['shown'] and (self.grep is None or self.grep.search(e['path']))]
        out('# %d paths. <game> is the directory of the executable. bytes_asked: the sum of the sizes asked of'
            ' ReadFile and ReadFileEx on handles opened on the path (what was asked, not what came back),'
            ' one figure per bin.' % len(shown))
        out('# ReadFile calls on handles whose opening is not in the log: %d calls, %d bytes asked'
            % tuple(self.unknown_reads))
        out('# first_time\tpath\toperations\taccess\tcreate\tflags\tsize\treads\tbytes_asked\twrites\tbytes_written\tmodules')
        for e in shown:
            acc = ''.join(c for bit, c in ((0x80000000, 'R'), (0x40000000, 'W'), (0x10000000, 'A')) if e['access'] & bit) \
                or ('0x%x' % e['access'] if e['access'] else '-')
            fl = '+'.join(nm for bit, nm in ((0x40000000, 'overlapped'), (0x20000000, 'nobuffer'),
                                             (0x10000000, 'random'), (0x08000000, 'sequential'))
                          if e['flags'] & bit) or '-'
            out('%.3f\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%d\t%d\t%s' % (
                e['first'], self.safe.text('', e['path']),
                ' '.join('%s:%d' % (k.replace(':ok', '').replace(':fail', '-FAIL'), c) for k, c in sorted(e['ops'].items())),
                acc, ','.join(str(d) for d in sorted(x for x in e['disp'] if x is not None)) or '-', fl,
                e['size'] if e['size'] is not None else '-', e['reads'], '/'.join(str(b) for b in e['rbytes']),
                e['writes'], e['wbytes'], ' '.join('%s:%d' % kv for kv in e['mods'].most_common(4))))
        self.tail(out)


# ------------------------------------------------------------------------------------ phases
# (label, function without its dll or None, regex on "function(args)" or None, calling module or None).
# A landmark with no function is tried on every call whose arguments hold LITERALS[label] (lower case).
LITERALS = {
    'vpk\\enable.txt asked for (step 21)': b'enable.txt',
    'first open of patch_master.rpak (step 9)': b'patch_master',
    'first .bik opened (start-up videos)': b'.bik',
    'first menu video opened (main menu)': b'menu_',
    'first savegame file touched': b'savegame',
    'first sp_ pak or map file opened (mission load)': b'sp_',
}
LANDMARKS = [
    ('launcher: GetCommandLineA (step 1)', 'GetCommandLineA', None, 'launcher'),
    ('launcher: WSAStartup (step 3)', 'WSAStartup', None, 'launcher'),
    ('singleton mutex (step 4)', 'CreateMutexA', r'titanfall', None),
    ('SetThreadExecutionState (step 5)', 'SetThreadExecutionState', None, None),
    ('launcher: SetCurrentDirectoryA (step 5)', 'SetCurrentDirectoryA', None, 'launcher'),
    ('tier0: first CreateThread (step 7, the job threads)', 'CreateThread', None, 'tier0'),
    ('rtech_game: first CreateThread (step 9, the pak loader)', 'CreateThread', None, 'rtech_game'),
    ('first open of patch_master.rpak (step 9)', None, r'(?i)Create\w+\(.*patch_master', None),
    ('CoInitialize (step 15)', 'CoInitialize', None, None),
    ('CoInitializeEx (step 15)', 'CoInitializeEx', None, None),
    ('materialsystem: first CreateThread (step 19, the render thread)', 'CreateThread', None, 'materialsystem_dx11'),
    ('GameInfo.txt asked for (step 21)', 'GetFileAttributesExW', r'(?i)gameinfo\.txt', None),
    ('vpk\\enable.txt asked for (step 21)', None, r'(?i)enable\.txt', None),
    ('RegisterClassExW (step 24)', 'RegisterClassExW', None, None),
    ('RegisterClassW', 'RegisterClassW', None, None),
    ('RegisterClassExA', 'RegisterClassExA', None, None),
    ('CreateWindowExW by the engine (step 24, W)', 'CreateWindowExW', None, 'engine'),
    ('CreateWindowExA', 'CreateWindowExA', None, None),
    ('first ShowWindow', 'ShowWindow', None, None),
    ('first SetWindowPos', 'SetWindowPos', None, None),
    ('first PeekMessageW by the engine (the message pump)', 'PeekMessageW', None, 'engine'),
    ('first PeekMessageA', 'PeekMessageA', None, None),
    ('first GetMessageW', 'GetMessageW', None, None),
    ('first XInputGetCapabilities', 'XInputGetCapabilities', None, None),
    ('first XInputGetState', 'XInputGetState', None, None),
    ('first socket', 'socket', None, None),
    ('first bind', 'bind', None, None),
    ('first .bik opened (start-up videos)', None, r'(?i)Create\w+\(.*\.bik"', None),
    ('first menu video opened (main menu)', None, r'(?i)Create\w+\(.*menu_\w+\.bik"', None),
    ('first savegame file touched', None, r'(?i)savegame', None),
    ('first sp_ pak or map file opened (mission load)', None, r'(?i)Create\w+\(.*\\\\(sp_\w+)\.(rpak|bsp|stbsp)', None),
    ('PostQuitMessage', 'PostQuitMessage', None, None),
    ('DestroyWindow', 'DestroyWindow', None, None),
    ('WSACleanup', 'WSACleanup', None, None),
    ('ExitProcess', 'ExitProcess', None, None),
    ('TerminateProcess', 'TerminateProcess', None, None),
]


class Phases(Coll):
    name = 'phases'

    @staticmethod
    def parser(p):
        common_args(p, window=False)
        p.add_argument('--mark', action='append', default=[],
                       help='a further landmark: LABEL=FUNCTION[@MODULE][:REGEX on function(args)]; '
                            'FUNCTION may be * for any')
        p.add_argument('--width', type=int, default=150)

    def __init__(self, a):
        a.frm = 'all'
        Coll.__init__(self, a)
        self.by_fn = collections.defaultdict(list)
        self.any = []
        marks = list(LANDMARKS)
        for m in a.mark:
            label, _, spec = m.partition('=')
            head, _, rx = spec.partition(':')
            fn, _, mod = head.partition('@')
            marks.append((label, None if fn in ('*', '') else fn, rx or None, mod.lower() or None))
        for label, fn, rx, mod in marks:
            item = [label, re.compile(rx) if rx else None, mod, LITERALS.get(label)]
            (self.by_fn[fn] if fn else self.any).append(item)
        self.rows = []
        self.first_from = {}
        self.first_into = {}
        self.names = set()
        self.exc = collections.Counter()
        self.last = 0.0
        self.end_line = None

    def _row(self, t, label, tid, site, text):
        self.rows.append((t, label, tid, site, text))

    def call(self, r):
        self.last = r.t
        if r.cls == 'ea':
            if ('ea', r.mod) not in self.first_from:
                self.first_from[('ea', r.mod)] = 1
                self._row(r.t, 'first relayed call by or into %s (EA\'s: not shown)' % r.mod, r.tid, '-', '')
            return
        if r.kind in ('call', 'snoop'):
            if r.cls == 'game' and r.mod not in self.first_from:
                self.first_from[r.mod] = 1
                self._row(r.t, 'first call made by %s' % r.mod, r.tid, r.site(), self._text(r))
        elif r.cls == 'game':
            key = (r.mod, r.fn)
            if key not in self.first_into:
                self.first_into[key] = 1
                self._row(r.t, 'first %s call into %s' % (r.fn, r.mod), r.tid, r.site(), self._text(r))
        name = r.fn.split('.', 1)[-1]
        todo = self.by_fn.get(name)
        if todo:
            self._try(todo, r)
        if self.any:
            self._try(self.any, r, r.args.lower())

    def _try(self, todo, r, low=None):
        text = None
        for item in todo[:]:
            label, rx, mod, lit = item
            if mod is not None and r.mod != mod:
                continue
            if lit is not None and low is not None and lit not in low:
                continue
            if rx is not None:
                if text is None:
                    text = '%s(%s)' % (r.fn, r.argtext())
                if not rx.search(text):
                    continue
            todo.remove(item)
            self._row(r.t, label, r.tid, r.site(), self._text(r))

    def _text(self, r):
        args = r.argtext()
        if len(args) > self.a.width:
            args = args[:self.a.width] + '...'
        return '%s(%s)' % (r.fn, self.safe.text(r.fn, args))

    def loaded(self, t, tid, path, base, cls, how):
        self.last = t
        name = short_name(path)
        if cls == 'ea':
            self._row(t, 'module loaded: %s (EA\'s)' % name, tid, '-', how)
        elif cls == 'game':
            self._row(t, 'module loaded: %s' % name, tid, '-', '%s at %x' % (how, base))
        elif name in ('d3d11', 'dxgi', 'xinput1_3', 'dsound', 'xaudio2_6', 'xaudio2_7', 'winmm', 'ws2_32',
                      'dinput8', 'imm32', 'd3dcompiler_46', 'd3dcompiler_47', 'msvcr110', 'msvcp110', 'user32',
                      'gdi32', 'iphlpapi', 'mmdevapi', 'hid', 'setupapi', 'steamclient64', 'winevulkan'):
            self._row(t, 'system module loaded: %s' % name, tid, '-', how)

    def other(self, t, tid, rest):
        self.last = t
        if b'threadname' in rest[:20]:
            m = RENAMED.match(rest)
            if m:
                name = unescape(m.group(1).decode('latin-1'))
                if name not in self.names:
                    self.names.add(name)
                    self._row(t, 'thread named "%s"' % self.safe.text('', name), tid, '-', '')
        elif rest.startswith(b'trace:seh:dispatch_exception code='):
            code = EXCEPTION.match(rest).group(1).decode()
            self.exc[code] += 1
            if self.exc[code] == 1:
                self._row(t, 'first exception %s' % code, tid, '-', '')
        elif rest.startswith(b'trace:loaddll') and b'Unloaded module' in rest:
            m = UNLOADED.match(rest)
            if m:
                path = m.group(1).decode('latin-1')
                if mod_class(path) != 'sys':
                    self._row(t, 'module unloaded: %s' % short_name(path), tid, '-', '')
        elif rest.startswith(b'trace:process:NtTerminateProcess'):
            self._row(t, 'NtTerminateProcess (the process ends)', tid, '-', '')

    def finish(self, out, env):
        out('# phases: %s; time zero is the first line of process %s (absolute %.3f)' % (env['log'], env['pid'], env['t0']))
        out('# time\tlandmark\tthread\tsite\tcall')
        for t, label, tid, site, text in sorted(self.rows, key=lambda x: x[0]):
            out('%.3f\t%s\t%s\t%s\t%s' % (t, label, tid, site, text))
        out('%.3f\tlast line of the process\t\t\t' % self.last)
        if self.exc:
            out('# exceptions dispatched: ' + ', '.join('%s x%d' % kv for kv in sorted(self.exc.items())))
        left = [i[0] for todo in list(self.by_fn.values()) + [self.any] for i in todo]
        if left:
            out('# landmarks never seen: ' + '; '.join(left))
        self.tail(out)


# -------------------------------------------------------------------------------- exceptions
EXC_LINE = re.compile(rb'^trace:seh:dispatch_exception code=([0-9a-fA-F]+)(?: \((\w+)\))? flags=([0-9a-fA-F]+) addr=([0-9a-fA-F]+)')
EXC_HANDLER = re.compile(rb'^trace:seh:call_\w+ calling (\w+ ?\w*) handler ([0-9A-Fa-f]+)')
EXC_UNWIND = re.compile(rb'^trace:seh:RtlUnwindEx code=([0-9a-fA-F]+) flags=([0-9a-fA-F]+) end_frame=\w+ target_ip=([0-9A-Fa-f]+)')
EXC_KINDS = {'e06d7363': 'a C++ throw', '40010006': 'a debugger message (OutputDebugString)',
             '4001000a': 'a debugger message (wide)', '406d1388': 'a debugger message (thread naming)',
             'c0000005': 'an access violation', '80000003': 'a breakpoint', 'c000001d': 'an illegal instruction',
             'c0000094': 'an integer division by zero', 'c00000fd': 'a stack overflow', '80000001': 'a guard page'}


class Exceptions(Coll):
    """Every exception dispatched in the process (the +seh channel): code, where it was raised,
    which handlers were called and where the unwind went.  Addresses inside EA's modules are
    given as the module's name only."""
    name = 'exceptions'

    @staticmethod
    def parser(p):
        common_args(p)

    def __init__(self, a):
        a.frm = 'all'
        Coll.__init__(self, a)
        self.rows = []
        self.open = {}

    def _where(self, addr):
        mod, cls, rva = self.mods.find(addr)
        return ('%s (EA\'s)' % mod) if cls == 'ea' else '%s+0x%x' % (mod, rva)

    def other(self, t, tid, rest):
        if not rest.startswith(b'trace:seh:'):
            return
        m = EXC_LINE.match(rest)
        if m:
            if not self.inside(t):
                self.open.pop(tid, None)
                return
            e = dict(t=t, tid=tid, code=m.group(1).decode().lower(), name=(m.group(2) or b'').decode(),
                     flags=m.group(3).decode(), where=self._where(int(m.group(4), 16)), handlers=[], unwind=None)
            self.rows.append(e)
            self.open[tid] = e
            return
        e = self.open.get(tid)
        if e is None:
            return
        m = EXC_HANDLER.match(rest)
        if m:
            e['handlers'].append('%s handler %s' % (m.group(1).decode(), self._where(int(m.group(2), 16))))
            return
        m = EXC_UNWIND.match(rest)
        if m:
            e['unwind'] = 'unwound to %s' % self._where(int(m.group(3), 16))

    def finish(self, out, env):
        out('# exceptions: %s; window %s; every "dispatch_exception code=" line of process %s' % (
            env['log'], self.a.t or 'whole run', env['pid']))
        by = collections.OrderedDict()
        for e in self.rows:
            by.setdefault(e['code'], []).append(e)
        out('# %d exceptions of %d codes' % (len(self.rows), len(by)))
        out('# code\tkind\tcount\tfirst_time\tfirst_thread\traised at (first)')
        for code, es in by.items():
            out('%s\t%s\t%d\t%.3f\t%s\t%s' % (code, EXC_KINDS.get(code, es[0]['name'] or 'other'), len(es),
                                             es[0]['t'], es[0]['tid'], es[0]['where']))
        out('# each one (the first 200): time thread code flags raised_at | handlers called | unwind')
        for e in self.rows[:200]:
            out('%.3f\t%s\t%s\t%s\t%s\t%s\t%s' % (e['t'], e['tid'], e['code'], e['flags'], e['where'],
                                                 '; '.join(e['handlers'][:4]) or 'no handler line',
                                                 e['unwind'] or 'no unwind line'))
        self.tail(out)


# ------------------------------------------------------------------------------------- index
def do_index(a, out):
    pid_arg = a.pid.encode() if a.pid and a.pid != 'auto' else None
    pids = {}
    loads = collections.defaultdict(list)
    offsets = []
    special = collections.defaultdict(collections.Counter)
    last_sec = -1
    off = 0
    lines = 0
    with open(a.log, 'rb', buffering=1 << 22) as f:
        for line in f:
            lines += 1
            here = off
            off += len(line)
            p = line.split(b':', 3)
            if len(p) != 4:
                continue
            ts = p[0]
            try:
                sec = int(ts[:-4])
            except ValueError:
                continue
            if sec > last_sec:
                last_sec = sec
                offsets.append([sec, here])
            pid = p[1]
            e = pids.get(pid)
            if e is None:
                e = pids[pid] = [float(ts), float(ts), 0, 0, '']
            e[1] = ts
            e[2] += 1
            rest = p[3]
            c = rest[:5]
            if c == b'Call ' or c == b'CALL ':
                e[3] += 1
                for prefix, label in SPECIAL:
                    if rest.startswith(prefix, 5):
                        special[pid][label] += 1
                        break
                else:
                    if c == b'CALL ':
                        special[pid]['(snoop)'] += 1
            elif rest.startswith(b'trace:loaddll'):
                m = LOADED.match(rest)
                if m:
                    path = m.group(1).decode('latin-1')
                    if not e[4]:
                        e[4] = short_name(path)
                    loads[pid].append([float(ts), int(m.group(2), 16), path, m.group(3).decode()])
                else:
                    m = UNLOADED.match(rest)
                    if m:
                        loads[pid].append([float(ts), -1, m.group(1).decode('latin-1'), 'unloaded'])
    game = pid_arg
    if game is None:
        for pid, e in pids.items():
            if e[4] == 'titanfall2.exe':
                game = pid
    res = dict(log=a.log, bytes=off, lines=lines, pid=game.decode() if game else None,
               processes=[dict(pid=pid.decode(), first=e[0], last=float(e[1]), lines=e[2], calls=e[3], image=e[4])
                          for pid, e in sorted(pids.items(), key=lambda kv: kv[1][0])])
    if game and game in pids:
        res['t0'] = pids[game][0]
        res['t_end'] = float(pids[game][1])
        res['mods'] = [[t, base, short_name(path), mod_class(path), how] for t, base, path, how in loads[game]]
        res['game_dir_modules'] = [short_name(path) for t, base, path, how in loads[game]
                                   if base >= 0 and mod_class(path) != 'sys']
        res['special_calls'] = dict(special[game])
        res['offsets'] = offsets
    out(json.dumps(res))


def make_ctx(index):
    """The part of an index the other commands take as --ctx (compressed, base64)."""
    small = dict(t0=index['t0'], mods=index['mods'], offsets=index['offsets'])
    return base64.urlsafe_b64encode(zlib.compress(json.dumps(small, separators=(',', ':')).encode(), 9)).decode()


# -------------------------------------------------------------------------------------- scan
def scan(log, pid, colls, ctx=None, out=print):
    pid = pid.encode()
    mods = Mods()
    t0 = ctx['t0'] if ctx else None
    start_off = 0
    lo = None if any(c.t_lo is None for c in colls) else min(c.t_lo for c in colls)
    hi = None if any(c.t_hi is None for c in colls) else max(c.t_hi for c in colls)
    if ctx and lo is not None and lo > 2 and not any(c.needs_history for c in colls):
        target = t0 + lo - 1.5
        sec = None
        for s, o in ctx['offsets']:
            if s <= target:
                sec, start_off = s, o
            else:
                break
        if sec is not None:
            for t, base, name, cls, how in ctx['mods']:
                if t < sec:
                    if base < 0:
                        mods.unload(name)
                    else:
                        mods.load(base, name, cls)
    stacks = {}
    names = {}
    tids = {}
    find = mods.find
    f = open(log, 'rb', buffering=1 << 22)
    if start_off:
        f.seek(start_off)
    stop_at = None if hi is None else hi + 1.0
    done_check = all(isinstance(co, Calls) for co in colls)
    ncolls = len(colls)
    for i, co in enumerate(colls):
        co.ix = i
        co.mods = mods
    t = 0.0
    for line in f:
        p = line.split(b':', 3)
        if len(p) != 4 or p[1] != pid:
            continue
        rest = p[3]
        c = rest[:5]
        if c == b'Call ' or c == b'CALL ':
            try:
                t = float(p[0])
            except ValueError:
                continue
            if t0 is None:
                t0 = t
            t -= t0
            if stop_at is not None and t > stop_at:
                break
            body = rest[5:]
            par = body.find(b'(')
            q = body.rfind(b' ret=')
            name = body[:par]
            r = Rec()
            r.t = t
            tid = tids.get(p[2])
            if tid is None:
                tid = tids[p[2]] = p[2].decode()
            r.tid = tid
            r.raw = name
            r.retval = None
            r.t_ret = None
            r.x = [None] * ncolls
            if q < 0 or b' ' in name:
                for prefix, label in SPECIAL:
                    if body.startswith(prefix):
                        break
                else:
                    continue
                r.fn = label
                r.kind = label
                if label in ('(wndproc)', '(hookproc)'):
                    try:
                        r.ra = int(body[len(prefix):par], 16)
                    except ValueError:
                        continue
                else:
                    m = PROC_ARG.search(body)
                    r.ra = int(m.group(1), 16) if m else 0
                end = body.rfind(b')')
                r.args = body[par + 1:end if end > par else None]
            else:
                fn = names.get(name)
                if fn is None:
                    d, _, rest_name = name.decode('latin-1').partition('.')
                    fn = names[name] = d.lower() + '.' + rest_name
                r.fn = fn
                r.kind = 'call' if c == b'Call ' else 'snoop'
                try:
                    r.ra = int(body[q + 5:], 16)
                except ValueError:
                    continue
                r.args = body[par + 1:q - 1]
            r.mod, r.cls, r.rva = find(r.ra)
            st = stacks.get(tid)
            if st is None:
                st = stacks[tid] = []
            r.depth = len(st)
            st.append(r)
            for co in colls:
                co.call(r)
        elif c == b'Ret  ' or c == b'RET  ':
            st = stacks.get(tids.get(p[2]))
            if not st:
                continue
            body = rest[5:]
            name = body[:body.find(b'(')]
            i = len(st) - 1
            while i >= 0 and st[i].raw != name:
                i -= 1
            if i < 0:
                continue
            r = st[i]
            del st[i:]
            rv = body.rfind(b' retval=')
            if rv >= 0:
                end = body.find(b' ', rv + 8)
                r.retval = body[rv + 8:end if end > 0 else None].strip().decode('latin-1')
            else:
                r.retval = ''
            t = float(p[0]) - t0
            r.t_ret = t
            for co in colls:
                co.ret(r)
        else:
            try:
                t = float(p[0])
            except ValueError:
                continue
            if t0 is None:
                t0 = t
            t -= t0
            tid = tids.get(p[2])
            if tid is None:
                tid = tids[p[2]] = p[2].decode()
            if rest.startswith(b'trace:loaddll'):
                m = LOADED.match(rest)
                if m:
                    path = m.group(1).decode('latin-1')
                    cls = mod_class(path)
                    base = int(m.group(2), 16)
                    mods.load(base, short_name(path), cls)
                    for co in colls:
                        co.loaded(t, tid, unescape(path), base, cls, m.group(3).decode())
                else:
                    m = UNLOADED.match(rest)
                    if m:
                        mods.unload(short_name(m.group(1).decode('latin-1')))
            for co in colls:
                co.other(t, tid, rest)
        if done_check and all(co.finished(t) for co in colls):
            break
    f.close()
    env = dict(log=log, pid=pid.decode(), t0=t0 or 0.0, mods=mods)
    for co in colls:
        if len(colls) > 1:
            out('##### %s' % ' '.join([co.name] + getattr(co.a, 'argv', [])))
        co.finish(out, env)


COLLECTORS = {'calls': Calls, 'census': Census, 'threads': Threads, 'files': Files, 'phases': Phases,
              'exceptions': Exceptions}


def load_ctx(text):
    if not text:
        return None
    if text.startswith('@'):
        with open(text[1:]) as f:
            index = json.load(f)
        return dict(t0=index['t0'], mods=index['mods'], offsets=index['offsets'])
    return json.loads(zlib.decompress(base64.urlsafe_b64decode(text.encode())).decode())


def sub_parser(name):
    p = argparse.ArgumentParser(prog='relay.py ' + name, description=COLLECTORS[name].__doc__ or name)
    COLLECTORS[name].parser(p)
    return p


def main(argv, out=print):
    if not argv or argv[0] in ('-h', '--help'):
        out(__doc__)
        return 0
    cmd = argv[0]
    if cmd not in COLLECTORS and cmd not in ('index', 'multi'):
        out('relay.py: unknown subcommand %r (index, calls, census, threads, files, phases, exceptions, multi)' % cmd)
        return 2
    if cmd in COLLECTORS and (len(argv) < 2 or argv[1] in ('-h', '--help')):
        sub_parser(cmd).print_help()
        return 0
    if len(argv) < 2:
        out('relay.py %s: the log comes first, then the options' % cmd)
        return 2
    top = argparse.ArgumentParser(prog='relay.py %s LOG' % cmd, add_help=False)
    top.add_argument('--pid', default='auto', help='the process (hex, as in the log); the index names the game\'s')
    top.add_argument('--ctx', help='from an index: base64 text, or @file.json (a local index file)')
    groups, cur = [], []
    for x in argv[2:]:
        if x == '--' and cmd == 'multi':
            groups.append(cur)
            cur = []
        else:
            cur.append(x)
    groups.append(cur)
    a, rest = top.parse_known_args(groups[0])
    a.log = argv[1]
    if cmd == 'index':
        do_index(a, out)
        return 0
    if cmd == 'multi':
        colls = []
        for g in groups[1:]:
            if g:
                ca = sub_parser(g[0]).parse_args(g[1:])
                ca.argv = g[1:]
                colls.append(COLLECTORS[g[0]](ca))
    else:
        colls = [COLLECTORS[cmd](sub_parser(cmd).parse_args(rest))]
    if a.pid == 'auto':
        out('relay.py: give --pid (the index subcommand names the game\'s process)')
        return 2
    scan(a.log, a.pid, colls, load_ctx(a.ctx), out)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except BrokenPipeError:
        sys.exit(0)
