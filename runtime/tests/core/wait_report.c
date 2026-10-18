/* THE WAIT REPORT (rt/sync.h, rt_wait_report_thread; rt/thread.h, rt_thread_report_waits):
 * what a run that does not end prints about every thread when the driver's time limit stops
 * it.  The expected lines are the ones the two headers specify; nothing here comes from a
 * run of the game.
 *
 *   - a thread queued on an object: the kind, "not signalled", how many are queued, and
 *     "never signalled" for an object nobody has signalled;
 *   - who signalled an object last, and on which side of the wait's beginning: BEFORE for a
 *     signal that came and went before the thread waited, AFTER for one that arrived while
 *     the thread is still queued (a wait for all of two objects, one of them set: the one
 *     case in which a healthy wait core leaves a thread queued on a signalled object);
 *   - a Sleep, a thread that does not wait, a named object, a timeout;
 *   - the report with the dispatcher lock held by the caller itself (a signal that found
 *     the thread inside the lock) does not block and says so;
 *   - the report of every guest thread: a real guest thread waiting on an event and one
 *     created suspended are both there, between the first and the last line; the report
 *     leaves them as they were (the waiter is resumed and ends when its event is set).
 *
 * Threads and conventions: wait_util.c; real guest threads as in thread_run.c.
 */
#include "wait_util.c"
#include "rt/thread.h"

#define CTX  T_CTX()

static Worker workers[2];
static char text[16384];

typedef struct { HANDLE h[2]; unsigned n; BOOL all; DWORD timeout; } Waits;

static void wait_some(Worker *w) {
    Waits *o = w->arg;
    w->result = rt_wait_handles(W_CTX(w), o->n, o->h, o->all, o->timeout, FALSE);
}

static void sleep_some(Worker *w) {
    DWORD *ms = w->arg;
    w->result = rt_sleep_ex(W_CTX(w), *ms, FALSE);
}

/* The wait core's lines for `t`, taken as the driver's time limit takes them. */
static const char *report(RtThread *t) {
    const bool locked = rt_wait_report_lock();
    size_t at = rt_wait_report_thread(text, sizeof text, 0, t);
    rt_wait_report_unlock();
    if (!locked || at >= sizeof text) return "";
    return text;
}

static bool has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

