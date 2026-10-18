#!/usr/bin/env python3
"""Candidate first-call tests and benchmarks for Gate 2 (finding 21, sections 2 to 4).

The specifications (what to call, what to pass, what to check, where the expected answer comes from, which
shims must exist) are written by hand from reading the disassembly; each carries the basis of its signature.
This script adds the measured columns for each candidate: instruction count of the root functions, lifter
status, the direct-call closure (with the C runtime's exit/abort paths and tier0's spew entry points cut),
whether everything in the closure is lifted, the Windows imports the closure reaches, the globals it touches
and whether any was written by the start-up code.

    .venv/bin/python tools/scout/gate2_candidates.py
"""

import json

import gate2_lib as L

T0, VS = "tier0", "vstdlib"

# Imports that mark a path as needing threads, files or the allocator.  Static reachability only: the
# curated "shims" column says what the executed path actually calls.
THREAD_API = {"CreateThread", "ResumeThread", "SuspendThread", "SetThreadAffinityMask", "SetThreadPriority",
              "WaitForMultipleObjects", "OpenThread", "TerminateThread"}
FILE_API = {"CreateFileA", "CreateFileW", "ReadFile", "WriteFile", "CreateDirectoryA", "DeleteFileA", "MoveFileA",
            "FindFirstFileA", "CreateFileMappingA", "FlushFileBuffers", "SetFilePointerEx"}
ALLOC_GLOBALS = {T0: 0xb42a8}        # g_pMemAllocSingleton in tier0's .data


def E(module, name):
    return (module, "exp", name)


def R(module, rva):
    return (module, "rva", rva)


