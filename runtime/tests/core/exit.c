/* Process exit: rt_process_exit, rt_process_terminate, rt_process_exit_code and
 * rt_fatal_app_exit (rt/crash.h, section "process exit"; core/exit.c). */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/crash.h"
#include "rt/../../core/report.h"
#include "../../core/thread_priv.h"

#define DETACH_LINE "loader: ExitProcess: DLL_PROCESS_DETACH is not delivered (RUNTIME.md 11.1)\n"

/* The line of step 3 for the main guest thread, as the child writes it. */
static void exit_line(char *buf, size_t n, const char *what, UINT code) {
    RtThread *t = RT_THREAD(T_CTX());
    snprintf(buf, n, "thread: process exit: %s(0x%08x) on thread 0x%x \"%s\"\n",
             what, (unsigned)code, (unsigned)t->id, t->name);
}

/* ---- what the children do ----------------------------------------------------------------------- */

static void child_exit_code(void *arg) {
    rt_process_exit(T_CTX(), *(UINT *)arg);
}

static void child_warn_thread(void *arg) {
    rt_log_level[RT_CH_THREAD] = RT_LL_WARN;
    rt_process_exit(T_CTX(), *(UINT *)arg);
}

static void child_warn_loader(void *arg) {
    (void)arg;
    rt_log_level[RT_CH_LOADER] = RT_LL_WARN;
    rt_process_exit(T_CTX(), 0);
}

static void child_terminate3(void *arg) {
    (void)arg;
    rt_process_terminate(T_CTX(), (HANDLE)-1, 3);
}

static void *host_exit5(void *arg) {
    (void)arg;
    rt_process_exit(NULL, 5);
}

static void child_host_thread(void *arg) {
    (void)arg;
    pthread_t th;
    pthread_create(&th, NULL, host_exit5, NULL);
    for (;;) pause();                                   /* the host thread ends the process */
}

static void child_exit6_null(void *arg) {
    (void)arg;
    rt_process_exit(NULL, 6);
}

/* ---- the tests ------------------------------------------------------------------------------------ */

TEST(core, exit)
{
    RtTestChild c;
    UINT code;
    char line[256];

    T_PLAN(1, "exit with 0") {
        code = 0;
        rt_test_child_run(child_exit_code, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 0));
        exit_line(line, sizeof line, "ExitProcess", 0);
        T_TRUE(T_CHILD_SAID(&c, line));
        T_TRUE(T_CHILD_SAID(&c, DETACH_LINE));
        T_TRUE(strstr(c.out, "FATAL") == NULL);
    }

    T_PLAN(2, "exit codes 7 and 254") {
        code = 7;
        rt_test_child_run(child_exit_code, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 7));
        exit_line(line, sizeof line, "ExitProcess", 7);
        T_TRUE(T_CHILD_SAID(&c, line));
        code = 254;
        rt_test_child_run(child_exit_code, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 254));
    }

    T_PLAN(3, "exit codes 255, 256 and 0xC0000409") {
        code = 255;
        rt_test_child_run(child_exit_code, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 255));
        code = 256;
        rt_test_child_run(child_exit_code, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 255));
        T_TRUE(!T_CHILD_EXITED(&c, 0));
        code = 0xC0000409;
        rt_test_child_run(child_exit_code, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 255));
        exit_line(line, sizeof line, "ExitProcess", 0xC0000409);
        T_TRUE(T_CHILD_SAID(&c, line));
    }

    T_PLAN(4, "levels hide INFO lines of a quiet channel") {
        code = 0;
        rt_test_child_run(child_warn_thread, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 0));
        T_TRUE(strstr(c.out, "process exit:") == NULL);
        code = 7;
        rt_test_child_run(child_warn_thread, &code, &c);
        T_TRUE(T_CHILD_EXITED(&c, 7));
        exit_line(line, sizeof line, "ExitProcess", 7);
        T_TRUE(T_CHILD_SAID(&c, line));
        rt_test_child_run(child_warn_loader, NULL, &c);
        T_TRUE(T_CHILD_EXITED(&c, 0));
        T_TRUE(strstr(c.out, DETACH_LINE) == NULL);
    }

    T_PLAN(5, "TerminateProcess logs its own line, no detach") {
        rt_test_child_run(child_terminate3, NULL, &c);
        T_TRUE(T_CHILD_EXITED(&c, 3));
        exit_line(line, sizeof line, "TerminateProcess", 3);
        T_TRUE(T_CHILD_SAID(&c, line));
        T_TRUE(strstr(c.out, "DLL_PROCESS_DETACH") == NULL);
    }

    T_PLAN(6, "ExitProcess from a host thread") {
        rt_test_child_run(child_host_thread, NULL, &c);
        T_TRUE(T_CHILD_EXITED(&c, 5));
        T_TRUE(T_CHILD_SAID(&c, "process exit: ExitProcess(0x00000005) on a host thread\n"));
    }

    T_PLAN(7, "NULL ctx names the calling thread") {
        rt_test_child_run(child_exit6_null, NULL, &c);
        T_TRUE(T_CHILD_EXITED(&c, 6));
        exit_line(line, sizeof line, "ExitProcess", 6);
        T_TRUE(T_CHILD_SAID(&c, line));
    }
}

