/* The harness's own checks must be able to fail (docs/RUNTIME.md section 10.4).  Each
 * guest function below breaks one rule of the Windows x64 ABI; rt_test_invoke, which CALL
 * is built on, must report exactly that violation.
 */
#include "rt/test.h"
#include "rttest_calls.h"

GUEST_FN(well_behaved)
{
    /* Everything a callee may do: use the volatile registers and the home space. */
    static const uint8_t vol[] = { 0, 1, 2, 8, 9, 10, 11 };
    for (unsigned i = 0; i < sizeof vol; i++) ctx->r[vol[i]] = 0;
    for (unsigned i = 0; i < 6; i++) ctx->xmm[i].q[0] = ctx->xmm[i].q[1] = 0;
    for (unsigned i = 0; i < 4; i++) ST64(ctx->r[4] + 8 + 8 * (uint64_t)i, 0);
    ctx->r[0] = 42;
    GUEST_RET(ctx);
}
GUEST_FN(clobbers_rbx)          { ctx->r[3] ^= 1; GUEST_RET(ctx); }
GUEST_FN(clobbers_r15)          { ctx->r[15] = 0; GUEST_RET(ctx); }
GUEST_FN(clobbers_xmm6)         { ctx->xmm[6].q[1] ^= 1; GUEST_RET(ctx); }
GUEST_FN(sets_df)               { ctx->df = 1; GUEST_RET(ctx); }
GUEST_FN(does_not_pop)          { (void)ctx; }
GUEST_FN(pops_twice)            { ctx->r[4] += 16; }
GUEST_FN(stays_in_host_mode)    { tf_fp_enter_host(); GUEST_RET(ctx); }
GUEST_FN(changes_mxcsr)         { ctx->mxcsr ^= 0x8000; tf_fp_enter_guest(ctx->mxcsr); GUEST_RET(ctx); }
GUEST_FN(writes_callers_frame)  { ST64(ctx->r[4] + 0x28, 0); GUEST_RET(ctx); }     /* just above the home space */

static uint32_t violations_of(uint64_t target, const char *expect_text)
{
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, 0, NULL, &why);
    if (expect_text && !strstr(why, expect_text))
        rt_test_fail(__FILE__, __LINE__, "the violation text \"%s\" does not mention \"%s\"", why, expect_text);
    return r.violations;
}

TEST(core, harness_detects_abi_violations)
{
    T_EQ(violations_of(T_GUEST_ADDR(well_behaved), NULL), 0);
    T_EQ(violations_of(T_GUEST_ADDR(clobbers_rbx), "rbx"), RT_TV_NONVOLATILE);
    T_EQ(violations_of(T_GUEST_ADDR(clobbers_r15), "r15"), RT_TV_NONVOLATILE);
    T_EQ(violations_of(T_GUEST_ADDR(clobbers_xmm6), "xmm6"), RT_TV_XMM);
    T_EQ(violations_of(T_GUEST_ADDR(sets_df), "direction flag"), RT_TV_DF);
    T_EQ(violations_of(T_GUEST_ADDR(does_not_pop), "rsp advanced by 0"), RT_TV_RSP);
    T_EQ(violations_of(T_GUEST_ADDR(pops_twice), "rsp advanced by 16"), RT_TV_RSP);
    T_EQ(violations_of(T_GUEST_ADDR(stays_in_host_mode), "FPCR"), RT_TV_FPCR);
    T_EQ(violations_of(T_GUEST_ADDR(changes_mxcsr), "mxcsr"), RT_TV_CTX);
    T_EQ(violations_of(T_GUEST_ADDR(writes_callers_frame), "caller's stack"), RT_TV_STACK);
    /* The Ctx is put back after every call, whatever the callee did to it. */
    T_EQ(violations_of(T_GUEST_ADDR(well_behaved), NULL), 0);
    T_EQ(T_CTX()->df, 0);
}

/* ---- what a callee sees ------------------------------------------------------------------------ */

static Ctx      seen;
static uint64_t seen_home[4], seen_stack_arg[2], seen_return;

GUEST_FN(records_what_it_sees)
{
    seen = *ctx;
    for (int i = 0; i < 4; i++) seen_home[i] = LD64(ctx->r[4] + 8 + 8 * (uint64_t)i);
    for (int i = 0; i < 2; i++) seen_stack_arg[i] = LD64(ctx->r[4] + 0x28 + 8 * (uint64_t)i);
    seen_return = LD64(ctx->r[4]);
    GUEST_RET(ctx);
}

