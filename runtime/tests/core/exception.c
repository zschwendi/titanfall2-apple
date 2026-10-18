/* Guest exceptions, plan A (rt/crash.h "exceptions, plan A"; runtime/core/exception.c):
 * the stored top-level filter, UnhandledExceptionFilter, RaiseException, DebugBreak.
 * Every fatal report happens in a forked child (rt/test_child.h). */
#include <pthread.h>
#include <stdatomic.h>

#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/crash.h"
#include "rt/vm.h"
#include "rt/../../core/report.h"       /* rt_exc_format_record, rt_exc_format_context */

/* ---- case 2: four host threads exchange the stored filter together ---- */
static atomic_int go;
static uint64_t sums[4], xors[4];

static void *racer(void *arg) {
    unsigned i = (unsigned)(uintptr_t)arg;
    uint64_t s = 0, x = 0;
    while (!atomic_load(&go)) { }
    for (uint64_t k = 1; k <= 10000; k++) {
        uint64_t r = rt_uef_set(((uint64_t)(i + 1) << 32) | k);
        s += r;
        x ^= r;
    }
    sums[i] = s;
    xors[i] = x;
    return NULL;
}

TEST(core, exception)
{
    T_PLAN(1, "exchange stores and returns the old filter; last error kept") {
        T_EQ(rt_uef_get(), 0);
        rt_thread_set_last_error(77);
        T_EQ(rt_uef_set(0x1111), 0);
        T_EQ(rt_uef_set(0x2222), 0x1111);
        T_EQ(rt_uef_get(), 0x2222);
        T_EQ(rt_uef_set(0), 0x2222);
        T_EQ(rt_uef_get(), 0);
        T_EQ(T_LAST_ERROR(), 77);
    }

    T_PLAN(2, "four threads exchange at once: sums and xors add up") {
        pthread_t th[4];
        uint64_t want_sum = 0, want_xor = 0;
        for (uint64_t i = 0; i < 4; i++)
            for (uint64_t k = 1; k <= 10000; k++) {
                uint64_t v = ((i + 1) << 32) | k;
                want_sum += v;
                want_xor ^= v;
            }
        for (int i = 0; i < 4; i++)
            pthread_create(&th[i], NULL, racer, (void *)(uintptr_t)i);
        atomic_store(&go, 1);
        for (int i = 0; i < 4; i++)
            pthread_join(th[i], NULL);
        uint64_t got_sum = sums[0] + sums[1] + sums[2] + sums[3] + rt_uef_get();
        uint64_t got_xor = xors[0] ^ xors[1] ^ xors[2] ^ xors[3] ^ rt_uef_get();
        T_EQ(got_sum, want_sum);
        T_EQ(got_xor, want_xor);
        rt_uef_set(0);
    }
}

/* ---- cases 3, 4, 6: UnhandledExceptionFilter, in forked children ---- */
static void uef_info(void *arg) { rt_uef_unhandled(T_CTX(), *(uint64_t *)arg); }