/* The handle cases run in the test process: they end with a refusal, not with an exit. */
TEST(core, exit_handles)
{
    T_PLAN(8, "terminate refuses a bad handle, does not exit") {
        rt_thread_set_last_error(0);
        T_TRUE(!rt_process_terminate(T_CTX(), NULL, 1));
        T_EQ(rt_thread_get_last_error(), ERROR_INVALID_HANDLE);
        rt_thread_set_last_error(0);
        T_TRUE(!rt_process_terminate(T_CTX(), (HANDLE)(uintptr_t)0x12344, 1));
        T_EQ(rt_thread_get_last_error(), ERROR_INVALID_HANDLE);
        HANDLE ev = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
        T_REQUIRE(ev != NULL);
        rt_thread_set_last_error(0);
        T_TRUE(!rt_process_terminate(T_CTX(), ev, 1));
        T_EQ(rt_thread_get_last_error(), ERROR_INVALID_HANDLE);
    }

    T_PLAN(9, "exit code of the process, refusals keep the code") {
        DWORD code = 0x55;
        T_TRUE(rt_process_exit_code(T_CTX(), (HANDLE)-1, &code));
        T_EQ(code, 259);
        code = 0x55;
        rt_thread_set_last_error(0);
        T_TRUE(!rt_process_exit_code(T_CTX(), NULL, &code));
        T_EQ(rt_thread_get_last_error(), ERROR_INVALID_HANDLE);
        T_EQ(code, 0x55);
        code = 0x55;
        rt_thread_set_last_error(0);
        T_TRUE(!rt_process_exit_code(T_CTX(), (HANDLE)(uintptr_t)0x12344, &code));
        T_EQ(rt_thread_get_last_error(), ERROR_INVALID_HANDLE);
        T_EQ(code, 0x55);
        HANDLE ev = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
        T_REQUIRE(ev != NULL);
        code = 0x55;
        rt_thread_set_last_error(0);
        T_TRUE(!rt_process_exit_code(T_CTX(), ev, &code));
        T_EQ(rt_thread_get_last_error(), ERROR_INVALID_HANDLE);
        T_EQ(code, 0x55);
    }

    T_PLAN(10, "a NULL exit code pointer traps") {
        T_TRAPS(rt_process_exit_code(T_CTX(), (HANDLE)-1, NULL), "GetExitCodeProcess with lpExitCode NULL");
    }
}

/* ---- one exit, and the abort wins: children ----------------------------------------------------- */

static atomic_int go;
static void *racer(void *arg) {
    (void)arg;
    while (!atomic_load(&go)) { }
    rt_process_exit(NULL, 11);
}
static void child_one_exit(void *arg) {
    (void)arg;
    pthread_t th;
    pthread_create(&th, NULL, racer, NULL);
    atomic_store(&go, 1);
    while (!atomic_load(&go)) { }                       /* both threads spin on the flag */
    rt_process_terminate(T_CTX(), (HANDLE)-1, 11);
}

static void *host_exit0(void *arg) {
    (void)arg;
    rt_process_exit(NULL, 0);
}
static void child_abort_wins(void *arg) {
    (void)arg;
    pthread_t th;
    rt_report_begin();
    pthread_create(&th, NULL, host_exit0, NULL);
    usleep(300 * 1000);
    rt_log_raw("FATAL test report\n", 18);
    rt_report_abort();
}

/* ---- the tests of the exit race ----------------------------------------------------------------- */

