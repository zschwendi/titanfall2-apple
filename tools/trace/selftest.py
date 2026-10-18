#!/usr/bin/env python3
"""Self-test of tools/trace/relay.py on a small made-up log (no game data, no real log).

    python3 tools/trace/selftest.py        exit 0 and one line when every check holds

The log below is invented: the addresses, names and strings are this file's own.  It has two
processes, a module unloaded and another loaded at its base, nested calls (a window procedure
called inside CreateWindowExW), a thread created and named, a wait and its signal, files opened
and read, a call made by a module of EA's, and one line for each rule of --commit-safe.
"""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import relay  # noqa: E402

G = r'S:\\steamapps\\common\\Titanfall2'
LOG = r"""
======================
Options: {'made', 'up'}
1000.000:0bbb:0001:trace:loaddll:build_module Loaded L"C:\\Program Files\\Electronic Arts\\EA Desktop\\EADesktop.exe" at 0000000140000000: native
1000.010:0bbb:0001:Call KERNEL32.GetCommandLineW() ret=140001000
1000.010:0bbb:0001:Ret  KERNEL32.GetCommandLineW() retval=00001000 ret=140001000
1000.100:0aaa:0010:trace:seh:install_bpf Seccomp filters already installed.
1000.200:0aaa:0010:trace:loaddll:build_module Loaded L"GAME\\Titanfall2.exe" at 0000000140000000: native
1000.201:0aaa:0010:trace:loaddll:build_ntdll_module Loaded L"C:\\windows\\system32\\ntdll.dll" at 00006FFFFFF30000: builtin
1000.202:0aaa:0010:trace:loaddll:build_module Loaded L"C:\\windows\\system32\\kernel32.dll" at 00006FFFFFEB0000: builtin
1000.203:0aaa:0010:trace:loaddll:build_module Loaded L"C:\\windows\\system32\\user32.dll" at 00006FFFFE700000: builtin
1000.300:0aaa:0010:Call PE DLL (proc=00006FFFFFF42EB0,module=00006FFFFFF30000 L"ntdll.dll",reason=PROCESS_ATTACH,res=0000000000000000)
1000.301:0aaa:0010:Ret  PE DLL (proc=00006FFFFFF42EB0,module=00006FFFFFF30000 L"ntdll.dll",reason=PROCESS_ATTACH,res=0000000000000000) retval=1
1000.400:0aaa:0010:Call KERNEL32.GetProcAddress(6fffffeb0000,140010a28 "D3D11CreateDeviceAndSwapChain") ret=140008ce0
1000.401:0aaa:0010:Ret  KERNEL32.GetProcAddress() retval=6fffffeb6fb0 ret=140008ce0
1000.500:0aaa:0010:trace:loaddll:build_module Loaded L"GAME\\bin\\x64_retail\\launcher.dll" at 00006FFFE9930000: native
1000.501:0aaa:0010:trace:loaddll:build_module Loaded L"GAME\\bin\\x64_retail\\tier0.dll" at 00006FFFE9600000: native
1000.502:0aaa:0010:trace:loaddll:build_module Loaded L"GAME\\bin\\x64_retail\\OriginSDK.dll" at 00006FFFE9000000: native
1000.503:0aaa:0010:trace:loaddll:build_module Loaded L"GAME\\bin\\x64_retail\\engine.dll" at 0000000180000000: native
1001.050:0aaa:0010:CALL launcher.LauncherMain(140000000,00000000,00000000,00000001) ret=140001234
1001.051:0aaa:0010:RET  launcher.LauncherMain() retval=00000000 ret=140001234
1001.100:0aaa:0010:Call KERNEL32.GetCommandLineA() ret=6fffe993cf10
1001.100:0aaa:0010:Ret  KERNEL32.GetCommandLineA() retval=01000000 ret=6fffe993cf10
1001.100:0aaa:0010:Call KERNEL32.CreateMutexA(00000000,00000000,6fffe9990000 "titanfall_singleton_mutex") ret=6fffe993d000
1001.101:0aaa:0010:Ret  KERNEL32.CreateMutexA() retval=00000044 ret=6fffe993d000
1001.150:0aaa:0010:Call KERNEL32.InitializeCriticalSectionAndSpinCount(6fffe97cab40,00000fa0) ret=6fffe965ab43
1001.150:0aaa:0010:Ret  KERNEL32.InitializeCriticalSectionAndSpinCount() retval=00000001 ret=6fffe965ab43
1001.151:0aaa:0010:Call KERNEL32.InitializeCriticalSectionAndSpinCount(6fffe97cab68,00000fa0) ret=6fffe965ab43
1001.151:0aaa:0010:Ret  KERNEL32.InitializeCriticalSectionAndSpinCount() retval=00000001 ret=6fffe965ab43
1001.152:0aaa:0010:Call KERNEL32.InitializeCriticalSectionAndSpinCount(6fffe97cab90,00000fa0) ret=6fffe965ab43
1001.152:0aaa:0010:Ret  KERNEL32.InitializeCriticalSectionAndSpinCount() retval=00000001 ret=6fffe965ab43
1001.200:0aaa:0010:Call KERNEL32.CreateEventA(00000000,00000000,00000000,00000000) ret=6fffe9601000
1001.200:0aaa:0010:Ret  KERNEL32.CreateEventA() retval=00000050 ret=6fffe9601000
1001.300:0aaa:0010:Call KERNEL32.CreateThread(00000000,00010000,6fffe964b890,041d52c0,00000000,06cece60) ret=6fffe964b677
1001.301:0aaa:0010:Ret  KERNEL32.CreateThread() retval=00000054 ret=6fffe964b677
1001.310:0aaa:0020:Starting thread proc 00006FFFE964B890 (arg=00000000041D52C0)
1001.320:0aaa:0020:Call KERNEL32.SetThreadPriority(fffffffffffffffe,00000002) ret=6fffe964b900
1001.320:0aaa:0020:Ret  KERNEL32.SetThreadPriority() retval=00000001 ret=6fffe964b900
1001.330:0aaa:0020:Call KERNEL32.WaitForSingleObject(00000050,ffffffff) ret=6fffe964b950
1001.500:0aaa:0010:Call KERNEL32.SetEvent(00000050) ret=6fffe9601100
1001.500:0aaa:0010:Ret  KERNEL32.SetEvent() retval=00000001 ret=6fffe9601100
1001.530:0aaa:0020:Ret  KERNEL32.WaitForSingleObject() retval=00000000 ret=6fffe964b950
1001.600:0aaa:0020:warn:threadname:NtSetInformationThread Thread renamed to L"worker"
1002.000:0aaa:0010:Call KERNEL32.GetFileAttributesExW(180300000 L"R2\\GameInfo.txt",00000000,008cd000) ret=1801c7b00
1002.000:0aaa:0010:Ret  KERNEL32.GetFileAttributesExW() retval=00000001 ret=1801c7b00
1002.100:0aaa:0010:Call KERNEL32.CreateFileW(180300100 L"GAME\\vpk\\enable.txt",80000000,00000001,00000000,6fff00000003,6fff00000080,00000000) ret=1801c7c00
1002.100:0aaa:0010:Ret  KERNEL32.CreateFileW() retval=00000060 ret=1801c7c00
1002.200:0aaa:0010:Call KERNEL32.ReadFile(00000060,008cd100,6fff00000004,008cd200,00000000) ret=1801c7c40
1002.200:0aaa:0010:Ret  KERNEL32.ReadFile() retval=00000001 ret=1801c7c40
1002.300:0aaa:0010:Call KERNEL32.CloseHandle(00000060) ret=1801c7c80
1002.300:0aaa:0010:Ret  KERNEL32.CloseHandle() retval=00000001 ret=1801c7c80
1002.400:0aaa:0010:Call KERNEL32.CreateFileW(180300200 L"GAME\\r2\\cfg\\missing, really.cfg",80000000,00000001,00000000,00000003,00000080,00000000) ret=1801c7c00
1002.400:0aaa:0010:Ret  KERNEL32.CreateFileW() retval=ffffffffffffffff ret=1801c7c00
1003.000:0aaa:0010:Call user32.RegisterClassExW(008cd300) ret=1801cd3cf
1003.000:0aaa:0010:Ret  user32.RegisterClassExW() retval=0000c123 ret=1801cd3cf
1003.100:0aaa:0010:Call user32.CreateWindowExW(00000000,1803a0000 L"Respawn001",1803a0020 L"A made-up title",80ca0000,00000000,00000000,00000780,00000438,00000000,00000000,140000000,00000000) ret=1801cd500
1003.110:0aaa:0010:Call window proc 00000001801CD9D0 (hwnd=000000000002012E,msg=WM_NCCREATE,wp=00000000,lp=008cd400)
1003.120:0aaa:0010:Call KERNEL32.GetTickCount() ret=1801cda00
1003.120:0aaa:0010:Ret  KERNEL32.GetTickCount() retval=00001234 ret=1801cda00
1003.130:0aaa:0010:Ret  window proc 00000001801CD9D0 (hwnd=000000000002012E,msg=WM_NCCREATE,wp=00000000,lp=008cd400) retval=00000001
1003.200:0aaa:0010:Ret  user32.CreateWindowExW() retval=0002012e ret=1801cd500
1004.000:0aaa:0010:Call KERNEL32.GetEnvironmentVariableA(180400000 "MadeUpAuthCode",008cd500,00000100) ret=6fffe9602000
1004.000:0aaa:0010:Ret  KERNEL32.GetEnvironmentVariableA() retval=00000000 ret=6fffe9602000
1004.100:0aaa:0010:Call KERNEL32.lstrcpyA(008cd600,008cd700 "someone@example.com") ret=6fffe9602100
1004.100:0aaa:0010:Ret  KERNEL32.lstrcpyA() retval=008cd600 ret=6fffe9602100
1004.200:0aaa:0010:Call KERNEL32.lstrlenA(008cd800 "id=QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVo0NTY3ODk=") ret=6fffe9602200
1004.200:0aaa:0010:Ret  KERNEL32.lstrlenA() retval=0000002f ret=6fffe9602200
1004.300:0aaa:0010:Call KERNEL32.lstrlenA(008cd900 "englishclient_frontend_and_more_letters") ret=6fffe9602300
1004.300:0aaa:0010:Ret  KERNEL32.lstrlenA() retval=00000027 ret=6fffe9602300
1004.400:0aaa:0010:Call KERNEL32.lstrlenA(008cda00 "user 1234567890123") ret=6fffe9602400
1004.400:0aaa:0010:Ret  KERNEL32.lstrlenA() retval=00000012 ret=6fffe9602400
1004.500:0aaa:0010:Call KERNEL32.lstrlenA(008cdb00 "on MADEUPHOST today") ret=6fffe9602500
1004.500:0aaa:0010:Ret  KERNEL32.lstrlenA() retval=00000013 ret=6fffe9602500
1005.000:0aaa:0030:Call KERNEL32.GetTickCount() ret=6fffe9001234
1005.000:0aaa:0030:Ret  KERNEL32.GetTickCount() retval=00001235 ret=6fffe9001234
1005.100:0aaa:0040:Starting thread proc 00006FFFE9004000 (arg=0000000000000000)
1005.200:0aaa:0050:Starting thread proc 00006FFFE9931640 (arg=0000000000000000)
1005.300:0aaa:0010:trace:seh:dispatch_exception code=e06d7363 (EXCEPTION_WINE_CXX_EXCEPTION) flags=1 addr=00006FFFFFEB1234
1005.300:0aaa:0010:trace:seh:dispatch_exception  info[0]=0000000019930520
1005.301:0aaa:0010:trace:seh:call_seh_handlers calling TEB handler 0000000180001000 (rec=000000000107ED90 frame=000000000107EFC0 context=000000000107E890 dispatch=000000000107E000) sp=107f140
1005.302:0aaa:0010:trace:seh:RtlUnwindEx code=e06d7363 flags=2 end_frame=000000000107EFC0 target_ip=0000000180002000
1005.400:0aaa:0030:trace:seh:dispatch_exception code=40010006 (DBG_PRINTEXCEPTION_C) flags=0 addr=00006FFFE9001111
1005.500:0aaa:0010:warn:debugstr:OutputDebugStringA "never printed"
1006.000:0aaa:0010:trace:loaddll:free_modref Unloaded module L"GAME\\bin\\x64_retail\\launcher.dll" : native
1006.100:0aaa:0010:trace:loaddll:build_module Loaded L"GAME\\bin\\x64_retail\\client.dll" at 00006FFFE9930000: native
1006.200:0aaa:0010:Call KERNEL32.GetTickCount() ret=6fffe9931000
1006.200:0aaa:0010:Ret  KERNEL32.GetTickCount() retval=00001236 ret=6fffe9931000
1007.000:0aaa:0010:Call KERNEL32.ExitProcess(00000000) ret=140002000
1007.001:0aaa:0010:trace:process:NtTerminateProcess (0xffffffffffffffff, 0)
""".replace('GAME', G)