# ---- the candidates, cheapest first --------------------------------------------------------------------
# keys: id, title, roots, sig, basis, test, check, oracle, shims, requires
CANDIDATES = [
    dict(title="Plat_GetMemPageSize", roots=[E(T0, "Plat_GetMemPageSize")],
         sig="int Plat_GetMemPageSize(void)",
         basis="disassembly: loads the constant 4 into eax and returns; the C name carries no types",
         test="Call with no arguments.", check="Returns 4 (the unit is not stated by the code; kilobytes inferred from the value).",
         oracle="The constant read from the disassembly.", shims=[],
         requires="Only that the dispatch table reaches the function. Proves the driver, the export lookup and the return path."),
    dict(title="Mode flags: Plat_SetToolMode, Plat_IsInToolMode, Plat_SetBenchmarkMode, Plat_IsInBenchmarkMode, Plat_SetRenderToolMode, Plat_IsInRenderToolMode",
         roots=[E(T0, "Plat_SetToolMode"), E(T0, "Plat_IsInToolMode"), E(T0, "Plat_SetBenchmarkMode"),
                E(T0, "Plat_IsInBenchmarkMode"), E(T0, "Plat_SetRenderToolMode"), E(T0, "Plat_IsInRenderToolMode")],
         sig="void Plat_SetXMode(bool); bool Plat_IsInXMode(void)  for X in Tool, Benchmark, RenderTool",
         basis="disassembly: each setter stores cl to one byte of tier0 .data, each getter loads it with movzx",
         test="For each pair: set 1, read; set 0, read. Then set one flag and check the other two did not move.",
         check="Round trip is exact; the three flags are independent bytes.",
         oracle="The round-trip property itself.", shims=[],
         requires="Writes .data (zero-initialised); nothing from the entry point."),
    dict(title="EncodeBinaryToString / DecodeBinaryFromString",
         roots=[E(T0, "EncodeBinaryToString"), E(T0, "DecodeBinaryFromString")],
         sig="int EncodeBinaryToString(const void* src, int srcBytes, char* dst, int dstCapacity); "
             "int DecodeBinaryFromString(const char* in, void* out, int outCapacity, char** end)",
         basis="disassembly (argument registers, return paths); the C names carry no types",
         test="Buffers of 0, 1, 6, 7, 8, 255 and 4096 pseudo-random bytes. Encode, then decode. Also: dst capacity one too small; "
              "an input whose first four bytes are below 0x80; a decode buffer one byte too small.",
         check="Encode returns n + 4 + ceil(n/7) (string length without the terminator) and every output byte is >= 0x80; "
               "capacity too small returns -(n + 5 + ceil(n/7)); decode returns n and sets *end one past the last byte read, "
               "and the bytes match; malformed input returns 0x80000000; too-small output returns -n.",
         oracle="A model written from the disassembly (4-byte 7-bit length header, then each group of up to 7 source bytes "
                "preceded by a carry byte holding the inverted top bits), the round trip, and the Unicorn oracle in "
                "harness/difftest.py (both are leaves with no calls out).",
         shims=[], requires="None: no imports, no globals, no allocator. Best first check of integer code lifting."),
    dict(title="V_UnicodeToUCS2 / V_UCS2ToUnicode", roots=[E(VS, "V_UnicodeToUCS2"), E(VS, "V_UCS2ToUnicode")],
         sig="int V_UnicodeToUCS2(const wchar_t* src, int cubSrcInBytes, char* dst, int cubDstInBytes); "
             "int V_UCS2ToUnicode(const ucs2* src, wchar_t* dst, int cubDstInBytes)",
         basis="public Source SDK strtools.h declarations, confirmed by disassembly (min of the two sizes, wcsncpy, force a terminator)",
         test="UTF-16 strings of 0, 1 and 10 characters with destination sizes below, equal to and above the need.",
         check="Return value = bytes copied (V_UnicodeToUCS2: min(src, dst); V_UCS2ToUnicode: min(2*(len+1), dst)); "
               "V_UCS2ToUnicode terminates the destination; contents equal.",
         oracle="Identity on 16-bit characters (Windows wchar_t is UCS-2/UTF-16); agreement between the two exports.",
         shims=[], requires="Calls two small self-contained C runtime routines (wcslen, wcsncpy); no Windows imports. Pass sign-extended 32-bit "
                            "sizes: V_UnicodeToUCS2 compares full 64-bit registers."),
    dict(title="V_UTF8_strncpy", roots=[E(VS, "V_UTF8_strncpy")],
         sig="char* V_UTF8_strncpy(char* dst, const char* src, size_t maxLen)",
         basis="public Source SDK strtools.h declaration, confirmed by disassembly (strncpy, force a terminator, drop an incomplete trailing sequence)",
         test="A UTF-8 string with 2-, 3- and 4-byte characters, copied with maxLen cutting inside each kind of character and at a boundary.",
         check="Result is valid UTF-8, no longer than maxLen-1, and is the longest valid prefix; returns dst.",
         oracle="Python: encode, cut at maxLen-1, strip an incomplete trailing sequence.",
         shims=[], requires="Calls the C runtime strncpy only."),
    dict(title="CThreadSpinRWLock (ctor, TryLockForRead, UnlockRead, TryLockForWrite, UnlockWrite, IsLockedForRead, IsLockedForWrite)",
         roots=[E(T0, "??0CThreadSpinRWLock@@QEAA@XZ"), E(T0, "?TryLockForRead@CThreadSpinRWLock@@QEAA_NXZ"),
                E(T0, "?UnlockRead@CThreadSpinRWLock@@QEAAXXZ"), E(T0, "?TryLockForWrite@CThreadSpinRWLock@@QEAA_NXZ"),
                E(T0, "?UnlockWrite@CThreadSpinRWLock@@QEAAXXZ"), E(T0, "?IsLockedForRead@CThreadSpinRWLock@@QEAA_NXZ"),
                E(T0, "?IsLockedForWrite@CThreadSpinRWLock@@QEAA_NXZ")],
         sig="CThreadSpinRWLock::CThreadSpinRWLock(); bool TryLockForRead(); void UnlockRead(); bool TryLockForWrite(); "
             "void UnlockWrite(); bool IsLockedForRead(); bool IsLockedForWrite()  (all on an 8-byte object)",
         basis="mangled names; object layout from disassembly (low word of the first dword = reader count, bit 16 = writer, "
               "second dword = writer thread id)",
         test="Zero 8 bytes through the constructor. TryLockForRead twice; IsLockedForRead; TryLockForWrite (must fail); "
              "UnlockRead twice; TryLockForWrite (succeeds); IsLockedForWrite; TryLockForRead (fails); UnlockWrite.",
         check="Return values as listed; the first dword reads 2, then 0, then 0x10000, then 0; second dword equals the "
               "thread id after a write lock.",
         oracle="The state machine itself; the dword values come from the instruction sequence (lock cmpxchg, lock dec).",
         shims=["kernel32!GetCurrentThreadId"],
         requires="Lock-prefixed atomics only. Good first test of the x86 memory-ordering mode."),
    dict(title="Thread-local read and write: JT_GetCurrentThread, JT_SetThreadIdx, JT_NumWorkerThreads",
         roots=[E(T0, "?JT_GetCurrentThread@@YAIXZ"), E(T0, "?JT_SetThreadIdx@@YAXI@Z"), E(T0, "?JT_NumWorkerThreads@@YAIXZ")],
         sig="unsigned JT_GetCurrentThread(void); void JT_SetThreadIdx(unsigned); unsigned JT_NumWorkerThreads(void)",
         basis="mangled names",
         test="On a fresh thread call JT_GetCurrentThread; call JT_SetThreadIdx(5) and read again; read from a second guest thread; "
              "call JT_NumWorkerThreads before ConfigureAndInitJobThreads.",
         check="First read returns 28 (the TLS template holds 28 at offset 0x10); then 5; the second thread still sees 28; "
               "worker count is 0.",
         oracle="The TLS template value read from the PE (28 at offset 0x10); the round trip.",
         shims=[],
         requires="Static TLS: the loader writes the module's TLS index to tier0 RVA 0x19aaf0 and each thread's gs:[0x58] array "
                  "holds the block (RUNTIME.md 1.6, 6.2). First test of that machinery; no Windows import."),
    dict(title="CreateInterface and VStdLib_GetICVarFactory", roots=[E(VS, "CreateInterface"), E(VS, "VStdLib_GetICVarFactory")],
         sig="void* CreateInterface(const char* name, int* returnCode); CreateInterfaceFn VStdLib_GetICVarFactory(void)",
         basis="public Source SDK interface.h convention, confirmed by disassembly (returnCode 0 = ok, 1 = failed)",
         test="CreateInterface with \"VEngineCvar007\", \"EventSystem001\", \"VProcessUtils002\", \"NoSuchThing\", a wrong-case name, "
              "and a NULL returnCode. Then call the function VStdLib_GetICVarFactory returns, with \"VEngineCvar007\".",
         check="The three names return vstdlib base + 0x72080, + 0x72370 and + 0x743b0 with *rc == 0; the others return NULL with "
               "*rc == 1; the factory's answer equals CreateInterface's; the function pointer equals base + 0x26140.",
         oracle="Addresses read from the creator functions (each is `lea rax,[object]; ret`) and the three registration constructors.",
         shims=[],
         requires="Needs vstdlib's entry point to have run: three static constructors insert the list nodes. Before that the "
                  "list is empty and every name fails."),
    dict(title="One-shim leaves: Plat_GetCommandLineA, Plat_GetCommandLine, GetFreeMemory, GetTotalMemory, Plat_GetOSVersion",
         roots=[E(T0, "Plat_GetCommandLineA"), E(T0, "Plat_GetCommandLine"), E(T0, "GetFreeMemory"), E(T0, "GetTotalMemory"),
                E(T0, "Plat_GetOSVersion")],
         sig="const char* Plat_GetCommandLine[A](void); uint64_t GetFreeMemory(void); uint64_t GetTotalMemory(void); int Plat_GetOSVersion(void)",
         basis="disassembly: the first two are `jmp [GetCommandLineA]`; the memory pair read ullAvailPhys and ullTotalPhys from a "
               "MEMORYSTATUSEX; the last returns dwMajorVersion of an OSVERSIONINFOA (size 0x94) or -1",
         test="Call each once.",
         check="Both command-line exports return the pointer the GetCommandLineA shim returns; total >= free > 0 and total equals the "
               "machine description's physical memory; OS version equals the major version the runtime reports.",
         oracle="The runtime's own machine description (RUNTIME.md 8.5): agreement between export and shim.",
         shims=["kernel32!GetCommandLineA", "kernel32!GlobalMemoryStatusEx", "kernel32!GetVersionExA"],
         requires="One shim each; no state."),
    dict(title="Uniform random numbers: CUniformRandomStream and RandomSeed / RandomInt / GetCurrentRandomSeed / RandomIntZeroMax",
         roots=[E(VS, "??0CUniformRandomStream@@QEAA@XZ"), E(VS, "?SetSeed@CUniformRandomStream@@UEAAXH@Z"),
                E(VS, "?GetSeed@CUniformRandomStream@@UEAAHXZ"), E(VS, "?RandomInt@CUniformRandomStream@@UEAAHHH@Z"),
                E(VS, "?GenerateRandomNumber@CUniformRandomStream@@AEAAHXZ"), E(VS, "RandomSeed"), E(VS, "RandomInt"),
                E(VS, "GetCurrentRandomSeed"), E(VS, "RandomIntZeroMax"), E(VS, "InstallUniformRandomStream")],
         sig="CUniformRandomStream::CUniformRandomStream(); void SetSeed(int); int GetSeed(); int RandomInt(int lo, int hi); "
             "void RandomSeed(int); int RandomInt(int lo, int hi); int GetCurrentRandomSeed(void); int RandomIntZeroMax(void)",
         basis="mangled names for the class members; the global functions are plain C names, typed from disassembly "
               "(each loads the installed stream pointer and tail-jumps through vtable slots 0, 1, 2 or 3)",
         test="Place a 0x38-byte object on the guest stack, construct it, SetSeed(1), draw 1000 RandomInt(0, 99). Repeat through "
              "RandomSeed(1) and the global RandomInt. Also seeds 0 and -5, and RandomInt(7, 7) and RandomInt(9, 3).",
         check="Same sequence from the object and from the globals; GetSeed tracks the generator state; seed 0 behaves as seed 1; "
               "RandomInt(lo, lo) returns lo.",
         oracle="Python: s = max(abs(seed), 1) if seed <= 0; each step s = s * 48271 mod 2147483647 (the multiplier 0xbc8f and modulus "
                "0x7fffffff are in the code); RandomInt draws x until x <= 0x7fffffff - (0x80000000 mod n) then returns lo + x mod n "
                "with n = hi - lo + 1 (returns lo when n <= 1). The Unicorn oracle can confirm it for the leaf GenerateRandomNumber.",
         shims=["kernel32!InitializeCriticalSectionAndSpinCount", "kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection"],
         requires="The global path needs vstdlib's static constructor for the default stream (it initialises the critical section); "
                  "the object path is self-contained. Object layout: vptr, seed at +8, critical section at +0x10."),
    dict(title="CThreadMutex (ctor, Lock, Unlock, TryLock, dtor)",
         roots=[E(T0, "??0CThreadMutex@@QEAA@XZ"), E(T0, "?Lock@CThreadMutex@@QEAAXXZ"), E(T0, "?Unlock@CThreadMutex@@QEAAXXZ"),
                E(T0, "?TryLock@CThreadMutex@@QEAA_NXZ"), E(T0, "??1CThreadMutex@@QEAA@XZ")],
         sig="CThreadMutex::CThreadMutex(); void Lock(); void Unlock(); bool TryLock(); ~CThreadMutex()  (object of at least 0x30 bytes)",
         basis="mangled names; disassembly: the object is a 40-byte CRITICAL_SECTION plus three small fields; Lock, Unlock and the "
               "destructor are the import thunks for EnterCriticalSection, LeaveCriticalSection and DeleteCriticalSection",
         test="Construct; Lock; TryLock on the same thread; Unlock twice; destroy. From a second thread, TryLock while locked.",
         check="After one Lock the critical section shows recursion count 1 and the owner's thread id; TryLock on the owner succeeds. "
               "From another thread TryLock returns false (true if TryEnterCriticalSection was not found: then it blocks).",
         oracle="Documented CRITICAL_SECTION semantics.",
         shims=["kernel32!InitializeCriticalSectionAndSpinCount", "kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection",
                "kernel32!DeleteCriticalSection", "kernel32!TryEnterCriticalSection (reached by GetProcAddress at load)"],
         requires="TryLock calls through a pointer tier0's static constructors fetch with Plat_GetProcAddress(\"Kernel32.dll\", "
                  "\"TryEnterCriticalSection\"); if the loader hands back NULL the pointer stays empty and TryLock degrades to Lock."),
    dict(title="Main-thread identity: ThreadInMainThread, DeclareCurrentThreadIsMainThread",
         roots=[E(T0, "ThreadInMainThread"), E(T0, "DeclareCurrentThreadIsMainThread")],
         sig="bool ThreadInMainThread(void); void DeclareCurrentThreadIsMainThread(void)",
         basis="disassembly: compares GetCurrentThreadId with a dword in tier0 .data; the declare function stores it",
         test="On the thread that loaded tier0 call ThreadInMainThread; call Declare from a second guest thread and ask again from both.",
         check="True on the loading thread (a static constructor stored its id at load); after the second thread declares, true there and false on the first.",
         oracle="Thread ids from the runtime.", shims=["kernel32!GetCurrentThreadId"],
         requires="Static constructor (the one that names the thread MainThrd) must have run; a second guest thread for the negative case."),
    dict(title="CThreadEvent and CThreadSemaphore",
         roots=[E(T0, "??0CThreadEvent@@QEAA@_N@Z"), E(T0, "?Set@CThreadEvent@@QEAA_NXZ"), E(T0, "?Reset@CThreadEvent@@QEAA_NXZ"),
                E(T0, "?Check@CThreadEvent@@QEAA_NXZ"), E(T0, "?Wait@CThreadEvent@@QEAA_NI@Z"),
                E(T0, "??0CThreadSemaphore@@QEAA@HH@Z"), E(T0, "?Release@CThreadSemaphore@@QEAA_NHPEAH@Z"),
                E(T0, "?Wait@CThreadSyncObject@@QEAA_NI@Z")],
         sig="CThreadEvent::CThreadEvent(bool manualReset); bool Set(); bool Reset(); bool Check(); bool Wait(unsigned ms); "
             "CThreadSemaphore::CThreadSemaphore(int initial, int max); bool Release(int count, int* previous); bool Wait(unsigned ms)",
         basis="mangled names; disassembly (CreateEventA(NULL, manualReset, FALSE, NULL); Check = WaitForSingleObject(h, 0) == 0)",
         test="Event: construct manual-reset, Check false, Set, Check true, Reset, Check false, Wait(20) false after about 20 ms. "
              "Semaphore(1, 3): Wait(0) true, Wait(0) false, Release(2, &prev), Wait(0) twice true, third false.",
         check="As listed; prev reflects the count before the release.",
         oracle="Documented Win32 event and semaphore semantics.",
         shims=["kernel32!CreateEventA", "kernel32!SetEvent", "kernel32!ResetEvent", "kernel32!WaitForSingleObject",
                "kernel32!CreateSemaphoreA", "kernel32!ReleaseSemaphore", "kernel32!CloseHandle"],
         requires="Handle table and wait core (RUNTIME.md 4.1, 4.2); no second thread needed."),
    dict(title="UTF-8 and UTF-16 conversion: V_UTF8ToUnicode, V_UnicodeToUTF8, V_UTF8ToUCS2, V_UCS2ToUTF8",
         roots=[E(VS, "V_UTF8ToUnicode"), E(VS, "V_UnicodeToUTF8"), E(VS, "V_UTF8ToUCS2"), E(VS, "V_UCS2ToUTF8")],
         sig="int V_UTF8ToUnicode(const char* src, wchar_t* dst, int cubDst); int V_UnicodeToUTF8(const wchar_t* src, char* dst, int cubDst); "
             "int V_UTF8ToUCS2(const char* src, int cubSrc, ucs2* dst, int cubDst); int V_UCS2ToUTF8(const ucs2* src, char* dst, int cubDst)",
         basis="public Source SDK strtools.h declarations, confirmed by disassembly (MultiByteToWideChar / WideCharToMultiByte with code "
               "page 65001 and length -1; the last destination element is forced to zero)",
         test="Strings with ASCII, 2-byte, 3-byte, a 4-byte character (surrogate pair) and an empty string; a NULL source for "
              "V_UTF8ToUnicode; destinations that are too small.",
         check="UTF-8 to UTF-16 to UTF-8 round trip; return value is the count including the terminator; NULL source returns 0; "
               "a too-small destination still ends in a terminator.",
         oracle="Python's UTF-8 and UTF-16-LE codecs (or host iconv).",
         shims=["kernel32!MultiByteToWideChar", "kernel32!WideCharToMultiByte (code page 65001 must be supported)"],
         requires="The two conversion shims, 16-bit wide characters in guest memory."),
    dict(title="Plat_GetProcAddress", roots=[E(T0, "Plat_GetProcAddress")],
         sig="void* Plat_GetProcAddress(const char* module, const char* proc)",
         basis="disassembly: LoadLibraryA(module), then tail-jumps to GetProcAddress(handle, proc); NULL when the load fails",
         test="(\"Kernel32.dll\", \"GetTickCount\"), (\"Kernel32.dll\", \"NoSuchExport\"), (\"NoSuchLibrary.dll\", \"X\"), "
              "and a lifted game module such as (\"vstdlib.dll\", \"RandomInt\").",
         check="Non-null equals what GetProcAddress gives for the same pair; the failures return NULL; the game-module answer equals "
               "vstdlib base + the export RVA.",
         oracle="Agreement with the loader's export table (RUNTIME.md 1.5).",
         shims=["kernel32!LoadLibraryA", "kernel32!GetProcAddress"],
         requires="Module registry; tier0's own static constructors already call this once."),
    dict(title="Plat_gmtime", roots=[E(T0, "Plat_gmtime")],
         sig="int Plat_gmtime(uint64_t time, struct tm* out)  (returns the C runtime's _gmtime64_s error code)",
         basis="disassembly: builds a 64-bit time on the stack and calls a routine with (out, &time); return value passed through",
         test="Times 0, 86399, 951782400, 1700000000, 253402300799 and 2^62; out is a 36-byte buffer of nine ints.",
         check="tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst equal the host gmtime_r values "
               "(tm_year = year - 1900); the out-of-range time returns nonzero.",
         oracle="Host libc gmtime_r on the same integer; no Windows needed.",
         shims=[], requires="Pure calendar arithmetic in the C runtime, but _gmtime64_s begins with a 36-byte memset (tier0 RVA 0x25bd0), which the "
                            "lifter skips today (movnti): blocked until that is lifted. The invalid-argument path also reaches _errno and the unlifted "
                            "FLS wrappers (section 5)."),
    dict(title="CommandLine() and the CCommandLine object",
         roots=[E(T0, "CommandLine"), R(T0, 0xbbb0), R(T0, 0xbdb0), R(T0, 0xc0b0), R(T0, 0xc0c0), R(T0, 0xc1c0), R(T0, 0xbe40), R(T0, 0xbe30)],
         sig="ICommandLine* CommandLine(void); then through the vtable: CreateCmdLine(const char*) [slot 1], GetCmdLine() [3], "
             "ParmValue(const char*, int) [8], ParmValue(const char*, const char*) [9], ParmCount() [10], FindParm(const char*) [11], GetParm(int) [12]",
         basis="vtable slots from CCommandLine's RTTI vtable (tier0 RVA 0x521a8); each slot's meaning verified from its code, see section 6",
         test="CreateCmdLine(\"game.exe -foo 5 +bar baz -neg -3\"); GetCmdLine; ParmCount; FindParm(\"-foo\"), FindParm(\"-FOO\"), FindParm(\"-nope\"); "
              "ParmValue(\"-foo\", 99); ParmValue(\"+bar\", \"x\"); ParmValue(\"-neg\", 7); GetParm(4); GetParm(99).",
         check="Count 7; FindParm returns 1, 1 (the compare is case-insensitive: it lower-cases A to Z) and 0; ParmValue(-foo) 5; ParmValue(+bar) \"baz\"; "
               "ParmValue(-neg, 7) returns the default 7 (a value starting with + or - is treated as another parm); GetParm(4) \"baz\"; out of range returns an empty string.",
         oracle="Hand-derived from the code and from the public ICommandLine contract.",
         shims=["kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection"],
         requires="CommandLine() returns NULL until two static constructors ran (they initialise the mutex and set a flag). "
                  "Copies the string with the game allocator, so the allocator is used (see rank 22)."),
    dict(title="GetCPUInformation and the clock-speed data exports",
         roots=[E(T0, "GetCPUInformation")],
         sig="const CPUInformation* GetCPUInformation(void); data: g_ClockSpeed (int64), g_dwClockSpeed (uint32), "
             "g_ClockSpeedMicrosecondsMultiplier / g_ClockSpeedMillisecondsMultiplier / g_ClockSpeedSecondsMultiplier (double)",
         basis="disassembly: returns tier0 .data+0xa99d8 after first-call set-up; the first dword is 0x18; the qword at +8 is the "
               "calibrated time-stamp-counter rate; a static constructor copies it into the g_ClockSpeed exports and divides 1e6, 1e3, 1.0 by it",
         test="Call; read the first dword, the qword at +8, byte flags at +4 to +7 and the processor-count byte. Compare with the data exports.",
         check="First dword 0x18; qword at +8 equals g_ClockSpeed (tier0 RVA 0xa9a60) and its low half g_dwClockSpeed; "
               "g_ClockSpeedSecondsMultiplier * speed == 1.0 within 1e-9; speed lies between 5e8 and 6e9 (a smaller value means the "
               "runtime's rdtsc is not a cycle counter); logical-processor byte matches GetSystemInfo.",
         oracle="Agreement between the function and the three data exports; host core count and clock.",
         shims=["kernel32!QueryPerformanceFrequency", "kernel32!QueryPerformanceCounter", "kernel32!GetSystemInfo", "(cpuid, rdtsc in the lifter)"],
         requires="Already runs once inside the load: a static constructor calls it and then spins on QueryPerformanceCounter for "
                  "about 1/32 s while reading rdtsc, so a counter that never advances hangs the load. Needs the lifted rdtsc and cpuid."),
    dict(title="Logging channel lookup: LoggingSystem_GetChannelCount, LoggingSystem_FindChannel",
         roots=[E(T0, "LoggingSystem_GetChannelCount"), E(T0, "LoggingSystem_FindChannel")],
         sig="int LoggingSystem_GetChannelCount(void); int LoggingSystem_FindChannel(const char* name)",
         basis="disassembly: returns the first dword of the channel table; FindChannel walks 0x38-byte records comparing the name at +0x18 "
               "and returns the index or -1; the C names carry no types",
         test="After load: GetChannelCount; FindChannel(\"General\"), FindChannel(\"general\"), FindChannel(\"LOADING\"), FindChannel(\"nothing\").",
         check="Count is at least 2; General and LOADING found with distinct non-negative ids below the count; \"general\" gives the same id as \"General\" "
               "(case-insensitive compare, the same routine FindParm uses); the last returns -1.",
         oracle="The two channel names come from the static constructors that register them (strings seen in those constructors).",
         shims=[], requires="Needs tier0's entry point to have run (static constructors register the channels). The table is "
                            "built lazily on first use and registers an atexit handler."),
    dict(title="AllocateThreadID / FreeThreadID", roots=[E(T0, "AllocateThreadID"), E(T0, "FreeThreadID")],
         sig="void AllocateThreadID(void); void FreeThreadID(void)",
         basis="disassembly: takes a lock, finds the first zero byte in a 128-byte table starting at index 1, marks it and "
               "stores the index in the thread's TLS slot; Free clears the table byte for the id in the slot; the C names carry no types",
         test="AllocateThreadID, read the TLS value for the index stored in tier0 RVA 0x19a3cc, FreeThreadID, Allocate again.",
         check="First id is 1; after Free the next Allocate gives 1 again; a second thread gets 2.",
         oracle="The table discipline in the code.", shims=["kernel32!TlsSetValue", "kernel32!TlsGetValue", "kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection"],
         requires="A static constructor allocates the TLS index with TlsAlloc; Error() is the out-of-ids path."),
    dict(title="MemAllocScratch / MemFreeScratch", roots=[E(T0, "MemAllocScratch"), E(T0, "MemFreeScratch")],
         sig="void* MemAllocScratch(int bytes); void MemFreeScratch(void)",
         basis="public Source SDK tier0/memalloc.h declarations, confirmed by disassembly (stack of sizes; returns buffer + running offset)",
         test="a = Alloc(100); b = Alloc(200); Free; c = Alloc(150); Free; Free. Write a pattern into each block.",
         check="b == a + 100; c == b; no overlap; buffer first created with at least 0x800 bytes.",
         oracle="Pointer arithmetic implied by the code (no alignment is applied).",
         shims=["kernel32!VirtualAlloc (and whatever the allocator constructor needs; unconfirmed)"],
         requires="Uses the game allocator (IMemAlloc Alloc and Realloc); first use constructs it (rank 22)."),
    dict(title="The game allocator: CreateGlobalMemAlloc, IMemAlloc vtable, CAlignedNewDelete operator new / delete",
         roots=[E(T0, "CreateGlobalMemAlloc"), R(T0, 0x13780), R(T0, 0x15f50), R(T0, 0x14df0), R(T0, 0x15370),
                E(T0, "??2?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAPEAX_K@Z"),
                E(T0, "??3?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAXPEAX@Z")],
         sig="IMemAlloc* CreateGlobalMemAlloc(void); then vtable slot 1 Alloc(size_t), 3 Realloc(void*, size_t), 5 Free(void*), 8 GetSize(void*); "
             "static void* CAlignedNewDelete::operator new(size_t); operator delete(void*)",
         basis="slots 1, 3 and 5 verified at call sites; slot 8 inferred (see section 6); operator new/delete from mangled names",
         test="p = CreateGlobalMemAlloc() twice; Alloc(100); GetSize; fill; Realloc to 1000 and check the first 100 bytes; Free; "
              "Alloc of 0x900 (large path); operator new(40) and delete.",
         check="Same pointer both times; vptr equals tier0 + 0x52fb8; Alloc returns non-null; GetSize >= 100; Realloc preserves contents; "
               "operator new returns an 8-byte aligned pointer and delete frees the block it took from.",
         oracle="Properties above; GetSize and the alignment rule are from the code.",
         shims=["kernel32!VirtualAlloc", "kernel32!VirtualFree", "kernel32!InitializeCriticalSectionAndSpinCount", "kernel32!EnterCriticalSection",
                "kernel32!LeaveCriticalSection", "(HeapAlloc and CreateSemaphoreA are statically reachable; not confirmed on this path)"],
         requires="The first caller builds the allocator in static storage; other threads spin on Sleep until it is published. "
                  "g_pMemAllocSingleton is NULL until some caller caches it."),
    dict(title="KeyValuesSystem", roots=[E(VS, "KeyValuesSystem"), R(VS, 0x1b4b0), R(VS, 0x1b490)],
         sig="IKeyValuesSystem* KeyValuesSystem(void); vtable slot 3 GetSymbolForString(const char*, bool), slot 4 GetStringForSymbol(symbol)",
         basis="C++ export; slots from CKeyValuesSystem's RTTI vtable (vstdlib RVA 0x5c0d8, 11 slots) matched to the public IKeyValuesSystem "
               "order by size and imports (inferred); no KeyValues parser or loader is exported by either DLL",
         test="Pointer equals vstdlib + 0x78f90; GetSymbolForString(\"abc\", true) twice and \"abd\" once; GetStringForSymbol on each.",
         check="Same symbol for the same string, different for different strings; the string comes back as given.",
         oracle="Round trip.", shims=["kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection", "kernel32!VirtualAlloc"],
         requires="The object is built by a static constructor at load; symbol creation grows a pool with VirtualAlloc and uses the game allocator."),
    dict(title="Plat_FloatTime, Plat_MSTime, Timer_GetTimeUS, Plat_GetClockStart (blocked on the lifter)",
         roots=[E(T0, "Plat_FloatTime"), E(T0, "Plat_MSTime"), E(T0, "Timer_GetTimeUS"), E(T0, "Plat_GetClockStart")],
         sig="double Plat_FloatTime(void); unsigned Plat_MSTime(void); int64_t Timer_GetTimeUS(void); int64_t Plat_GetClockStart(void)",
         basis="disassembly: FloatTime = (QueryPerformanceCounter - start) / frequency as a double; MSTime = the same in milliseconds; "
               "Timer_GetTimeUS = MSTime * 1000; GetClockStart = the stored counter at first use",
         test="First FloatTime call, then Sleep(50) and again; MSTime and Timer_GetTimeUS twice; then Plat_SetBenchmarkMode(1) and FloatTime three times.",
         check="First value is small and the second exceeds it by 0.045 to 0.08; MSTime*1000 within 1000 of Timer_GetTimeUS; "
               "in benchmark mode each call adds 1/66 s (0.0151515...) exactly.",
         oracle="Host monotonic clock; the benchmark-mode step is a constant in tier0 .data.",
         shims=["kernel32!QueryPerformanceCounter", "kernel32!QueryPerformanceFrequency", "kernel32!Sleep"],
         requires="Plat_FloatTime and Plat_MSTime are skipped by the lifter today (scalar double SSE: cvtsi2sd, divsd, addsd, mulsd, cvttsd2si), "
                  "so Timer_GetTimeUS fails through them too."),
    dict(title="Local time: Plat_GetLocalTime, Plat_ConvertToLocalTime, GetCurrentDate, GetCurrentDayOfTheWeek, GetCurrentDayOfTheYear",
         roots=[E(T0, "Plat_GetLocalTime"), E(T0, "Plat_ConvertToLocalTime"), E(T0, "GetCurrentDate"),
                E(T0, "GetCurrentDayOfTheWeek"), E(T0, "GetCurrentDayOfTheYear")],
         sig="void Plat_GetLocalTime(struct tm*); void Plat_ConvertToLocalTime(uint64_t, struct tm*); void GetCurrentDate(int* day, int* month, int* year) (typed from use); "
             "int GetCurrentDayOfTheWeek(void); int GetCurrentDayOfTheYear(void)",
         basis="Plat_ConvertToLocalTime and Plat_GetLocalTime from disassembly (a lock, _localtime64, nine ints copied); the date functions' "
               "argument order is not decoded",
         test="Plat_ConvertToLocalTime(1700000000, &tm) and Plat_GetLocalTime(&tm) against the host in the same zone.",
         check="Fields equal host localtime_r for the zone the runtime reports to GetTimeZoneInformation; GetLocalTime within 2 s of now.",
         oracle="Host libc localtime_r with the same zone.",
         shims=["kernel32!GetSystemTimeAsFileTime", "kernel32!GetTimeZoneInformation", "kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection",
                "kernel32!FlsGetValue / FlsSetValue or TlsGetValue / TlsSetValue (per-thread C runtime data)"],
         requires="The C runtime's per-thread data goes through the FLS/TLS wrappers that the lifter skips (section 5)."),
    dict(title="Msg / Warning with a logging listener: Msg, Warning, ConMsg, LoggingSystem_RegisterLoggingListener",
         roots=[E(T0, "Msg"), E(T0, "Warning"), E(T0, "LoggingSystem_RegisterLoggingListener")],
         sig="void Msg(const char* fmt, ...); void Warning(const char* fmt, ...); void LoggingSystem_RegisterLoggingListener(ILoggingListener*)",
         basis="mangled/C names plus the public Source convention; the listener is ILoggingListener (vtable shape 2 slots, tier0 RVA 0x52b00)",
         test="Register a native object whose Log slot records its arguments; Msg(\"n=%d s=%s\\n\", 7, \"x\"); Warning(...).",
         check="The listener receives \"n=7 s=x\\n\" once with the severity the function implies.",
         oracle="printf semantics on the host.",
         shims=["kernel32!EnterCriticalSection", "kernel32!LeaveCriticalSection", "kernel32!OutputDebugStringA", "kernel32!GetStdHandle / WriteFile (default listener)",
                "TLS or FLS for the per-thread logging state"],
         requires="The varargs path (home space, floating arguments duplicated in integer registers), the C runtime formatter, the default listener's "
                  "console and debugger output, and a guest-ABI native object (RUNTIME.md 3.7)."),
    dict(title="CreateSimpleThread / ThreadJoin / ThreadSleep / ThreadGetPriority",
         roots=[E(T0, "CreateSimpleThread"), E(T0, "ThreadJoin"), E(T0, "ThreadSleep"), E(T0, "ThreadGetPriority")],
         sig="ThreadHandle_t CreateSimpleThread(ThreadFunc_t fn, void* arg, unsigned stackSize); bool ThreadJoin(ThreadHandle_t, unsigned ms); "
             "void ThreadSleep(unsigned ms); int ThreadGetPriority(ThreadHandle_t)",
         basis="CreateSimpleThread typed from disassembly (allocates a 16-byte {fn, arg} block, CreateThread with a tier0 wrapper); the others "
               "from the public SDK and a short read",
         test="Pass the address of an exported function that returns a constant (Plat_GetMemPageSize) as the thread function; join; read the exit code.",
         check="The thread runs, the join returns true, the exit code is 4.",
         oracle="The constant returned by the chosen function.",
         shims=["kernel32!CreateThread", "kernel32!WaitForSingleObject", "kernel32!GetExitCodeThread", "kernel32!CloseHandle",
                "DLL_THREAD_ATTACH delivery to both DLLs (C runtime per-thread data)"],
         requires="Threads; the new thread's DllMain call reaches the FLS wrappers the lifter skips."),
    dict(title="ConfigureAndInitJobThreads with no workers", roots=[E(T0, "ConfigureAndInitJobThreads")],
         sig="void ConfigureAndInitJobThreads(void)",
         basis="C name (no types); behaviour from finding 29 and a read: it reads -numworkerthreads and -numreservedcores from the command line",
         test="Create the command line \"game.exe -numworkerthreads 0\", then call; read JT_NumWorkerThreads.",
         check="JT_NumWorkerThreads() == 0 and no thread was created.",
         oracle="Finding 29, section 3.", shims=["kernel32!GetLogicalProcessorInformation", "kernel32!InitializeConditionVariable / SRW lock calls",
                                               "kernel32!QueryPerformanceCounter"],
         requires="The command-line object, the SRW lock and condition-variable family, the CPU-count shim; zero workers avoids CreateThread. "
                  "Blocked today: the job-system init routine (tier0 RVA 0x8210; scalar float conversions) and the CPU-count helper (0x5220; punpcklqdq) are skipped by the lifter."),
    dict(title="Built-in self tests: RunTSListTests, RunTSQueueTests, RunThreadPoolTests",
         roots=[E(T0, "RunTSListTests"), E(T0, "RunTSQueueTests"), E(VS, "RunThreadPoolTests")],
         sig="void RunTSListTests(void); void RunTSQueueTests(void); void RunThreadPoolTests(void)",
         basis="C names (no types); the RTTI shows classes TSListTests::CTestOps, CQueueOps, CListOps and ThreadPoolTest::CCountJob, CExecuteTestJob",
         test="Call each; capture Msg output through a listener.",
         check="Original code reports its own failures through Error/Warning; none is raised, and the call returns.",
         oracle="The engine's own assertions.", shims=["kernel32!CreateThread", "kernel32!InterlockedPushEntrySList / InterlockedPopEntrySList / InterlockedFlushSList",
                                                     "kernel32!Sleep", "kernel32!WaitForSingleObject"],
         requires="Threads, the SList family, and a working Msg path. Last integration check before the game DLLs."),
    dict(title="Plat_ExitProcess", roots=[E(T0, "Plat_ExitProcess")],
         sig="void Plat_ExitProcess(int code)",
         basis="disassembly: TerminateProcess(GetCurrentProcess(), code)",
         test="Run in a child process of the harness with code 7.",
         check="The child exits with status 7.", oracle="Process exit status.",
         shims=["kernel32!GetCurrentProcess", "kernel32!TerminateProcess"],
         requires="A subprocess; the runtime's exit path must be a normal exit, not a trap (finding 24)."),
]