TEST(core, wait_report_names_the_object_and_who_signalled_it)
{
    char want[160];
    const uint32_t me = RT_THREAD(CTX)->id;
    Waits o = { { rt_event_create(CTX, FALSE, FALSE, NULL), NULL }, 1, FALSE, INFINITE };

    /* Nobody waits, nobody has waited. */
    T_TRUE(has(report(RT_THREAD(CTX)), "  wait core: not waiting"));

    /* One thread queued on an event nobody has signalled. */
    wu_start(workers, 1, wait_some, &o);
    T_REQUIRE(EVENTUALLY(wu_queued(o.h[0]) == 1));
    const char *r = report(&workers[0].t);
    T_TRUE(has(r, "  wait core: WAITING for "));
    T_TRUE(has(r, " ms so far for its 1 object(s) (no timeout); the wait began after signal #"));
    snprintf(want, sizeof want, "    [0] event %p: not signalled for this thread now; 1 thread(s) queued on it; never signalled\n", (void *)wu_obj(o.h[0]));
    T_TRUE(has(r, want));
    T_EQ(rt_event_set(CTX, o.h[0]), TRUE);
    T_REQUIRE(wu_finish(workers, 1, "one waiter"));
    T_EQ(workers[0].result, WAIT_OBJECT_0);
    T_TRUE(has(report(&workers[0].t), "  wait core: not waiting (not queued on any object and not in Sleep)"));

    /* The same event again: it was signalled once, by this thread, BEFORE the new wait began
     * (the first waiter took it, so the event is reset and the second thread sleeps). */
    wu_start(workers, 1, wait_some, &o);
    T_REQUIRE(EVENTUALLY(wu_queued(o.h[0]) == 1));
    r = report(&workers[0].t);
    snprintf(want, sizeof want, "queued on it; signalled 1 time(s), last by thread 0x%x as signal #", me);
    T_TRUE(has(r, want));
    T_TRUE(has(r, ", before this wait began\n"));
    T_TRUE(!has(r, "AFTER"));
    T_EQ(rt_event_set(CTX, o.h[0]), TRUE);
    T_REQUIRE(wu_finish(workers, 1, "second waiter"));

    /* A wait for ALL of two objects with a timeout, the first of them named.  Setting the
     * first leaves the thread queued on an object that is signalled for it: the signal came
     * AFTER the wait began, and the report says both. */
    Waits two = { { rt_event_create(CTX, TRUE, FALSE, wu_w("wait_report_first")), rt_semaphore_create(CTX, 0, 1, NULL) }, 2, TRUE, 600000 };
    wu_start(workers, 1, wait_some, &two);
    T_REQUIRE(EVENTUALLY(wu_queued(two.h[0]) == 1 && wu_queued(two.h[1]) == 1));
    T_EQ(rt_event_set(CTX, two.h[0]), TRUE);
    r = report(&workers[0].t);
    T_TRUE(has(r, " ms so far for all of 2 object(s) (timeout 600000 ms); the wait began after signal #"));
    snprintf(want, sizeof want, "    [0] event %p \"wait_report_first\": SIGNALLED for this thread now; 1 thread(s) queued on it; signalled 1 time(s), last by thread 0x%x as signal #",
             (void *)wu_obj(two.h[0]), me);
    T_TRUE(has(r, want));
    T_TRUE(has(r, ", AFTER this wait began\n"));
    snprintf(want, sizeof want, "    [1] semaphore %p: not signalled for this thread now; 1 thread(s) queued on it; never signalled\n", (void *)wu_obj(two.h[1]));
    T_TRUE(has(r, want));
    T_EQ(rt_semaphore_release(CTX, two.h[1], 1, NULL), TRUE);
    T_REQUIRE(wu_finish(workers, 1, "wait for all"));
    T_EQ(workers[0].result, WAIT_OBJECT_0);

    /* A Sleep. */
    DWORD ms = 400;
    wu_start(workers, 1, sleep_some, &ms);
    T_TRUE(EVENTUALLY(has(report(&workers[0].t), "  wait core: IN Sleep for ") || wu_load(&workers[0].done)));
    T_TRUE(wu_load(&workers[0].done) || has(text, " ms so far (timeout 400 ms); it began after signal #"));
    T_REQUIRE(wu_finish(workers, 1, "sleeper"));

    /* The sleeper's Sleep ran out: it is counted, and it did not end before its deadline. */
    size_t at = rt_wait_report_lateness(text, sizeof text, 0);
    unsigned long long count = 0, avg = 0, most = 0;
    T_TRUE(at > 0 && sscanf(text, "timed waits and sleeps that ran out: %llu; they ended on average %llu and at most %llu microseconds after their deadline", &count, &avg, &most) == 3);
    T_TRUE(count >= 1 && most >= avg);

    T_EQ(rt_handle_close(o.h[0]), TRUE);
    T_EQ(rt_handle_close(two.h[0]), TRUE);
    T_EQ(rt_handle_close(two.h[1]), TRUE);
}

TEST(core, wait_report_does_not_block_inside_the_dispatcher_lock)
{
    /* A signal can find a thread inside the lock; the handler's report must not take it again. */
    rt_dispatcher_lock();
    const bool took = rt_wait_report_lock();
    size_t at = rt_wait_report_thread(text, sizeof text, 0, RT_THREAD(CTX));
    rt_wait_report_unlock();                    /* releases nothing: it took nothing */
    const bool still = rt_dispatcher_held();
    rt_dispatcher_unlock();
    T_EQ(took, false);
    T_EQ(still, true);
    T_TRUE(at > 0 && has(text, "interrupted inside the dispatcher lock"));
    T_TRUE(has(text, "  wait core: not waiting"));
    T_TRUE(has(report(NULL), "no thread"));
}

/* ---- the report of every guest thread ------------------------------------------------------ */

static HANDLE go;