TEST(core, harness_plants_garbage)
{
    /* (DWORD 5, float 1.5, WORD 7, BYTE 9, LONG -2, double 2.5) */
    const RtTestArg args[6] = {
        { RT_AC_U32, 4, { .u = 5 } }, { RT_AC_F32, 4, { .f = 1.5f } }, { RT_AC_U16, 2, { .u = 7 } },
        { RT_AC_U8, 1, { .u = 9 } }, { RT_AC_I32, 4, { .u = (uint64_t)(int64_t)-2 } }, { RT_AC_F64, 8, { .d = 2.5 } },
    };
    RtTestRet r = rt_test_invoke(T_GUEST_ADDR(records_what_it_sees), 6, args, NULL);
    T_EQ(r.violations, 0);

    /* The declared part of each argument is right and the rest of its 8 bytes is garbage. */
    T_EQ((uint32_t)seen.r[1], 5);           T_TRUE(seen.r[1] >> 32 != 0);
    T_EQ(seen.xmm[1].f[0], 1.5f);           T_TRUE(seen.xmm[1].d[1] != 0 && seen.xmm[1].q[1] != 0);
    T_EQ((uint16_t)seen.r[8], 7);           T_TRUE(seen.r[8] >> 16 != 0);
    T_EQ((uint8_t)seen.r[9], 9);            T_TRUE(seen.r[9] >> 8 != 0);
    T_EQ((int32_t)seen_stack_arg[0], -2);   T_TRUE(seen_stack_arg[0] >> 32 != 0xFFFFFFFF);
    double d; memcpy(&d, &seen_stack_arg[1], 8);
    T_EQ(d, 2.5);
    /* A float argument is not in the integer register of its position, and an integer
     * argument is not in the xmm register of its position: both hold garbage. */
    T_TRUE((uint32_t)seen.r[2] != 0 && seen.r[2] >> 63);
    T_TRUE(seen.xmm[0].q[0] >> 63 && seen.xmm[2].q[0] >> 63 && seen.xmm[3].q[0] >> 63);
    /* Volatile registers that carry no argument, and the home space, are garbage too. */
    T_TRUE(seen.r[0] >> 63 && seen.r[10] >> 63 && seen.r[11] >> 63);
    T_TRUE(seen.xmm[4].q[0] >> 63 && seen.xmm[5].q[1] >> 63);
    for (int i = 0; i < 4; i++) T_TRUE(seen_home[i] >> 63);
    /* The frame is the Windows one: return address on top, rsp = 8 mod 16, guest float mode. */
    T_EQ(seen.r[4] & 15, 8);
    char where[64];
    T_TRUE(!strcmp(rt_module_describe(seen_return, where, sizeof where), "rttest.dll!rt_test_return"));
    T_EQ(seen.df, 0);
    T_TRUE(seen.r[4] > RT_THREAD(T_CTX())->stack_limit && seen.r[4] < RT_THREAD(T_CTX())->stack_base);
}

#include <stdlib.h>

/* T_PLAN starts a numbered case and runs its block; RT_TEST_PLAN_CASE selects one block. */
TEST(core, harness_plan_cases)
{
    int ran = 0;
    T_PLAN(1, "first") { ran |= 1; T_TRUE(1); }
    T_PLAN(2, "second") { ran |= 2; T_TRUE(1); }
    T_EQ(ran, 3);
    setenv("RT_TEST_PLAN_CASE", "2", 1);
    ran = 0;
    T_PLAN(1, "first again") ran |= 1;
    T_PLAN(2, "second again") { ran |= 2; T_TRUE(1); }
    T_EQ(ran, 2);
    unsetenv("RT_TEST_PLAN_CASE");
}

/* ---- the runner itself ---------------------------------------------------------------------------
 * The runner's own rules (the time limit, what it does with a test's process group) are
 * tested by running the runner again, as a child process, on a FIXTURE: a test below that
 * misbehaves on purpose when RT_TEST_FIXTURE names it and otherwise makes one check and
 * returns, so that it passes in an ordinary run. */
#include <errno.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "rt/test_child.h"

static bool fixture(const char *name) {
    const char *f = getenv("RT_TEST_FIXTURE");
    if (f && !strcmp(f, name)) return true;
    T_TRUE(1);
    return false;
}

typedef struct { const char *fixture, *test, *timeout, *file, *tmpdir, *skip; } RunnerArgs;

