/* The fatal report that takes no lock: rt_log_raw, rt_log_flush_try (rt/log.h), the
 * one-report rule and the text helpers (core/report.h); review finding R14. */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>

#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/vm.h"
#include "rt/../../core/report.h"

/* ---- what the children do ----------------------------------------------------------------------- */

static void raw_bytes(void *arg) {
    (void)arg;
    rt_log_raw("one\ntwo", 7);
    rt_log_raw("never", 0);                             /* zero bytes: nothing */
    rt_log_raw("-3\0x", 4);                             /* bytes, not a string: the NUL is written */
    _exit(0);
}

/* A sink runs with the log's lock held by the calling thread. */
static int sink_flush_result = -1;
static void sink_that_flushes(RtChannel ch, RtLogLevel level, const char *line, void *user) {
    (void)ch; (void)level; (void)line; (void)user;
    sink_flush_result = rt_log_flush_try();             /* must be false, at once */
    rt_log_raw("raw inside the lock\n", 20);            /* must not wait for the lock */
}
static void flush_inside_lock(void *arg) {
    (void)arg;
    bool free_before = rt_log_flush_try();
    rt_log_set_sink(sink_that_flushes, NULL);
    rt_log(RT_CH_VM, RT_LL_ERROR, "x");
    rt_log_set_sink(NULL, NULL);
    _exit(free_before && sink_flush_result == 0 && rt_log_flush_try() ? 0 : 1);
}

static void sink_that_dies(RtChannel ch, RtLogLevel level, const char *line, void *user) {
    (void)ch; (void)level; (void)line; (void)user;
    rt_fatal("inside the sink, value %d", 42);          /* the log's lock is held: R14 */
}
static void fatal_inside_lock(void *arg) {
    (void)arg;
    rt_log_set_sink(sink_that_dies, NULL);
    rt_log(RT_CH_VM, RT_LL_ERROR, "x");
}

static atomic_int go;
static void *racer(void *arg) {
    while (!atomic_load(&go)) { }
    rt_fatal("racer %d", (int)(intptr_t)arg);
}
static void four_fatals_at_once(void *arg) {
    (void)arg;
    pthread_t th[4];
    for (intptr_t i = 0; i < 4; i++) pthread_create(&th[i], NULL, racer, (void *)i);
    atomic_store(&go, 1);
    for (;;) pause();                                   /* the winner ends the process */
}

static void begin_twice(void *arg) {
    (void)arg;
    bool idle = !rt_report_active();
    RtReportTurn a = rt_report_begin();
    unsigned armed = alarm(0);                          /* the watchdog: seconds that were left */
    RtReportTurn b = rt_report_begin();
    size_t cap = 0;
    char *buf = rt_report_buffer(&cap);
    _exit(idle && a == RT_REPORT_FIRST && b == RT_REPORT_NESTED && rt_report_active() &&
          armed >= 1 && armed <= RT_REPORT_WATCHDOG_S && buf && cap == RT_REPORT_BUF ? 0 : 1);
}

static atomic_int second_returned;
static void *second_claim(void *arg) {
    (void)arg;
    rt_report_begin();                                  /* another thread owns the report: never returns */
    atomic_store(&second_returned, 1);
    return NULL;
}
static void begin_on_two_threads(void *arg) {
    (void)arg;
    pthread_t th;
    if (rt_report_begin() != RT_REPORT_FIRST) _exit(2);
    alarm(0);
    pthread_create(&th, NULL, second_claim, NULL);
    usleep(200 * 1000);
    _exit(atomic_load(&second_returned) ? 1 : 0);
}

static void die_with_block(void *arg) {
    (void)arg;
    rt_report_die("FATAL a block\n  second line\n");
}

/* Lines that rt_log buffered in a file come before the report in that file. */
static void buffered_then_fatal(void *arg) {
    setenv("RT_LOG_FILE", (const char *)arg, 1);
    rt_log_init();
    rt_log(RT_CH_VM, RT_LL_ERROR, "before the crash");
    rt_fatal("then the report");
}