/* A guest region: the first 0x10000 bytes committed, the rest reserved (rt/vm.h). */
static uint64_t make_region(void) {
    uint64_t base = rt_vm_alloc(0, 0x20000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    if (base != 0) rt_vm_alloc(base, 0x10000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    return base;
}

TEST(core, exception_uef)
{
    RtTestChild c;
    char want[256];
    uint64_t g = 0;

    T_PLAN(3, "unhandled with no pointers: the top-level filter is 0") {
        const char *e = "FATAL guest called UnhandledExceptionFilter\n  no EXCEPTION_POINTERS\n"
                        "  top-level filter 0x0000000000000000, not called (RUNTIME.md 9.3)\n";
        uint64_t zero = 0;
        rt_test_child_run(uef_info, &zero, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(strncmp(c.out, e, strlen(e)) == 0);
        T_TRUE(T_CHILD_SAID(&c, "thread 0x"));
    }

    T_PLAN(4, "unreadable EXCEPTION_POINTERS: host memory and a reserved guest page") {
        uint64_t local = 0;
        uint64_t h = (uint64_t)(uintptr_t)&local;
        g = make_region();
        T_REQUIRE(g != 0);
        snprintf(want, sizeof want, "  EXCEPTION_POINTERS at 0x%llx is not readable guest memory\n",
                 (unsigned long long)h);
        rt_test_child_run(uef_info, &h, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(T_CHILD_SAID(&c, want));
        uint64_t reserved = g + 0x10000;
        snprintf(want, sizeof want, "  EXCEPTION_POINTERS at 0x%llx is not readable guest memory\n",
                 (unsigned long long)reserved);
        rt_test_child_run(uef_info, &reserved, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(T_CHILD_SAID(&c, want));
    }

    T_PLAN(6, "both pointers NULL: no record and no context") {
        g = make_region();
        T_REQUIRE(g != 0);
        ST64(g, 0);
        ST64(g + 8, 0);
        rt_test_child_run(uef_info, &g, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(T_CHILD_SAID(&c, "  no exception record\n  no context record\n"));
    }
}

/* ---- cases 7 to 11: the thread-name exception, on the main thread ---- */
TEST(core, exception_name)
{
    RtThread *t = RT_THREAD(T_CTX());
    char before[256];
    char nm[101];
    uint64_t a[3];

    T_PLAN(7, "a thread-name exception names the calling thread; last error kept") {
        a[0] = 0x1000; a[1] = (uint64_t)(uintptr_t)"Worker 7"; a[2] = 0xFFFFFFFF;
        rt_thread_set_last_error(77);
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, a);
        T_EQ(strcmp(t->name, "Worker 7"), 0);
        T_EQ(T_LAST_ERROR(), 77);
    }

    T_PLAN(8, "only the low 32 bits of the first two words count") {
        a[0] = 0xDEADBEEF00001000ULL; a[1] = (uint64_t)(uintptr_t)"Worker 8";
        a[2] = 0xDEADBEEF00000000ULL | t->id;
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, a);
        T_EQ(strcmp(t->name, "Worker 8"), 0);
    }

    T_PLAN(9, "a 100-byte name is cut to 63 bytes") {
        memset(nm, 'n', 100);
        nm[100] = 0;
        a[0] = 0x1000; a[1] = (uint64_t)(uintptr_t)nm; a[2] = 0xFFFFFFFF;
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, a);
        T_EQ(strlen(t->name), 63);
    }

    T_PLAN(10, "another thread's id: the name is not set, one WARN line") {
        snprintf(before, sizeof before, "%s", t->name);
        a[0] = 0x1000; a[1] = (uint64_t)(uintptr_t)"Other"; a[2] = (uint64_t)t->id + 4;
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, a);
        T_EQ(strcmp(t->name, before), 0);
        T_TRUE(T_LOGGED(RT_CH_THREAD, "thread name \"Other\" for thread 0x"));
        T_TRUE(T_LOGGED(RT_CH_THREAD, "ignored: naming another thread is not built"));
    }

    T_PLAN(11, "malformed shapes: name kept, exactly one line each") {
        uint64_t n0;
        snprintf(before, sizeof before, "%s", t->name);
        a[0] = 0x1000; a[1] = (uint64_t)(uintptr_t)"Bad"; a[2] = 0xFFFFFFFF;

        n0 = T_LOG_COUNT(RT_CH_THREAD);
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 2, a);
        T_EQ(T_LOG_COUNT(RT_CH_THREAD), n0 + 1);
        T_TRUE(T_LOGGED(RT_CH_THREAD, "malformed thread-name exception ignored"));
        T_EQ(strcmp(t->name, before), 0);

        a[0] = 0x1001;
        n0 = T_LOG_COUNT(RT_CH_THREAD);
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, a);
        T_EQ(T_LOG_COUNT(RT_CH_THREAD), n0 + 1);
        T_EQ(strcmp(t->name, before), 0);

        a[0] = 0x1000; a[1] = 0;
        n0 = T_LOG_COUNT(RT_CH_THREAD);
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, a);
        T_EQ(T_LOG_COUNT(RT_CH_THREAD), n0 + 1);
        T_EQ(strcmp(t->name, before), 0);

        n0 = T_LOG_COUNT(RT_CH_THREAD);
        rt_raise_exception(T_CTX(), 0x406D1388, 0, 3, NULL);
        T_EQ(T_LOG_COUNT(RT_CH_THREAD), n0 + 1);
        T_EQ(strcmp(t->name, before), 0);
    }
}