/* In a forked child: become `rt-test --report <test>` with the fixture's environment. */
static void exec_runner(void *arg) {
    const RunnerArgs *a = arg;
    char exe[4096];
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) != 0 || dup2(2, 1) < 0) _exit(96);
    setenv("RT_TEST_FIXTURE", a->fixture, 1);
    if (a->timeout) setenv("RT_TEST_TIMEOUT", a->timeout, 1); else unsetenv("RT_TEST_TIMEOUT");
    if (a->file) setenv("RT_TEST_FIXTURE_FILE", a->file, 1);
    if (a->tmpdir) setenv("TMPDIR", a->tmpdir, 1);
    if (a->skip) setenv("RT_TEST_SKIP", a->skip, 1); else unsetenv("RT_TEST_SKIP");
    unsetenv("RT_TEST_PLAN_CASE");
    execl(exe, exe, "--report", a->test, (char *)NULL);
    _exit(95);
}

/* The fixture's forked process writes its pid to RT_TEST_FIXTURE_FILE and sleeps for ever. */
static void leave_a_process_behind(void) {
    pid_t pid = fork();
    if (pid != 0) return;
    FILE *f = fopen(getenv("RT_TEST_FIXTURE_FILE"), "w");
    if (f) { fprintf(f, "%d\n", (int)getpid()); fclose(f); }
    for (;;) pause();
}

static int pid_in_file(const char *path) {
    int pid = 0;
    FILE *f = fopen(path, "r");
    if (f) { if (fscanf(f, "%d", &pid) != 1) pid = 0; fclose(f); }
    return pid;
}

/* Wait until the process whose pid the fixture wrote is gone: it was killed before the
 * runner returned, and whoever inherited it has to reap it.  False if it lives on. */
static bool process_in_file_is_gone(const char *path) {
    int pid = pid_in_file(path);
    if (pid <= 1) return false;
    for (int tries = 0; tries < 30000; tries++, usleep(1000))
        if (kill((pid_t)pid, 0) < 0 && errno == ESRCH) return true;
    kill((pid_t)pid, SIGKILL);
    return false;
}

#define FIXTURE_HUNG "a line the fixture logged before it hung"

TEST(core, harness_fixture_hangs)
{
    if (!fixture("hangs")) return;
    T_CASE("the case that never ends");
    T_TRUE(1);
    leave_a_process_behind();
    while (pid_in_file(getenv("RT_TEST_FIXTURE_FILE")) <= 1) usleep(1000);
    rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, FIXTURE_HUNG);
    for (;;) pause();
}

TEST(core, harness_fixture_leaves_a_process)
{
    if (!fixture("leaves_a_process")) return;
    leave_a_process_behind();
    while (pid_in_file(getenv("RT_TEST_FIXTURE_FILE")) <= 1) usleep(1000);
    T_TRUE(1);                                          /* and the test passes */
}

static void temp_file(char *path, size_t cap) {
    const char *tmp = getenv("TMPDIR");
    snprintf(path, cap, "%s/rt-fixture.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    unlink(path);
}

/* A test that does not end is killed at the limit, with everything it started, and reported
 * as failed with the limit, its last log lines and the case it was in.  Nothing here depends
 * on how fast the machine is: a limit that passes before the fixture has reached the place
 * where it hangs is tried again four times as long. */
TEST(core, harness_time_limit)
{
    static RtTestChild c;
    static const char *const limits[] = { "1", "4", "16", "64" };
    char file[1100], expect[128];
    temp_file(file, sizeof file);
    RunnerArgs a = { "hangs", "core/harness_fixture_hangs", NULL, file, NULL, NULL };
    bool hung = false;
    for (unsigned i = 0; i < 4 && !hung; i++) {
        unsigned secs = (unsigned)atoi(limits[i]);
        a.timeout = limits[i];
        unlink(file);
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        rt_test_child_run(exec_runner, &a, &c);
        T_TRUE(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0 >= UINT64_C(1000000000) * secs);    /* not before the limit */
        T_TRUE(T_CHILD_EXITED(&c, 1));
        snprintf(expect, sizeof expect, "FAIL  core/harness_fixture_hangs   (timed out after %u s", secs);
        T_TRUE(T_CHILD_SAID(&c, expect));
        snprintf(expect, sizeof expect, "had not ended after %u s and was killed", secs);
        T_TRUE(T_CHILD_SAID(&c, expect));
        T_TRUE(T_CHILD_SAID(&c, "1 tests, 0 passed, 1 failed"));
        T_TRUE(!T_CHILD_SAID(&c, "#rt result"));        /* it never reported a result */
        hung = T_CHILD_SAID(&c, "guest.debug: " FIXTURE_HUNG);
    }
    T_REQUIRE(hung);
    T_TRUE(T_CHILD_SAID(&c, "it was in the case \"the case that never ends\""));
    T_TRUE(process_in_file_is_gone(file));              /* the process it forked went with it */
    unlink(file);

    /* The limit is a number of seconds; anything else is a usage error, not a default. */
    a = (RunnerArgs){ "none", "core/harness_fixture_hangs", "soon", NULL, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 2));
    T_TRUE(T_CHILD_SAID(&c, "RT_TEST_TIMEOUT=soon"));
    a.timeout = "0";
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 2));
}

