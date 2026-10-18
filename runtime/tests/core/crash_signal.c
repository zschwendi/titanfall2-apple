/* Host signals: the report for a fault in lifted code, in a shim and in the runtime
 * (rt/crash.h, runtime/core/crash.c; docs/RUNTIME.md section 9.3).  Every fault happens in a
 * forked child (rt/test_child.h); the handlers are the runtime's own, installed by rt_init. */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>

#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/crash.h"
#include "rt/vm.h"
#include "rt/../../core/report.h"       /* rt_vm_test_hold_lock */

__attribute__((noinline)) static void poke(uint64_t addr)  { *(volatile uint32_t *)(uintptr_t)addr = 1; }
__attribute__((noinline)) static uint32_t peek(uint64_t addr) { return *(volatile uint32_t *)(uintptr_t)addr; }

static void write_at(void *arg) { poke(*(uint64_t *)arg); }
static void read_at(void *arg)  { (void)peek(*(uint64_t *)arg); }

/* "Lifted code": a guest function that stores a register and then writes through rcx. */
GUEST_FN(crasher) {
    ctx->r[3] = UINT64_C(0x1122334455667788);
    poke(ctx->r[1]);
    GUEST_RET(ctx);
}
static void fault_in_guest_fn(void *arg) {
    rt_guest_call(T_CTX(), T_GUEST_ADDR(crasher), 1, (uint64_t *)arg);
}

static void fault_in_shim(void *arg) {
    (void)arg;
    CALL(kernel32, GetSystemTimeAsFileTime, (LPFILETIME)(uintptr_t)0x20);
}

static int deeper(volatile char *prev, int n) {
    volatile char pad[4096];
    pad[0] = prev ? prev[0] : 0;
    return n > 0 ? deeper(pad, n + 1) + pad[0] : 0;     /* never ends: runs off the host stack */
}
static void overflow_host_stack(void *arg) { (void)arg; (void)deeper(NULL, 1); }

static void plain_abort(void *arg) { (void)arg; abort(); }
static void plain_trap(void *arg)  { (void)arg; __builtin_trap(); }

static void *thread_null_write(void *arg) { (void)arg; poke(0x30); return NULL; }
static void fault_on_host_thread(void *arg) {
    (void)arg;
    pthread_t th;
    pthread_create(&th, NULL, thread_null_write, NULL);
    pthread_join(th, NULL);
}

static void sink_that_faults(RtChannel ch, RtLogLevel level, const char *line, void *user) {
    (void)ch; (void)level; (void)line; (void)user;
    poke(0x40);                                         /* with the log's lock held */
}
static void fault_inside_log_lock(void *arg) {
    (void)arg;
    rt_log_set_sink(sink_that_faults, NULL);
    rt_log(RT_CH_VM, RT_LL_ERROR, "x");
}

static void fault_inside_vm_lock(void *arg) {
    rt_vm_test_hold_lock(true);
    (void)peek(*(uint64_t *)arg);
}

static atomic_int go;
static void *racer(void *arg) { (void)arg; while (!atomic_load(&go)) { } poke(0x50); return NULL; }
static void four_faults_at_once(void *arg) {
    (void)arg;
    pthread_t th[4];
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, racer, NULL);
    atomic_store(&go, 1);
    for (;;) pause();
}

static void *altstack_thread(void *arg) {
    int *ok = arg;
    stack_t ss;
    sigaltstack(NULL, &ss);
    bool none_before = (ss.ss_flags & SS_DISABLE) != 0;
    rt_crash_thread_attach();
    sigaltstack(NULL, &ss);
    bool on = !(ss.ss_flags & SS_DISABLE) && ss.ss_size >= 64 * 1024;
    void *sp = ss.ss_sp;
    rt_crash_thread_attach();                           /* idempotent: the same stack */
    sigaltstack(NULL, &ss);
    bool same = ss.ss_sp == sp;
    rt_crash_thread_detach();
    sigaltstack(NULL, &ss);
    bool off = (ss.ss_flags & SS_DISABLE) != 0;
    rt_crash_thread_detach();                           /* nothing to undo: no effect */
    *ok = none_before && on && same && off;
    return NULL;
}