/* ---- cases 12 to 15: the debug-print codes, on the main thread ---- */
TEST(core, exception_debug)
{
    T_PLAN(12, "debug print: up to the NUL, at most args[0] bytes, a final newline and CR dropped") {
        uint64_t a[2] = { 6, (uint64_t)(uintptr_t)"hello" };
        rt_raise_exception(T_CTX(), 0x40010006, 0, 2, a);
        T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "hello"));

        uint64_t b[2] = { 3, (uint64_t)(uintptr_t)"abcdef" };
        rt_raise_exception(T_CTX(), 0x40010006, 0, 2, b);
        T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "abc"));
        T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "abcd"));

        uint64_t c[2] = { 7, (uint64_t)(uintptr_t)"line\r\n" };
        rt_raise_exception(T_CTX(), 0x40010006, 0, 2, c);
        T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "line"));
        T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "line\r"));
    }

    T_PLAN(13, "debug print with nothing to print: nothing logged, returns") {
        uint64_t n0 = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
        uint64_t a1[2] = { 6, 0 };
        rt_raise_exception(T_CTX(), 0x40010006, 0, 1, a1);
        T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n0);

        uint64_t a2[2] = { 6, (uint64_t)(uintptr_t)"hello" };
        rt_raise_exception(T_CTX(), 0x4001000A, 0, 2, a2);
        T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n0);
    }

    T_PLAN(14, "wide debug print: the ANSI copy in args[2] and args[3] is logged") {
        static const WCHAR wtext[] = { 'w', 'i', 'd', 'e', '!', 0 };
        uint64_t a[4] = { 7, (uint64_t)(uintptr_t)wtext, 6, (uint64_t)(uintptr_t)"wide!" };
        rt_raise_exception(T_CTX(), 0x4001000A, 0, 4, a);
        T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "wide!"));
    }

    T_PLAN(15, "debug print: at most 1,000 bytes of a 2,000-byte text") {
        char dd[2001];
        char s1000[1001], s1001[1002];
        uint64_t a[2] = { 2001, (uint64_t)(uintptr_t)dd };
        memset(dd, 'd', 2000);
        dd[2000] = 0;
        rt_raise_exception(T_CTX(), 0x40010006, 0, 2, a);
        memset(s1000, 'd', 1000);
        s1000[1000] = 0;
        T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, s1000));
        memset(s1001, 'd', 1001);
        s1001[1001] = 0;
        T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, s1001));
    }
}

/* ---- cases 16 to 21: RaiseException and DebugBreak, fatal, in forked children ---- */
static void raise_cpp(void *arg) { rt_raise_exception(T_CTX(), 0xE06D7363, 1, 4, (const uint64_t *)arg); }
static void raise_unknown(void *arg) { (void)arg; rt_raise_exception(T_CTX(), 0x100, 0, 0, NULL); }
static void raise_many(void *arg) { rt_raise_exception(T_CTX(), 0x100, 0, 20, (const uint64_t *)arg); }
static void raise_nargs_no_args(void *arg) { (void)arg; rt_raise_exception(T_CTX(), 0x100, 0, 5, NULL); }
static void debug_break_null(void *arg) { (void)arg; rt_debug_break(NULL); }

static atomic_int raise_gate;
static void *raiser(void *arg) {
    (void)arg;
    while (!atomic_load(&raise_gate)) { }
    rt_raise_exception(NULL, 0x100, 0, 0, NULL);
    return NULL;
}
static void four_raises(void *arg) {
    (void)arg;
    pthread_t th[4];
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, raiser, NULL);
    atomic_store(&raise_gate, 1);
    for (;;) pause();
}

