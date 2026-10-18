/* refcalls.exe: the calls of our Gate 2 driver, made to the real tier0.dll and vstdlib.dll.
 *
 * A Windows x64 console program for the owner's other machine (tools/ref/run_ref.sh runs it
 * under Proton's Wine; docs/handoff/ref-proton.md).  It compiles the driver's own call tables
 * (gate2/calls.c, calls_more.c, calls_wide.c: ranks 1 to 90) against tools/ref/compat, where a
 * "guest call" is a plain call of the DLL's function, and prints for every rank what the
 * driver prints with GATE2_VALUES=1 (gate2/values.h): the values checked and the verdict.
 * tools/ref/compare.py puts the two side by side.
 *
 *   refcalls.exe calls <dir> [--timeout <s>] [<rank> | <first>-<last> ...]
 *       Every rank (or the ones named), each in a process of its own with a time limit
 *       (120 s), so that a fault or a hang in one loses nothing else.
 *   refcalls.exe queue <dir> [--runs <n>] [--timeout <s>]
 *       tier0's queue self-test, RunTSQueueTests(10000, 1) as rank 29 calls it, n times (400),
 *       each in a fresh process with a time limit (15 s); per run: PASS, FAIL with the message,
 *       CRASH with the exception code, or TIMEOUT; then the counts.
 *
 * <dir> is the directory that holds tier0.dll and vstdlib.dll; the DLLs they import are found
 * there too.  tier0.dll imports OriginSDK.dll, so loading it loads that DLL; this program
 * never looks up or calls anything in it, and refuses the ten Origin and language exports of
 * tier0 by name (a rank that asks for one is reported SKIP).  Rank 29 is not run in `calls`:
 * its thread-pool test cannot return, by the game's own code.  Nothing here opens a window or
 * a socket, and no rank calls an export that does.
 *
 * The program contains no game code: every function of the DLLs is found by name with
 * GetProcAddress at run time (which is also why it needs no import library made from them,
 * starts when an export is missing, and can load the DLLs from a directory given at run time).
 *
 * Built on the Mac without a Windows SDK or C library (tools/ref/ref.mk): Apple clang makes
 * the objects, lld-link links them against an import library made from kernel32.def, the C
 * library is tools/ref/ref_libc.c.  It has never been run by its authors: see the handoff.
 *
 * Output lines (one tab between fields):
 *   # ...                                    remarks
 *   L <OK|ERROR> <text>                      the load of the two DLLs, tried once before the ranks
 *   V <rank> <times> <first> <last> <digest> <E|M> <what>  a checked (E) or measured (M) value (gate2/values.h)
 *   X <rank> <code> <where> <first chance|unhandled>     an exception seen in the rank's process
 *   R <rank> <status> <checks> <digest> <name> <message> the rank: PASS FAIL SKIP CRASH TIMEOUT ERROR
 *   Q <run> <status> <ms> <bad links> <message>          one run of the queue self-test
 *   S <mode> <counts>                        the last line of a complete run
 */
#include "ref_rt.h"
#include "rt/win/base.h"
#include "rt/win/error.h"
#include "rt/win/exception.h"
#include "rt/win/file.h"
#include "rt/win/memory.h"
#include "rt/win/pe.h"
#include "rt/win/sysinfo.h"

#include "calls.h"
#include "host.h"
#include "pool.h"
#include "values.h"

/* ---- Windows: the functions used, all of kernel32 (kernel32.def), and what the runtime's headers lack ---- */

#define K32 __declspec(dllimport)
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } RefFileTime;
typedef struct {                                /* STARTUPINFOA, 104 bytes */
    DWORD  cb; char *lpReserved, *lpDesktop, *lpTitle;
    DWORD  dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD   wShowWindow, cbReserved2; BYTE *lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} RefStartupInfo;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } RefProcessInfo;
typedef struct { DWORD dwSize, cntUsage, th32ThreadID, th32OwnerProcessID; LONG tpBasePri, tpDeltaPri; DWORD dwFlags; } RefThreadEntry;
_Static_assert(sizeof(RefStartupInfo) == 104 && offsetof(RefStartupInfo, dwFlags) == 60 && offsetof(RefStartupInfo, hStdOutput) == 88, "STARTUPINFOA");
_Static_assert(sizeof(RefProcessInfo) == 24 && sizeof(RefThreadEntry) == 28, "PROCESS_INFORMATION, THREADENTRY32");
_Static_assert(sizeof(SECURITY_ATTRIBUTES) == 24 && sizeof(SYSTEM_INFO) == 48 && sizeof(MEMORYSTATUSEX) == 64 && sizeof(OSVERSIONINFOA) == 148 &&
               sizeof(TIME_ZONE_INFORMATION) == 172, "sizes Windows checks or fills");
_Static_assert(sizeof(long) == 4 && sizeof(void *) == 8 && sizeof(size_t) == 8, "this file is for Windows x64");

#define REF_SEM_QUIET              0x8003u      /* SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX */
#define REF_STARTF_USESTDHANDLES   0x100u
#define REF_TH32CS_SNAPTHREAD      0x4u

K32 char  *GetCommandLineA(void);
K32 void   ExitProcess(UINT code);
K32 BOOL   TerminateProcess(HANDLE process, UINT code);
K32 HANDLE GetCurrentProcess(void);
K32 DWORD  GetCurrentProcessId(void);
K32 DWORD  GetCurrentThreadId(void);
K32 DWORD  GetModuleFileNameA(HANDLE module, char *buf, DWORD size);
K32 BOOL   CreateProcessA(const char *app, char *command, void *pa, void *ta, BOOL inherit, DWORD flags, void *env, const char *dir,
                          RefStartupInfo *si, RefProcessInfo *pi);