GUEST_FN(wait_for_go)
{
    ctx->r[0] = rt_wait_handle(ctx, go, INFINITE, FALSE) == WAIT_OBJECT_0 ? 7 : 1;
    GUEST_RET(ctx);
}

static size_t slurp(int fd, char *out, size_t cap) {
    size_t at = 0;
    for (;;) {
        ssize_t r = read(fd, out + at, cap - 1 - at);
        if (r <= 0 || at + (size_t)r >= cap - 1) { at += r > 0 ? (size_t)r : 0; break; }
        at += (size_t)r;
    }
    out[at] = 0;
    return at;
}

TEST(core, wait_report_of_every_guest_thread)
{
    char want[200];
    DWORD id_wait = 0, id_susp = 0;
    go = rt_event_create(CTX, TRUE, FALSE, NULL);
    HANDLE waiter = rt_thread_create(CTX, 0, T_GUEST_ADDR(wait_for_go), 0, 0, &id_wait);
    HANDLE susp = rt_thread_create(CTX, 0, T_GUEST_ADDR(wait_for_go), 0, CREATE_SUSPENDED, &id_susp);
    T_REQUIRE(waiter && susp);
    T_REQUIRE(EVENTUALLY(wu_queued(go) == 1));

    int fds[2];
    T_REQUIRE(pipe(fds) == 0);
    rt_thread_report_waits(fds[1]);
    close(fds[1]);
    static char all[32768];
    slurp(fds[0], all, sizeof all);
    close(fds[0]);

    T_TRUE(!strncmp(all, "THE WAITS OF EVERY GUEST THREAD (3 running; newest first)\n", 58));
    T_TRUE(has(all, "\nEND OF THE WAITS\n"));
    /* The waiter: its block names it and its wait on the event. */
    snprintf(want, sizeof want, "\nguest thread 0x%x \"\"", (unsigned)id_wait);
    const char *w = strstr(all, want);
    T_REQUIRE(w != NULL);
    const char *w_end = strstr(w + 1, "\nguest thread 0x");
    T_REQUIRE(w_end != NULL);                   /* newest first: guest thread 1 comes after it */
    snprintf(want, sizeof want, "    [0] event %p: not signalled for this thread now; 1 thread(s) queued on it; never signalled\n", (void *)wu_obj(go));
    const char *line = strstr(w, want);
    T_TRUE(line && line < w_end);
    line = strstr(w, "  wait core: WAITING for ");
    T_TRUE(line && line < w_end);
    /* The one created suspended has run nothing. */
    snprintf(want, sizeof want, "\nguest thread 0x%x \"\"", (unsigned)id_susp);
    const char *s = strstr(all, want);
    T_REQUIRE(s != NULL);
    line = strstr(s, "  SUSPENDED: created suspended and not resumed yet");
    T_TRUE(line && line < w);                   /* it is the newest: its block is the first */
    /* Guest thread 1 wrote the report. */
    snprintf(want, sizeof want, "\nguest thread 0x%x \"\" (guest thread 1)", RT_THREAD(CTX)->id);
    const char *m = strstr(all, want);
    T_REQUIRE(m != NULL);
    T_TRUE(has(m, "  (the report was written on this thread: it is where the signal found it)\n"));
    T_TRUE(has(m, "  wait core: not waiting"));

    /* The report changed nothing: both threads go on and end as they would have. */
    T_EQ(rt_dispatcher_held(), false);
    T_EQ(wu_queued(go), 1);
    T_EQ(rt_thread_resume(susp), 1);
    T_EQ(rt_event_set(CTX, go), TRUE);
    T_EQ(rt_wait_handle(CTX, waiter, 30000, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(CTX, susp, 30000, FALSE), WAIT_OBJECT_0);
    DWORD code = 0;
    T_EQ(rt_thread_get_exit_code(waiter, &code), TRUE);
    T_EQ(code, 7);
    T_EQ(rt_handle_close(waiter), TRUE);
    T_EQ(rt_handle_close(susp), TRUE);
    T_EQ(rt_handle_close(go), TRUE);
}
