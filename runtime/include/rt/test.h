/* The test harness (docs/RUNTIME.md section 10.4).  Runner: runtime/tests/harness.c.
 *
 *     #include "rt/test.h"
 *
 *     TEST(kernel32, CloseHandle)
 *     {
 *         T_CASE("closing twice fails");
 *         HANDLE h = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, NULL);
 *         T_TRUE(h != NULL);
 *         T_EQ(CALL(kernel32, CloseHandle, h), TRUE);
 *         T_EQ(CALL(kernel32, CloseHandle, h), FALSE);
 *         T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
 *     }
 *
 * CALL goes through the GUEST ABI, not a C call: it puts the arguments in the Ctx registers
 * and on the guest stack, pushes a return address, reads the shim's address from an import
 * slot, finds the host function in the dispatch table and runs the generated thunk.  Around
 * every call the harness
 *   - fills the upper bits of every argument narrower than 64 bits, the home space and all
 *     volatile registers with garbage;
 *   - plants known values in the non-volatile registers (rbx rbp rsi rdi r12-r15,
 *     xmm6-xmm15) and checks them afterwards, with the direction flag, gs base and mxcsr;
 *   - checks that rsp advanced by exactly 8, that the caller's stack above the arguments
 *     is untouched, and that the thunk came back in the guest float mode.
 * A violation fails the test.
 *
 * Every TEST runs in its own process, started from scratch (rt_init) with a temporary state
 * directory and a temporary empty install root that the runner creates and removes.  Tests
 * never touch the real install or the user's files.  They run as plain command-line
 * programs, so the x86 ordering mode is requested and not required (RT_TSO=try).
 *
 *     make rt-test                           all tests
 *     make rt-test SHIM=kernel32/CloseHandle one test
 *
 * THE TIME LIMIT.  A test gets 300 seconds of wall-clock time (RT_TEST_TIMEOUT=<seconds> in
 * the environment changes it).  After that the runner kills it, with every process it
 * forked, and reports "timed out after N s", the case it was in and the last lines it had
 * logged.  The limit is a guard against a hang and not a budget: on a machine at a load
 * average of 190 the slowest test took 23 s.  A process a test forks and leaves behind is
 * killed when the test ends.
 *
 * THREADS.  In a test of the core (lib `core`) the checks T_TRUE and T_EQ, rt_test_fail,
 * T_LOGGED and T_LOG_COUNT may be used on any thread the test started: the counters are
 * atomic and a failure is printed as one whole line.  The limits:
 *   - T_CASE and T_PLAN belong to the main thread; a check counts in the case that is
 *     current when it is made, so start a case before the threads that check in it and
 *     join them before the next;
 *   - T_REQUIRE returns from the function it stands in, which on another thread is that
 *     thread's function and not the test;
 *   - CALL, T_CTX, T_LAST_ERROR and rt_test_invoke use the Ctx of guest thread 1: main
 *     thread only;
 *   - T_TRAPS and rt/test_child.h fork: main thread only, and only while no other thread
 *     of the test can hold a lock.  A fork copies a held lock into a child where nobody
 *     releases it, and the child waits for ever (a thread asleep in a wait holds none).
 *     Two locks are exempt, because the child puts them back first thing (rt_test_forked,
 *     rt/test_fork.h): the log's mutex and the dispatcher lock, which are the two a thread
 *     can still hold after a wait on its thread object has returned.  So T_TRAPS straight
 *     after joining a thread is safe; waiting for rt_thread_host_threads (rt/thread.h)
 *     first is no longer needed for that;
 *   - the test function returns only after every thread that checks has been joined: the
 *     result is reported when it returns.
 * A shim test (any other lib) starts no thread: it reaches its shim through CALL, which is
 * main-thread only, and nothing has shown the shims themselves safe to call that way from
 * two threads of a test.
 */
#ifndef RT_TEST_H
#define RT_TEST_H

#include "rt/shim.h"
#include "rt/init.h"
#include "rt/dispatch.h"

/* ---- writing a test ------------------------------------------------------------------------- */

/* Define the test of one shim (or, with lib `core`, of a piece of the runtime core).  The
 * body is a void function.  One TEST(lib, Name) per shim test file. */
