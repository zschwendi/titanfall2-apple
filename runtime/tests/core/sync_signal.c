/* SignalObjectAndWait and PulseEvent (rt/sync.h: rt_signal_and_wait, rt_event_pulse), the two
 * calls whose point is what happens between a signal and a wait.  Helpers: wait_util.c.
 */
#include "wait_util.c"

#define CTX        T_CTX()
#define ME         RT_THREAD(CTX)
#define SENTINEL   0xABCD1234u

static DWORD poll(HANDLE h) { return rt_wait_handle(CTX, h, 0, FALSE); }

/* ==== SignalObjectAndWait: what is signalled ======================================================= */

TEST(core, sync_signal_and_wait_kinds)
{
    HANDLE unset = rt_event_create(CTX, FALSE, FALSE, NULL), manual = rt_event_create(CTX, TRUE, TRUE, NULL);

    /* An event is set, as by SetEvent. */
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL), target = rt_event_create(CTX, FALSE, TRUE, NULL);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, e, target, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(wu_state(e).set, true);
    T_EQ(wu_state(target).set, false);                  /* and the wait took the second object */

    /* A semaphore is released by one.  The signal stands even when the wait times out. */
    HANDLE s = rt_semaphore_create(CTX, 0, 2, NULL);
    rt_set_last_error(CTX, SENTINEL);                   /* (the Create functions set last error) */
    uint64_t t0 = rt_time_mono_100ns();
    T_EQ(rt_signal_and_wait(CTX, s, unset, 20, FALSE), WAIT_TIMEOUT);
    T_TRUE(rt_time_mono_100ns() - t0 >= 20 * 10000);
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* a timeout is not a failure */
    T_EQ(wu_state(s).count, 1);
    T_EQ(rt_signal_and_wait(CTX, s, manual, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(s).count, 2);

    /* A mutex is released once, as by ReleaseMutex. */
    HANDLE m = rt_mutex_create(CTX, TRUE, NULL);
    T_EQ(poll(m), WAIT_OBJECT_0);                       /* depth 2 */
    T_EQ(rt_signal_and_wait(CTX, m, manual, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(m).recursion, 1);
    T_EQ(wu_state(m).owner_thread_id, ME->id);
    T_EQ(rt_signal_and_wait(CTX, m, unset, 0, TRUE), WAIT_TIMEOUT);
    T_EQ(wu_state(m).owned, false);

    /* The same object on both sides: signalled, then waited on. */
    HANDLE s2 = rt_semaphore_create(CTX, 0, 1, NULL);
    T_EQ(rt_signal_and_wait(CTX, s2, s2, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(s2).count, 0);
    HANDLE e2 = rt_event_create(CTX, FALSE, FALSE, NULL);
    T_EQ(rt_signal_and_wait(CTX, e2, e2, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(e2).set, false);
    HANDLE m2 = rt_mutex_create(CTX, TRUE, NULL);
    T_EQ(rt_signal_and_wait(CTX, m2, m2, INFINITE, FALSE), WAIT_OBJECT_0);      /* released, then taken again */
    T_EQ(wu_state(m2).owner_thread_id, ME->id);
    T_EQ(wu_state(m2).recursion, 1);

    /* The wait half is a wait like any other: here it takes a mutex. */
    HANDLE m3 = rt_mutex_create(CTX, FALSE, NULL);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, e, m3, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(m3).owner_thread_id, ME->id);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    HANDLE all[9] = { unset, manual, e, target, s, m, s2, e2, m2 };
    for (int i = 0; i < 9; i++) T_EQ(wu_obj(all[i])->refs, 1);  /* no reference is kept */
}

/* ==== SignalObjectAndWait: failures ================================================================= */

static void own_for_a_while(Worker *w) {
    HANDLE *h = w->arg;                 /* h[0]: the mutex to own; h[1]: an event to wait for */
    W_CHECK(w, rt_wait_handle(W_CTX(w), h[0], INFINITE, FALSE) == WAIT_OBJECT_0);
    wu_store((int *)&w->n[0], 1);
    W_CHECK(w, rt_wait_handle(W_CTX(w), h[1], INFINITE, FALSE) == WAIT_OBJECT_0);
    W_CHECK(w, rt_mutex_release(W_CTX(w), h[0]) == TRUE);
}

TEST(core, sync_signal_and_wait_failures)
{
    static Worker w;
    HANDLE target = rt_event_create(CTX, FALSE, TRUE, NULL);    /* set: a wait on it would succeed and reset it */
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL);
    HANDLE full = rt_semaphore_create(CTX, 2, 2, NULL), part = rt_semaphore_create(CTX, 1, 2, NULL);
    HANDLE free_m = rt_mutex_create(CTX, FALSE, NULL), mine = rt_mutex_create(CTX, TRUE, NULL);
    HANDLE plain = rt_handle_new(wu_new_plain());
    WuFlag *flag = wu_new_flag();
    rt_object_ref(&flag->obj);
    HANDLE flag_h = rt_handle_new(&flag->obj);
    T_REQUIRE(target && e && full && part && free_m && mine && plain && flag_h);
    HANDLE closed = rt_event_create(CTX, FALSE, FALSE, NULL);
    rt_handle_close(closed);

    /* A semaphore that is full: ERROR_TOO_MANY_POSTS, and there is no wait. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, full, target, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(wu_state(full).count, 2);
    T_EQ(wu_state(target).set, true);

    /* A mutex the caller does not own: free, or owned by another thread. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, free_m, target, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(wu_state(free_m).owned, false);
    HANDLE pair[2] = { free_m, e };
    wu_start_one(&w, 0, own_for_a_while, pair);
    T_REQUIRE(EVENTUALLY(wu_load((int *)&w.n[0])));
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, free_m, target, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(wu_state(free_m).owner_thread_id, w.t.id);
    T_EQ(wu_state(free_m).recursion, 1);
    T_EQ(wu_state(target).set, true);
    T_EQ(rt_event_set(CTX, e), TRUE);
    if (!wu_finish(&w, 1, "the mutex's owner")) return;

    /* An object that cannot be signalled this way: not an event, mutex or semaphore, whether
     * or not it can be waited on. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, plain, target, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, flag_h, target, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(flag->signaled, false);
    T_EQ(wu_state(target).set, true);
    /* ... but such a waitable object is fine as the one to wait on. */
    T_EQ(rt_signal_and_wait(CTX, e, flag_h, 5, FALSE), WAIT_TIMEOUT);
    T_EQ(poll(e), WAIT_OBJECT_0);

    /* A handle that is not open, on either side.  When it is the second, NOTHING was
     * signalled: not the event, not the semaphore, not the mutex. */
    static const uintptr_t bad[] = { 0, 4, 0x12345678 };
    for (unsigned i = 0; i < 4; i++) {
        HANDLE b = i < 3 ? (HANDLE)bad[i] : closed;
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_signal_and_wait(CTX, b, target, INFINITE, FALSE), WAIT_FAILED);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_signal_and_wait(CTX, e, b, INFINITE, FALSE), WAIT_FAILED);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_signal_and_wait(CTX, part, b, INFINITE, FALSE), WAIT_FAILED);
        T_EQ(rt_signal_and_wait(CTX, mine, b, INFINITE, FALSE), WAIT_FAILED);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_signal_and_wait(CTX, b, b, INFINITE, FALSE), WAIT_FAILED);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }
    /* The same when the second object exists but cannot be waited on. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_signal_and_wait(CTX, e, plain, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_signal_and_wait(CTX, part, plain, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(rt_signal_and_wait(CTX, mine, plain, INFINITE, FALSE), WAIT_FAILED);
    T_EQ(wu_state(e).set, false);
    T_EQ(wu_state(part).count, 1);
    T_EQ(wu_state(mine).recursion, 1);
    T_EQ(wu_state(mine).owner_thread_id, ME->id);
    T_EQ(wu_state(target).set, true);

    /* No failed call kept a reference. */
    HANDLE all[7] = { target, e, full, part, free_m, mine, plain };
    for (int i = 0; i < 7; i++) T_EQ(wu_obj(all[i])->refs, 1);
    T_EQ(flag->obj.refs, 2);
    T_EQ(rt_dispatcher_held(), false);
    rt_object_unref(&flag->obj);
}

