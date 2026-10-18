/* The quiet end of a fatal report (rt/crash.h, THE QUIET END; runtime/core/report.c): where
 * the switch is on, the report's abort() ends in exit status 134 and not in death by SIGABRT,
 * so that macOS writes no crash report for a stop that was expected; and what the switch
 * must NOT make quiet.  The expected values are the header's own words and POSIX wait
 * statuses, read in the parent with the plain macros of <sys/wait.h>.
 *
 * TWO CHILDREN OF THIS FILE DIE OF THE REAL SIGABRT ON PURPOSE (the cases marked LOUD): they
 * are the proof that a crash stays as loud as it was, and each costs one crash report of
 * macOS per run of the suite.  Do not add a third without a reason of that weight.
 */
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/crash.h"

__attribute__((noinline)) static void poke(uint64_t addr) { *(volatile uint32_t *)(uintptr_t)addr = 1; }

typedef struct { int set; RtReportQuiet q; bool fault; } How;

/* In the child: set the switch if asked (rt_test_forked has set RT_QUIET_ALL already), then
 * stop by rt_fatal or fault with a write to a null pointer. */
static void end_child(void *arg) {
    const How *h = arg;
    if (h->set) rt_report_quiet(h->q);
    if (h->fault) poke(0x60);
    rt_fatal("the stop of the quiet test");
}

/* A line in a stream of the C library that nobody flushed, then a stop. */
static void stop_with_a_full_buffer(void *arg) {
    (void)arg;
    FILE *f = fdopen(dup(2), "w");
    if (!f) _exit(90);
    setvbuf(f, NULL, _IOFBF, 4096);
    fputs("a line still in a stdio buffer\n", f);
    rt_fatal("the stop after the buffered line");
}

static bool exited_quietly(const RtTestChild *c) { return WIFEXITED(c->status) && WEXITSTATUS(c->status) == 134; }
static bool died_of_sigabrt(const RtTestChild *c) { return WIFSIGNALED(c->status) && WTERMSIG(c->status) == SIGABRT; }

TEST(core, report_quiet_end)
{
    static RtTestChild c;
    How h;

    T_CASE("in a child a test forked, a stop exits with status 134 and is not a death by signal");
    h = (How){ 0, RT_QUIET_OFF, false };
    rt_test_child_run(end_child, &h, &c);
    T_EQ(RT_REPORT_QUIET_STATUS, 128 + SIGABRT);
    T_TRUE(exited_quietly(&c) && !WIFSIGNALED(c.status));
    T_TRUE(T_CHILD_ABORTED(&c));
    T_TRUE(strstr(c.out, "FATAL runtime: the stop of the quiet test\n") == c.out);
    T_EQ(rt_test_count(c.out, "FATAL"), 1);
    T_TRAPS(rt_fatal("the same through T_TRAPS"), "FATAL runtime: the same through T_TRAPS");

    T_CASE("there the report of a host fault ends the same way: the fault is the test");
    h = (How){ 0, RT_QUIET_OFF, true };
    rt_test_child_run(end_child, &h, &c);
    T_TRUE(exited_quietly(&c));
    T_TRUE(strstr(c.out, "FATAL host signal SIGSEGV (11), code ") == c.out && T_CHILD_SAID(&c, "(offset 0x60)"));
    T_TRUE(T_CHILD_SAID(&c, "  host frames "));                 /* the report is whole: its last block is there */

    T_CASE("the quiet end is still abort(): what the C library had buffered is written");
    rt_test_child_run(stop_with_a_full_buffer, NULL, &c);
    T_TRUE(exited_quietly(&c));
    T_TRUE(T_CHILD_SAID(&c, "FATAL runtime: the stop after the buffered line\n"));
    T_TRUE(T_CHILD_SAID(&c, "a line still in a stdio buffer\n"));

    T_CASE("LOUD: with the switch off a stop dies of SIGABRT, as in every program that did not ask");
    h = (How){ 1, RT_QUIET_OFF, false };
    rt_test_child_run(end_child, &h, &c);
    T_TRUE(died_of_sigabrt(&c) && !T_CHILD_ABORTED(&c));
    T_TRUE(strstr(c.out, "FATAL runtime: the stop of the quiet test\n") == c.out);

    T_CASE("RT_QUIET_STOPS, a driver's setting: a stop is quiet");
    h = (How){ 1, RT_QUIET_STOPS, false };
    rt_test_child_run(end_child, &h, &c);
    T_TRUE(exited_quietly(&c));
    T_TRUE(strstr(c.out, "FATAL runtime: the stop of the quiet test\n") == c.out);

    T_CASE("LOUD: RT_QUIET_STOPS does not quieten a host fault: the report, then SIGABRT");
    h = (How){ 1, RT_QUIET_STOPS, true };
    rt_test_child_run(end_child, &h, &c);
    T_TRUE(died_of_sigabrt(&c));
    T_TRUE(strstr(c.out, "FATAL host signal SIGSEGV (11), code ") == c.out && T_CHILD_SAID(&c, "(offset 0x60)"));
    T_EQ(rt_test_count(c.out, "FATAL"), 1);
}
