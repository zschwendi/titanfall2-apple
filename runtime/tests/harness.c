/* The test runner and the guest-ABI call machinery (docs/RUNTIME.md section 10.4).
 * What a test sees: rt/test.h.
 *
 *     rt-test [-v] [--list] [--report] [lib/Name ...]
 *
 * With no names, every registered test runs.  Each test runs in its own forked process,
 * which starts the runtime from scratch with a temporary state directory and a temporary
 * empty install root; the parent removes them afterwards.  Exit status: 0 if every selected
 * test passed, 1 if any failed, 2 for a usage error or a name that matches no test.
 *
 * THE TIME LIMIT.  A test gets RT_TEST_TIMEOUT seconds (default DEFAULT_TIME_LIMIT_S, below)
 * of wall-clock time.  A test that has not ended by then is killed and reported as
 *     FAIL  <lib>/<Name>   (timed out after N s; ...)
 * after the last lines it logged.  Each test is the leader of its own process group, and the
 * runner kills the whole group when the test is over, in time or not: a process the test
 * forked and left behind (the child of a T_TRAPS that hangs) does not outlive it, and so
 * cannot keep the runner's output pipe open after the runner has exited.
 *
 * --report makes each test print, on stdout, lines that runtime/tools/check_shim.py reads:
 *     #rt test <lib>/<Name>
 *     #rt case <checks> <failures> <case text>
 *     #rt calls <lib>/<Name> <count>        (shims entered through CALL in this test)
 *     #rt result <pass|fail> <checks> <failures>
 */
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/test_fork.h"
#include "rt/vm.h"
#include "rttest_ids.h"
#include "rttest_com_ids.h"

/* ---- registration ---------------------------------------------------------------------------- */

static RtTestCase    *tests;
static RtTestGuestFn *guest_fns;

void rt_test_register(RtTestCase *c)            { c->next = tests; tests = c; }
void rt_test_register_guest_fn(RtTestGuestFn *g) { g->next = guest_fns; guest_fns = g; }

/* ---- state of the running test (child process) ----------------------------------------------- */

#define MAX_CASES 64
#define MAX_LOG   4096

/* Seconds of wall-clock time a test gets unless RT_TEST_TIMEOUT says otherwise. */
#define DEFAULT_TIME_LIMIT_S 300

/* The counters are atomic: the checks (T_TRUE, T_EQ, rt_test_fail) may be made by any thread
 * of a test.  Cases are started by the test's main thread only (rt/test.h, THREADS). */
typedef struct { const char *text; _Atomic unsigned checks, failures; } Case;

static const RtTestCase *current;
static Case     cases[MAX_CASES];
static _Atomic unsigned ncases;         /* cases[0] is the part of the test before the first T_CASE */
static _Atomic unsigned total_checks, total_failures;
static int      verbose, report;
static char     state_dir[1024], install_root[1024], scratch_dir[1024], log_path[1024];
static unsigned time_limit_s;           /* RT_TEST_TIMEOUT */
static int      log_fd = -1;            /* log_path, open for appending: what the runner shows of a test it had to kill */

/* Per shim, main table then rttest table.  The memory is SHARED with every process the test
 * forks (an anonymous shared mapping, made before any fork) and the increments are atomic,
 * so a CALL made in the child of a T_TRAPS or of rt_test_child_run counts in the test's
 * report: a shim whose every call ends the process can only be called there. */
static unsigned *call_counts;
static uint64_t  iat;                   /* guest memory: one import slot per shim */
static uint64_t  return_stub, trap_by_name, trap_by_ordinal;

/* The captured log.  log_sink runs under the log's own lock, one call at a time; T_LOGGED
 * and T_LOG_COUNT may run on any thread meanwhile, so the count is atomic and an entry is
 * complete before the count includes it. */
static struct { RtChannel ch; char *line; } log_lines[MAX_LOG];
static _Atomic unsigned nlog;
static _Atomic unsigned log_dropped;    /* lines that came after the capture was full */
static _Atomic unsigned log_count[RT_CH_COUNT];     /* every line, kept or not: T_LOG_COUNT is exact */
static uint8_t  user_level[RT_CH_COUNT];        /* what RT_LOG asked for, before the harness raised the levels */

const char *rt_test_state_dir(void)    { return state_dir; }
const char *rt_test_install_root(void) { return install_root; }
Ctx *rt_test_ctx(void)                 { return rt_main_ctx(); }

void rt_test_case(const char *text) {
    unsigned n = ncases;
    if (n == MAX_CASES) { rt_test_fail(__FILE__, __LINE__, "more than %d cases in one test", MAX_CASES - 1); return; }
    cases[n].text = text;                       /* its counters are zero: nothing has counted in it */
    ncases = n + 1;                             /* published last: a check on another thread counts in a whole case */
    if (verbose) fprintf(stderr, "    case: %s\n", text);
    if (log_fd >= 0) dprintf(log_fd, "C %s\n", text);
}