TEST(core, exit_race)
{
    RtTestChild c;

    T_PLAN(11, "one exit wins a race, twenty rounds") {
        for (int round = 0; round < 20; round++) {
            rt_test_child_run(child_one_exit, NULL, &c);
            T_TRUE(T_CHILD_EXITED(&c, 11));
            T_EQ(rt_test_count(c.out, "process exit:"), 1);
        }
    }

    T_PLAN(12, "an abort in progress beats an exit") {
        rt_test_child_run(child_abort_wins, NULL, &c);
        T_TRUE(T_CHILD_ABORTED(&c));
        T_TRUE(strstr(c.out, "process exit:") == NULL);
    }
}

/* ---- the log is flushed: child ------------------------------------------------------------------- */

static void buffered_then_exit(void *arg) {
    setenv("RT_LOG_FILE", (const char *)arg, 1);
    rt_log_init();
    rt_log(RT_CH_VM, RT_LL_ERROR, "last words");
    rt_process_exit(T_CTX(), 0);
}

TEST(core, exit_log)
{
    RtTestChild c;

    T_PLAN(13, "the log is flushed before the exit") {
        char path[1200];
        snprintf(path, sizeof path, "%s/log.txt", rt_test_state_dir());
        rt_test_child_run(buffered_then_exit, path, &c);
        T_TRUE(T_CHILD_EXITED(&c, 0));
        char text[4096];
        int fd = open(path, O_RDONLY);
        T_REQUIRE(fd >= 0);
        ssize_t n = read(fd, text, sizeof text - 1);
        close(fd);
        unlink(path);
        text[n > 0 ? n : 0] = 0;
        T_TRUE(strstr(text, "vm: last words\n") != NULL);
    }
}

/* ---- rt_fatal_app_exit: children ----------------------------------------------------------------- */

static char long_msg[1501];

static void child_fae_disk(void *arg) {
    (void)arg;
    rt_fatal_app_exit(T_CTX(), 0, "disk on fire");
}
static void child_fae_null(void *arg) {
    (void)arg;
    rt_fatal_app_exit(T_CTX(), 0, NULL);
}
static void child_fae_action(void *arg) {
    (void)arg;
    rt_fatal_app_exit(T_CTX(), 0x10, "disk on fire");
}
static void child_fae_long(void *arg) {
    (void)arg;
    rt_fatal_app_exit(T_CTX(), 0, long_msg);
}
static void child_fae_nothread(void *arg) {
    (void)arg;
    rt_fatal_app_exit(NULL, 0, "x");
}

static bool starts_with(const char *hay, const char *pre) {
    return strncmp(hay, pre, strlen(pre)) == 0;
}

/* ---- the tests of rt_fatal_app_exit -------------------------------------------------------------- */

TEST(core, exit_fatal)
{
    RtTestChild c;

    T_PLAN(14, "FatalAppExit: the report, then exit 3") {
        rt_test_child_run(child_fae_disk, NULL, &c);
        T_TRUE(T_CHILD_EXITED(&c, 3));
        T_TRUE(starts_with(c.out, "FATAL guest called FatalAppExit: disk on fire\n  uAction 0x0\n"));
        T_TRUE(T_CHILD_SAID(&c, "thread 0x"));
        T_EQ(rt_test_count(c.out, "FATAL"), 1);
    }

    T_PLAN(15, "a NULL message prints (null); action 0x10 is printed") {
        rt_test_child_run(child_fae_null, NULL, &c);
        T_TRUE(starts_with(c.out, "FATAL guest called FatalAppExit: (null)\n"));
        rt_test_child_run(child_fae_action, NULL, &c);
        const char *nl = strchr(c.out, '\n');
        T_TRUE(nl != NULL && strncmp(nl + 1, "  uAction 0x10\n", strlen("  uAction 0x10\n")) == 0);
    }

    T_PLAN(16, "a 1,500-byte message is cut at 1,000 bytes") {
        memset(long_msg, 'm', 1500);
        long_msg[1500] = 0;
        rt_test_child_run(child_fae_long, NULL, &c);
        const char *nl = strchr(c.out, '\n');
        T_TRUE(nl != NULL && (size_t)(nl - c.out) == strlen("FATAL guest called FatalAppExit: ") + 1000);
    }

    T_PLAN(17, "no guest thread: the report says so, exit 3") {
        rt_test_child_run(child_fae_nothread, NULL, &c);
        T_TRUE(T_CHILD_EXITED(&c, 3));
        T_TRUE(T_CHILD_SAID(&c, "  (no guest thread)\n"));
    }
}