TEST(core, exception_raise)
{
    RtTestChild c;
    char want[512];

    T_PLAN(16, "C++ throw, fatal: the ThrowInfo names bad_alloc") {
        uint64_t g = make_region();
        T_REQUIRE(g != 0);
        /* ThrowInfo at g+0x100 (image base g); CatchableTypeArray at g+0x200;
         * CatchableType at g+0x300; TypeDescriptor at g+0x400 (x64 MSVC layout) */
        ST32(g + 0x100, 0);                     /* attributes */
        ST32(g + 0x104, 0);                     /* pmfnUnwind */
        ST32(g + 0x108, 0);                     /* pForwardCompat */
        ST32(g + 0x10C, 0x200);                 /* pCatchableTypeArray */
        ST32(g + 0x200, 1);                     /* nCatchableTypes */
        ST32(g + 0x204, 0x300);                 /* arrayOfCatchableTypes[0] */
        ST32(g + 0x300, 0);                     /* properties */
        ST32(g + 0x304, 0x400);                 /* pType */
        ST32(g + 0x308, 0);                     /* thisDisplacement.mdisp */
        ST32(g + 0x30C, 0);                     /* thisDisplacement.pdisp */
        ST32(g + 0x310, 0);                     /* thisDisplacement.vdisp */
        ST32(g + 0x314, 0);                     /* sizeOrOffset */
        ST32(g + 0x318, 0);                     /* copyFunction */
        ST64(g + 0x400, 0);                     /* TypeDescriptor.pVFTable */
        ST64(g + 0x408, 0);                     /* TypeDescriptor.spare */
        memcpy((void *)(uintptr_t)(g + 0x410), ".?AVbad_alloc@std@@", sizeof ".?AVbad_alloc@std@@");
        uint64_t a[4] = { 0x19930520, g + 0x800, g + 0x100, g };
        snprintf(want, sizeof want,
                 "FATAL guest raised exception 0xe06d7363 (C++ exception)\n"
                 "  flags 0x1, 4 parameters: 0x19930520 0x%llx 0x%llx 0x%llx\n"
                 "  C++ exception of type .?AVbad_alloc@std@@ (object at 0x%llx)\n",
                 (unsigned long long)(g + 0x800), (unsigned long long)(g + 0x100),
                 (unsigned long long)g, (unsigned long long)(g + 0x800));
        rt_test_child_run(raise_cpp, a, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(strncmp(c.out, want, strlen(want)) == 0);
        T_TRUE(T_CHILD_SAID(&c, "thread 0x"));
        T_EQ(rt_test_count(c.out, "FATAL"), 1);
    }

    T_PLAN(17, "unknown code: no name, flags, no parameters") {
        const char *e = "FATAL guest raised exception 0x00000100\n  flags 0x0, 0 parameters\n";
        rt_test_child_run(raise_unknown, NULL, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(strncmp(c.out, e, strlen(e)) == 0);
    }

    T_PLAN(18, "parameters: at most 15 printed; args NULL means none") {
        uint64_t a20[20];
        for (int i = 0; i < 20; i++) a20[i] = (uint64_t)(i + 1);
        rt_test_child_run(raise_many, a20, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(T_CHILD_SAID(&c, "\n  flags 0x0, 15 parameters: 0x1 0x2 0x3 0x4 0x5 0x6 0x7 0x8 0x9 0xa 0xb 0xc 0xd 0xe 0xf\n"));
        rt_test_child_run(raise_nargs_no_args, NULL, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(T_CHILD_SAID(&c, "  flags 0x0, 0 parameters\n"));
    }

    T_PLAN(19, "access violation: the report names the address") {
        uint64_t av[2] = { 1, 0x10 };
        T_TRAPS(rt_raise_exception(T_CTX(), 0xC0000005, 0, 2, av), "write of address 0x0000000000000010");
    }

    T_PLAN(20, "DebugBreak: a fatal report, and no guest thread when ctx is NULL") {
        T_TRAPS(rt_debug_break(T_CTX()), "FATAL guest called DebugBreak");
        rt_test_child_run(debug_break_null, NULL, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(strcmp(c.out, "FATAL guest called DebugBreak\n  (no guest thread)\n") == 0);
    }

    T_PLAN(21, "four threads raise at once: one report, five rounds") {
        for (int round = 0; round < 5; round++) {
            rt_test_child_run(four_raises, NULL, &c);
            T_TRUE(T_CHILD_ABORTED(&c));
            T_EQ(rt_test_count(c.out, "FATAL"), 1);
            /* added: the one report is the raise's own, not some other fatal report */
            T_TRUE(T_CHILD_SAID(&c, "FATAL guest raised exception 0x00000100\n"));
        }
    }
}

/* ---- case 5: the stored filter is never called ---- */
GUEST_FN(filter_fn) { rt_log_raw("FILTER RAN\n", 11); GUEST_RET(ctx); }
static void uef_filter_child(void *arg) {
    rt_uef_set(T_GUEST_ADDR(filter_fn));
    rt_uef_unhandled(T_CTX(), *(uint64_t *)arg);
}

TEST(core, exception_filter)
{
    RtTestChild c;
    char e[4096], rec[2048], ctxs[2048];

    T_PLAN(5, "the stored filter is never called; the report names it") {
        uint64_t g = make_region();
        T_REQUIRE(g != 0);
        ST64(g, g + 0x100);                     /* EXCEPTION_POINTERS.ExceptionRecord */
        ST64(g + 8, g + 0x400);                 /* EXCEPTION_POINTERS.ContextRecord */
        ST32(g + 0x100, 0xC0000409);            /* record: ExceptionCode */
        ST32(g + 0x104, 1);                     /* record: ExceptionFlags */
        memset((void *)(uintptr_t)(g + 0x400), 0, 0x4D0);
        ST64(g + 0x400 + 0xF8, 0x7777);         /* context: Rip */
        /* ASSUMED signatures, not given in the brief: (buf, cap, at, addr), returning the new length */
        size_t nr = rt_exc_format_record(rec, sizeof rec, 0, g + 0x100);
        size_t nc = rt_exc_format_context(ctxs, sizeof ctxs, 0, g + 0x400);
        rec[nr] = 0;
        ctxs[nc] = 0;
        snprintf(e, sizeof e,
                 "FATAL guest called UnhandledExceptionFilter\n%s%s"
                 "  top-level filter 0x%016llx, not called (RUNTIME.md 9.3)\n",
                 rec, ctxs, (unsigned long long)T_GUEST_ADDR(filter_fn));
        rt_test_child_run(uef_filter_child, &g, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(strncmp(c.out, e, strlen(e)) == 0);
        T_TRUE(!T_CHILD_SAID(&c, "FILTER RAN"));
        T_EQ(rt_test_count(c.out, "FATAL"), 1);
    }
}