/* ==== SignalObjectAndWait is one step ============================================================== */

/* The use the call was made for: a thread releases a mutex and waits for an event as one
 * step, so that whoever takes the mutex next can pulse the event and be sure the thread is
 * already waiting for it.  If the release and the wait were two steps, the pulse could fall
 * between them and be lost.
 *
 * Each round the worker owns the mutex and holds it until the main thread is queued behind
 * it; then it calls SignalObjectAndWait.  The release hands the mutex straight to the main
 * thread, which looks at the event's queue the moment it runs. */
typedef struct { HANDLE m, e; unsigned rounds; int stop; } Step;

static void release_and_wait(Worker *w) {
    Step *s = w->arg;
    for (;;) {
        W_CHECK(w, rt_wait_handle(W_CTX(w), s->m, INFINITE, FALSE) == WAIT_OBJECT_0);
        while (wu_queued(s->m) != 1 && !wu_load(&s->stop)) sched_yield();
        if (wu_load(&s->stop)) break;
        DWORD r = rt_signal_and_wait(W_CTX(w), s->m, s->e, INFINITE, FALSE);
        W_CHECK(w, r == WAIT_OBJECT_0);
        w->n[0]++;
    }
    W_CHECK(w, rt_mutex_release(W_CTX(w), s->m) == TRUE);
}