#define TEST(lib, name) \
    static void rt_testfn_##lib##_##name(void); \
    static RtTestCase rt_testcase_##lib##_##name = { #lib, #name, __FILE__, rt_testfn_##lib##_##name, NULL }; \
    __attribute__((constructor)) static void rt_testreg_##lib##_##name(void) { rt_test_register(&rt_testcase_##lib##_##name); } \
    static void rt_testfn_##lib##_##name(void)

/* Code that runs in the test's process BEFORE the runtime starts (before rt_init), for the
 * one test with the same (lib, name): at most one TEST_SETUP per TEST.  For what must exist
 * when rt_init looks: files in the install root (rt_test_install_root(); the loader reads
 * every lifted module's DLL at start-up, see rt/test_pe.h).  There is no runtime yet: no
 * Ctx, no guest memory, no CALL, no T_ check; a failure must abort() with a message. */
#define TEST_SETUP(lib, name) \
    static void rt_testsetup_##lib##_##name(void); \
    static RtTestSetup rt_testsetupreg_##lib##_##name = { #lib, #name, rt_testsetup_##lib##_##name, NULL }; \
    __attribute__((constructor)) static void rt_testsetupctor_##lib##_##name(void) { rt_test_register_setup(&rt_testsetupreg_##lib##_##name); } \
    static void rt_testsetup_##lib##_##name(void)

/* Call a shim through the guest ABI.  Arguments and result have the types of the SHIM
 * declaration (the wrappers are generated: build/gen/shim_calls.h).  For a SHIM_RAW shim
 * the arguments are (number of slots, const uint64_t *slots) and the result is rax. */
#define CALL(lib, name, ...)  rt_call_##lib##_##name(__VA_ARGS__)

/* Call a method of an object of the interface layer (rt/com.h) as lifted code does: the
 * target is read from the object's vtable, [[this] + 8*slot], and entered through the guest
 * ABI with the checks CALL makes.  Arguments (`this` first) and result have the types of the
 * description (the wrappers are generated: build/gen/com_calls.h; for the test-only
 * interfaces of runtime/tests/com include "rttest_com_calls.h").
 *     COM_CALL(ID3D11Device, GetFeatureLevel, device) */
#define COM_CALL(iface, method, ...)  rt_comcall_##iface##_##method(__VA_ARGS__)

/* Start a case.  The text must be one of the entries of the shim's RT-CASES header tag,
 * spelled the same; `make shim-check` requires every entry to have a case that ran and
 * made at least one check. */
#define T_CASE(text)  rt_test_case(text)

/* One numbered case of a core piece's test plan (docs/handoff/rt-contract-*.md), written as
 * a block:
 *
 *     T_PLAN(3, "get of an unset index") { ...the checks of case 3 of the plan... }
 *
 * It starts the case "3: get of an unset index" and runs the block.  `n` is the plan's case
 * number as a decimal literal and `text` a string literal.  With RT_TEST_PLAN_CASE=<n> in the
 * environment only the block of case n runs, the others are skipped (code outside the blocks
 * always runs): tools/swarm/core_check.py uses it to see every case meet a body that is not
 * built yet.  A block must not be left with `break` or `continue`; variables that a later
 * case needs are declared before the first block. */
#define T_PLAN(n, text)  if (rt_test_plan_case((n), #n ": " text))

/* Checks.  A failed check is reported with file and line and fails the test; the test goes
 * on, so that one run shows every failure.  T_REQUIRE leaves the test instead.
 * T_EQ compares the two sides as 64-bit values (each converted as C converts it to
 * uint64_t, so write (DWORD)-1, not -1, for an unsigned all-ones), or as doubles if either
 * side is float or double. */