/* T_PLAN: start the case and say whether its block runs (RT_TEST_PLAN_CASE=<n>: only case n). */
bool rt_test_plan_case(unsigned n, const char *text) {
    const char *only = getenv("RT_TEST_PLAN_CASE");
    if (only && *only && strtoul(only, NULL, 10) != n) return false;
    rt_test_case(text);
    return true;
}

static void count_check(bool ok) {
    Case *c = &cases[ncases - 1];
    total_checks++;                             /* atomic, each of them */
    c->checks++;
    if (!ok) { total_failures++; c->failures++; }
}

void rt_test_fail(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    count_check(false);
    flockfile(stderr);                          /* one line, whichever thread reports */
    fprintf(stderr, "  %s:%d: ", file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    funlockfile(stderr);
}

bool rt_test_check(bool ok, const char *file, int line, const char *expr, int kind,
                   uint64_t a, uint64_t b, double fa, double fb) {
    if (ok) { count_check(true); return true; }
    if (kind == 1)      rt_test_fail(file, line, "%s failed: left = %#llx (%lld), right = %#llx (%lld)", expr,
                                     (unsigned long long)a, (long long)a, (unsigned long long)b, (long long)b);
    else if (kind == 2) rt_test_fail(file, line, "%s failed: left = %.17g, right = %.17g", expr, fa, fb);
    else                rt_test_fail(file, line, "%s failed", expr);
    return false;
}

/* ---- log capture ------------------------------------------------------------------------------- */

static void log_sink(RtChannel ch, RtLogLevel level, const char *line, void *user) {
    (void)user;
    unsigned n = nlog;
    if (n < MAX_LOG) { log_lines[n].ch = ch; log_lines[n].line = strdup(line); nlog = n + 1; }
    else log_dropped++;
    log_count[ch]++;
    if (log_fd >= 0) dprintf(log_fd, "L %s: %s\n", rt_log_channel_name(ch), line);
    /* Not fprintf(stderr): this runs on any thread, with the log's mutex held, and a fork at
     * this moment would copy stderr's own lock, held, into the child, where nothing can
     * release it (the fatal report flushes stderr).  dprintf takes no lock that outlives it. */
    if (verbose || user_level[ch] >= level) dprintf(2, "    %s: %s\n", rt_log_channel_name(ch), line);
}

bool rt_test_logged(RtChannel ch, const char *text) {
    for (unsigned i = 0, n = nlog; i < n; i++)
        if (log_lines[i].ch == ch && log_lines[i].line && strstr(log_lines[i].line, text)) return true;
    /* "No" is only an answer if every line was looked at.  The capture keeps the first
     * MAX_LOG lines; after that a line that was logged would be reported as not logged, so
     * the question itself fails the test. */
    if (log_dropped)
        rt_test_fail(current ? current->file : "?", 0, "T_LOGGED(%s, \"%s\") cannot be answered: the test has logged %u "
                     "lines, the harness keeps the first %d, and the text is not among those", rt_log_channel_name(ch),
                     text, MAX_LOG + (unsigned)log_dropped, MAX_LOG);
    return false;
}

unsigned rt_test_log_count(RtChannel ch) { return log_count[ch]; }

void rt_test_trace(bool on) {
    rt_log_level[RT_CH_TRACE] = on ? RT_LL_DEBUG : user_level[RT_CH_TRACE];
}

/* ---- calling through the guest ABI --------------------------------------------------------------- */

static uint64_t prng_state;
static uint64_t garbage(void) {                 /* splitmix64: deterministic, different on every call */
    uint64_t z = (prng_state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return (z ^ (z >> 31)) | UINT64_C(0x8000000000000000);     /* never looks like a small number or NULL */
}

static const uint8_t arg_reg[4]     = { 1, 2, 8, 9 };                   /* rcx rdx r8 r9 */
static const uint8_t volatile_reg[] = { 0, 1, 2, 8, 9, 10, 11 };       /* rax rcx rdx r8-r11 */
static const uint8_t saved_reg[]    = { 3, 5, 6, 7, 12, 13, 14, 15 };  /* rbx rbp rsi rdi r12-r15 */
static const char *const reg_name[16] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                          "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
#define CANARY_WORDS 8

static void return_stub_fn(Ctx *ctx) {
    (void)ctx;
    rt_fatal("the test harness's return address was called; it is a return address, not a function");
}

RtTestRet rt_test_invoke(uint64_t target, unsigned nargs, const RtTestArg *args, const char **detail) {
    static char why[256];
    Ctx *ctx = rt_main_ctx();
    const Ctx before = *ctx;
    RtTestRet r = { 0 };
    why[0] = 0;
    if (detail) *detail = why;
    if (nargs > 16) rt_fatal("rt_test_invoke: %u arguments", nargs);

    /* Volatile state: garbage, so that a shim that reads what it must not read gets nonsense. */
    for (unsigned i = 0; i < sizeof volatile_reg; i++) ctx->r[volatile_reg[i]] = garbage();
    for (unsigned i = 0; i < 6; i++) { ctx->xmm[i].q[0] = garbage(); ctx->xmm[i].q[1] = garbage(); }
    ctx->cf = garbage() & 1; ctx->pf = garbage() & 1; ctx->af = garbage() & 1;
    ctx->zf = garbage() & 1; ctx->sf = garbage() & 1; ctx->of = garbage() & 1;
    ctx->df = 0;                                /* the ABI: clear at every call boundary */

    /* Non-volatile state: known values, checked afterwards. */
    for (unsigned i = 0; i < sizeof saved_reg; i++) ctx->r[saved_reg[i]] = garbage();
    for (unsigned i = 6; i < 16; i++) { ctx->xmm[i].q[0] = garbage(); ctx->xmm[i].q[1] = garbage(); }

    /* The frame: 16-byte aligned before the push; home space and stack arguments; above
     * them a few words of the "caller's frame" that the callee must not write. */
    unsigned slots = nargs < 4 ? 4 : nargs;
    uint64_t top = (before.r[4] - 0x200 - 8 * (uint64_t)(slots + CANARY_WORDS)) & ~UINT64_C(15);
    uint64_t canary[CANARY_WORDS];
    for (unsigned i = 0; i < CANARY_WORDS; i++) { canary[i] = garbage(); ST64(top + 8 * (uint64_t)(slots + i), canary[i]); }
    for (unsigned i = 0; i < 4; i++) ST64(top + 8 * (uint64_t)i, garbage());            /* home space */

    for (unsigned i = 0; i < nargs; i++) {
        const RtTestArg *a = &args[i];
        uint64_t bits = a->v.u, junk = garbage();
        unsigned size = a->size;
        if (a->cls == RT_AC_F32)      { uint32_t b; memcpy(&b, &a->v.f, 4); bits = b; size = 4; }
        else if (a->cls == RT_AC_F64) { memcpy(&bits, &a->v.d, 8); size = 8; }
        if (size < 8) {                         /* the upper bits of a narrow argument are undefined */
            uint64_t mask = (UINT64_C(1) << (8 * size)) - 1;
            bits = (bits & mask) | (junk & ~mask);
        }
        if (i >= 4) { ST64(top + 8 * (uint64_t)i, bits); continue; }
        if (a->cls == RT_AC_F32 || a->cls == RT_AC_F64) {
            ctx->xmm[i].q[0] = bits;            /* by position; the integer register stays garbage */
            ctx->xmm[i].q[1] = garbage();
        } else {
            ctx->r[arg_reg[i]] = bits;          /* the xmm register of this position stays garbage */
        }
    }
    uint64_t sp = top - 8;
    ST64(sp, return_stub);
    ctx->r[4] = sp;
    const Ctx planted = *ctx;

    tf_fp_enter_guest(ctx->mxcsr);              /* the caller is "lifted code" */
    rt_lookup(ctx, target)(ctx);
    uint64_t fpcr = tf_fpcr_read();
    tf_fp_enter_host();

#define VIOLATION(bit, ...) do { if (!r.violations) snprintf(why, sizeof why, __VA_ARGS__); r.violations |= (bit); } while (0)
    if (ctx->r[4] != sp + 8)
        VIOLATION(RT_TV_RSP, "rsp advanced by %lld, not by 8", (long long)(ctx->r[4] - sp));
    for (unsigned i = 0; i < sizeof saved_reg; i++) {
        unsigned reg = saved_reg[i];
        if (ctx->r[reg] != planted.r[reg])
            VIOLATION(RT_TV_NONVOLATILE, "non-volatile register %s changed: planted %#llx, found %#llx", reg_name[reg],
                      (unsigned long long)planted.r[reg], (unsigned long long)ctx->r[reg]);
    }
    for (unsigned i = 6; i < 16; i++)
        if (memcmp(&ctx->xmm[i], &planted.xmm[i], sizeof(Xmm)))
            VIOLATION(RT_TV_XMM, "non-volatile register xmm%u changed", i);
    if (ctx->df != 0) VIOLATION(RT_TV_DF, "the direction flag was left set");
    if (ctx->gs_base != planted.gs_base || ctx->host != planted.host || ctx->mxcsr != planted.mxcsr)
        VIOLATION(RT_TV_CTX, "gs base, mxcsr or the host pointer changed");
    if (fpcr != tf_fpcr_for_mxcsr(ctx->mxcsr))
        VIOLATION(RT_TV_FPCR, "returned with FPCR %#llx; the guest float mode is %#llx", (unsigned long long)fpcr,
                  (unsigned long long)tf_fpcr_for_mxcsr(ctx->mxcsr));
    for (unsigned i = 0; i < CANARY_WORDS; i++)
        if (LD64(top + 8 * (uint64_t)(slots + i)) != canary[i])
            VIOLATION(RT_TV_STACK, "the caller's stack at rsp+%#x was overwritten", 8 + 8 * (slots + i));
#undef VIOLATION

    r.rax = ctx->r[0];
    r.xmm0 = ctx->xmm[0];
    *ctx = before;                              /* calls are independent of each other */
    return r;
}

static unsigned shim_index(const RtShimEntry *shim) {
    if (shim >= rt_shim_table && shim < rt_shim_table + rt_shim_count) return (unsigned)(shim - rt_shim_table);
    if (shim >= rttest_shim_table && shim < rttest_shim_table + rttest_shim_count)
        return rt_shim_count + (unsigned)(shim - rttest_shim_table);
    rt_fatal("rt_test_call: not a row of a shim table");
}

uint64_t rt_test_import_slot(const RtShimEntry *shim) { return LD64(iat + 8 * (uint64_t)shim_index(shim)); }

static RtTestRet call_checked(const RtShimEntry *shim, unsigned nargs, const RtTestArg *args) {
    const char *why;
    unsigned idx = shim_index(shim);
    __atomic_fetch_add(&call_counts[idx], 1, __ATOMIC_SEQ_CST);
    /* `call [rip+slot]`: the target comes from the import slot, as in lifted code. */
    RtTestRet r = rt_test_invoke(LD64(iat + 8 * (uint64_t)idx), nargs, args, &why);
    if (!r.violations && !shim->raw && (shim->ret == RT_AC_I32 || shim->ret == RT_AC_U32) && (r.rax >> 32)) {
        r.violations |= RT_TV_RET_UPPER;
        why = "a 32-bit result came back with a nonzero upper half of rax";
    }
    if (r.violations)
        rt_test_fail(current ? current->file : "?", 0, "CALL(%s, %s): the shim broke the guest ABI: %s",
                     shim->lib, shim->name, why);
    return r;
}

RtTestRet rt_test_call(const RtShimEntry *shim, unsigned nargs, const RtTestArg *args, int sret) {
    (void)sret;
    return call_checked(shim, nargs, args);
}

RtTestRet rt_test_call_raw(const RtShimEntry *shim, unsigned nslots, const uint64_t *slots) {
    RtTestArg a[16];
    if (nslots > 16) rt_fatal("rt_test_call_raw: %u slots", nslots);
    for (unsigned i = 0; i < nslots; i++) a[i] = (RtTestArg){ RT_AC_U64, 8, { .u = slots[i] } };
    return call_checked(shim, nslots, a);
}

RtTestRet rt_test_com_call(const RtComIface *iface, unsigned slot, unsigned nargs, const RtTestArg *args) {
    const char *why = "";
    const char *file = current ? current->file : "?";
    const RtComMethod *m = &iface->methods[slot];
    uint64_t self = nargs ? args[0].v.u : 0;
    RtTestRet r = { 0 };
    if (!self || LD64(self) != iface->vtable) {
        rt_test_fail(file, 0, "COM_CALL(%s): `this` (%#llx) does not carry the vtable of %s", m->entry.name,
                     (unsigned long long)self, iface->name);
        return r;
    }
    /* `mov rax, [rcx]; call [rax + 8*slot]`: the target comes from the object, as in lifted code. */
    r = rt_test_invoke(LD64(LD64(self) + 8 * (uint64_t)slot), nargs, args, &why);
    if (!r.violations && (m->entry.ret == RT_AC_I32 || m->entry.ret == RT_AC_U32) && (r.rax >> 32)) {
        r.violations |= RT_TV_RET_UPPER;
        why = "a 32-bit result came back with a nonzero upper half of rax";
    }
    if (r.violations) rt_test_fail(file, 0, "COM_CALL(%s): the method broke the guest ABI: %s", m->entry.name, why);
    return r;
}

void rt_test_check_sret(const RtShimEntry *shim, RtTestRet r, const void *dst) {
    if (r.rax != (uint64_t)(uintptr_t)dst)
        rt_test_fail(current ? current->file : "?", 0, "CALL(%s, %s): a struct-returning function must return the "
                     "destination pointer in rax: expected %p, got %#llx", shim->lib, shim->name, dst,
                     (unsigned long long)r.rax);
}

uint64_t rt_test_trap_import(void)            { return trap_by_name; }
uint64_t rt_test_trap_import_by_ordinal(void) { return trap_by_ordinal; }

/* ---- death tests ------------------------------------------------------------------------------- */

static pid_t trap_child;
static char  trap_output[1100];

int rt_test_trap_begin(void) {
    snprintf(trap_output, sizeof trap_output, "%s/trap-output.txt", scratch_dir);
    fflush(stdout);
    fflush(stderr);
    trap_child = fork();
    if (trap_child < 0) { perror("fork"); exit(2); }
    if (trap_child != 0) {
        if (log_fd >= 0) dprintf(log_fd, "L harness: T_TRAPS forked process %d and waits for it\n", (int)trap_child);
        return 0;
    }
    rt_test_forked();                           /* first: the next lines can wait for a lock that fork copied held */
    int fd = open(trap_output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || dup2(fd, 2) < 0) _exit(98);
    close(fd);
    rt_log_set_sink(NULL, NULL);                /* what the child logs goes to the file with the report */
    return 1;
}

_Noreturn void rt_test_trap_survived(void) {
    fflush(stderr);
    _exit(97);
}

void rt_test_trap_end(const char *file, int line, const char *stmt, const char *text) {
    int status = 0;
    static char out[16384];
    while (waitpid(trap_child, &status, 0) < 0 && errno == EINTR) { }
    if (log_fd >= 0) dprintf(log_fd, "L harness: process %d ended\n", (int)trap_child);
    size_t n = 0;
    FILE *f = fopen(trap_output, "r");
    if (f) { n = fread(out, 1, sizeof out - 1, f); fclose(f); }
    out[n] = 0;
    unlink(trap_output);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 97) {
        rt_test_fail(file, line, "T_TRAPS(%s): the statement returned; it should have ended in a fatal report", stmt);
    } else if (!RT_TEST_ABORTED(status)) {       /* the quiet end of a report: exit status 134 (rt/test_child.h) */
        rt_test_fail(file, line, "T_TRAPS(%s): the statement did not abort with a report (wait status %#x); output:\n%s",
                     stmt, status, out);
    } else if (!strstr(out, "FATAL") || !strstr(out, text)) {
        rt_test_fail(file, line, "T_TRAPS(%s): the fatal report does not contain \"%s\"; it was:\n%s", stmt, text, out);
    } else {
        count_check(true);
        if (verbose) fprintf(stderr, "    trapped as expected:\n%s", out);
    }
}

/* ---- starting the runtime for one test ----------------------------------------------------------- */

/* Runs inside rt_init, after the native modules exist and before the dispatch table is
 * sealed: the last moment at which guest code addresses can be created. */
static RtTestSetup *setups;
void rt_test_register_setup(RtTestSetup *s) { s->next = setups; setups = s; }

/* Rows a test adds, in its TEST_SETUP, to the shims its runtime starts with (rt/test.h). */
static const RtShimEntry *more_shims;
static unsigned n_more_shims;
void rt_test_extra_shims(const RtShimEntry *rows, unsigned n) { more_shims = rows; n_more_shims = n; }

static void before_seal(void *user) {
    (void)user;
    RtModule *m = rt_module_find("rttest.dll");
    if (!m) rt_fatal("the test build has no rttest.dll module");
    return_stub = rt_native_stub(m, "rt_test_return", return_stub_fn);
    for (RtTestGuestFn *g = guest_fns; g; g = g->next) g->addr = rt_native_stub(m, g->name, g->fn);
    trap_by_name = rt_native_import("rttest.dll", "NotImplemented", 0);
    trap_by_ordinal = rt_native_import("rttest.dll", NULL, 77);
    rt_com_register(&rttest_com_table);         /* the test-only interfaces of runtime/tests/com (rt/com.h) */

    /* The import address table: one slot per shim, filled the way the loader fills a
     * module's slots (section 1.4). */
    unsigned n = rt_shim_count + rttest_shim_count;
    call_counts = mmap(NULL, (n ? n : 1) * sizeof *call_counts, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (call_counts == MAP_FAILED) call_counts = NULL;
    iat = rt_vm_alloc(0, 8 * (uint64_t)(n ? n : 1), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    if (!iat || !call_counts) rt_fatal("cannot allocate the test import table");
    for (unsigned i = 0; i < n; i++) {
        const RtShimEntry *e = i < rt_shim_count ? &rt_shim_table[i] : &rttest_shim_table[i - rt_shim_count];
        const char *dll = NULL;
        for (unsigned j = 0; j < rt_module_count() && !dll; j++)
            if (!strcmp(rt_module_at(j)->lib, e->lib)) dll = rt_module_at(j)->name;
        if (!dll) rt_fatal("shim %s!%s has no module", e->lib, e->name);
        uint64_t stub = rt_native_import(dll, e->name, 0);
        if (e->ordinal && rt_native_import(dll, NULL, e->ordinal) != stub)
            rt_fatal("%s!%s: import by ordinal %u and by name give different addresses", e->lib, e->name, e->ordinal);
        ST64(iat + 8 * (uint64_t)i, stub);
    }
}

/* The time limit is the runner's to enforce (run_one).  This is for a test whose runner is
 * gone (killed by whoever started it): the test's own alarm, a little after the limit, kills
 * the test's process group, which is the test and everything it forked.  Only a signal
 * handler can do that, and only kill() is called in it. */
#define BACKSTOP_GRACE_S 15
static void on_backstop_alarm(int sig) {
    (void)sig;
    kill(0, SIGKILL);
}

/* In a process a test forked (T_TRAPS, rt/test_child.h), first thing.  fork() copied every
 * lock as it was, and a lock that another thread held is held for ever in the child: the two
 * of the runtime that the child's path takes and that a thread can hold while the test
 * follows the rule for forking (rt/test.h, THREADS) are put back.  fork() also cancelled the
 * alarm; set it again.  A fatal report replaces it with its own shorter watchdog (runtime/core/report.c).
 * And the quiet end (rt/crash.h): a child of a test exists to end in a report, about 2,100 of
 * them in one run of the suite, and each death by SIGABRT is a crash report macOS writes.
 * Here every report ends in exit status 134 instead (RT_TEST_ABORTED).  The test's own
 * process never comes here: a report there is a failed test and dies of SIGABRT as before. */
void rt_test_forked(void) {
    rt_report_quiet(RT_QUIET_ALL);
    rt_log_forked_child();                      /* the locks fork may have copied held (rt/test_fork.h) */
    rt_dispatcher_forked_child();
    if (time_limit_s) alarm(time_limit_s + BACKSTOP_GRACE_S);
}

static _Noreturn void run_child(const RtTestCase *t) {
    current = t;
    signal(SIGINT, SIG_DFL);                    /* the runner's handlers (main) are not the test's */
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGTTOU, SIG_IGN);                   /* a background process group may write to the terminal */
    setpgid(0, 0);                              /* the runner does the same: whoever is first */
    sigset_t alrm;                              /* whatever started the runner may have blocked or ignored it */
    sigemptyset(&alrm);
    sigaddset(&alrm, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &alrm, NULL);
    signal(SIGALRM, getpgrp() == getpid() ? on_backstop_alarm : SIG_DFL);
    alarm(time_limit_s + BACKSTOP_GRACE_S);
    log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    prng_state = 0x243F6A8885A308D3;            /* the same garbage in every run */

    for (RtTestSetup *s = setups; s; s = s->next)       /* TEST_SETUP: before the runtime exists */
        if (!strcmp(s->lib, t->lib) && !strcmp(s->name, t->name)) s->fn();

    /* The test library's shims, and after them the rows the test's set-up added (the import
     * table of CALL is built from the first part alone: before_seal). */
    const RtShimEntry *extra = rttest_shim_table;
    unsigned n_extra = rttest_shim_count;
    if (n_more_shims) {
        RtShimEntry *all = calloc(rttest_shim_count + n_more_shims, sizeof *all);
        if (!all) { fprintf(stderr, "  %s: out of memory for the test's extra shims\n", t->file); _exit(1); }
        if (rttest_shim_count) memcpy(all, rttest_shim_table, rttest_shim_count * sizeof *all);
        memcpy(all + rttest_shim_count, more_shims, n_more_shims * sizeof *all);
        extra = all;
        n_extra = rttest_shim_count + n_more_shims;
    }
    RtConfig cfg = {
        .state_dir = state_dir, .install_root = install_root, .tso = RT_TSO_TRY,
        .extra_modules = rttest_module_defs, .n_extra_modules = rttest_module_def_count,
        .extra_shims = extra, .n_extra_shims = n_extra,
        .before_seal = before_seal,
    };
    rt_init(&cfg);

    /* Capture every channel at full detail (T_LOGGED), except the per-call trace. */
    memcpy(user_level, rt_log_level, sizeof user_level);
    for (int ch = 0; ch < RT_CH_COUNT; ch++) if (ch != RT_CH_TRACE) rt_log_level[ch] = RT_LL_DEBUG;
    rt_log_set_sink(log_sink, NULL);

    cases[0].text = "(before the first T_CASE)";
    ncases = 1;
    t->fn();

    if (total_checks == 0) { fprintf(stderr, "  %s: the test made no check\n", t->file); total_failures++; }
    if (report) {
        printf("#rt test %s/%s\n", t->lib, t->name);
        for (unsigned i = 1; i < ncases; i++)
            printf("#rt case %u %u %s\n", (unsigned)cases[i].checks, (unsigned)cases[i].failures, cases[i].text);
        for (unsigned i = 0; i < rt_shim_count + rttest_shim_count; i++) {
            const RtShimEntry *e = i < rt_shim_count ? &rt_shim_table[i] : &rttest_shim_table[i - rt_shim_count];
            if (call_counts[i]) printf("#rt calls %s/%s %u\n", e->lib, e->name, call_counts[i]);
        }
        printf("#rt result %s %u %u\n", total_failures ? "fail" : "pass", (unsigned)total_checks, (unsigned)total_failures);
    }
    fflush(stdout);
    fflush(stderr);
    _exit(total_failures ? 1 : 0);
}

/* ---- the runner (parent process) ------------------------------------------------------------------ */

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)st; (void)flag; (void)ftw;
    return remove(path);
}

