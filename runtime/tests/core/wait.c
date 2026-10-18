/* The wait core (docs/RUNTIME.md section 4.2; rt/handle.h: rt_wait, rt_signal, the dispatcher
 * lock; rt/sync.h: rt_wait_locked, rt_wait_queue_length).  This file is the argument rules and
 * what one wait takes; contention is in wait_threads.c, APCs and sleeping in wait_apc.c,
 * thread exit and abandoned mutexes in wait_exit.c, the three kinds themselves in sync*.c.
 * Helpers: wait_util.c.
 *
 * The tests call rt_wait with objects, as a core unit would.  Events, mutexes and semaphores
 * are made through rt/sync.h and looked at with rt_sync_query, which takes nothing.
 */
#include "wait_util.c"

#define CTX        T_CTX()
#define SENTINEL   0xABCD1234u          /* a last error no call sets */

static HANDLE ev_auto(bool set)   { return rt_event_create(CTX, FALSE, set, NULL); }
static HANDLE ev_manual(bool set) { return rt_event_create(CTX, TRUE, set, NULL); }
static HANDLE sem(LONG n)         { return rt_semaphore_create(CTX, n, 1000, NULL); }
static HANDLE mtx(void)           { return rt_mutex_create(CTX, FALSE, NULL); }

/* rt_wait on the objects behind `n` handles. */
static DWORD wait_h(unsigned n, const HANDLE *h, bool all, DWORD ms) {
    RtObject *objs[MAXIMUM_WAIT_OBJECTS + 2];
    for (unsigned i = 0; i < n && i < MAXIMUM_WAIT_OBJECTS + 2; i++) objs[i] = wu_obj(h[i]);
    return rt_wait(CTX, n, objs, all, ms, false);
}
static DWORD poll1(HANDLE h) { return wait_h(1, &h, false, 0); }

/* ---- n: 0, 1, 64, 65 ------------------------------------------------------------------------- */

TEST(core, wait_count_limits)
{
    HANDLE h[MAXIMUM_WAIT_OBJECTS + 2];
    RtObject *objs[MAXIMUM_WAIT_OBJECTS + 2];
    for (unsigned i = 0; i < MAXIMUM_WAIT_OBJECTS + 2; i++) {
        h[i] = ev_auto(true);
        T_REQUIRE(h[i] != NULL);
        objs[i] = wu_obj(h[i]);
    }

    /* Zero objects: WAIT_FAILED and ERROR_INVALID_PARAMETER, for wait-any and wait-all, with
     * and without an array, whatever the timeout.  Nothing is taken. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 0, objs, false, 0, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 0, objs, true, INFINITE, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 0, NULL, false, 10, true), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    /* 65 and more: the same.  Every object is signalled, and none is taken. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS + 1, objs, false, 0, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS + 2, objs, true, INFINITE, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 0xFFFFFFFFu, objs, false, 0, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    for (unsigned i = 0; i < MAXIMUM_WAIT_OBJECTS + 2; i++) T_EQ(wu_state(h[i]).set, true);

    /* Exactly 64 is allowed.  Wait-all takes all 64 and only those. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS, objs, true, 0, false), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* success leaves last error alone */
    for (unsigned i = 0; i < MAXIMUM_WAIT_OBJECTS; i++) T_EQ(wu_state(h[i]).set, false);
    T_EQ(wu_state(h[MAXIMUM_WAIT_OBJECTS]).set, true);
    T_EQ(wu_state(h[MAXIMUM_WAIT_OBJECTS + 1]).set, true);

    /* 64, wait-any: nothing signalled, then only the last one. */
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS, objs, false, 0, false), WAIT_TIMEOUT);
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* a timeout is not a failure */
    T_EQ(rt_event_set(CTX, h[MAXIMUM_WAIT_OBJECTS - 1]), TRUE);
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS, objs, false, 0, false), WAIT_OBJECT_0 + MAXIMUM_WAIT_OBJECTS - 1);
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS, objs, false, 0, false), WAIT_TIMEOUT);
    /* 64, wait-all, one missing: nothing is taken. */
    for (unsigned i = 0; i < MAXIMUM_WAIT_OBJECTS - 1; i++) rt_event_set(CTX, h[i]);
    T_EQ(rt_wait(CTX, MAXIMUM_WAIT_OBJECTS, objs, true, 0, false), WAIT_TIMEOUT);
    for (unsigned i = 0; i < MAXIMUM_WAIT_OBJECTS - 1; i++) T_EQ(wu_state(h[i]).set, true);

    /* One object. */
    T_EQ(rt_wait(CTX, 1, objs, false, 0, false), WAIT_OBJECT_0);
    T_EQ(rt_wait(CTX, 1, objs, false, 0, false), WAIT_TIMEOUT);
    T_EQ(rt_wait(CTX, 1, objs + 1, true, 0, false), WAIT_OBJECT_0);     /* wait-all on one is a wait on one */
    T_EQ(rt_wait(CTX, 1, objs + 1, true, 0, false), WAIT_TIMEOUT);
}

