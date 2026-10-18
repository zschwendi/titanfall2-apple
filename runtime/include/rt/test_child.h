/* For tests of code that ends the process (a fatal report, a host signal, ExitProcess): run a
 * function in a forked child and look at how the child ended and at what it wrote to file
 * descriptor 2.  T_TRAPS (rt/test.h) covers the common case, "aborts with a report that
 * contains this text"; this header is for everything T_TRAPS cannot say: an exit status,
 * several texts in one report, a text that must NOT be there, counting reports.
 *
 *     static void do_exit(void *arg) { rt_process_exit(T_CTX(), *(UINT *)arg); }
 *     ...
 *     RtTestChild c;
 *     UINT code = 7;
 *     rt_test_child_run(do_exit, &code, &c);
 *     T_TRUE(T_CHILD_EXITED(&c, 7));
 *     T_TRUE(strstr(c.out, "process exit: ExitProcess(0x00000007)") != NULL);
 *
 * The child is a copy of the test process at the moment of the call: the runtime is
 * started, guest thread 1 is the calling thread, and only the calling host thread exists
 * (threads the test started before are not copied).  In the child the log sink of the
 * harness is removed, so rt_log lines go to file descriptor 2 with everything else.
 * Checks (T_TRUE, T_EQ) made inside the child are lost: decide in the parent, from
 * `status` and `out`; a child that wants to report a result of its own calls _exit(n)
 * (not 134, RT_REPORT_QUIET_STATUS: that is how a fatal report ends in the child, and not
 * 97 or 98).
 */
#ifndef RT_TEST_CHILD_H
#define RT_TEST_CHILD_H

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rt/crash.h"
#include "rt/test.h"

typedef struct {
    int    status;              /* the wait status, for WIFEXITED, WIFSIGNALED and so on */
    size_t len;                 /* bytes in out[] */
    char   out[16384];          /* what the child wrote to descriptor 2, NUL-terminated; cut at 16,383 bytes */
} RtTestChild;

#define RT_TEST_CHILD_RETURNED 97   /* the exit status of a child whose function returned */

/* Run fn(arg) in a forked child with descriptor 2 going to a temporary file, wait for the
 * child, and fill in *res.  If fn returns, the child exits with status 97. */
static inline __attribute__((unused)) void rt_test_child_run(void (*fn)(void *), void *arg, RtTestChild *res) {
    char path[1100];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/rt-child.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(2); }
    unlink(path);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        rt_test_forked();
        if (dup2(fd, 2) < 0) _exit(98);
        close(fd);
        rt_log_set_sink(NULL, NULL);
        fn(arg);
        _exit(RT_TEST_CHILD_RETURNED);
    }
    res->status = 0;
    while (waitpid(pid, &res->status, 0) < 0 && errno == EINTR) { }
    ssize_t n = pread(fd, res->out, sizeof res->out - 1, 0);
    res->len = n > 0 ? (size_t)n : 0;
    res->out[res->len] = 0;
    close(fd);
}

/* A wait status that says "ended by a fatal report" in a process a test forked.  There the
 * quiet end is on (rt_test_forked calls rt_report_quiet(RT_QUIET_ALL): rt/crash.h, THE QUIET
 * END), so the report's abort() ends in exit status 134 and macOS writes no crash report.
 * Death by the real SIGABRT is NOT taken for it: with the switch on nothing in the child
 * ends so, and a child that turned the switch off is asked about with WIFSIGNALED. */
#define RT_TEST_ABORTED(status)    (WIFEXITED(status) && WEXITSTATUS(status) == RT_REPORT_QUIET_STATUS)
/* The child ended in a fatal report. */
#define T_CHILD_ABORTED(res)       RT_TEST_ABORTED((res)->status)
/* The child ended with exit status `code` (0 to 255). */
#define T_CHILD_EXITED(res, code)  (WIFEXITED((res)->status) && WEXITSTATUS((res)->status) == (code))
/* The child's output contains `text`. */
#define T_CHILD_SAID(res, text)    (strstr((res)->out, (text)) != NULL)

/* How many times `needle` occurs in `hay` (occurrences do not overlap). */
static inline __attribute__((unused)) unsigned rt_test_count(const char *hay, const char *needle) {
    unsigned n = 0;
    size_t len = strlen(needle);
    if (!len) return 0;
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p += len) n++;
    return n;
}

#endif /* RT_TEST_CHILD_H */