/* A test that passes but leaves a process running: the process is killed when the test is
 * over, so it cannot hold the runner's output open (which is how one hung T_TRAPS child
 * once stalled a check for 67 minutes). */
TEST(core, harness_kills_what_a_test_leaves_behind)
{
    static RtTestChild c;
    char file[1100];
    temp_file(file, sizeof file);
    RunnerArgs a = { "leaves_a_process", "core/harness_fixture_leaves_a_process", NULL, file, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "ok    core/harness_fixture_leaves_a_process"));
    T_TRUE(process_in_file_is_gone(file));
    unlink(file);
}

/* ---- checks from several threads, the log capture, the temporary root (review R25) ------------- */
#include <pthread.h>

#define CHECK_THREADS      8
#define CHECKS_PER_THREAD  100000
#define FAILS_PER_THREAD   3

static void *checks_on_a_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < CHECKS_PER_THREAD; i++) T_TRUE(i >= 0);
    for (int i = 0; i < FAILS_PER_THREAD; i++) T_EQ(i, -1);
    return NULL;
}

TEST(core, harness_fixture_checks_on_threads)
{
    if (!fixture("checks_on_threads")) return;
    pthread_t th[CHECK_THREADS];
    T_CASE("the checks of eight threads");
    for (int i = 0; i < CHECK_THREADS; i++) pthread_create(&th[i], NULL, checks_on_a_thread, NULL);
    for (int i = 0; i < CHECK_THREADS; i++) pthread_join(th[i], NULL);
}

/* No check and no failure is lost when the threads of a test check at the same time: the
 * totals of the report are exact. */
TEST(core, harness_counts_checks_of_all_threads)
{
    static RtTestChild c;
    char expect[128];
    unsigned checks = CHECK_THREADS * (CHECKS_PER_THREAD + FAILS_PER_THREAD), fails = CHECK_THREADS * FAILS_PER_THREAD;
    RunnerArgs a = { "checks_on_threads", "core/harness_fixture_checks_on_threads", NULL, NULL, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 1));
    snprintf(expect, sizeof expect, "#rt case %u %u the checks of eight threads\n", checks, fails);
    T_TRUE(T_CHILD_SAID(&c, expect));
    snprintf(expect, sizeof expect, "#rt result fail %u %u\n", checks, fails);
    T_TRUE(T_CHILD_SAID(&c, expect));
    T_EQ(rt_test_count(c.out, "T_EQ(i, -1) failed: left = "), fails);       /* whole lines, none inside another */
}

#define MANY_LINES 5000         /* more than the harness keeps (4,096) */

TEST(core, harness_fixture_logs_many_lines)
{
    const char *f = getenv("RT_TEST_FIXTURE");
    bool ask_late = f && !strcmp(f, "logs_many_lines_and_asks");
    if (!ask_late && !fixture("logs_many_lines")) return;
    for (int i = 0; i < MANY_LINES; i++) rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, "line %d.", i);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), MANY_LINES);   /* the count is of every line */
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "line 17."));    /* "yes" is always right */
    if (ask_late) (void)T_LOGGED(RT_CH_GUEST_DEBUG, "line 4999.");  /* logged, but not kept */
}

/* A test may log more lines than the harness keeps, and T_LOG_COUNT still counts them all;
 * but T_LOGGED never answers "not logged" about lines it did not keep: that fails the test. */