/* ---- an object whose kind is not waitable ---------------------------------------------------- */

TEST(core, wait_not_waitable)
{
    RtObject *plain = wu_new_plain();
    HANDLE s = sem(1), e = ev_auto(true);
    RtObject *so = wu_obj(s), *eo = wu_obj(e);

    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 1, &plain, false, INFINITE, false), WAIT_FAILED);     /* fails at once, even with no timeout */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* A signalled object before it in a wait-any is not taken: every object is checked first. */
    RtObject *a[3] = { so, plain, eo };
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 3, a, false, 0, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(wu_state(s).count, 1);
    T_EQ(wu_state(e).set, true);

    /* Nor is anything taken in a wait-all, wherever the object stands. */
    RtObject *b[3] = { eo, so, plain };
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 3, b, true, INFINITE, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    RtObject *c[2] = { plain, so };
    T_EQ(rt_wait(CTX, 2, c, true, 0, false), WAIT_FAILED);
    T_EQ(wu_state(s).count, 1);
    T_EQ(wu_state(e).set, true);
    T_EQ(rt_wait_queue_length(so), 0);
    T_EQ(rt_wait_queue_length(eo), 0);

    /* The object is checked before the wait-all rule about copies: a non-waitable object
     * named twice is ERROR_INVALID_HANDLE, not ERROR_INVALID_PARAMETER. */
    RtObject *d[2] = { plain, plain };
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait(CTX, 2, d, true, 0, false), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* The same objects without the plain one do work. */
    T_EQ(rt_wait(CTX, 2, b, true, 0, false), WAIT_OBJECT_0);
    T_EQ(wu_state(s).count, 0);
    T_EQ(wu_state(e).set, false);
    rt_object_unref(plain);
}

/* ---- the same object twice ------------------------------------------------------------------- */