K32 DWORD  WaitForSingleObject(HANDLE h, DWORD ms);
K32 BOOL   GetExitCodeProcess(HANDLE process, DWORD *code);
K32 BOOL   GetExitCodeThread(HANDLE thread, DWORD *code);
K32 BOOL   CloseHandle(HANDLE h);
K32 void   Sleep(DWORD ms);
K32 HANDLE GetStdHandle(DWORD which);
K32 BOOL   WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *written, void *overlapped);
K32 BOOL   ReadFile(HANDLE h, void *buf, DWORD n, DWORD *read, void *overlapped);
K32 HANDLE CreateFileA(const char *name, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa, DWORD disposition, DWORD flags, HANDLE tmpl);
K32 DWORD  SetFilePointer(HANDLE h, LONG distance, LONG *high, DWORD method);
K32 BOOL   SetEndOfFile(HANDLE h);
K32 BOOL   SetDllDirectoryA(const char *dir);
K32 HANDLE LoadLibraryExA(const char *name, HANDLE file, DWORD flags);
K32 HANDLE GetModuleHandleA(const char *name);
K32 void  *GetProcAddress(HANDLE module, const char *name);
K32 DWORD  GetLastError(void);
K32 UINT   SetErrorMode(UINT mode);
K32 void  *SetUnhandledExceptionFilter(LONG (*filter)(EXCEPTION_POINTERS *));
K32 void  *AddVectoredExceptionHandler(ULONG first, LONG (*handler)(EXCEPTION_POINTERS *));
K32 HANDLE CreateThread(SECURITY_ATTRIBUTES *sa, SIZE_T stack, DWORD (*start)(void *), void *arg, DWORD flags, DWORD *id);
K32 void  *TlsGetValue(DWORD index);
K32 HANDLE CreateToolhelp32Snapshot(DWORD flags, DWORD pid);
K32 BOOL   Thread32First(HANDLE snapshot, RefThreadEntry *e);
K32 BOOL   Thread32Next(HANDLE snapshot, RefThreadEntry *e);
K32 HANDLE GetProcessHeap(void);
K32 void  *HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size);
K32 BOOL   HeapFree(HANDLE heap, DWORD flags, void *p);
K32 void   GetSystemTimeAsFileTime(RefFileTime *out);
K32 BOOL   QueryPerformanceCounter(LARGE_INTEGER *out);
K32 BOOL   QueryPerformanceFrequency(LARGE_INTEGER *out);
K32 DWORD  GetTickCount(void);
K32 DWORD  GetTimeZoneInformation(TIME_ZONE_INFORMATION *out);
K32 void   GetSystemInfo(SYSTEM_INFO *out);
K32 BOOL   GlobalMemoryStatusEx(MEMORYSTATUSEX *out);
K32 BOOL   GetVersionExA(OSVERSIONINFOA *out);

/* The linker's name for "this object uses floating point", and the stack probe the compiler
 * calls in a function whose frame is a page or more: rax is the frame's size, every register
 * is kept, and each page from the caller's stack pointer down is touched in order, so that
 * the guard page is moved one page at a time (the routine the Microsoft ABI calls __chkstk). */
int _fltused = 1;
__asm__(".text\n"
        ".globl __chkstk\n"
        ".def __chkstk; .scl 2; .type 32; .endef\n"
        "__chkstk:\n"
        "    pushq %rcx\n"
        "    pushq %rax\n"
        "    cmpq $0x1000, %rax\n"
        "    leaq 24(%rsp), %rcx\n"
        "    jb 1f\n"
        "2:  subq $0x1000, %rcx\n"
        "    testq %rcx, (%rcx)\n"
        "    subq $0x1000, %rax\n"
        "    cmpq $0x1000, %rax\n"
        "    ja 2b\n"
        "1:  subq %rax, %rcx\n"
        "    testq %rcx, (%rcx)\n"
        "    popq %rax\n"
        "    popq %rcx\n"
        "    ret\n");

/* ---- output ------------------------------------------------------------------------------------ */

static HANDLE       out_handle;
static volatile int out_lock;

static void out_bytes(const char *s, size_t n) {
    while (__atomic_exchange_n(&out_lock, 1, __ATOMIC_ACQUIRE)) Sleep(0);
    while (n) {
        DWORD w = 0;
        if (!WriteFile(out_handle, s, (DWORD)n, &w, NULL) || w == 0) break;
        s += w; n -= w;
    }
    __atomic_store_n(&out_lock, 0, __ATOMIC_RELEASE);
}