static int cmp_tests(const void *a, const void *b) {
    const RtTestCase *x = *(const RtTestCase *const *)a, *y = *(const RtTestCase *const *)b;
    int c = strcmp(x->lib, y->lib);
    return c ? c : strcmp(x->name, y->name);
}

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

/* Wait until the child `pid` has ended or `limit_s` seconds have passed; true if it has ended.
 * The child is not reaped here: as a zombie it keeps its pid, which is also the id of its
 * process group, from being given to another process before the group has been killed. */
static bool wait_for_end(pid_t pid, unsigned limit_s) {
    const uint64_t deadline = now_ns() + UINT64_C(1000000000) * limit_s;
    struct kevent ev;
    int kq = kqueue();
    EV_SET(&ev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
    if (kq >= 0 && kevent(kq, &ev, 1, NULL, 0, NULL) < 0) { close(kq); kq = -1; }  /* ESRCH: it has ended already */
    bool ended = false;
    for (;;) {
        siginfo_t si;
        memset(&si, 0, sizeof si);
        if (waitid(P_PID, (id_t)pid, &si, WEXITED | WNOHANG | WNOWAIT) == 0 && si.si_pid == pid) { ended = true; break; }
        uint64_t now = now_ns();
        if (now >= deadline) break;
        /* Sleep until the exit event; look again at least every quarter second, so that a
         * missed event costs a quarter second and not the whole limit.  Without a kqueue: poll. */
        uint64_t left = deadline - now, slice = kq >= 0 ? UINT64_C(250000000) : UINT64_C(2000000);
        if (left > slice) left = slice;
        struct timespec ts = { (time_t)(left / UINT64_C(1000000000)), (long)(left % UINT64_C(1000000000)) };
        if (kq >= 0) kevent(kq, NULL, 0, &ev, 1, &ts); else nanosleep(&ts, NULL);
    }
    if (kq >= 0) close(kq);
    return ended;
}

/* What a test that was killed had logged (log_path: "C <case>" and "L <channel>: <line>"
 * lines, written by rt_test_case and log_sink): the case it was in and the last lines. */
#define TIMEOUT_LOG_LINES 30
static void show_log_of_killed_test(const RtTestCase *t, unsigned limit_s) {
    static char buf[1 << 20];
    size_t n = 0;
    FILE *f = fopen(log_path, "r");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0 && ftell(f) > (long)(sizeof buf - 1)) fseek(f, -(long)(sizeof buf - 1), SEEK_END);
        else rewind(f);
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    buf[n] = 0;
    unsigned lines = 0, shown = 0;
    const char *last_case = NULL;
    for (char *p = buf; *p; p++) if (*p == '\n') lines++;
    fprintf(stderr, "  %s: %s/%s had not ended after %u s and was killed", t->file, t->lib, t->name, limit_s);
    unsigned skip = lines > TIMEOUT_LOG_LINES ? lines - TIMEOUT_LOG_LINES : 0, i = 0;
    for (char *p = buf, *nl; (nl = strchr(p, '\n')) != NULL; p = nl + 1, i++) {
        *nl = 0;
        if (p[0] == 'C' && p[1] == ' ') last_case = p + 2;
        if (i < skip) continue;
        if (!shown++) fprintf(stderr, "; the last %u of the %u lines it logged:\n", lines - skip, lines);
        if (p[0] == 'C' && p[1] == ' ') fprintf(stderr, "    case: %s\n", p + 2);
        else if (p[0] == 'L' && p[1] == ' ') fprintf(stderr, "    %s\n", p + 2);
    }
    if (!shown) fprintf(stderr, "; it had logged nothing\n");
    if (last_case) fprintf(stderr, "  it was in the case \"%s\"\n", last_case);
    else fprintf(stderr, "  it had not reached a T_CASE\n");
}