TEST(core, harness_log_capture_overflow)
{
    static RtTestChild c;
    RunnerArgs a = { "logs_many_lines", "core/harness_fixture_logs_many_lines", NULL, NULL, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "#rt result pass 2 0"));
    a.fixture = "logs_many_lines_and_asks";
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 1));
    T_TRUE(T_CHILD_SAID(&c, "T_LOGGED(guest.debug, \"line 4999.\") cannot be answered: the test has logged 5000 lines"));
    T_TRUE(T_CHILD_SAID(&c, "#rt result fail 3 1"));
}

TEST(core, harness_fixture_prints_its_root)
{
    if (!fixture("prints_its_root")) return;
    fprintf(stderr, "state directory: [%s]\n", rt_test_state_dir());
    T_TRUE(1);
}

/* The temporary root of a test has no doubled slash when TMPDIR ends in one (it does on macOS). */
TEST(core, harness_root_has_no_double_slash)
{
    static RtTestChild c;
    char tmpdir[1100];
    const char *tmp = getenv("TMPDIR");
    snprintf(tmpdir, sizeof tmpdir, "%s", tmp && *tmp ? tmp : "/tmp");
    size_t n = strlen(tmpdir);
    while (n > 1 && tmpdir[n - 1] == '/') tmpdir[--n] = 0;
    snprintf(tmpdir + n, sizeof tmpdir - n, "/");                   /* exactly one trailing slash */
    RunnerArgs a = { "prints_its_root", "core/harness_fixture_prints_its_root", NULL, NULL, tmpdir, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    const char *line = strstr(c.out, "state directory: [");
    T_REQUIRE(line != NULL);
    const char *end = strchr(line, ']');
    T_REQUIRE(end != NULL);
    T_TRUE(strstr(line, "/rt-test.") != NULL && strstr(line, "/rt-test.") < end);
    const char *dbl = strstr(line, "//");
    T_TRUE(dbl == NULL || dbl > end);
}

/* ---- a fork while other threads hold locks of the runtime ----------------------------------------
 * rt_test_forked, the first thing T_TRAPS and rt_test_child_run do in the child, puts the
 * log's mutex and the dispatcher lock back (rt/test_fork.h).  The fixture forks while one
 * thread is stopped inside the harness's log sink (the log's mutex held: the sink shows the
 * line on descriptor 2, which the fixture has made a pipe that is full) and another holds
 * the dispatcher lock.  The child takes both locks, logs and ends in a fatal report. */
#include <fcntl.h>
#include <stdatomic.h>

#include "rt/handle.h"

static atomic_int logger_done, holder_in, holder_out;

static void *logs_into_a_full_pipe(void *arg) {
    (void)arg;
    rt_log(RT_CH_GUEST_DEBUG, RT_LL_ERROR, "a line that cannot be shown yet");
    atomic_store(&logger_done, 1);
    return NULL;
}

static void *holds_the_dispatcher_lock(void *arg) {
    (void)arg;
    rt_dispatcher_lock();
    atomic_store(&holder_in, 1);
    while (!atomic_load(&holder_out)) usleep(1000);
    rt_dispatcher_unlock();
    return NULL;
}

static void takes_both_locks_and_dies(void *arg) {
    (void)arg;
    rt_dispatcher_lock();
    rt_dispatcher_unlock();
    rt_log(RT_CH_GUEST_DEBUG, RT_LL_ERROR, "the child logged a line");
    rt_fatal("the child's own fatal report");
}

TEST(core, harness_fixture_forks_under_held_locks)
{
    if (!fixture("forks_under_held_locks")) return;
    static RtTestChild c;
    static char block[4096];
    int p[2] = { -1, -1 }, saved = dup(2);
    T_REQUIRE(saved >= 0 && pipe(p) == 0);
    fcntl(p[1], F_SETFL, O_NONBLOCK);                   /* fill the pipe to the last byte */
    while (write(p[1], block, sizeof block) > 0) { }
    while (write(p[1], block, 1) > 0) { }
    fcntl(p[1], F_SETFL, 0);
    dup2(p[1], 2);

    pthread_t logger, holder;
    unsigned before = rt_test_log_count(RT_CH_GUEST_DEBUG);
    pthread_create(&logger, NULL, logs_into_a_full_pipe, NULL);
    pthread_create(&holder, NULL, holds_the_dispatcher_lock, NULL);
    /* The sink counts the line with the mutex held, before it shows it. */
    while (rt_test_log_count(RT_CH_GUEST_DEBUG) == before || !atomic_load(&holder_in)) usleep(1000);
    usleep(50000);                                      /* from the count to the write that blocks: a few lines */

    rt_test_child_run(takes_both_locks_and_dies, NULL, &c);
    bool held = !atomic_load(&logger_done);             /* the logger never left the sink: it held the mutex at the fork */

    fcntl(p[0], F_SETFL, O_NONBLOCK);                   /* let both threads go, then look at the child */
    while (!atomic_load(&logger_done))
        if (read(p[0], block, sizeof block) <= 0) usleep(1000);
    atomic_store(&holder_out, 1);
    pthread_join(logger, NULL);
    pthread_join(holder, NULL);
    dup2(saved, 2);
    close(saved); close(p[0]); close(p[1]);

    T_TRUE(held);
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(T_CHILD_SAID(&c, "the child logged a line"));
    T_TRUE(T_CHILD_SAID(&c, "the child's own fatal report"));
}

/* Without rt_test_forked's two calls the fixture's child waits for ever and the fixture is
 * killed at its limit.  The limit is no measure of speed: the fixture forks once. */
TEST(core, harness_fork_while_locks_are_held)
{
    static RtTestChild c;
    RunnerArgs a = { "forks_under_held_locks", "core/harness_fixture_forks_under_held_locks", "60", NULL, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    if (!T_CHILD_EXITED(&c, 0)) fprintf(stderr, "%s", c.out);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "1 tests, 1 passed, 0 failed"));
    T_TRUE(!T_CHILD_SAID(&c, "timed out"));
}