# ---- benchmarks ------------------------------------------------------------------------------------------
BENCHMARKS = [
    dict(id="B1", title="Integer loop, byte output: EncodeBinaryToString", roots=[E(T0, "EncodeBinaryToString")], kind="tight integer loop with byte stores",
         input="One call over a 512 KB buffer into a 600 KB destination (about 12 instructions per input byte; at 3 to 10 billion lifted instructions per second that is 0.6 to 2 ms).",
         notes="Calibrate once: time one call, scale the length so it runs about 1 ms."),
    dict(id="B2", title="Integer loop with compare and shift: DecodeBinaryFromString", roots=[E(T0, "DecodeBinaryFromString")], kind="tight integer loop, branch per byte",
         input="Decode the output of B1 for a 512 KB source (one validation pass plus one decode pass, about 17 instructions per byte).", notes="Compare the ratio B1:B2 native versus lifted."),
    dict(id="B3", title="Virtual dispatch, lock and integer arithmetic: RandomInt", roots=[E(VS, "RandomInt"), E(VS, "?RandomInt@CUniformRandomStream@@UEAAHHH@Z")],
         kind="indirect jump through a vtable, critical-section enter and leave, multiply and divide",
         input="25,000 calls of RandomInt(0, 1000) (about 70 instructions and two shim crossings each, roughly 40 ns).",
         notes="Separates the cost of the dispatch table, the two shim thunks and the arithmetic; repeat with RandomInt(0, 2^30) to change the divide."),
    dict(id="B4", title="Atomic read-modify-write: CThreadSpinRWLock TryLockForRead / UnlockRead", roots=[E(T0, "?TryLockForRead@CThreadSpinRWLock@@QEAA_NXZ"), E(T0, "?UnlockRead@CThreadSpinRWLock@@QEAAXXZ")],
         kind="lock cmpxchg and lock dec", input="100,000 pairs (about 20 instructions per pair, roughly 10 ns).",
         notes="Direct measure of the cost of x86 memory-ordering mode on atomics; run with the mode on and off (RUNTIME.md E4)."),
    dict(id="B5", title="Shim crossing: CThreadMutex Lock / Unlock", roots=[E(T0, "?Lock@CThreadMutex@@QEAAXXZ"), E(T0, "?Unlock@CThreadMutex@@QEAAXXZ")],
         kind="two import thunks per iteration, uncontended critical section", input="50,000 Lock/Unlock pairs (each export is one jump to the shim, roughly 20 ns per pair).",
         notes="Measures the dispatch-table lookup plus the shim wrapper and float-mode switch (RUNTIME.md 2, 3.6) with almost no guest work."),
    dict(id="B6", title="Allocation: IMemAlloc Alloc and Free of 64 bytes", roots=[R(T0, 0x13780), R(T0, 0x14df0)], kind="size-class heap, pointer chasing, virtual calls",
         input="10,000 Alloc(64) followed by 10,000 Free in reverse order, repeated (the small-block path, about 100 to 200 instructions per call).",
         notes="Also run with 4096-byte blocks to hit the large path (VirtualAlloc-backed) and see the shim cost."),
    dict(id="B7", title="Byte compare against a linked list: CreateInterface", roots=[E(VS, "CreateInterface")], kind="byte-wise string compare loop, indirect tail jump",
         input="20,000 calls with \"VEngineCvar007\" (inserted first, so last in the list: two mismatching nodes, then a 15-byte match; about 130 instructions per call, roughly 50 ns).",
         notes="Name length and position in the list scale the loop count; no calls out. \"VProcessUtils002\" is the list head (one compare) and gives the short case."),
    dict(id="B8", title="String copy and UTF-8 scan: V_UTF8_strncpy", roots=[E(VS, "V_UTF8_strncpy")], kind="C runtime strncpy (memory copy) plus a backward scan",
         input="One call copying about 3 MB of text with maxLen cutting inside a 4-byte character (strncpy dominates, about 12 instructions per 8 bytes; scale the length to about 1 ms).",
         notes="strncpy here is a word-at-a-time loop with no calls (lifted), so this is integer and load/store throughput, not memmove."),
    dict(id="B9", title="Shim-bound conversion: V_UTF8ToUnicode", roots=[E(VS, "V_UTF8ToUnicode")], kind="boundary cost, host conversion",
         input="One call converting about 1 MB of mixed ASCII and two-byte text (the time is the MultiByteToWideChar shim; the guest does 30 instructions).", notes="Tells whether shim work or guest work dominates a typical call."),
    dict(id="B10", title="Calendar arithmetic: Plat_gmtime", roots=[E(T0, "Plat_gmtime")], kind="integer divide and modulo chains, small struct writes",
         input="10,000 calls with times spread over 1970 to 2100 (about 150 to 300 instructions each).", notes="Division-heavy; compare divide cost between the lifted code and native."),
    dict(id="B11", title="Time source cost: Plat_FloatTime (after the lifter handles scalar double)", roots=[E(T0, "Plat_FloatTime")], kind="shim call plus scalar double conversion and divide",
         input="20,000 calls (QueryPerformanceCounter shim, two cvtsi2sd, one divsd).", notes="Not runnable today; it is what every engine frame calls repeatedly."),
    dict(id="B13", title="Memory copy, internal: the C runtime memmove (tier0 RVA 0x249c0; vstdlib 0x2f5f0)", roots=[R(T0, 0x249c0)],
         kind="bulk copy with non-temporal stores and an overlap check",
         input="One 256 KB copy (rep movsb or movnti loops; about 0.1 to 0.3 ms native), repeated 4 times for 1 ms.",
         notes="No export is a plain memory copy, so call it by its RVA through the dispatch table. It is skipped by the lifter today (movnti); "
               "memset (RVA 0x25bd0) is the same case. Both are on the load path of both DLLs."),
    dict(id="B12", title="String search through a table: LoggingSystem_FindChannel", roots=[E(T0, "LoggingSystem_FindChannel")], kind="loop over 0x38-byte records with a string compare each",
         input="30,000 calls looking up \"General\" (add channels with LoggingSystem_RegisterLoggingChannel to lengthen the loop; each record costs one case-insensitive compare).", notes="Compare loop and call-return costs."),
]