/* The process group of the test that is running, for a runner that is told to stop. */
static volatile sig_atomic_t running_group;

static void on_stop_signal(int sig) {
    if (running_group > 0) kill(-(pid_t)running_group, SIGKILL);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Run one test in a child with fresh directories.  Returns true if it passed. */
static bool run_one(const RtTestCase *t) {
    char root[900];
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof root, "%s/rt-test.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    size_t len = strlen(root);
    /* A TMPDIR that ends in '/' (it does on macOS): "/rt-test.XXXXXX" is 15 characters, so the
     * doubled slash is at len - 16 and len - 15. */
    while (len > 16 && root[len - 15] == '/' && root[len - 16] == '/') { memmove(root + len - 16, root + len - 15, 16); len--; }
    if (!mkdtemp(root)) { perror("mkdtemp"); exit(2); }
    snprintf(state_dir, sizeof state_dir, "%s/state", root);
    snprintf(install_root, sizeof install_root, "%s/install", root);
    snprintf(scratch_dir, sizeof scratch_dir, "%s/scratch", root);
    snprintf(log_path, sizeof log_path, "%s/log.txt", root);
    if (mkdir(state_dir, 0700) || mkdir(install_root, 0700) || mkdir(scratch_dir, 0700)) { perror("mkdir"); exit(2); }

    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) run_child(t);
    setpgid(pid, pid);                          /* the test's own process group; the child does the same */
    running_group = pid;

    int status = 0;
    bool in_time = wait_for_end(pid, time_limit_s);
    /* The whole group, whether the test is over or over the limit: the test and whatever it
     * forked and did not wait for.  Before the test is reaped (see wait_for_end). */
    kill(-pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    running_group = 0;
    if (!in_time) show_log_of_killed_test(t, time_limit_s);
    nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);

    if (!in_time) { printf("FAIL  %s/%s   (timed out after %u s; see the report above)\n", t->lib, t->name, time_limit_s); return false; }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) { printf("ok    %s/%s\n", t->lib, t->name); return true; }
    if (WIFSIGNALED(status)) printf("FAIL  %s/%s   (killed by signal %d%s; see the report above)\n", t->lib, t->name,
                                    WTERMSIG(status), WTERMSIG(status) == SIGABRT ? ", abort" : "");
    /* Only a test that turned the quiet end on in its own process ends so; said in the same words. */
    else if (RT_TEST_ABORTED(status)) printf("FAIL  %s/%s   (exit status %d, abort with the quiet end on; see the report above)\n",
                                             t->lib, t->name, WEXITSTATUS(status));
    else printf("FAIL  %s/%s   (%s)\n", t->lib, t->name, t->file);
    return false;
}