/* ---- the tests ------------------------------------------------------------------------------------ */

TEST(core, crash_report_raw)
{
    RtTestChild c;
    rt_test_child_run(raw_bytes, NULL, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_EQ(c.len, 11);
    T_TRUE(memcmp(c.out, "one\ntwo-3\0x", 11) == 0);    /* as given: no prefix, nothing appended */

    rt_test_child_run(flush_inside_lock, NULL, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "raw inside the lock\n"));

    /* A fatal error with the log's lock held is reported; it used to wait for itself. */
    rt_test_child_run(fatal_inside_lock, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL runtime: inside the sink, value 42\n"));

    rt_test_child_run(die_with_block, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(strcmp(c.out, "FATAL a block\n  second line\n") == 0);

    char path[1200];
    snprintf(path, sizeof path, "%s/log.txt", rt_test_state_dir());
    rt_test_child_run(buffered_then_fatal, path, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
    static char text[4096];
    int fd = open(path, O_RDONLY);
    T_REQUIRE(fd >= 0);
    ssize_t n = read(fd, text, sizeof text - 1);
    close(fd);
    unlink(path);
    text[n > 0 ? n : 0] = 0;
    const char *line = strstr(text, "vm: before the crash\n"), *fatal = strstr(text, "FATAL runtime: then the report\n");
    T_TRUE(line && fatal && line < fatal);
}

TEST(core, crash_report_one)
{
    RtTestChild c;
    T_TRUE(!rt_report_active());
    rt_test_child_run(begin_twice, NULL, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    rt_test_child_run(begin_on_two_threads, NULL, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));

    /* Four threads fail together: one report, whole, and the process aborts. */
    for (int round = 0; round < 5; round++) {
        rt_test_child_run(four_fatals_at_once, NULL, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_EQ(rt_test_count(c.out, "FATAL"), 1);
        T_TRUE(T_CHILD_SAID(&c, "FATAL runtime: racer ") && c.len > 0 && c.out[c.len - 1] == '\n');
    }
    T_TRUE(!rt_report_active());                        /* all of that happened in children */
}

TEST(core, crash_report_text)
{
    char b[8], big[2048];
    T_EQ(rt_report_appendf(b, sizeof b, 0, "%s", "abcdefghij"), 7);
    T_TRUE(strcmp(b, "abcdefg") == 0);                  /* cut, terminated, length n-1 */
    T_EQ(rt_report_appendf(b, sizeof b, 7, "more"), 7);
    T_TRUE(strcmp(b, "abcdefg") == 0);
    T_EQ(rt_report_appendf(b, sizeof b, 0, "%d", 12), 2);
    T_EQ(rt_report_appendf(b, sizeof b, 2, "-%x", 255), 5);
    T_TRUE(strcmp(b, "12-ff") == 0);

    uint64_t r[16];
    for (int i = 0; i < 16; i++) r[i] = 0x1000 + (uint64_t)i;
    size_t at = rt_report_regs(big, sizeof big, 0, r, "  ");
    T_TRUE(strcmp(big,
        "  rax=0000000000001000 rcx=0000000000001001 rdx=0000000000001002 rbx=0000000000001003\n"
        "  rsp=0000000000001004 rbp=0000000000001005 rsi=0000000000001006 rdi=0000000000001007\n"
        "  r8 =0000000000001008 r9 =0000000000001009 r10=000000000000100a r11=000000000000100b\n"
        "  r12=000000000000100c r13=000000000000100d r14=000000000000100e r15=000000000000100f\n") == 0);
    T_EQ(at, strlen(big));

    /* A region: 2 pages read-write, 1 no-access, 1 guard, the rest reserved. */
    uint64_t base = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(base != 0);
    rt_vm_alloc(base, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    rt_vm_alloc(base + 0x2000, 0x1000, MEM_COMMIT, PAGE_NOACCESS, RT_VM_PRIVATE);
    rt_vm_alloc(base + 0x3000, 0x1000, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD, RT_VM_PRIVATE);
    int local = 0;
    T_TRUE(rt_report_readable(base, 1) && rt_report_readable(base, 0x2000) && rt_report_readable(base + 0x1FFF, 1));
    T_TRUE(rt_report_readable(base + 0x10, 0));                         /* 0 is treated as 1 */
    T_TRUE(!rt_report_readable(base, 0x2001));                          /* runs into the no-access page */
    T_TRUE(!rt_report_readable(base + 0x2000, 1) && !rt_report_readable(base + 0x3000, 1));
    T_TRUE(!rt_report_readable(base + 0x4000, 1));                      /* reserved */
    T_TRUE(!rt_report_readable((uint64_t)(uintptr_t)&local, 4));        /* host memory */
    T_TRUE(!rt_report_readable(0, 1) && !rt_report_readable(UINT64_C(0xFFFFFFFFFFFFFFFF), 2));
    rt_vm_test_hold_lock(true);
    bool busy = rt_report_readable(base, 1);
    rt_vm_test_hold_lock(false);
    T_TRUE(!busy);                                                      /* a busy map is "no" */

    /* rt_report_address. */
    RtThread *t = RT_THREAD(T_CTX());
    rt_report_address(big, sizeof big, 0, 0x10, t);
    T_TRUE(strcmp(big, "  address: a null pointer (offset 0x10)\n") == 0);
    rt_report_address(big, sizeof big, 0, (uint64_t)(uintptr_t)&local, NULL);
    T_TRUE(strcmp(big, "  address: host memory or unmapped, outside the guest arenas\n") == 0);
    char want[512];
    rt_report_address(big, sizeof big, 0, base + 0x1008, NULL);
    snprintf(want, sizeof want, "  address: vm:0x%llx, in the private region at 0x%llx (0x10000 bytes): committed, "
             "protection 0x4 (PAGE_READWRITE)\n", (unsigned long long)base + 0x1008, (unsigned long long)base);
    T_TRUE(strcmp(big, want) == 0);
    rt_report_address(big, sizeof big, 0, base + 0x3000, NULL);
    T_TRUE(strstr(big, "protection 0x104 (PAGE_READWRITE | PAGE_GUARD)\n") != NULL);
    rt_report_address(big, sizeof big, 0, base + 0x8000, NULL);
    T_TRUE(strstr(big, "(0x10000 bytes): reserved, not committed\n") != NULL);
    rt_vm_test_hold_lock(true);
    rt_report_address(big, sizeof big, 0, base, NULL);
    rt_vm_test_hold_lock(false);
    T_TRUE(strstr(big, "the VM map is busy") != NULL);
    rt_report_address(big, sizeof big, 0, t->stack_limit - 8, t);
    T_TRUE(strstr(big, "  address: GUEST STACK OVERFLOW: the guard area below this thread's guest stack") == big);
    rt_report_address(big, sizeof big, 0, t->stack_limit - 8, NULL);    /* not this thread's: no such claim */
    T_TRUE(strstr(big, "GUEST STACK OVERFLOW") == NULL);
    rt_vm_free(base, 0, MEM_RELEASE);
    rt_report_address(big, sizeof big, 0, base, NULL);
    T_TRUE(strstr(big, ", free: in no VM region\n") != NULL);

    /* rt_report_guest. */
    rt_report_guest(big, sizeof big, 0, NULL);
    T_TRUE(strcmp(big, "  (no guest thread)\n") == 0);
    uint64_t saved = t->ctx.r[3];
    t->ctx.r[3] = UINT64_C(0x1122334455667788);
    rt_report_guest(big, sizeof big, 0, t);
    t->ctx.r[3] = saved;
    T_TRUE(strstr(big, " rbx=1122334455667788\n") != NULL && strstr(big, " mxcsr=00001f80 gs_base=") != NULL);
    T_EQ(rt_test_count(big, "\n"), 5);                                  /* four register lines, one flag line */
}