static void outf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void outf(const char *fmt, ...) {
    char line[2000];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    out_bytes(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

/* ---- the C library's Windows half (ref_libc.h) ------------------------------------------------- */

int errno;
static const char *child_selftests;             /* --selftests: what the tables read as GATE2_SELFTESTS */

void *malloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }
void *calloc(size_t n, size_t size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * size ? n * size : 1); }
void  free(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
int   fflush(void *stream) { (void)stream; return 0; }         /* nothing is buffered */
/* The tables read three settings of the driver from the environment.  Here only the one this
 * program sets itself exists; the other machine's environment decides nothing. */
char *getenv(const char *name) { return child_selftests && !strcmp(name, "GATE2_SELFTESTS") ? (char *)(uintptr_t)child_selftests : NULL; }
time_t time(time_t *out) {
    RefFileTime ft;
    GetSystemTimeAsFileTime(&ft);                               /* 100 ns units since 1601-01-01 */
    time_t t = (time_t)((((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - 116444736000000000ull) / 10000000ull);
    if (out) *out = t;
    return t;
}
int clock_gettime(int clock, struct timespec *out) {
    LARGE_INTEGER now, hz;
    (void)clock;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&hz);
    out->tv_sec = (time_t)(now.QuadPart / hz.QuadPart);
    out->tv_nsec = (int64_t)((now.QuadPart % hz.QuadPart) * 1000000000ll / hz.QuadPart);
    return 0;
}
int   usleep(unsigned us) { Sleep((us + 999) / 1000); return 0; }
pid_t fork(void) { return -1; }
pid_t waitpid(pid_t pid, int *status, int options) { (void)pid; (void)status; (void)options; return -1; }
void  _exit(int code) { TerminateProcess(GetCurrentProcess(), (UINT)code); for (;;) Sleep(1000); }

/* ---- the runtime's part (compat/ref_rt.h) ------------------------------------------------------ */

uint32_t ref_first_thread_id;
static RtModule mod_tier0 = { "tier0.dll", 0, 0 }, mod_vstdlib = { "vstdlib.dll", 0, 0 }, mod_kernel32 = { "kernel32.dll", 0, 0 },
                mod_ntdll = { "ntdll.dll", 0, 0 }, mod_self = { "refcalls.exe", 0, 0 };

static void module_set(RtModule *m, HANDLE h) {
    m->base = (uint64_t)(uintptr_t)h;
    m->size = 0;
    if (h) {                                    /* SizeOfImage: 0x50 after the "PE\0\0" that the dword at 0x3c points to */
        const uint8_t *p = (const uint8_t *)h;
        uint32_t pe, size;
        memcpy(&pe, p + 0x3c, 4);
        memcpy(&size, p + pe + 0x50, 4);
        m->size = size;
    }
}

RtModule *rt_module_find(const char *name) {
    RtModule *all[4] = { &mod_tier0, &mod_vstdlib, &mod_kernel32, &mod_ntdll };
    for (int i = 0; i < 4; i++) if (!strcmp(name, all[i]->name)) return all[i]->base ? all[i] : NULL;
    return NULL;
}

GUESTFN rt_module_export(RtModule *m, const char *name, uint32_t ordinal) {
    (void)ordinal;
    return m && m->base ? (GUESTFN)(uintptr_t)GetProcAddress((HANDLE)(uintptr_t)m->base, name) : 0;
}

void ref_host_ctx(Ctx *ctx, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    memset(ctx, 0, sizeof *ctx);
    ctx->r[1] = a0; ctx->r[2] = a1; ctx->r[8] = a2; ctx->r[9] = a3;
    ctx->thread.id = GetCurrentThreadId();
}

/* Sixteen argument slots are always passed: the caller owns the argument area in this
 * calling convention, so a callee that takes fewer never sees the rest. */
typedef uint64_t (*RefFnInt)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
/* Declared variadic on purpose: for a call through such a type the compiler must put each of
 * the first four arguments in its xmm register AND in its integer register, so one call
 * serves a callee that takes an integer or a float in any of those positions. */
typedef double (*RefFnMixed)(double, double, double, double, ...);

uint64_t rt_guest_call(Ctx *ctx, GUESTFN fn, unsigned nargs, const uint64_t *args) {
    uint64_t a[RT_GUEST_CALL_MAX_ARGS] = { 0 };
    (void)ctx;
    for (unsigned i = 0; i < nargs && i < RT_GUEST_CALL_MAX_ARGS; i++) a[i] = args[i];
    return ((RefFnInt)(uintptr_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
}

static double call_mixed(GUESTFN fn, const RtArg *args, unsigned nargs) {
    uint64_t a[RT_GUEST_CALL_MAX_ARGS] = { 0 };
    double d[4];
    for (unsigned i = 0; i < nargs && i < RT_GUEST_CALL_MAX_ARGS; i++) {
        if (args[i].cls == RT_AC_F32) { uint32_t b; memcpy(&b, &args[i].v.f, 4); a[i] = b; }      /* a float is the low half of its slot */
        else if (args[i].cls == RT_AC_F64) memcpy(&a[i], &args[i].v.d, 8);
        else a[i] = args[i].v.u;
    }
    memcpy(d, a, sizeof d);
    return ((RefFnMixed)(uintptr_t)fn)(d[0], d[1], d[2], d[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
}

double rt_guest_call_fp(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs) {
    (void)ctx;
    return call_mixed(fn, args, nargs);
}

RtGuestRet rt_guest_call_ex(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs) {
    RtGuestRet r;
    (void)ctx;
    memset(&r, 0, sizeof r);
    r.xmm0.pd[0] = call_mixed(fn, args, nargs);                 /* a float result is the low half: xmm0.f[0] */
    return r;
}

const RtMachine *rt_machine(void) {
    static RtMachine m;
    SYSTEM_INFO si;
    MEMORYSTATUSEX ms;
    OSVERSIONINFOA v;
    memset(&si, 0, sizeof si); memset(&ms, 0, sizeof ms); memset(&v, 0, sizeof v);
    ms.dwLength = sizeof ms;
    v.dwOSVersionInfoSize = sizeof v;
    GetSystemInfo(&si);
    GlobalMemoryStatusEx(&ms);
    GetVersionExA(&v);
    m.os_major = v.dwMajorVersion;
    m.cpu_count = si.dwNumberOfProcessors;
    m.cpu_hz = 0;
    m.phys_total = ms.ullTotalPhys;
    return &m;
}

static unsigned threads_now(void) {
    HANDLE snap = CreateToolhelp32Snapshot(REF_TH32CS_SNAPTHREAD, 0);
    RefThreadEntry e;
    unsigned n = 0;
    DWORD pid = GetCurrentProcessId();
    if (snap == INVALID_HANDLE_VALUE || !snap) return 0;
    memset(&e, 0, sizeof e);
    e.dwSize = sizeof e;
    if (Thread32First(snap, &e)) {
        do { if (e.th32OwnerProcessID == pid) n++; e.dwSize = sizeof e; } while (Thread32Next(snap, &e));
    }
    CloseHandle(snap);
    return n;
}

unsigned rt_thread_count(void) {
    static unsigned last;
    unsigned n = threads_now();
    for (int i = 0; i < 50 && last && n != last; i++) { Sleep(5); n = threads_now(); }
    last = n;
    return n;
}

BOOL     rt_thread_get_exit_code(HANDLE thread, DWORD *code) { return GetExitCodeThread(thread, code); }
BOOL     rt_handle_close(HANDLE h) { return CloseHandle(h); }
uint64_t rt_tls_get(Ctx *ctx, DWORD index) { (void)ctx; return (uint64_t)(uintptr_t)TlsGetValue(index); }
LONG     rt_time_zone_bias_minutes(void) {
    TIME_ZONE_INFORMATION tz;
    memset(&tz, 0, sizeof tz);
    GetTimeZoneInformation(&tz);
    return tz.Bias;
}

/* ---- what the tables need from the driver (gate2/calls.h, host.h, pool.h) ----------------------- */

static Gate2Values  values;
static const Entry *entry_running;
static Call        *call_running;
#define AT_KEPT 3
static char         at_line[AT_KEPT][200];      /* the last lines given to gate2_progress */
static unsigned     at_count;

/* The ten exports of tier0.dll that reach EA's Origin SDK or decide the language: never looked
 * up, never called (swarm/briefs/_round3-rules.md; docs/handoff/round3-plan.md 2.4 item 1). */
static const char *const refused_exports[] = {
    "Tier0_InitOrigin", "Tier0_ShutdownOrigin", "Tier0_GetOriginLanguage", "Tier0_IsOriginInit", "Tier0_GetOriginStartupResult",
    "Tier0_GetOriginGameInfo", "Tier0_GetOriginVersionString", "Tier0_DetectDefaultLanguage", "DetectLanguage", "GetLanguageName" };

GUESTFN gate2_export(Call *c, RtModule *m, const char *name) {
    for (unsigned i = 0; i < sizeof refused_exports / sizeof refused_exports[0]; i++)
        if (!strcmp(name, refused_exports[i])) {
            gate2_skip(c, "the rank asks for %s, one of the ten Origin and language exports: this program does not call them", name);
            return 0;
        }
    if (!m) { gate2_fail_text(c, "no module to look up %s in", name); return 0; }
    GUESTFN f = rt_module_export(m, name, 0);
    if (!f) gate2_fail_text(c, "no export %s in %s", name, m->name);
    return f;
}

void gate2_fail(Call *c, const char *what, uint64_t got, uint64_t want) {
    gate2_fail_text(c, "%s: got %#llx (%lld), expected %#llx (%lld)", what, (unsigned long long)got, (long long)got,
                    (unsigned long long)want, (long long)want);
}

void gate2_fail_text(Call *c, const char *fmt, ...) {
    va_list ap;
    if (c->result != GATE2_PASS) return;        /* the first failure is the one reported */
    c->result = GATE2_FAIL;
    va_start(ap, fmt);
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
    va_end(ap);
}

void gate2_skip(Call *c, const char *fmt, ...) {
    va_list ap;
    if (c->result != GATE2_PASS) return;
    c->result = GATE2_SKIP;
    va_start(ap, fmt);
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
    va_end(ap);
}

void gate2_note(Call *c, const char *text) {
    if (c->result == GATE2_PASS) snprintf(c->msg, sizeof c->msg, "(%s)", text);
}

bool gate2_check(Call *c, const char *what, uint64_t got, uint64_t want) {
    gate2_values_record(&values, 'E', what, got);
    if (got == want) return true;
    gate2_fail(c, what, got, want);
    return false;
}

bool gate2_check_true(Call *c, const char *what, int holds) {
    gate2_values_record(&values, 'E', what, (uint64_t)holds);
    if (holds) return true;
    gate2_fail_text(c, "%s: does not hold", what);
    return false;
}

void gate2_value(Call *c, const char *what, uint64_t value) {
    (void)c;
    gate2_values_record(&values, 'M', what, value);
}

void *gate2_mem(size_t size) {                  /* zeroed, 16-byte aligned, never freed */
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
    if (!p) { outf("# gate2_mem(%llu): the heap gave nothing\n", (unsigned long long)size); TerminateProcess(GetCurrentProcess(), 12); }
    return p;
}

void gate2_progress(const char *what) {
    char *line = at_line[__atomic_fetch_add(&at_count, 1, __ATOMIC_RELAXED) % AT_KEPT];
    size_t k = 0;
    for (; what[k] && k < sizeof at_line[0] - 1; k++) line[k] = what[k] == '\t' || what[k] == '\n' || what[k] == '\r' ? ' ' : what[k];
    line[k] = 0;
    outf("A\t%d\t%s\n", entry_running ? entry_running->rank : 0, line);
}

static const char *status_text(int result) { return result == GATE2_PASS ? "PASS" : result == GATE2_FAIL ? "FAIL" : "SKIP"; }
enum { EXIT_PASS = 0, EXIT_FAIL = 10, EXIT_SKIP = 11, EXIT_ERROR = 12, EXIT_CRASH = 13, EXIT_STOPPED = 14 };

/* The rank's V lines and its R line; then the process ends without unloading the DLLs (the
 * driver's children end with _exit: no static destructor of the DLLs runs there either). */
static void child_report(const char *status, const char *msg, UINT code) __attribute__((noreturn));
static void child_report(const char *status, const char *msg, UINT code) {
    char line[1400];
    int rank = entry_running ? entry_running->rank : 0;
    for (unsigned i = 0; gate2_values_line(&values, i, rank, line, sizeof line) > 0; i++) outf("%s\n", line);
    if (values.dropped) outf("# rank %d: %u checks had no free row in the value table\n", rank, values.dropped);
    gate2_values_result(&values, rank, status, entry_running ? entry_running->name : "", msg, line, sizeof line);
    outf("%s\n", line);
    TerminateProcess(GetCurrentProcess(), code);
    for (;;) Sleep(1000);
}

void gate2_end(Call *c) {
    child_report(status_text(c->result), c->msg, c->result == GATE2_PASS ? EXIT_PASS : c->result == GATE2_FAIL ? EXIT_FAIL : EXIT_SKIP);
}

/* A second thread that runs a function of the tables (host.h). */
typedef struct {
    void (*fn)(Call *c, void *arg);
    void  *arg;
    Call  *parent;
    int    result;
    char   msg[600];
    HANDLE h;
    DWORD  id;
} Job;

static DWORD thread_main(void *arg) {
    Job *job = arg;
    Ctx cx;
    Call c = *job->parent;
    ref_host_ctx(&cx, 0, 0, 0, 0);
    c.ctx = &cx;
    c.result = GATE2_PASS;
    c.msg[0] = 0;
    job->fn(&c, job->arg);
    job->result = c.result;
    memcpy(job->msg, c.msg, sizeof job->msg);
    return 0;
}

void *gate2_thread_start(Call *c, void (*fn)(Call *c2, void *arg), void *arg) {
    Job *job = calloc(1, sizeof *job);
    job->fn = fn; job->arg = arg; job->parent = c; job->result = GATE2_PASS;
    job->h = CreateThread(NULL, 0, thread_main, job, 0, &job->id);
    if (!job->h) { gate2_fail_text(c, "CreateThread failed, last error %u", (unsigned)GetLastError()); free(job); return NULL; }
    return job;
}

bool gate2_thread_finish(Call *c, void *token) {
    Job *job = token;
    DWORD code = 0;
    DWORD w = WaitForSingleObject(job->h, 60000);
    if (w != 0) { gate2_fail_text(c, "the second thread did not end in 60 s (wait result %#x)", (unsigned)w); return false; }
    GetExitCodeThread(job->h, &code);
    CloseHandle(job->h);
    bool ok = false;
    if (job->result == GATE2_FAIL) gate2_fail_text(c, "on the second thread: %s", job->msg);
    else if (job->result == GATE2_SKIP) gate2_skip(c, "%s", job->msg);
    else if (code != 0) gate2_fail_text(c, "the second thread ended with code %u", (unsigned)code);
    else ok = true;
    free(job);
    return ok;
}

bool gate2_on_thread(Call *c, void (*fn)(Call *c2, void *arg), void *arg, uint32_t *thread_id) {
    Job *job = gate2_thread_start(c, fn, arg);
    if (!job) return false;
    if (thread_id) *thread_id = job->id;
    return gate2_thread_finish(c, job);
}

/* The logging listener (host.h): an object {vtable} whose slot 1 is Log(this, context, text). */
static Gate2Listener *listener_rec;

static uint64_t listener_slot0(void *self) {
    (void)self;
    if (listener_rec) listener_rec->slot0_calls++;
    return 0;
}

static uint64_t listener_log(void *self, const uint32_t *context, const char *text) {
    Gate2Listener *r = listener_rec;
    (void)self;
    if (r) {
        unsigned i = r->calls++;
        if (i < GATE2_LISTENER_KEPT) {
            r->channel[i] = context[0];
            r->severity[i] = context[2];
            r->colour[i] = context[3];
            snprintf(r->text[i], sizeof r->text[i], "%s", text ? text : "(null)");
        }
        if (context[2] > r->worst) r->worst = context[2];
        if (text && (strstr(text, "FAIL") || strstr(text, "invalid value")) && r->fails++ == 0)
            snprintf(r->first_fail, sizeof r->first_fail, "%s", text);
        if (r->echo && text) {
            char line[160];
            snprintf(line, sizeof line, "message %u: %s", r->calls, text);
            gate2_progress(line);
        }
    }
    return 0;
}

uint64_t gate2_listener(Gate2Listener *rec) {
    uint64_t *vtable = gate2_mem(16), *obj = gate2_mem(8);
    vtable[0] = (uint64_t)(uintptr_t)listener_slot0;
    vtable[1] = (uint64_t)(uintptr_t)listener_log;
    obj[0] = (uint64_t)(uintptr_t)vtable;
    memset(rec, 0, sizeof *rec);
    listener_rec = rec;
    return (uint64_t)(uintptr_t)obj;
}

/* The pool watcher belongs to rank 29's thread-pool test, which this program does not run. */
bool gate2_pool_watch(Call *c, Gate2Pool *r) { (void)r; gate2_skip(c, "the thread-pool test is not run by this program: it cannot return"); return false; }
void gate2_pool_watcher(Call *c, unsigned idle_ms, void (*verdict)(Call *c)) { (void)c; (void)idle_ms; (void)verdict; }
void gate2_pool_touch(void) { }

/* ---- command line and processes ----------------------------------------------------------------- */

#define MAX_ARGS 40
static char  arg_text[2048];
static char *cmd_arg[MAX_ARGS];
static int   cmd_nargs;

/* The Microsoft rules: white space separates; a double quote starts or ends a quoted part;
 * 2n backslashes before a quote are n backslashes, 2n+1 are n and a literal quote. */
static void parse_command_line(const char *s) {
    char *o = arg_text, *end = arg_text + sizeof arg_text - 1;
    while (*s && cmd_nargs < MAX_ARGS) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        cmd_arg[cmd_nargs++] = o;
        int quoted = 0;
        while (*s && (quoted || (*s != ' ' && *s != '\t'))) {
            unsigned slashes = 0;
            while (*s == '\\') { slashes++; s++; }
            if (*s == '"') {
                for (unsigned i = 0; i < slashes / 2 && o < end; i++) *o++ = '\\';
                if (slashes & 1) { if (o < end) *o++ = '"'; } else quoted = !quoted;
                s++;
            } else {
                for (unsigned i = 0; i < slashes && o < end; i++) *o++ = '\\';
                if (*s && (quoted || (*s != ' ' && *s != '\t'))) { if (o < end) *o++ = *s; s++; }
            }
        }
        if (o < end) *o++ = 0; else { *end = 0; break; }
    }
}

static char self_path[600], dll_dir[600];

static void set_dir(const char *dir) {
    size_t n = strlen(dir);
    if (n >= sizeof dll_dir) n = sizeof dll_dir - 1;
    memcpy(dll_dir, dir, n);
    while (n > 1 && (dll_dir[n - 1] == '\\' || dll_dir[n - 1] == '/')) n--;       /* a trailing backslash would swallow the closing quote */
    dll_dir[n] = 0;
}

typedef struct {
    bool  started, timed_out, no_file;         /* no_file: the file for the child's output could not be created */
    DWORD exit_code, ms, error;
    char *out;                                  /* what the child wrote, NUL-terminated */
} Child;

static HANDLE child_file = INVALID_HANDLE_VALUE;
static char   child_out[1 << 19];

/* Run this program again with `tail` after its own name; the child's output goes to a file
 * in the current directory that both hold open, so nothing can block on a pipe.  The file
 * (refcalls-<process id>.tmp) is left behind: run_ref.sh runs the program in a directory it
 * removes afterwards. */
static void run_child(const char *tail, DWORD timeout_ms, Child *res) {
    char command[1600];
    RefStartupInfo si;
    RefProcessInfo pi;
    memset(res, 0, sizeof *res);
    res->out = child_out;
    child_out[0] = 0;
    if (child_file == INVALID_HANDLE_VALUE) {
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };                       /* the handle is inherited */
        char name[64];
        snprintf(name, sizeof name, "refcalls-%u.tmp", (unsigned)GetCurrentProcessId());
        child_file = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
        if (child_file == INVALID_HANDLE_VALUE) { res->error = GetLastError(); res->no_file = true; return; }
    }
    SetFilePointer(child_file, 0, NULL, FILE_BEGIN);
    SetEndOfFile(child_file);
    snprintf(command, sizeof command, "\"%s\" %s", self_path, tail);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = REF_STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = child_file;
    si.hStdError = child_file;
    DWORD t0 = GetTickCount();
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) { res->error = GetLastError(); return; }
    res->started = true;
    if (WaitForSingleObject(pi.hProcess, timeout_ms) != 0) {
        res->timed_out = true;
        TerminateProcess(pi.hProcess, EXIT_STOPPED);
        WaitForSingleObject(pi.hProcess, 10000);
    }
    res->ms = GetTickCount() - t0;
    GetExitCodeProcess(pi.hProcess, &res->exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    SetFilePointer(child_file, 0, NULL, FILE_BEGIN);
    size_t n = 0;
    for (;;) {
        DWORD got = 0;
        if (n >= sizeof child_out - 1 || !ReadFile(child_file, child_out + n, (DWORD)(sizeof child_out - 1 - n), &got, NULL) || got == 0) break;
        n += got;
    }
    child_out[n] = 0;
}

/* The next line of `text` at *at (NUL-terminated in place); NULL at the end. */
static char *next_line(char *text, size_t *at) {
    char *line = text + *at;
    if (!*line) return NULL;
    size_t n = strcspn(line, "\n");
    *at += n + (line[n] == '\n');
    line[n] = 0;
    if (n && line[n - 1] == '\r') line[n - 1] = 0;
    return line;
}

/* Field `k` (from 0) of a tab-separated line, copied; "" when there is none. */
static void field(const char *line, int k, char *out, size_t cap) {
    while (k-- > 0 && *line) { line += strcspn(line, "\t"); if (*line == '\t') line++; }
    size_t n = strcspn(line, "\t");
    if (n >= cap) n = cap - 1;
    memcpy(out, line, n);
    out[n] = 0;
}

/* What a child that wrote no R line left behind: how it ended, the last exception it saw, and
 * the last lines the rank gave gate2_progress. */
typedef struct { char status[12], msg[900]; unsigned bad_links; } Outcome;

static void read_child(const Child *ch, int rank, DWORD timeout_ms, bool copy, Outcome *o) {
    char *last_x = NULL, *last_a[2] = { NULL, NULL }, *result = NULL;
    size_t at = 0;
    memset(o, 0, sizeof *o);
    for (char *line; (line = next_line(ch->out, &at)); ) {
        if (line[0] == 'R' && line[1] == '\t') result = line;
        else if (line[0] == 'X' && line[1] == '\t') last_x = line;
        else if (line[0] == 'A' && line[1] == '\t') { last_a[0] = last_a[1]; last_a[1] = line; if (strstr(line, "Bad node link")) o->bad_links++; }
        if (copy && line[0] != 'A' && line[0] != 'R' && line[0]) outf("%s\n", line);
    }
    if (result) {
        field(result, 2, o->status, sizeof o->status);
        field(result, 6, o->msg, sizeof o->msg);
        if (copy) outf("%s\n", result);
        return;
    }
    size_t n;
    if (!ch->started) {
        strcpy(o->status, "ERROR");
        n = (size_t)snprintf(o->msg, sizeof o->msg, ch->no_file ? "the file for the rank's output could not be created in the current directory, last error %u"
                             : "the rank's process could not be started (CreateProcess), last error %u", (unsigned)ch->error);
    } else if (ch->timed_out) {
        strcpy(o->status, "TIMEOUT");
        n = (size_t)snprintf(o->msg, sizeof o->msg, "no answer after %u s (stopped by this program)", (unsigned)(timeout_ms / 1000));
    } else {
        strcpy(o->status, "CRASH");
        n = (size_t)snprintf(o->msg, sizeof o->msg, "the rank's process ended with exit code %#x and no result", (unsigned)ch->exit_code);
    }
    if (last_x && n < sizeof o->msg) {
        char code[24], where[80], kind[24];
        field(last_x, 2, code, sizeof code); field(last_x, 3, where, sizeof where); field(last_x, 4, kind, sizeof kind);
        n += (size_t)snprintf(o->msg + n, sizeof o->msg - n, "; last exception seen: %s at %s (%s)", code, where, kind);
    }
    for (int i = 0; i < 2; i++)
        if (last_a[i] && n < sizeof o->msg) {
            char text[200];
            field(last_a[i], 2, text, sizeof text);
            n += (size_t)snprintf(o->msg + n, sizeof o->msg - n, "%s%s%s", i == 0 || !last_a[0] ? " [during: " : "; ", text, i == 1 ? "]" : "");
        }
    if (last_a[0] && !last_a[1] && n < sizeof o->msg) snprintf(o->msg + n, sizeof o->msg - n, "]");
    if (copy) {
        char line[1400];
        Gate2Values none;
        const Entry *e = entry_running;
        memset(&none, 0, sizeof none);
        gate2_values_result(&none, rank, o->status, e ? e->name : "", o->msg, line, sizeof line);
        outf("%s\n", line);
    }
}

/* ---- a child: one rank --------------------------------------------------------------------------- */

static unsigned nentries(void) { return gate2_ncalls + gate2_ncalls_wide; }
static const Entry *entry_at(unsigned i) { return i < gate2_ncalls ? &gate2_calls[i] : &gate2_calls_wide[i - gate2_ncalls]; }
static const Entry *entry_of(int rank) {
    for (unsigned i = 0; i < nentries(); i++) if (entry_at(i)->rank == rank) return entry_at(i);
    return NULL;
}

/* "tier0.dll+0x1d627" for an address inside a module this program knows, else the address. */
static void where_is(uint64_t addr, char *buf, size_t cap) {
    RtModule *all[5] = { &mod_tier0, &mod_vstdlib, &mod_self, &mod_kernel32, &mod_ntdll };
    for (int i = 0; i < 5; i++)
        if (all[i]->base && addr >= all[i]->base && addr - all[i]->base < all[i]->size) {
            snprintf(buf, cap, "%s+%#llx", all[i]->name, (unsigned long long)(addr - all[i]->base));
            return;
        }
    snprintf(buf, cap, "%#llx", (unsigned long long)addr);
}

static unsigned x_lines;

/* First chance: a line for the kinds that usually end a process, and the search goes on (the
 * DLLs have handlers of their own: a C++ catch, the thread-name exception). */
static LONG on_exception(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if ((code >> 28) == 0xC || code == 0xE06D7363u || code == 0x80000003u) {
        if (__atomic_fetch_add(&x_lines, 1, __ATOMIC_RELAXED) < 6) {
            char where[80];
            where_is((uint64_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress, where, sizeof where);
            outf("X\t%d\t%#x\t%s\tfirst chance\n", entry_running ? entry_running->rank : 0, (unsigned)code, where);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Nothing handled it: report the rank as CRASH and end, before Windows (or Wine's debugger) does. */
static LONG on_unhandled(EXCEPTION_POINTERS *ep) {
    static volatile int once;
    char where[80], msg[500];
    if (__atomic_exchange_n(&once, 1, __ATOMIC_ACQUIRE)) for (;;) Sleep(1000);
    const EXCEPTION_RECORD *r = ep->ExceptionRecord;
    where_is((uint64_t)(uintptr_t)r->ExceptionAddress, where, sizeof where);
    int n = snprintf(msg, sizeof msg, "exception %#x at %s", (unsigned)r->ExceptionCode, where);
    if (r->ExceptionCode == 0xC0000005u && r->NumberParameters >= 2)
        n += snprintf(msg + n, sizeof msg - (size_t)n, " (%s address %#llx)", r->ExceptionInformation[0] == 0 ? "reading" : r->ExceptionInformation[0] == 1 ? "writing" : "executing",
                      (unsigned long long)r->ExceptionInformation[1]);
    n += snprintf(msg + n, sizeof msg - (size_t)n, " on thread %u%s", (unsigned)GetCurrentThreadId(), GetCurrentThreadId() == ref_first_thread_id ? " (the rank's own)" : "");
    unsigned have = at_count < AT_KEPT ? at_count : AT_KEPT;
    for (unsigned i = 0; i < have && (size_t)n < sizeof msg; i++)
        n += snprintf(msg + n, sizeof msg - (size_t)n, "%s%s%s", i ? "; " : " [during: ", at_line[(at_count - have + i) % AT_KEPT], i == have - 1 ? "]" : "");
    outf("X\t%d\t%#x\t%s\tunhandled\n", entry_running ? entry_running->rank : 0, (unsigned)r->ExceptionCode, where);
    child_report("CRASH", msg, EXIT_CRASH);
}

/* Load the two DLLs from dll_dir; on failure `why` says which and with what error. */
static bool load_dlls(char *why, size_t cap) {
    static const char *const names[2] = { "tier0.dll", "vstdlib.dll" };
    RtModule *mods[2] = { &mod_tier0, &mod_vstdlib };
    module_set(&mod_self, GetModuleHandleA(NULL));
    module_set(&mod_kernel32, GetModuleHandleA("kernel32.dll"));
    module_set(&mod_ntdll, GetModuleHandleA("ntdll.dll"));
    SetDllDirectoryA(dll_dir);                  /* where the DLLs' own imports are looked for */
    for (int i = 0; i < 2; i++) {
        char path[700];
        snprintf(path, sizeof path, "%s\\%s", dll_dir, names[i]);
        HANDLE h = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h) {
            DWORD e = GetLastError();
            snprintf(why, cap, "LoadLibraryEx(%s) failed, last error %u%s", path, (unsigned)e,
                     e == 126 ? " (ERROR_MOD_NOT_FOUND: the file, or a DLL it imports, was not found)" :
                     e == 193 ? " (ERROR_BAD_EXE_FORMAT: not a 64-bit DLL)" : e == 1114 ? " (ERROR_DLL_INIT_FAILED: its entry point returned FALSE)" : "");
            return false;
        }
        module_set(mods[i], h);
    }
    return true;
}

static void child_setup(void) {
    SetErrorMode(REF_SEM_QUIET);
    ref_first_thread_id = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, on_exception);
    SetUnhandledExceptionFilter(on_unhandled);
}

/* Rank 30 in the driver forks and lets the copy call Plat_ExitProcess(7).  Windows has no
 * fork: the call is made by a third process, `--exit30`, and this one checks its exit code. */
static void ref_run_30(Call *c) {
    char tail[800];
    Child ch;
    EXPORT(f, c->tier0, "Plat_ExitProcess");
    (void)f;
    snprintf(tail, sizeof tail, "--exit30 \"%s\"", dll_dir);
    run_child(tail, 60000, &ch);
    if (!ch.started) { gate2_fail_text(c, "the process that calls Plat_ExitProcess(7) could not be started, last error %u", (unsigned)ch.error); return; }
    if (ch.timed_out) { gate2_fail_text(c, "the process that called Plat_ExitProcess(7) did not end in 60 s"); return; }
    EXPECT("exit status of the child that called Plat_ExitProcess(7)", ch.exit_code, 7);
}

static int child_exit30(void) {
    char why[900];
    child_setup();
    if (!load_dlls(why, sizeof why)) ExitProcess(98);
    GUESTFN f = rt_module_export(&mod_tier0, "Plat_ExitProcess", 0);
    if (!f) ExitProcess(97);
    rt_guest_call(NULL, f, 1, &(const uint64_t){ 7 });
    ExitProcess(99);                            /* Plat_ExitProcess returned */
    return 99;
}

static int child_probe(void) {
    char why[900];
    child_setup();
    if (!load_dlls(why, sizeof why)) { outf("L\tERROR\t%s\n", why); TerminateProcess(GetCurrentProcess(), EXIT_ERROR); }
    outf("L\tOK\ttier0.dll at %#llx, %#x bytes; vstdlib.dll at %#llx, %#x bytes; %u threads in the process after the load\n",
         (unsigned long long)mod_tier0.base, (unsigned)mod_tier0.size, (unsigned long long)mod_vstdlib.base, (unsigned)mod_vstdlib.size, threads_now());
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

static int child_rank(int rank) {
    static Ctx  ctx;
    static Call c;
    char why[900];
    child_setup();
    entry_running = entry_of(rank);
    if (!entry_running) { outf("# no rank %d in the tables\n", rank); TerminateProcess(GetCurrentProcess(), EXIT_ERROR); }
    if (!load_dlls(why, sizeof why)) child_report("ERROR", why, EXIT_ERROR);
    /* What the driver's runtime does before any rank (gate2_host_before_seal, gate2/host.c):
     * the wide table fills in the addresses of its callbacks.  Without this call every rank
     * that hands the DLLs a callback would hand them address 0. */
    gate2_wide_before_seal(&mod_ntdll);
    ref_host_ctx(&ctx, 0, 0, 0, 0);
    c.ctx = &ctx;
    c.tier0 = &mod_tier0;
    c.vstdlib = &mod_vstdlib;
    c.map_only = false;
    call_running = &c;
    if (!entry_running->run) gate2_skip(&c, "not typed in yet");
    else if (rank == 30) ref_run_30(&c);
    else entry_running->run(&c);
    gate2_end(&c);
}

/* ---- the two modes ------------------------------------------------------------------------------- */

static unsigned number(const char *s, unsigned fallback) {
    unsigned v = 0;
    if (!s || !(*s >= '0' && *s <= '9')) return fallback;
    while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0');
    return v;
}

static bool probe(void) {
    char tail[800], line[900] = "";
    Child ch;
    Outcome o;
    snprintf(tail, sizeof tail, "--probe \"%s\"", dll_dir);
    run_child(tail, 120000, &ch);
    size_t at = 0;
    for (char *l; (l = next_line(ch.out, &at)); ) {
        if (l[0] == 'L' && l[1] == '\t') snprintf(line, sizeof line, "%s", l);
        else if (l[0] == 'X') outf("%s\n", l);
    }
    if (line[0]) { outf("%s\n", line); return line[2] == 'O'; }
    ch.out[0] = 0;
    read_child(&ch, 0, 120000, false, &o);
    outf("L\tERROR\tthe process that only loads the two DLLs: %s: %s\n", o.status, o.msg);
    return false;
}

static int mode_calls(int argc, char **argv) {
    static bool want[256];
    bool any = false;
    DWORD timeout_ms = 120000;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--timeout") && i + 1 < argc) { timeout_ms = number(argv[++i], 120) * 1000; continue; }
        unsigned first = number(argv[i], 0), last = first;
        const char *dash = strchr(argv[i], '-');
        if (dash && dash != argv[i]) last = number(dash + 1, 0);
        if (first < 1 || last < first || last > 255) { outf("# '%s' is not a rank or a range of ranks\n", argv[i]); return 2; }
        for (unsigned n = first; n <= last; n++) want[n] = any = true;
    }
    outf("# refcalls calls: the Gate 2 driver's ranks against the real DLLs in %s; %u ranks in the tables; %u s for each\n", dll_dir, nentries(),
         (unsigned)(timeout_ms / 1000));
    bool up = probe();
    unsigned count[6] = { 0 }, nrun = 0;
    static const char *const kinds[6] = { "PASS", "FAIL", "SKIP", "CRASH", "TIMEOUT", "ERROR" };
    for (unsigned i = 0; i < nentries(); i++) {
        const Entry *e = entry_at(i);
        char line[1400], tail[800];
        Gate2Values none;
        Outcome o;
        if (any && !want[e->rank & 255]) continue;
        nrun++;
        entry_running = e;
        memset(&none, 0, sizeof none);
        memset(&o, 0, sizeof o);
        if (e->rank == 29) {
            strcpy(o.status, "SKIP");
            gate2_values_result(&none, e->rank, "SKIP", e->name, "not run by this program: the thread-pool self-test cannot return, by the game's own code "
                                "(docs/handoff/gate2-threads.md, stop 8); the queue self-test it begins with is what `refcalls queue` measures", line, sizeof line);
            outf("%s\n", line);
        } else if (!up) {
            strcpy(o.status, "ERROR");
            gate2_values_result(&none, e->rank, "ERROR", e->name, "the two DLLs do not load: see the L line", line, sizeof line);
            outf("%s\n", line);
        } else {
            Child ch;
            snprintf(tail, sizeof tail, "--child %d \"%s\"", e->rank, dll_dir);
            run_child(tail, timeout_ms, &ch);
            read_child(&ch, e->rank, timeout_ms, true, &o);
        }
        int k = 5;
        for (int j = 0; j < 6; j++) if (!strcmp(o.status, kinds[j])) k = j;
        count[k]++;
    }
    outf("S\tcalls\tPASS=%u\tFAIL=%u\tSKIP=%u\tCRASH=%u\tTIMEOUT=%u\tERROR=%u\tof=%u\n", count[0], count[1], count[2], count[3], count[4], count[5], nrun);
    return 0;
}

static int mode_queue(int argc, char **argv) {
    unsigned runs = 400;
    DWORD timeout_ms = 15000;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = number(argv[++i], 400);
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout_ms = number(argv[++i], 15) * 1000;
        else { outf("# '%s' is not an option of queue\n", argv[i]); return 2; }
    }
    outf("# refcalls queue: RunTSQueueTests(10000, 1) of the real tier0.dll in %s, as the driver's rank 29 calls it with GATE2_SELFTESTS=queue; "
         "%u runs, each in a fresh process, %u s for each\n", dll_dir, runs, (unsigned)(timeout_ms / 1000));
    bool up = probe();
    unsigned count[5] = { 0 }, with_bad_links = 0, done = 0;
    static const char *const kinds[5] = { "PASS", "FAIL", "CRASH", "TIMEOUT", "ERROR" };
    entry_running = entry_of(29);
    for (unsigned i = 1; up && i <= runs; i++, done++) {
        char tail[800];
        Child ch;
        Outcome o;
        snprintf(tail, sizeof tail, "--child 29 \"%s\" --selftests queue", dll_dir);
        run_child(tail, timeout_ms, &ch);
        read_child(&ch, 29, timeout_ms, false, &o);
        int k = 4;
        for (int j = 0; j < 5; j++) if (!strcmp(o.status, kinds[j])) k = j;
        count[k]++;
        with_bad_links += o.bad_links != 0;
        outf("Q\t%u\t%s\t%u\t%u\t%s\n", i, kinds[k], (unsigned)ch.ms, o.bad_links, o.msg);
    }
    outf("S\tqueue\truns=%u\tPASS=%u\tFAIL=%u\tCRASH=%u\tTIMEOUT=%u\tERROR=%u\truns_with_bad_node_link=%u\n", done, count[0], count[1], count[2], count[3], count[4],
         with_bad_links);
    return 0;
}

static void usage(void) {
    outf("usage: refcalls.exe calls <dir> [--timeout <s>] [<rank> | <first>-<last> ...]\n"
         "       refcalls.exe queue <dir> [--runs <n>] [--timeout <s>]\n"
         "<dir> holds tier0.dll and vstdlib.dll (and the DLLs they import)\n");
}

void mainCRTStartup(void) {
    int rc = 2;
    out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    SetErrorMode(REF_SEM_QUIET);                /* inherited by every process this one starts */
    parse_command_line(GetCommandLineA());
    if (!GetModuleFileNameA(NULL, self_path, sizeof self_path - 1)) snprintf(self_path, sizeof self_path, "%s", cmd_nargs ? cmd_arg[0] : "refcalls.exe");
    if (cmd_nargs >= 3 && !strcmp(cmd_arg[1], "calls")) { set_dir(cmd_arg[2]); rc = mode_calls(cmd_nargs - 3, cmd_arg + 3); }
    else if (cmd_nargs >= 3 && !strcmp(cmd_arg[1], "queue")) { set_dir(cmd_arg[2]); rc = mode_queue(cmd_nargs - 3, cmd_arg + 3); }
    else if (cmd_nargs >= 4 && !strcmp(cmd_arg[1], "--child")) {
        set_dir(cmd_arg[3]);
        if (cmd_nargs >= 6 && !strcmp(cmd_arg[4], "--selftests")) child_selftests = cmd_arg[5];
        rc = child_rank((int)number(cmd_arg[2], 0));
    }
    else if (cmd_nargs >= 3 && !strcmp(cmd_arg[1], "--exit30")) { set_dir(cmd_arg[2]); rc = child_exit30(); }
    else if (cmd_nargs >= 3 && !strcmp(cmd_arg[1], "--probe")) { set_dir(cmd_arg[2]); rc = child_probe(); }
    else usage();
    ExitProcess((UINT)rc);
}