/* ---- the skip list --------------------------------------------------------------------------------
 * RT_TEST_SKIP names a file of tests not to run, each with a reason.  The fixture fails when
 * it runs, so "skipped" is seen to mean "not run". */
TEST(core, harness_fixture_fails)
{
    if (!fixture("fails")) return;
    T_TRUE(0);
}

TEST(core, harness_skip_list)
{
    static RtTestChild c;
    char file[1100];
    temp_file(file, sizeof file);
    RunnerArgs a = { "fails", "core/harness_fixture_fails", NULL, NULL, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);             /* not listed: it runs, and fails */
    T_TRUE(T_CHILD_EXITED(&c, 1));
    T_TRUE(T_CHILD_SAID(&c, "1 tests, 0 passed, 1 failed"));

    FILE *f = fopen(file, "w");
    T_REQUIRE(f != NULL);
    fprintf(f, "# a comment\n\ncore/harness_fixture_fails   it fails on purpose   # and says why\n");
    fclose(f);
    a.skip = file;
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "skip  core/harness_fixture_fails   (it fails on purpose"));
    T_TRUE(T_CHILD_SAID(&c, "0 tests, 0 passed, 0 failed, 1 skipped"));

    f = fopen(file, "w");                               /* a reason is required */
    T_REQUIRE(f != NULL);
    fprintf(f, "core/harness_fixture_fails\n");
    fclose(f);
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 2));
    T_TRUE(T_CHILD_SAID(&c, "has no reason"));
    unlink(file);

    a.skip = "/nonexistent/skip-list";                  /* and so is the file */
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 2));
}

/* ---- call counts cross a fork -------------------------------------------------------------------
 * The report's `#rt calls` lines count the CALLs a test made in the children it forked too
 * (shared memory, runtime/tests/harness.c): the fixture calls a shim once itself, once in
 * the child of a T_TRAPS and once in the child of rt_test_child_run. */
static void calls_then_dies(void *arg) {
    (void)arg;
    (void)CALL(rttest, RetFloat, 1.5f, -3);
    rt_fatal("after the call");
}

TEST(core, harness_fixture_calls_in_children)
{
    if (!fixture("calls_in_children")) return;
    static RtTestChild c;
    T_EQ(CALL(rttest, RetFloat, 1.5f, -3), -4.5f);
    T_TRAPS(calls_then_dies(NULL), "after the call");
    rt_test_child_run(calls_then_dies, NULL, &c);
    T_TRUE(T_CHILD_ABORTED(&c));
}

TEST(core, harness_counts_calls_made_in_children)
{
    static RtTestChild c;
    RunnerArgs a = { "calls_in_children", "core/harness_fixture_calls_in_children", NULL, NULL, NULL, NULL };
    rt_test_child_run(exec_runner, &a, &c);
    T_TRUE(T_CHILD_EXITED(&c, 0));
    T_TRUE(T_CHILD_SAID(&c, "#rt calls rttest/RetFloat 3\n"));
}