TEST(core, crash_signal)
{
    RtTestChild c;
    char want[256];
    RtThread *t = RT_THREAD(T_CTX());
    uint64_t a;

    /* A null pointer written by host code on the guest thread. */
    a = 0x10;
    rt_test_child_run(write_at, &a, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(strstr(c.out, "FATAL host signal SIGSEGV (11), code ") == c.out);
    T_TRUE(T_CHILD_SAID(&c, ", fault address 0x0000000000000010\n"));
    T_TRUE(T_CHILD_SAID(&c, "  address: a null pointer (offset 0x10)\n"));
    snprintf(want, sizeof want, "  thread 0x%x \"%s\", in lifted code, or in host code outside a shim (in_host=0)\n", t->id, t->name);
    T_TRUE(T_CHILD_SAID(&c, want));
    T_TRUE(T_CHILD_SAID(&c, "  host pc=0x") && T_CHILD_SAID(&c, " (pc is rt-test+0x"));
    T_TRUE(T_CHILD_SAID(&c, "  rax=") && T_CHILD_SAID(&c, " mxcsr=00001f80 gs_base="));
    T_TRUE(T_CHILD_SAID(&c, "  host frames ") && T_CHILD_SAID(&c, "    #0 0x") && T_CHILD_SAID(&c, " poke+0x"));
    T_EQ(rt_test_count(c.out, "FATAL"), 1);

    /* Guest memory: a write to a read-only page, a read of a reserved page. */
    uint64_t base = rt_vm_alloc(0, 0x20000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(base != 0);
    rt_vm_alloc(base, 0x10000, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE);
    a = base + 0x1234;
    rt_test_child_run(write_at, &a, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL host signal SIGBUS (10), code "));
    snprintf(want, sizeof want, "  address: vm:0x%llx, in the private region at 0x%llx (0x20000 bytes): committed, "
             "protection 0x2 (PAGE_READONLY)\n", (unsigned long long)a, (unsigned long long)base);
    T_TRUE(T_CHILD_SAID(&c, want));
    a = base + 0x10008;
    rt_test_child_run(read_at, &a, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "(0x20000 bytes): reserved, not committed\n"));

    /* The VM map's lock held by the faulting thread: reported all the same, as busy (R14). */
    rt_test_child_run(fault_inside_vm_lock, &a, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL host signal ") && T_CHILD_SAID(&c, "the VM map is busy"));
    /* The log's lock held by the faulting thread (R14). */
    rt_test_child_run(fault_inside_log_lock, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL host signal SIGSEGV") && T_CHILD_SAID(&c, "(offset 0x40)"));

    /* A fault in lifted code: the function, the registers it stored, the callers. */
    a = base + 0x100;                                   /* read-only: the write faults */
    rt_test_child_run(fault_in_guest_fn, &a, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "  nearest dispatch entry below pc: rttest.dll!crasher +0x"));
    T_TRUE(T_CHILD_SAID(&c, " rbx=1122334455667788\n") && T_CHILD_SAID(&c, ", in lifted code, or in host code outside a shim (in_host=0)\n"));
    snprintf(want, sizeof want, " rcx=%016llx ", (unsigned long long)a);
    T_TRUE(T_CHILD_SAID(&c, want));
    T_TRUE(T_CHILD_SAID(&c, " rttest.dll!crasher  [crasher+0x") && T_CHILD_SAID(&c, " rt_guest_call"));
    T_TRUE(T_CHILD_SAID(&c, "(guest registers as last stored in Ctx;"));
    if (getenv("RT_SHOW_SAMPLE")) fprintf(stderr, "%s", c.out);

    /* A fault inside a shim: the shim and its guest caller are named. */
    rt_test_child_run(fault_in_shim, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, ", inside a shim (in_host=1)\n"));
    T_TRUE(T_CHILD_SAID(&c, "  in shim kernel32.dll!GetSystemTimeAsFileTime, called from "));
    T_TRUE(T_CHILD_SAID(&c, "(offset 0x20)"));

    /* The two stack overflows. */
    a = t->stack_region;                                /* the lowest guard page of the guest stack */
    rt_test_child_run(read_at, &a, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "  address: GUEST STACK OVERFLOW: the guard area below this thread's guest stack"));
    rt_test_child_run(overflow_host_stack, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL host signal ") && T_CHILD_SAID(&c, "  HOST STACK OVERFLOW: the address is just below"));
    T_TRUE(T_CHILD_SAID(&c, " deeper+0x"));

    /* Other signals, and a host thread that is not a guest thread. */
    rt_test_child_run(plain_trap, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL host signal SIGTRAP (5), code ") && !T_CHILD_SAID(&c, "  address: "));
    rt_test_child_run(plain_abort, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(strstr(c.out, "FATAL host abort(): SIGABRT (6) raised outside a fatal report") == c.out);
    T_TRUE(T_CHILD_SAID(&c, " plain_abort+0x"));
    T_EQ(rt_test_count(c.out, "FATAL"), 1);             /* the abort that ends the report is not reported */
    rt_test_child_run(fault_on_host_thread, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "  not a guest thread (host thread 0x") && T_CHILD_SAID(&c, "(offset 0x30)"));
    T_TRUE(!T_CHILD_SAID(&c, "  rax="));

    /* Four threads fault together: one report, whole.  How the process then ends is not one
     * thing: the owner of the report sets the signals back to their default action and calls
     * abort() (rt_report_abort), and a thread that had not yet reached its fault when the
     * owner did that faults with no handler, so the process dies of that thread's SIGSEGV
     * and not of the owner's abort.  Which of the two happens is the scheduler's choice (on a
     * busy machine a thread can be held up between `go` and its fault), so both are accepted.
     * The abort is the quiet one of a test's child (exit status 134, T_CHILD_ABORTED); the
     * late fault is the real signal as it always was: the quiet end changes nothing but the
     * last step of a report (rt/crash.h), and that thread is not in one. */
    for (int round = 0; round < 5; round++) {
        rt_test_child_run(four_faults_at_once, NULL, &c);
        bool late_fault = WIFSIGNALED(c.status) && WTERMSIG(c.status) == SIGSEGV;
        if (!T_CHILD_ABORTED(&c) && !late_fault)
            rt_test_fail(__FILE__, __LINE__, "four faults at once: the process ended neither in the report's abort nor of SIGSEGV "
                         "(wait status %#x); output:\n%s", c.status, c.out);
        else T_TRUE(1);
        T_EQ(rt_test_count(c.out, "FATAL"), 1);
        T_EQ(rt_test_count(c.out, "  host frames "), 1);
    }

    /* A fatal report of the other kind is unchanged by the handlers: the same abort, one report. */
    T_TRAPS(rt_fatal("ordinary %d", 7), "FATAL runtime: ordinary 7");

    /* The alternate stack of another host thread. */
    pthread_t th;
    int ok = 0;
    T_REQUIRE(pthread_create(&th, NULL, altstack_thread, &ok) == 0);
    pthread_join(th, NULL);
    T_TRUE(ok);
    stack_t ss;
    sigaltstack(NULL, &ss);
    T_TRUE(!(ss.ss_flags & SS_DISABLE));                /* the main thread got one from rt_crash_init */
    rt_vm_free(base, 0, MEM_RELEASE);
}