#define T_TRUE(cond) \
    rt_test_check(!!(cond), __FILE__, __LINE__, "T_TRUE(" #cond ")", 0, 0, 0, 0.0, 0.0)
#define T_REQUIRE(cond) \
    do { if (!rt_test_check(!!(cond), __FILE__, __LINE__, "T_REQUIRE(" #cond ")", 0, 0, 0, 0.0, 0.0)) return; } while (0)
#define T_EQ(a, b) \
    do { __typeof__(a) rt_a_ = (a); __typeof__(b) rt_b_ = (b); \
         int rt_fp_ = RT_T_ISF_(rt_a_) || RT_T_ISF_(rt_b_); \
         rt_test_check(rt_fp_ ? RT_T_F64_(rt_a_) == RT_T_F64_(rt_b_) : RT_T_U64_(rt_a_) == RT_T_U64_(rt_b_), \
                       __FILE__, __LINE__, "T_EQ(" #a ", " #b ")", rt_fp_ ? 2 : 1, \
                       RT_T_U64_(rt_a_), RT_T_U64_(rt_b_), RT_T_F64_(rt_a_), RT_T_F64_(rt_b_)); } while (0)

/* The calling thread's last error, read from the thread block as guest code would. */
#define T_LAST_ERROR()  rt_get_last_error(rt_test_ctx())

/* The statement must end in a fatal report (rt_unimplemented, a trap stub, a dispatch
 * miss, rt_fatal: anything that prints "FATAL" and aborts) whose text contains `text`.
 * The statement runs in a forked child, so it has no effect on the rest of the test. */
#define T_TRAPS(stmt, text) \
    do { if (rt_test_trap_begin()) { stmt; rt_test_trap_survived(); } \
         rt_test_trap_end(__FILE__, __LINE__, #stmt, (text)); } while (0)

/* True if a line containing `text` was logged on channel `ch` since the test began.  The
 * harness captures every channel at its most verbose level except `trace`, which a test
 * turns on itself with rt_test_trace(true).  It keeps the first 4,096 lines of a test.  A
 * test may log more, but then "not logged" cannot be told from "not kept": a T_LOGGED that
 * does not find its text among the kept lines after more were logged FAILS THE TEST (and
 * returns false).  Ask before the test logs in bulk. */
#define T_LOGGED(ch, text)  rt_test_logged((ch), (text))

/* Number of lines logged on channel `ch` since the test began: every line, kept or not. */
#define T_LOG_COUNT(ch)     rt_test_log_count((ch))

/* The Ctx the calls run on (guest thread 1), for looking at guest memory directly. */
#define T_CTX()  rt_test_ctx()

/* A guest function written in C, for tests that need a guest callback: it has the type of a
 * lifted function, is entered like one (return address at ctx->r[4], arguments in the guest
 * registers) and leaves with GUEST_RET.  T_GUEST_ADDR(name) is its guest code address.
 *
 *     GUEST_FN(twice) { ctx->r[0] = 2 * ctx->r[1]; GUEST_RET(ctx); }
 */
#define GUEST_FN(name) \
    static void name(Ctx *ctx); \
    static RtTestGuestFn rt_guestfn_##name = { #name, name, 0, NULL }; \
    __attribute__((constructor)) static void rt_guestfn_reg_##name(void) { rt_test_register_guest_fn(&rt_guestfn_##name); } \
    static void name(Ctx *ctx)
#define GUEST_RET(ctx)      do { (ctx)->r[4] += 8; return; } while (0)
#define T_GUEST_ADDR(name)  (rt_guestfn_##name.addr)

/* ---- the machinery behind the macros -------------------------------------------------------- */

typedef struct RtTestCase {
    const char *lib, *name, *file;
    void (*fn)(void);
    struct RtTestCase *next;
} RtTestCase;

typedef struct RtTestSetup {
    const char *lib, *name;
    void (*fn)(void);
    struct RtTestSetup *next;
} RtTestSetup;
void rt_test_register_setup(RtTestSetup *s);

/* In TEST_SETUP only: start this test's runtime with `n` more rows of shims after the test
 * library's own (RtConfig.extra_shims, rt/init.h), as an embedding program hands rows of its
 * own to rt_init.  The rows must stay valid for the whole test.  CALL does not know them:
 * reach one through rt_native_import(dll, name, 0) and rt_test_invoke.  Called twice, the
 * second call replaces the first. */
void rt_test_extra_shims(const RtShimEntry *rows, unsigned n);

typedef struct RtTestGuestFn {
    const char *name;
    LiftedFn    fn;
    uint64_t    addr;           /* set when the runtime starts */
    struct RtTestGuestFn *next;
} RtTestGuestFn;

/* One argument of a call through the guest ABI. */
typedef struct {
    uint8_t cls;                /* RtArgClass */
    uint8_t size;               /* significant bytes: the harness puts garbage in the rest of the 8 */
    union { uint64_t u; float f; double d; } v;
} RtTestArg;

/* What the harness found wrong with a call (bits of RtTestRet.violations). */
#define RT_TV_RSP          0x001u   /* rsp did not advance by exactly 8 */
#define RT_TV_NONVOLATILE  0x002u   /* rbx rbp rsi rdi r12-r15 changed */
#define RT_TV_XMM          0x004u   /* xmm6-xmm15 changed */
#define RT_TV_DF           0x008u   /* direction flag changed */
#define RT_TV_FPCR         0x010u   /* did not return in the guest float mode */
#define RT_TV_CTX          0x020u   /* gs base, mxcsr or the host pointer changed */
#define RT_TV_STACK        0x040u   /* the caller's stack above the arguments was written */
#define RT_TV_RET_UPPER    0x080u   /* a 32-bit result came back with a nonzero upper half */
#define RT_TV_SRET         0x100u   /* a struct-returning function did not return the destination in rax */

typedef struct {
    uint64_t rax;
    Xmm      xmm0;
    uint32_t violations;
} RtTestRet;

void rt_test_register(RtTestCase *c);
void rt_test_register_guest_fn(RtTestGuestFn *g);

Ctx *rt_test_ctx(void);
void rt_test_case(const char *text);
bool rt_test_plan_case(unsigned n, const char *text);
bool rt_test_check(bool ok, const char *file, int line, const char *expr, int kind,
                   uint64_t a, uint64_t b, double fa, double fb);
void rt_test_fail(const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Call the guest code address `target` through the guest ABI with everything described at
 * the top of this file, and report what went wrong in the result instead of failing the
 * test.  `*detail`, if not NULL, receives a description of the first violation (a static
 * buffer).  This is what CALL is built on; tests of the harness itself use it directly. */
RtTestRet rt_test_invoke(uint64_t target, unsigned nargs, const RtTestArg *args, const char **detail);

/* CALL: look the shim up through its import slot, invoke, and fail the test on any
 * violation.  `sret` is 1 if args[0] is the hidden struct-return pointer. */
RtTestRet rt_test_call(const RtShimEntry *shim, unsigned nargs, const RtTestArg *args, int sret);
RtTestRet rt_test_call_raw(const RtShimEntry *shim, unsigned nslots, const uint64_t *slots);
void      rt_test_check_sret(const RtShimEntry *shim, RtTestRet r, const void *dst);

/* COM_CALL: args[0] is `this`.  Fails the test if the vtable of `this` is not `iface`'s (the
 * object was handed out as another interface) or the method broke the guest ABI. */
struct RtComIface;
RtTestRet rt_test_com_call(const struct RtComIface *iface, unsigned slot, unsigned nargs, const RtTestArg *args);

/* The value of the shim's import slot: its stub address. */
uint64_t rt_test_import_slot(const RtShimEntry *shim);

/* A struct of 1, 2, 4 or 8 bytes as the 64-bit value that carries it. */
static inline __attribute__((unused)) uint64_t rt_test_pack(const void *p, size_t size) {
    uint64_t v = 0;
    memcpy(&v, p, size < 8 ? size : 8);
    return v;
}

/* Two imports that have no shim, bound at start-up like any missing import: by name
 * (rttest.dll!NotImplemented) and by ordinal (rttest.dll!#77).  Calling either is the
 * "unimplemented import" report of section 9.1. */
uint64_t rt_test_trap_import(void);
uint64_t rt_test_trap_import_by_ordinal(void);

int  rt_test_trap_begin(void);                      /* forks: 1 in the child, 0 in the parent */
void rt_test_forked(void);                          /* in a process a test forked, first: its time limit (fork cancels alarms),
                                                     * the locks fork copied, and the quiet end of a fatal report: exit
                                                     * status 134 in place of SIGABRT (rt/crash.h; RT_TEST_ABORTED, rt/test_child.h) */
_Noreturn void rt_test_trap_survived(void);
void rt_test_trap_end(const char *file, int line, const char *stmt, const char *text);

bool rt_test_logged(RtChannel ch, const char *text);
unsigned rt_test_log_count(RtChannel ch);
void rt_test_trace(bool on);                        /* turn the `trace` channel on or off for this test */

/* The directories the runner made for this test (what rt_init was given). */
const char *rt_test_state_dir(void);
const char *rt_test_install_root(void);

#define RT_T_ISF_(x)  _Generic((x), float: 1, double: 1, default: 0)
#define RT_T_U64_(x)  _Generic((x), float: (uint64_t)0, double: (uint64_t)0, default: (uint64_t)(x))
#define RT_T_F64_(x)  _Generic((x), float: (x), double: (x), default: (double)RT_T_U64_(x))

/* The generated typed wrappers behind CALL and COM_CALL. */
#include "shim_calls.h"
#include "com_calls.h"

#endif /* RT_TEST_H */