TEST(core, wait_all_refuses_copies)
{
    HANDLE a = ev_auto(true), b = ev_auto(true), c = ev_auto(true);
    HANDLE two[2] = { a, a };
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(wait_h(2, two, true, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(wu_state(a).set, true);                        /* nothing taken */

    HANDLE apart[3] = { a, b, a };                      /* not adjacent */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(wait_h(3, apart, true, INFINITE), WAIT_FAILED);        /* fails at once, even with no timeout */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    HANDLE tail[4] = { a, b, c, c };                    /* the last two */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(wait_h(4, tail, true, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    HANDLE head[4] = { a, a, b, c };                    /* the first two */
    T_EQ(wait_h(4, head, true, 0), WAIT_FAILED);
    T_EQ(wu_state(a).set, true);
    T_EQ(wu_state(b).set, true);
    T_EQ(wu_state(c).set, true);

    /* It is the OBJECT that counts: two handles to one object are copies too. */
    HANDLE a2 = rt_handle_dup(a);
    T_REQUIRE(a2 != NULL && a2 != a);
    HANDLE dup[2] = { a, a2 };
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait_handles(CTX, 2, dup, TRUE, 0, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(wu_state(a).set, true);

    /* Three different objects are fine. */
    HANDLE ok[3] = { a, b, c };
    T_EQ(wait_h(3, ok, true, 0), WAIT_OBJECT_0);
}

static void wait_any_x_s_s(Worker *w) {
    HANDLE *h = w->arg;
    HANDLE three[3] = { h[0], h[1], h[1] };
    w->result = rt_wait_handles(W_CTX(w), 3, three, FALSE, INFINITE, FALSE);
}

static void wait_single_s(Worker *w) {
    HANDLE *h = w->arg;
    w->result = rt_wait_handle(W_CTX(w), h[1], INFINITE, FALSE);
}

TEST(core, wait_any_allows_copies)
{
    /* A wait-any may name an object twice; the lowest index is reported and the object is
     * taken once. */
    HANDLE e = ev_auto(true), other = ev_auto(false);
    HANDLE two[2] = { e, e };
    T_EQ(wait_h(2, two, false, 0), WAIT_OBJECT_0);
    T_EQ(wu_state(e).set, false);
    T_EQ(wait_h(2, two, false, 0), WAIT_TIMEOUT);

    rt_event_set(CTX, e);
    HANDLE three[3] = { other, e, e };
    T_EQ(wait_h(3, three, false, 0), WAIT_OBJECT_0 + 1);

    HANDLE s = sem(2);
    HANDLE ss[2] = { s, s };
    T_EQ(wait_h(2, ss, false, 0), WAIT_OBJECT_0);
    T_EQ(wu_state(s).count, 1);                         /* one unit, not two */

    /* A sleeping wait-any with copies: both entries are queued and both leave the queue. */
    T_EQ(wait_h(3, three, false, 20), WAIT_TIMEOUT);
    T_EQ(wu_queued(e), 0);
    T_EQ(wu_queued(other), 0);

    /* The object is signalled while such a thread sleeps, with another thread queued behind
     * its two entries: the first thread takes one unit, once, and the second the other. */
    static Worker w[2];
    HANDLE pair[2] = { other, sem(0) };
    wu_start_one(&w[0], 0, wait_any_x_s_s, pair);
    T_REQUIRE(EVENTUALLY(wu_queued(pair[1]) == 2 && wu_queued(other) == 1));
    wu_start_one(&w[1], 1, wait_single_s, pair);
    T_REQUIRE(EVENTUALLY(wu_queued(pair[1]) == 3));
    T_EQ(rt_semaphore_release(CTX, pair[1], 3, NULL), TRUE);
    T_EQ(wu_queued(pair[1]), 0);
    T_EQ(wu_queued(other), 0);
    T_EQ(wu_state(pair[1]).count, 1);                   /* three units, two waits: one is left */
    if (!wu_finish(w, 2, "a wait-any with copies and a thread behind it")) return;
    T_EQ(w[0].result, WAIT_OBJECT_0 + 1);               /* the lower of its two indices */
    T_EQ(w[1].result, WAIT_OBJECT_0);
}

/* ---- which index ------------------------------------------------------------------------------ */

TEST(core, wait_any_reports_lowest_index)
{
    HANDLE h[5];
    for (int i = 0; i < 5; i++) h[i] = ev_auto(false);
    rt_event_set(CTX, h[3]);
    rt_event_set(CTX, h[1]);
    rt_event_set(CTX, h[4]);
    T_EQ(wait_h(5, h, false, 0), WAIT_OBJECT_0 + 1);            /* 1, 3 and 4 are set: the lowest */
    T_EQ(wu_state(h[1]).set, false);                            /* only that one is taken */
    T_EQ(wu_state(h[3]).set, true);
    T_EQ(wu_state(h[4]).set, true);
    T_EQ(wait_h(5, h, false, INFINITE), WAIT_OBJECT_0 + 3);
    T_EQ(wait_h(5, h, false, 5), WAIT_OBJECT_0 + 4);
    T_EQ(wait_h(5, h, false, 0), WAIT_TIMEOUT);

    /* Index 0, and a wait that names a subset. */
    rt_event_set(CTX, h[0]);
    rt_event_set(CTX, h[2]);
    T_EQ(wait_h(3, h + 1, false, 0), WAIT_OBJECT_0 + 1);        /* h[2] is index 1 of this wait */
    T_EQ(wait_h(5, h, false, 0), WAIT_OBJECT_0);

    /* Manual-reset events are not taken: the lowest keeps winning until it is reset. */
    HANDLE m[3] = { ev_manual(false), ev_manual(true), ev_manual(true) };
    T_EQ(wait_h(3, m, false, 0), WAIT_OBJECT_0 + 1);
    T_EQ(wait_h(3, m, false, 0), WAIT_OBJECT_0 + 1);
    rt_event_reset(CTX, m[1]);
    T_EQ(wait_h(3, m, false, 0), WAIT_OBJECT_0 + 2);
    rt_event_set(CTX, m[0]);
    T_EQ(wait_h(3, m, false, 0), WAIT_OBJECT_0);

    /* Mixed kinds: a semaphore with two units before a set event. */
    HANDLE k[3] = { mtx(), sem(2), ev_auto(true) };
    T_EQ(wait_h(3, k, false, 0), WAIT_OBJECT_0);                /* the free mutex: now owned */
    T_EQ(wu_state(k[0]).owner_thread_id, RT_THREAD(CTX)->id);
    T_EQ(wait_h(3, k, false, 0), WAIT_OBJECT_0);                /* owned by the caller: signalled for it again */
    T_EQ(wu_state(k[0]).recursion, 2);
    T_EQ(wait_h(2, k + 1, false, 0), WAIT_OBJECT_0);
    T_EQ(wait_h(2, k + 1, false, 0), WAIT_OBJECT_0);
    T_EQ(wait_h(2, k + 1, false, 0), WAIT_OBJECT_0 + 1);
    T_EQ(wait_h(2, k + 1, false, 0), WAIT_TIMEOUT);
}

/* ---- wait-all takes everything or nothing ------------------------------------------------------ */

TEST(core, wait_all_takes_all_or_nothing)
{
    HANDLE e = ev_auto(true), s = sem(0), m = mtx();
    HANDLE h[3] = { e, s, m };

    /* The event is set and the mutex free, the semaphore is empty: nothing is taken, whether
     * the wait polls or sleeps. */
    T_EQ(wait_h(3, h, true, 0), WAIT_TIMEOUT);
    T_EQ(wu_state(e).set, true);
    T_EQ(wu_state(m).owned, false);
    T_EQ(wait_h(3, h, true, 30), WAIT_TIMEOUT);
    T_EQ(wu_state(e).set, true);
    T_EQ(wu_state(m).owned, false);
    T_EQ(wu_state(s).count, 0);
    T_EQ(wu_queued(e), 0);                              /* and the thread left every queue */
    T_EQ(wu_queued(s), 0);
    T_EQ(wu_queued(m), 0);

    /* All three signalled: all three are taken in one step. */
    T_EQ(rt_semaphore_release(CTX, s, 2, NULL), TRUE);
    T_EQ(wait_h(3, h, true, 0), WAIT_OBJECT_0);
    T_EQ(wu_state(e).set, false);
    T_EQ(wu_state(s).count, 1);
    T_EQ(wu_state(m).owner_thread_id, RT_THREAD(CTX)->id);
    T_EQ(wu_state(m).recursion, 1);

    /* The event is now reset: the semaphore's unit and the mutex are left alone. */
    T_EQ(wait_h(3, h, true, 0), WAIT_TIMEOUT);
    T_EQ(wu_state(s).count, 1);
    T_EQ(wu_state(m).recursion, 1);
    rt_event_set(CTX, e);
    T_EQ(wait_h(3, h, true, INFINITE), WAIT_OBJECT_0);
    T_EQ(wu_state(s).count, 0);
    T_EQ(wu_state(m).recursion, 2);

    /* The result of a wait-all is WAIT_OBJECT_0 whatever the order of the objects. */
    HANDLE r[2] = { m, ev_manual(true) };
    T_EQ(wait_h(2, r, true, 0), WAIT_OBJECT_0);
    T_EQ(wu_state(m).recursion, 3);
}

/* ---- timeouts ---------------------------------------------------------------------------------- */

TEST(core, wait_timeout_lower_bound)
{
    HANDLE e = ev_auto(false), s = sem(0);
    static const DWORD ms[] = { 1, 2, 17, 60 };
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        uint64_t t0 = rt_time_mono_100ns();
        T_EQ(poll1(e), WAIT_TIMEOUT);                           /* a poll costs no time worth a bound */
        T_EQ(wait_h(1, &e, false, ms[i]), WAIT_TIMEOUT);
        uint64_t t1 = rt_time_mono_100ns();
        /* Never early: the runtime clock has passed the whole timeout. */
        T_TRUE(t1 - t0 >= (uint64_t)ms[i] * 10000);
        HANDLE both[2] = { e, s };
        t0 = rt_time_mono_100ns();
        T_EQ(wait_h(2, both, i & 1, ms[i]), WAIT_TIMEOUT);     /* wait-any and wait-all alike */
        t1 = rt_time_mono_100ns();
        T_TRUE(t1 - t0 >= (uint64_t)ms[i] * 10000);
    }
    T_EQ(wu_queued(e), 0);
    T_EQ(wu_queued(s), 0);
    T_EQ(wu_state(e).set, false);

    /* And not absurdly late: a 200 ms timeout is over well within 1.9 s, which a factor of ten
     * in the unit conversion would not be.  The machine may be very busy, so three tries. */
    bool in_time = false;
    for (int attempt = 0; attempt < 3 && !in_time; attempt++) {
        uint64_t t0 = rt_time_mono_100ns();
        T_EQ(wait_h(1, &e, false, 200), WAIT_TIMEOUT);
        uint64_t took = rt_time_mono_100ns() - t0;
        T_TRUE(took >= 200 * 10000);
        in_time = took < 1900 * 10000;
    }
    T_TRUE(in_time);
}

typedef struct { HANDLE h; int go; } Setter;

/* Set the event once the main thread is queued on it. */
static void set_when_queued(Worker *w) {
    Setter *s = w->arg;
    W_CHECK(w, EVENTUALLY(wu_queued(s->h) == 1));
    W_CHECK(w, rt_event_set(W_CTX(w), s->h) == TRUE);
}

TEST(core, wait_long_timeouts_sleep)
{
    /* The largest timeouts are not mistaken for short ones (an overflow in the deadline
     * would end the wait at once with WAIT_TIMEOUT): the wait sleeps until the event is set. */
    static const DWORD ms[] = { INFINITE, 0xFFFFFFFEu, 0x80000000u, 0x7FFFFFFFu, 3600000u };
    static Worker w;
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        Setter s = { ev_auto(false), 0 };
        wu_start_one(&w, 0, set_when_queued, &s);
        T_EQ(wait_h(1, &s.h, false, ms[i]), WAIT_OBJECT_0);
        if (!wu_finish(&w, 1, "setter")) return;
        T_EQ(wu_state(s.h).set, false);
        T_EQ(wu_queued(s.h), 0);
    }
}

/* ---- a kind defined elsewhere ------------------------------------------------------------------ */

static void wait_flag(Worker *w) {
    RtObject *o = w->arg;
    w->result = rt_wait(W_CTX(w), 1, &o, false, INFINITE, false);
}

TEST(core, wait_kind_of_another_unit)
{
    /* What the thread unit will do for a thread object: a kind with is_signaled and no
     * consume, signalled once under the dispatcher lock.  Nothing but rt/handle.h is used. */
    WuFlag *f = wu_new_flag();
    RtObject *o = &f->obj;
    T_EQ(rt_wait(CTX, 1, &o, false, 0, false), WAIT_TIMEOUT);
    T_EQ(rt_wait(CTX, 1, &o, false, 15, false), WAIT_TIMEOUT);
    T_TRUE(f->asked >= 2);

    static Worker w[4];
    wu_start(w, 4, wait_flag, o);
    T_REQUIRE(EVENTUALLY(rt_wait_queue_length(o) == 4));

    /* rt_signal with the state unchanged releases nobody. */
    rt_dispatcher_lock();
    rt_signal(o);
    rt_dispatcher_unlock();
    wu_settle();
    T_EQ(wu_done_count(w, 4), 0);
    T_EQ(rt_wait_queue_length(o), 4);

    /* Signalled: every waiter is released, and since nothing is taken it stays signalled. */
    wu_flag_signal(f);
    if (!wu_finish(w, 4, "waiters on a foreign kind")) return;
    for (int i = 0; i < 4; i++) T_EQ(w[i].result, WAIT_OBJECT_0);
    T_EQ(rt_wait_queue_length(o), 0);
    T_TRUE(o->waiters == NULL);
    T_EQ(rt_wait(CTX, 1, &o, false, 0, false), WAIT_OBJECT_0);
    T_EQ(rt_wait(CTX, 1, &o, true, INFINITE, false), WAIT_OBJECT_0);

    /* It mixes with the other kinds in one wait. */
    HANDLE e = ev_auto(true);
    RtObject *mix[2] = { wu_obj(e), o };
    T_EQ(rt_wait(CTX, 2, mix, true, 0, false), WAIT_OBJECT_0);
    T_EQ(wu_state(e).set, false);
    T_EQ(rt_wait(CTX, 2, mix, false, 0, false), WAIT_OBJECT_0 + 1);
    rt_object_unref(o);
}

/* ---- queue order -------------------------------------------------------------------------------- */

typedef struct { HANDLE h[2]; unsigned n; bool all; } QWait;

static void q_wait(Worker *w) {
    QWait *q = w->arg;
    w->result = rt_wait_handles(W_CTX(w), q->n, q->h, q->all, INFINITE, FALSE);
}

TEST(core, wait_queue_is_first_in_first_out)
{
    enum { N = 5 };
    static Worker w[N];
    HANDLE e = ev_auto(false);
    QWait q = { { e, NULL }, 1, false };
    /* Queue the threads one at a time, so the order is known. */
    for (unsigned i = 0; i < N; i++) {
        wu_start_one(&w[i], i, q_wait, &q);
        T_REQUIRE(EVENTUALLY(wu_queued(e) == i + 1));
    }
    /* Each Set releases the thread that has waited longest, and only that one. */
    for (unsigned i = 0; i < N; i++) {
        T_EQ(rt_event_set(CTX, e), TRUE);
        T_REQUIRE(EVENTUALLY(wu_load(&w[i].done)));
        T_EQ(wu_queued(e), N - 1 - i);
        T_EQ(wu_state(e).set, false);
        wu_settle();
        T_EQ(wu_done_count(w, N), i + 1);
        for (unsigned j = i + 1; j < N; j++) T_EQ(wu_load(&w[j].done), 0);
    }
    if (!wu_finish(w, N, "queued waiters")) return;
    for (unsigned i = 0; i < N; i++) T_EQ(w[i].result, WAIT_OBJECT_0);

    /* A semaphore released by two hands its units to the first two. */
    HANDLE s = sem(0);
    QWait qs = { { s, NULL }, 1, false };
    for (unsigned i = 0; i < 4; i++) {
        wu_start_one(&w[i], i, q_wait, &qs);
        T_REQUIRE(EVENTUALLY(wu_queued(s) == i + 1));
    }
    LONG prev = -1;
    T_EQ(rt_semaphore_release(CTX, s, 2, &prev), TRUE);
    T_EQ(prev, 0);
    T_REQUIRE(EVENTUALLY(wu_load(&w[0].done) && wu_load(&w[1].done)));
    T_EQ(wu_state(s).count, 0);
    T_EQ(wu_queued(s), 2);
    wu_settle();
    T_EQ(wu_load(&w[2].done), 0);
    T_EQ(wu_load(&w[3].done), 0);
    T_EQ(rt_semaphore_release(CTX, s, 2, NULL), TRUE);
    if (!wu_finish(w, 4, "semaphore waiters")) return;
}

/* A thread leaves a queue from every position.  Each worker waits for the shared event OR an
 * event of its own, so the test can take any one of them out of the shared event's queue by
 * setting its own event; then it checks who is left, and in which order, by setting the
 * shared event once per thread. */
typedef struct { HANDLE shared, own[8]; } Leave;

static void wait_shared_or_own(Worker *w) {
    Leave *l = w->arg;
    HANDLE h[2] = { l->shared, l->own[w->index] };
    w->result = rt_wait_handles(W_CTX(w), 2, h, FALSE, INFINITE, FALSE);
}

TEST(core, wait_queue_leave_from_every_position)
{
    enum { N = 8 };
    static Worker w[N];
    static Leave l;
    l.shared = ev_auto(false);
    for (unsigned i = 0; i < N; i++) l.own[i] = ev_auto(false);
    RtObject *shared = wu_obj(l.shared);
#define JOIN(i, len) do { wu_start_one(&w[i], (i), wait_shared_or_own, &l); \
                          T_REQUIRE(EVENTUALLY(wu_queued(l.shared) == (len))); } while (0)
#define LEAVE(i, len) do { T_EQ(rt_event_set(CTX, l.own[i]), TRUE); \
                           T_EQ(wu_queued(l.shared), (len)); T_EQ(wu_queued(l.own[i]), 0); \
                           T_REQUIRE(EVENTUALLY(wu_load(&w[i].done))); T_EQ(w[i].result, WAIT_OBJECT_0 + 1); } while (0)

    /* The only entry. */
    JOIN(0, 1);
    LEAVE(0, 0);
    T_TRUE(shared->waiters == NULL);                    /* an empty queue is NULL: the handle unit relies on it */
    if (!wu_finish(&w[0], 1, "only entry")) return;

    /* Queue 0 1 2 3 4, then: the middle, the tail, a newcomer, the head, another newcomer. */
    for (unsigned i = 0; i < 5; i++) JOIN(i, i + 1);
    LEAVE(2, 4);                                        /* 0 1 3 4 */
    LEAVE(4, 3);                                        /* 0 1 3: the tail left ... */
    JOIN(5, 4);                                         /* 0 1 3 5: ... and the next one is appended after the new tail */
    LEAVE(0, 3);                                        /* 1 3 5: the head left */
    JOIN(6, 4);                                         /* 1 3 5 6 */
    LEAVE(3, 3);                                        /* 1 5 6 */
    JOIN(7, 4);                                         /* 1 5 6 7 */
    LEAVE(7, 3);                                        /* 1 5 6: the newest left again */
    T_EQ(wu_state(l.shared).set, false);

    /* Who is left comes out in the order they joined: 1, 5, 6. */
    static const unsigned order[3] = { 1, 5, 6 };
    for (unsigned k = 0; k < 3; k++) {
        T_EQ(rt_event_set(CTX, l.shared), TRUE);
        T_EQ(wu_queued(l.shared), 2 - k);
        T_REQUIRE(EVENTUALLY(wu_load(&w[order[k]].done)));
        T_EQ(w[order[k]].result, WAIT_OBJECT_0);
        T_EQ(wu_queued(l.own[order[k]]), 0);            /* it left its own event's queue too */
        for (unsigned j = k + 1; j < 3; j++) T_EQ(wu_load(&w[order[j]].done), 0);
    }
    T_TRUE(shared->waiters == NULL);
    if (!wu_finish(w, N, "threads leaving a queue")) return;
    T_EQ(rt_event_set(CTX, l.shared), TRUE);            /* nobody waits: it stays set */
    T_EQ(wu_state(l.shared).set, true);
#undef JOIN
#undef LEAVE
}

TEST(core, wait_signal_passes_over_a_wait_that_cannot_finish)
{
    /* Queue on event E: first a wait-all that also needs an empty semaphore, then a plain
     * wait.  Setting E cannot finish the first, so the second gets it; the first stays queued
     * and takes E and the semaphore together later. */
    static Worker w[2];
    HANDLE e = ev_auto(false), s = sem(0);
    QWait both = { { e, s }, 2, true }, one = { { e, NULL }, 1, false };
    wu_start_one(&w[0], 0, q_wait, &both);
    T_REQUIRE(EVENTUALLY(wu_queued(e) == 1 && wu_queued(s) == 1));
    wu_start_one(&w[1], 1, q_wait, &one);
    T_REQUIRE(EVENTUALLY(wu_queued(e) == 2));

    T_EQ(rt_event_set(CTX, e), TRUE);
    T_REQUIRE(EVENTUALLY(wu_load(&w[1].done)));
    T_EQ(wu_load(&w[0].done), 0);
    T_EQ(wu_state(e).set, false);                       /* taken by the second */
    T_EQ(wu_queued(e), 1);

    /* The semaphore alone does not finish the wait-all, and its unit is not taken. */
    T_EQ(rt_semaphore_release(CTX, s, 1, NULL), TRUE);
    wu_settle();
    T_EQ(wu_load(&w[0].done), 0);
    T_EQ(wu_state(s).count, 1);
    T_EQ(wu_queued(s), 1);

    T_EQ(rt_event_set(CTX, e), TRUE);                   /* now both: taken in one step */
    if (!wu_finish(w, 2, "wait-all behind a plain wait")) return;
    T_EQ(w[0].result, WAIT_OBJECT_0);
    T_EQ(w[1].result, WAIT_OBJECT_0);
    T_EQ(wu_state(e).set, false);
    T_EQ(wu_state(s).count, 0);
    T_EQ(wu_queued(e), 0);
    T_EQ(wu_queued(s), 0);
}

/* ---- the dispatcher lock and rt_wait_locked ----------------------------------------------------- */

TEST(core, wait_locked_entry_releases_the_lock)
{
    HANDLE e = ev_auto(true);
    RtObject *o = wu_obj(e);
    T_EQ(rt_dispatcher_held(), false);
    rt_dispatcher_lock();
    T_EQ(rt_dispatcher_held(), true);
    T_EQ(rt_wait_queue_length(o), 0);                   /* both usable with the lock held */
    RtSyncState st;
    rt_sync_query(o, &st);
    T_EQ(st.set, true);
    T_EQ(rt_dispatcher_held(), true);                   /* and they leave it held */
    rt_dispatcher_unlock();
    T_EQ(rt_dispatcher_held(), false);

    /* Satisfied at once, timed out at once, timed out after sleeping: the lock is released
     * in each case. */
    rt_dispatcher_lock();
    T_EQ(rt_wait_locked(CTX, 1, &o, false, INFINITE, false), WAIT_OBJECT_0);
    T_EQ(rt_dispatcher_held(), false);
    rt_dispatcher_lock();
    T_EQ(rt_wait_locked(CTX, 1, &o, false, 0, false), WAIT_TIMEOUT);
    T_EQ(rt_dispatcher_held(), false);
    rt_dispatcher_lock();
    T_EQ(rt_wait_locked(CTX, 1, &o, true, 10, false), WAIT_TIMEOUT);
    T_EQ(rt_dispatcher_held(), false);

    /* No objects: only the timeout can end it. */
    rt_dispatcher_lock();
    T_EQ(rt_wait_locked(CTX, 0, NULL, false, 0, false), WAIT_TIMEOUT);
    T_EQ(rt_dispatcher_held(), false);
    rt_dispatcher_lock();
    uint64_t t0 = rt_time_mono_100ns();
    T_EQ(rt_wait_locked(CTX, 0, NULL, false, 12, true), WAIT_TIMEOUT);
    T_TRUE(rt_time_mono_100ns() - t0 >= 12 * 10000);
    T_EQ(rt_dispatcher_held(), false);
}

/* ---- misuse is loud ----------------------------------------------------------------------------- */

static void bad_consume(RtObject *o, struct RtThread *t) { (void)t; rt_signal(o); }
static bool always(RtObject *o, struct RtThread *t) { (void)o; (void)t; return true; }
static bool relock(RtObject *o, struct RtThread *t) { (void)o; (void)t; rt_dispatcher_lock(); return true; }
static const RtObjectOps bad_consume_ops = { .name = "test-bad-consume", .is_signaled = always, .consume = bad_consume };
static const RtObjectOps relock_ops = { .name = "test-relock", .is_signaled = relock };

TEST(core, wait_misuse_is_fatal)
{
    HANDLE e = ev_auto(false), m = mtx();
    RtObject *eo = wu_obj(e), *mo = wu_obj(m);
    RtObject *with_null[2] = { eo, NULL };
    RtObject *plain = wu_new_plain();
    RtSyncState st;

    /* The lock is not recursive, and it knows who holds it. */
    T_TRAPS((rt_dispatcher_lock(), rt_dispatcher_lock()), "already holds the dispatcher lock");
    T_TRAPS(rt_dispatcher_unlock(), "does not hold the dispatcher lock");
    T_TRAPS(rt_signal(eo), "rt_signal: called without the dispatcher lock");
    T_TRAPS((rt_dispatcher_lock(), rt_signal(NULL)), "rt_signal: NULL object");
    T_TRAPS(rt_wait_locked(CTX, 1, &eo, false, 0, false), "rt_wait_locked: called without the dispatcher lock");
    T_TRAPS((rt_dispatcher_lock(), rt_wait_locked(CTX, MAXIMUM_WAIT_OBJECTS + 1, &eo, false, 0, false)),
            "65 objects; the limit is 64");
    T_TRAPS((rt_dispatcher_lock(), rt_wait(CTX, 1, &eo, false, 0, false)), "already holds the dispatcher lock");
    T_TRAPS((rt_dispatcher_lock(), rt_event_set(CTX, e)), "already holds the dispatcher lock");

    /* Arguments that only a bug in core code can produce. */
    T_TRAPS(rt_wait(CTX, 2, with_null, false, 0, false), "rt_wait: object 1 of 2 is NULL");
    T_TRAPS(rt_wait(CTX, 1, NULL, false, 0, false), "rt_wait: NULL object array with n = 1");
    T_TRAPS(rt_wait(NULL, 1, &eo, false, 0, false), "rt_wait: no ctx");
    T_TRAPS(rt_wait_queue_length(NULL), "rt_wait_queue_length: NULL object");
    T_TRAPS(rt_wait_report_abandoned(), "rt_wait_report_abandoned: called outside a consume operation");
    T_TRAPS((rt_dispatcher_lock(), rt_wait_report_abandoned()), "called outside a consume operation");

    /* Operations of a kind that break the rules. */
    RtObject *bc = rt_object_new(RT_K_PIPE, sizeof *bc, &bad_consume_ops, NULL, NULL);
    RtObject *rl = rt_object_new(RT_K_PIPE, sizeof *rl, &relock_ops, NULL, NULL);
    T_REQUIRE(bc && rl);
    T_TRAPS(rt_wait(CTX, 1, &bc, false, 0, false), "rt_signal: called from a consume operation");
    T_TRAPS(rt_wait(CTX, 1, &rl, false, 0, false), "already holds the dispatcher lock");

    /* The object-level calls of rt/sync.h check the kind. */
    T_TRAPS(rt_event_set_object(mo), "is a mutex, not an event");
    T_TRAPS(rt_event_reset_object(plain), "is a test-plain, not an event");
    T_TRAPS(rt_event_set_object(NULL), "rt_event_set_object: NULL object");
    T_TRAPS(rt_sync_query(plain, &st), "is a test-plain, not an event, mutex or semaphore");
    T_TRAPS(rt_sync_query(NULL, &st), "rt_sync_query: NULL object");
    T_TRAPS(rt_sync_query(eo, NULL), "rt_sync_query: NULL result");
    T_TRAPS(rt_event_set(NULL, e), "rt_event_set: no ctx");
    T_TRAPS(rt_sleep_ex(NULL, 0, FALSE), "no ctx");

    /* None of that happened in this process. */
    T_EQ(rt_dispatcher_held(), false);
    T_EQ(poll1(e), WAIT_TIMEOUT);
    rt_object_unref(plain);
    rt_object_unref(bc);
    rt_object_unref(rl);
}