TEST(core, sync_signal_and_wait_is_one_step)
{
    static Worker w;
    for (int manual = 0; manual < 2; manual++) {
        Step s = { rt_mutex_create(CTX, FALSE, NULL), rt_event_create(CTX, manual, FALSE, NULL), 150, 0 };
        wu_start_one(&w, 0, release_and_wait, &s);
        for (unsigned round = 0; round < s.rounds; round++) {
            T_REQUIRE(EVENTUALLY(wu_state(s.m).owner_thread_id == w.t.id));
            T_EQ(rt_wait_handle(CTX, s.m, INFINITE, FALSE), WAIT_OBJECT_0);
            /* This thread has the mutex, so the worker has released it, so it is waiting for
             * the event: not "soon", now. */
            T_EQ(wu_queued(s.e), 1);
            T_EQ(wu_state(s.m).owner_thread_id, ME->id);
            T_EQ(rt_event_pulse(CTX, s.e), TRUE);
            T_EQ(wu_queued(s.e), 0);
            T_EQ(wu_state(s.e).set, false);
            T_EQ(rt_mutex_release(CTX, s.m), TRUE);
        }
        T_REQUIRE(EVENTUALLY(wu_state(s.m).owner_thread_id == w.t.id));
        wu_store(&s.stop, 1);
        if (!wu_finish(&w, 1, "release-and-wait")) return;
        T_EQ(w.n[0], s.rounds);
        T_EQ(wu_state(s.m).owned, false);
        T_EQ(wu_queued(s.e), 0);
    }
}

/* ==== PulseEvent ====================================================================================== */

typedef struct { HANDLE h[2]; unsigned n; bool all; DWORD timeout; } PWait;

static void p_wait(Worker *w) {
    PWait *p = w->arg;
    w->result = rt_wait_handles(W_CTX(w), p->n, p->h, p->all, p->timeout, FALSE);
}

TEST(core, sync_pulse_releases_who_is_waiting_now)
{
    enum { N = 6 };
    static Worker w[N];

    /* Manual-reset: everyone who is waiting, and then the event is reset. */
    PWait man = { { rt_event_create(CTX, TRUE, FALSE, NULL), NULL }, 1, false, INFINITE };
    wu_start(w, N, p_wait, &man);
    T_REQUIRE(EVENTUALLY(wu_queued(man.h[0]) == N));
    T_EQ(rt_event_pulse(CTX, man.h[0]), TRUE);
    T_EQ(wu_state(man.h[0]).set, false);
    T_EQ(wu_queued(man.h[0]), 0);
    if (!wu_finish(w, N, "waiters on a pulsed manual-reset event")) return;
    for (int i = 0; i < N; i++) T_EQ(w[i].result, WAIT_OBJECT_0);
    /* A thread that comes after the pulse is not released by it. */
    PWait late = { { man.h[0], NULL }, 1, false, 40 };
    wu_start(w, 2, p_wait, &late);
    if (!wu_finish(w, 2, "waiters after the pulse")) return;
    T_EQ(w[0].result, WAIT_TIMEOUT);
    T_EQ(w[1].result, WAIT_TIMEOUT);

    /* Auto-reset: one thread, the one that has waited longest, however many are waiting. */
    PWait aut = { { rt_event_create(CTX, FALSE, FALSE, NULL), NULL }, 1, false, INFINITE };
    for (unsigned i = 0; i < N; i++) {
        wu_start_one(&w[i], i, p_wait, &aut);
        T_REQUIRE(EVENTUALLY(wu_queued(aut.h[0]) == i + 1));
    }
    for (unsigned i = 0; i < N; i++) {
        T_EQ(rt_event_pulse(CTX, aut.h[0]), TRUE);
        T_EQ(wu_state(aut.h[0]).set, false);
        T_EQ(wu_queued(aut.h[0]), N - 1 - i);
        T_REQUIRE(EVENTUALLY(wu_load(&w[i].done)));
        wu_settle();
        T_EQ(wu_done_count(w, N), i + 1);
    }
    if (!wu_finish(w, N, "waiters on a pulsed auto-reset event")) return;
    T_EQ(rt_event_pulse(CTX, aut.h[0]), TRUE);          /* nobody left: it just stays reset */
    T_EQ(wu_state(aut.h[0]).set, false);

    /* A waiter that cannot be released immediately (its wait-all needs a semaphore that is
     * empty) is not released, and the event does not stay set for it. */
    for (int manual = 0; manual < 2; manual++) {
        HANDLE e = rt_event_create(CTX, manual, FALSE, NULL), s = rt_semaphore_create(CTX, 0, 1, NULL);
        PWait both = { { e, s }, 2, true, INFINITE };
        wu_start_one(&w[0], 0, p_wait, &both);
        T_REQUIRE(EVENTUALLY(wu_queued(e) == 1 && wu_queued(s) == 1));
        T_EQ(rt_event_pulse(CTX, e), TRUE);
        T_EQ(wu_state(e).set, false);
        T_EQ(wu_queued(e), 1);
        T_EQ(rt_semaphore_release(CTX, s, 1, NULL), TRUE);      /* too late for that pulse */
        wu_settle();
        T_EQ(wu_load(&w[0].done), 0);
        T_EQ(wu_state(s).count, 1);
        T_EQ(rt_event_pulse(CTX, e), TRUE);             /* now it can be released at once */
        if (!wu_finish(&w[0], 1, "wait-all released by a pulse")) return;
        T_EQ(w[0].result, WAIT_OBJECT_0);
        T_EQ(wu_state(e).set, false);
        T_EQ(wu_state(s).count, 0);
    }
}