def resolve(spec):
    module, kind, key = spec
    M = _mods[module]
    if kind == "rva":
        return M, key
    for e in M.exports:
        if e["name"] == key:
            return M, e["rva"]
    raise KeyError("no export %s in %s" % (key, module))


_mods = {}


def metrics(roots, entry_written):
    insns = 0
    lifted = True
    units, skipped, closure_insns, indirect = set(), set(), 0, 0
    imports, data_imports = set(), set()
    gread, gwrite = set(), set()
    flags = {"threads": False, "files": False, "allocator": False}
    cut = set()
    mod = None
    for spec in roots:
        M, rva = resolve(spec)
        mod = M.name
        r = M.rep(rva)
        insns += r["insns"] if r else 0
        st = M.status(rva)
        lifted = lifted and st != "skipped" and st != "not-in-report"
        c = M.closure(rva)
        key = M.name
        units |= {(key, u) for u in c["units"]}
        skipped |= {(key, u) for u in c["skipped"]}
        closure_insns += c["insns"]
        indirect += c["indirect"]
        imports |= {(key, d, n) for d, n in c["imports"]}
        data_imports |= {(d, n) for d, n in c["data_imports"]}
        gread |= {(key, g) for g in c["globals_read"]}
        gwrite |= {(key, g) for g in c["globals_written"]}
        cut |= {(key, u) for u in c["cut"]}
    names = {n for k, d, n in imports}
    flags["threads"] = bool(names & THREAD_API)
    flags["files"] = bool(names & FILE_API)
    touched_all = {g for k, g in gread | gwrite}
    flags["allocator"] = (any(d == "tier0.dll" and n in ("g_pMemAllocSingleton", "CreateGlobalMemAlloc") for k, d, n in imports)
                          or any(d == "tier0.dll" and n == "g_pMemAllocSingleton" for d, n in data_imports)
                          or ALLOC_GLOBALS.get(T0) in touched_all)
    ew = {g for g in entry_written}
    needs_init = sorted(hex(g) for k, g in (gread | gwrite) if g in ew and (k, g) in (gread | gwrite))
    return {
        "root_insns": insns, "roots_lifted": lifted, "closure_units": len(units), "closure_insns": closure_insns,
        "skipped_in_closure": sorted("%s+%#x" % (k, u) for k, u in skipped), "indirect_call_sites": indirect,
        "windows_imports": sorted({"%s!%s" % (d, n) for k, d, n in imports if not d.startswith(("tier0", "vstdlib"))}),
        "tier0_imports": sorted({n for k, d, n in imports if d.startswith("tier0")}),
        "cut_error_paths": len(cut), "globals_touched": len(touched_all),
        "globals_set_by_startup": needs_init[:12], "globals_set_by_startup_count": len(needs_init),
        "uses": [k for k, v in flags.items() if v],
    }