FAILS = []


def run(*argv):
    out = []
    code = relay.main(list(argv), out.append)
    return code, out


def check(what, cond, shown=None):
    if not cond:
        FAILS.append(what)
        print('FAIL: ' + what)
        for line in (shown or [])[:14]:
            print('    | ' + line[:230])


def has(lines, *parts):
    return any(all(p in line for p in parts) for line in lines)


def main():
    d = tempfile.mkdtemp(prefix='relay-selftest-')
    log = os.path.join(d, 'made-up.log')
    with open(log, 'w') as f:
        f.write(LOG.lstrip('\n'))

    # index: the game's process is the one whose first module is the executable
    code, out = run('index', log)
    index = json.loads(out[0])
    check('index: game pid', index['pid'] == '0aaa', out)
    check('index: time zero', abs(index['t0'] - 1000.1) < 1e-6)
    check('index: two processes', [p['pid'] for p in index['processes']] == ['0bbb', '0aaa'])
    check('index: modules', ['launcher', 'tier0', 'originsdk', 'engine'] ==
          [m[2] for m in index['mods'] if m[3] != 'sys' and m[1] > 0 and m[2] not in ('titanfall2.exe', 'client')][:4])
    check('index: classes', dict((m[2], m[3]) for m in index['mods'])['originsdk'] == 'ea')
    check('index: special calls', index['special_calls'] == {'(dllmain)': 1, '(wndproc)': 1, '(snoop)': 1}, out)
    ctx = relay.make_ctx(index)

    # calls: caller+rva, results, nesting, a call into the game, EA's calls counted only
    code, out = run('calls', log, '--pid', '0aaa')
    check('calls: mutex line', has(out, '    1.000 0010 launcher+0xd000 kernel32.CreateMutexA(',
                                   '"titanfall_singleton_mutex") = 00000044'), out)
    check('calls: the executable is a game module', has(out, 'titanfall2.exe+0x8ce0 kernel32.GetProcAddress('), out)
    check('calls: snoop line', has(out, 'titanfall2.exe+0x1234 launcher.LauncherMain(140000000'), out)
    check('calls: window procedure nested in CreateWindowExW',
          has(out, '0010   >engine+0x1cd9d0 (wndproc)(hwnd=000000000002012E,msg=WM_NCCREATE', '= 00000001'), out)
    check('calls: call nested in the window procedure', has(out, '0010     engine+0x1cda00 kernel32.GetTickCount() = 00001234'), out)
    check('calls: order is call order', [i for i, l in enumerate(out) if 'CreateWindowExW' in l][0] <
          [i for i, l in enumerate(out) if '(wndproc)' in l][0], out)
    check('calls: module reloaded at the same base', has(out, 'client+0x1000 kernel32.GetTickCount() = 00001236'), out)
    check('calls: EA caller not printed', not has(out, 'originsdk+0x'), out)
    check('calls: EA caller counted', has(out, 'counted and not shown: originsdk 1'), out)
    check('calls: a call that never returns', has(out, 'kernel32.ExitProcess(00000000) = ... (not returned yet)'), out)
    check('calls: system DllMain not in the default set', not has(out, '(dllmain)'), out)
    check('calls: debugstr never printed', not has(out, 'never printed'), out)
    check('calls: says it is not commit-safe', has(out, '# commit-safe: OFF'), out)

    code, out = run('calls', log, '--pid', '0aaa', '--from', 'all', '--fn', 'dllmain')
    check('calls --from all: DllMain into ntdll', has(out, '>ntdll+0x12eb0 (dllmain)(', '= 1'), out)

    code, out = run('calls', log, '--pid', '0aaa', '--dll', 'user32', '--top')
    body = [l for l in out if not l.startswith('#')]
    check('calls --dll --top', len(body) == 2 and 'RegisterClassExW' in body[0] and 'CreateWindowExW' in body[1], out)

    code, out = run('calls', log, '--pid', '0aaa', '--after', 'RegisterClassExW', '--until', r'\(wndproc\)')
    body = [l for l in out if not l.startswith('#')]
    check('calls --after --until', len(body) == 3 and '(wndproc)' in body[2], out)

    code, out = run('calls', log, '--pid', '0aaa', '--collapse', '--fn', 'InitializeCriticalSection')
    body = [l for l in out if not l.startswith('#')]
    check('calls --collapse', len(body) == 2 and '2 more of kernel32.InitializeCriticalSectionAndSpinCount' in body[1], out)

    code, out = run('calls', log, '--pid', '0aaa', '--max', '3')
    check('calls --max', len([l for l in out if not l.startswith('#')]) == 3 and has(out, 'stopped at --max'), out)

    code, out = run('calls', log, '--pid', '0aaa', '--tid', '0020', '--other', 'threadname')
    check('calls --tid --other', has(out, '0020 | warn:threadname:NtSetInformationThread Thread renamed to L"worker"')
          and has(out, 'tier0+0x4b950 kernel32.WaitForSingleObject(00000050,ffffffff) = 00000000'), out)

    # a window reached by a seek gives the same lines as the same window reached by a pass
    code, a = run('calls', log, '--pid', '0aaa', '--t', '5.5:7')
    code, b = run('calls', log, '--pid', '0aaa', '--t', '5.5:7', '--ctx', ctx)
    check('calls --ctx: seek equals pass', a == b and has(a, 'client+0x1000'), a + b)
    check('calls --t: nothing from before the window', not has(a, 'CreateMutexA'), a)

    # commit-safe: one line per rule
    code, out = run('calls', log, '--pid', '0aaa', '--commit-safe', '--redact', 'madeuphost')
    check('safe: name', has(out, 'kernel32.GetEnvironmentVariableA(<redacted:name>)'), out)
    check('safe: email', has(out, 'kernel32.lstrcpyA(<redacted:email>)') and not has(out, 'example.com'), out)
    check('safe: run', has(out, 'kernel32.lstrlenA(<redacted:run>)') and not has(out, 'QUJDREVG'), out)
    check('safe: digits', has(out, 'kernel32.lstrlenA(<redacted:digits>)') and not has(out, '1234567890123'), out)
    check('safe: host', has(out, 'kernel32.lstrlenA(<redacted:host>)') and not has(out, 'MADEUPHOST'), out)
    check('safe: letters-only run let through', has(out, '"englishclient_frontend_and_more_letters"'), out)
    check('safe: symbol let through', has(out, '"D3D11CreateDeviceAndSwapChain"'), out)
    check('safe: report', has(out, '# commit-safe: caught digits 1, email 1, host 1, name 1, run 1; '
                                   'let through: letters-only run 2, symbol argument 1'), out)
    code, out = run('calls', log, '--pid', '0aaa', '--commit-safe', '--strict')
    check('safe --strict', not has(out, 'englishclient_frontend') and not has(out, 'D3D11CreateDeviceAndSwapChain'), out)

    # census
    code, out = run('census', log, '--pid', '0aaa')
    check('census: totals', has(out, '# 29 calls of 22 distinct functions'), out)
    check('census: row', has(out, '2.900\t1\tuser32.RegisterClassExW\tengine+0x1cd3cf\t0010\tengine:1'), out)
    check('census: two callers', has(out, 'kernel32.GetTickCount\tengine+0x1cda00\t0010\t', 'client:1', 'engine:1'), out)
    rows = [l.split('\t') for l in out if not l.startswith('#')]
    check('census: in order of first call', [float(r[0]) for r in rows] == sorted(float(r[0]) for r in rows), out)

    code, out = run('census', log, '--pid', '0aaa', '--fn', 'wndproc|CreateFileW', '--key', r'msg=(\w+)|,\w*?(\w{8}),\w+,\w+$',
                    '--by-module')
    check('census --key: a message name', has(out, '\t1\t(wndproc)@engine{WM_NCCREATE}\t>engine+0x1cd9d0\t'), out)
    check('census --key: an argument', has(out, '\t2\tkernel32.CreateFileW@engine{00000003}\tengine+0x1c7c00\t'), out)
    code, out = run('census', log, '--pid', '0aaa', '--fn', 'ReadFile', '--pow2', '2', '--sum', '2')
    check('census --pow2 --sum: the whole register', has(out, '\t1\tkernel32.ReadFile{<2^47}\tengine+0x1c7c40\t0010\tengine:1\t%d' % 0x6fff00000004), out)
    code, out = run('census', log, '--pid', '0aaa', '--fn', 'ReadFile', '--pow2', '2', '--sum', '2', '--dword')
    check('census --pow2 --sum --dword', has(out, '\t1\tkernel32.ReadFile{<2^3}\tengine+0x1c7c40\t0010\tengine:1\t4'), out)

    # threads
    code, out = run('threads', log, '--pid', '0aaa')
    i = [k for k, l in enumerate(out) if l.startswith('thread 0020')]
    block = out[i[0]:i[0] + 12] if i else []
    check('threads: named', has(block[:1], 'name "worker"'), out)
    check('threads: start', has(block, 'start: tier0+0x4b890 (arg 41d52c0)'), out)
    check('threads: creator', has(block, 'created: 1.200 by thread 0010 at tier0+0x4b677', 'handle 54'), out)
    check('threads: priority', has(block, 'set: 1.220 SetThreadPriority(00000002) = 00000001 by 0020'), out)
    check('threads: wait', has(block, 'waits: WaitForSingleObject on event#', 'x1 (0.2 s inside)'), out)
    check('threads: object row', has(out, 'event#', 'kernel32.CreateEventA tier0+0x1000', '0020:1\t0010:1'), out)

    check('threads: start resolved when the thread starts', has(out, 'start: launcher+0x1640 (arg 0)'), out)
    check('threads: EA threads counted only', has(out, "# 1 threads started inside originsdk (EA's)")
          and not has(out, 'thread 0040') and not has(out, 'originsdk+0x4000'), out)

    # files
    code, out = run('files', log, '--pid', '0aaa', '--bins', '2')
    check('files: attribute query', has(out, '1.900\tR2\\GameInfo.txt\tattr:1\t'), out)
    check('files: open and read', has(out, '<game>\\vpk\\enable.txt\topen:1\tR\t3\t-\t-\t1\t0/4\t0\t0\tengine:2'), out)
    check('files: failed open, comma in the name', has(out, '<game>\\r2\\cfg\\missing, really.cfg\topen-FAIL:1'), out)
    code, out = run('files', log, '--pid', '0aaa', '--events')
    check('files --events', has(out, 'engine+0x1c7c00 open <game>\\vpk\\enable.txt = 00000060'), out)

    # phases
    code, out = run('phases', log, '--pid', '0aaa', '--mark', 'the class=CreateWindowExW@engine:Respawn001')
    check('phases: mutex', has(out, '1.000\tsingleton mutex (step 4)\t0010\tlauncher+0xd000'), out)
    check('phases: load', has(out, '0.400\tmodule loaded: launcher\t'), out)
    check('phases: EA module', has(out, "module loaded: originsdk (EA's)"), out)
    check('phases: window', has(out, '3.000\tCreateWindowExW by the engine (step 24, W)\t0010\tengine+0x1cd500'), out)
    check('phases: --mark', has(out, '3.000\tthe class\t'), out)
    check('phases: first call into', has(out, 'first (wndproc) call into engine'), out)
    check('phases: unload', has(out, '5.900\tmodule unloaded: launcher'), out)
    check('phases: end', has(out, '6.901\tNtTerminateProcess'), out)
    check('phases: first exception', has(out, '5.200\tfirst exception e06d7363\t0010'), out)
    check('phases: EA timed, not shown', has(out, "4.900\tfirst relayed call by or into originsdk (EA's: not shown)\t0030\t-\t")
          and not has(out, 'originsdk+0x'), out)

    # exceptions
    code, out = run('exceptions', log, '--pid', '0aaa')
    check('exceptions: totals', has(out, '# 2 exceptions of 2 codes'), out)
    check('exceptions: a C++ throw, its handler and unwind',
          has(out, '5.200\t0010\te06d7363\t1\tkernel32+0x1234\tTEB handler engine+0x1000\tunwound to engine+0x2000'), out)
    check('exceptions: per code', has(out, 'e06d7363\ta C++ throw\t1\t5.200\t0010\tkernel32+0x1234'), out)
    check('exceptions: raised inside an EA module, no address',
          has(out, "40010006\ta debugger message (OutputDebugString)\t1\t5.300\t0030\toriginsdk (EA's)")
          and not has(out, 'originsdk+0x'), out)

    # several collectors in one pass
    code, out = run('multi', log, '--pid', '0aaa', '--', 'census', '--', 'threads', '--', 'files', '--', 'phases')
    check('multi: sections', [l for l in out if l.startswith('#####')] ==
          ['##### census', '##### threads', '##### files', '##### phases'], out)

    check('multi: same census', has(out, '# 29 calls of 22 distinct functions'), out)
    check('multi: same threads', has(out, 'created: 1.200 by thread 0010 at tier0+0x4b677'), out)
    check('multi: same files', has(out, '<game>\\vpk\\enable.txt\topen:1\tR\t3\t-\t-\t1\t4\t0\t0\tengine:2'), out)

    code, out = run('multi', log, '--pid', '0aaa', '--', 'census', '--dll', 'user32', '--', 'census', '--t', '0:1')
    check('multi: a collector twice, its options in the header',
          [l for l in out if l.startswith('#####')] == ['##### census --dll user32', '##### census --t 0:1']
          and has(out, '# 2 calls of 2 distinct functions'), out)

    code, out = run('multi', log, '--pid', '0aaa', '--', 'threads', '--', 'threads', '--t', '0:1.3', '--',
                    'files', '--', 'files', '--t', '2.2:3', '--', 'calls', '--max', '1', '--', 'calls', '--dll', 'user32')
    cut = [i for i, l in enumerate(out) if l.startswith('#####')] + [len(out)]
    sec = [out[cut[i]:cut[i + 1]] for i in range(len(cut) - 1)]
    check('multi: two threads collectors do not share state',
          has(sec[0], 'created: 1.200 by thread 0010') and has(sec[1], 'created: 1.200 by thread 0010')
          and has(sec[0], 'waits: WaitForSingleObject') and has(sec[1], 'waits: WaitForSingleObject'), out)
    check('multi: two files collectors', has(sec[2], 'enable.txt\topen:1') and not has(sec[3], 'enable.txt')
          and has(sec[3], 'missing, really.cfg'), out)
    check('multi: two calls collectors', len([l for l in sec[4] if not l.startswith('#')]) == 1
          and has(sec[5], 'user32.CreateWindowExW(', '= 0002012e'), out)

    os.remove(log)
    os.rmdir(d)
    if FAILS:
        print('trace selftest: %d FAILED' % len(FAILS))
        return 1
    print('trace selftest: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