/* The skip list: RT_TEST_SKIP names a file with one test to a line, `lib/Name  reason`
 * (`#` starts a comment).  A listed test is not run and is shown as `skip` with its reason;
 * the reason is required.  For a build in which some tests cannot work for reasons that are
 * the build's and not the code's (make rt-test-tsan, runtime/tests/tsan.skip). */
typedef struct Skip { struct Skip *next; bool used; char *id, *reason; } Skip;
static Skip *skips;

static bool read_skip_list(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "rt-test: RT_TEST_SKIP=%s: %s\n", path, strerror(errno)); return false; }
    char line[1024];
    for (unsigned n = 1; fgets(line, sizeof line, f); n++) {
        line[strcspn(line, "#\n")] = 0;
        char *id = line + strspn(line, " \t");
        if (!*id) continue;
        char *reason = id + strcspn(id, " \t");
        if (*reason) { *reason++ = 0; reason += strspn(reason, " \t"); }
        if (!*reason) {
            fprintf(stderr, "rt-test: %s:%u: '%s' has no reason; a line is `lib/Name  reason`\n", path, n, id);
            fclose(f);
            return false;
        }
        Skip *k = calloc(1, sizeof *k);
        k->id = strdup(id);
        k->reason = strdup(reason);
        k->next = skips;
        skips = k;
    }
    fclose(f);
    return true;
}