def build(entry_written_by_module=None):
    for mn in L.MODULES:
        _mods[mn] = L.load(mn)
    if entry_written_by_module is None:
        import gate2_entry
        entry_written_by_module = {}
        for mn in L.MODULES:
            M, R_ = gate2_entry.analyze(mn)
            entry_written_by_module[mn] = set(R_["entry_globals_written"])
    ew = set()
    for mn in L.MODULES:
        ew |= entry_written_by_module[mn]
    out = {"candidates": [], "benchmarks": []}
    for i, c in enumerate(CANDIDATES, 1):
        d = dict(c)
        d["rank"] = i
        d["roots"] = [{"module": m, "kind": k, "key": ("%#x" % key) if isinstance(key, int) else key} for m, k, key in c["roots"]]
        d["metrics"] = metrics(c["roots"], ew)
        out["candidates"].append(d)
    for b in BENCHMARKS:
        d = dict(b)
        d["roots"] = [{"module": m, "kind": k, "key": ("%#x" % key) if isinstance(key, int) else key} for m, k, key in b["roots"]]
        d["metrics"] = metrics(b["roots"], ew)
        out["benchmarks"].append(d)
    return out


def main():
    d = build()
    for c in d["candidates"]:
        m = c["metrics"]
        print("%2d %-70s insns=%-4d lifted=%-5s closure=%-5d skipped=%d ind=%-3d uses=%s imports=%d" % (
            c["rank"], c["title"][:70], m["root_insns"], m["roots_lifted"], m["closure_insns"], len(m["skipped_in_closure"]),
            m["indirect_call_sites"], m["uses"], len(m["windows_imports"])))
    for b in d["benchmarks"]:
        m = b["metrics"]
        print("%-4s %-60s insns=%-4d lifted=%-5s closure=%-5d skipped=%d" % (b["id"], b["title"][:60], m["root_insns"], m["roots_lifted"], m["closure_insns"], len(m["skipped_in_closure"])))
    json.dump(d, open("/dev/null", "w"), default=str)


if __name__ == "__main__":
    main()