static Skip *skipped(const char *id) {
    for (Skip *k = skips; k; k = k->next) if (!strcmp(k->id, id)) return k;
    return NULL;
}

int main(int argc, char **argv) {
    int list = 0, nfilters = 0;
    char **filters = calloc((size_t)argc, sizeof *filters);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--report")) report = 1;
        else if (argv[i][0] == '-') { fprintf(stderr, "usage: rt-test [-v] [--list] [--report] [lib/Name ...]\n"); return 2; }
        else if (argv[i][0]) filters[nfilters++] = argv[i];
    }

    time_limit_s = DEFAULT_TIME_LIMIT_S;
    const char *limit = getenv("RT_TEST_TIMEOUT");
    if (limit && *limit) {
        char *end;
        unsigned long v = strtoul(limit, &end, 10);
        if (*end || v < 1 || v > 86400) { fprintf(stderr, "rt-test: RT_TEST_TIMEOUT=%s: not a number of seconds from 1 to 86400\n", limit); return 2; }
        time_limit_s = (unsigned)v;
    }

    const char *skip_file = getenv("RT_TEST_SKIP");
    if (skip_file && *skip_file && !read_skip_list(skip_file)) return 2;

    unsigned n = 0;
    for (RtTestCase *t = tests; t; t = t->next) n++;
    RtTestCase **all = calloc(n ? n : 1, sizeof *all);
    n = 0;
    for (RtTestCase *t = tests; t; t = t->next) all[n++] = t;
    qsort(all, n, sizeof *all, cmp_tests);
    for (unsigned i = 1; i < n; i++)
        if (!cmp_tests(&all[i - 1], &all[i])) {
            fprintf(stderr, "rt-test: test %s/%s is defined twice (%s and %s)\n", all[i]->lib, all[i]->name,
                    all[i - 1]->file, all[i]->file);
            return 2;
        }

    if (list) {
        for (unsigned i = 0; i < n; i++) printf("%s/%s\t%s\n", all[i]->lib, all[i]->name, all[i]->file);
        return 0;
    }

    signal(SIGINT, on_stop_signal);             /* a test is not in the runner's process group: take it along */
    signal(SIGTERM, on_stop_signal);
    signal(SIGHUP, on_stop_signal);

    unsigned ran = 0, failed = 0, nskipped = 0;
    for (int f = 0; f < (nfilters ? nfilters : 1); f++) {
        bool matched = false;
        for (unsigned i = 0; i < n; i++) {
            char id[256];
            snprintf(id, sizeof id, "%s/%s", all[i]->lib, all[i]->name);
            if (nfilters && strcmp(id, filters[f])) continue;
            matched = true;
            Skip *k = skipped(id);
            if (k) { k->used = true; nskipped++; printf("skip  %s   (%s)\n", id, k->reason); continue; }
            ran++;
            if (!run_one(all[i])) failed++;
        }
        if (nfilters && !matched) {
            fprintf(stderr, "rt-test: no test named '%s' (rt-test --list shows them)\n", filters[f]);
            return 2;
        }
    }
    /* A line that names no test is a line somebody forgot: say so, when the whole suite ran. */
    if (!nfilters)
        for (Skip *k = skips; k; k = k->next)
            if (!k->used) fprintf(stderr, "rt-test: the skip list names %s, which is not a test\n", k->id);
    if (skips) printf("%u tests, %u passed, %u failed, %u skipped\n", ran, ran - failed, failed, nskipped);
    else printf("%u tests, %u passed, %u failed\n", ran, ran - failed, failed);
    return failed ? 1 : 0;
}
