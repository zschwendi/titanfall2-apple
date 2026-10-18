/* Thread exit and abandoned mutexes (docs/RUNTIME.md sections 4.2 and 6.3; rt/sync.h:
 * rt_wait_thread_exit).
 *
 * The thread unit does not exist yet, so a thread "exits" here when a worker calls
 * rt_wait_thread_exit for its own hand-made thread, which is the one call the thread unit
 * will make.  Helpers: wait_util.c.
 */
#include "wait_util.c"

#define CTX        T_CTX()
#define ME         RT_THREAD(CTX)
#define SENTINEL   0xABCD1234u

static HANDLE mtx(void) { return rt_mutex_create(CTX, FALSE, NULL); }

/* Acquire each of up to four mutexes `depth[i]` times and exit owning them. */
typedef struct { HANDLE h[4]; unsigned depth[4]; unsigned n; } Own;

static void own_and_exit(Worker *w) {
    Own *o = w->arg;
    for (unsigned i = 0; i < o->n; i++)
        for (unsigned d = 0; d < o->depth[i]; d++)
            W_CHECK(w, rt_wait_handle(W_CTX(w), o->h[i], INFINITE, FALSE) == WAIT_OBJECT_0);
    rt_wait_thread_exit(&w->t);
}

static bool exit_with(Worker *w, Own *o) {
    wu_start_one(w, 0, own_and_exit, o);
    return wu_finish(w, 1, "a thread that exits owning mutexes");
}

/* ---- WAIT_ABANDONED and what it does to the mutex ------------------------------------------------ */

TEST(core, wait_exit_abandons_owned_mutexes)
{
    static Worker w;
    HANDLE m1 = mtx(), m2 = mtx(), mine = rt_mutex_create(CTX, TRUE, NULL), untouched = mtx();
    HANDLE e = rt_event_create(CTX, FALSE, TRUE, NULL);
    Own o = { { m1, m2 }, { 1, 3 }, 2 };
    if (!exit_with(&w, &o)) return;
    T_TRUE(T_LOGGED(RT_CH_THREAD, "exited owning 2 mutexes: abandoned"));

    /* Both are unowned now, whatever the depth was, and marked. */
    RtSyncState s1 = wu_state(m1), s2 = wu_state(m2);
    T_EQ(s1.owned, false);
    T_EQ(s1.recursion, 0);
    T_EQ(s1.abandoned, true);
    T_EQ(s2.owned, false);
    T_EQ(s2.recursion, 0);
    T_EQ(s2.abandoned, true);
    /* Nothing else is touched: not the main thread's mutex, not a free one, not an event. */
    T_EQ(wu_state(mine).owner_thread_id, ME->id);
    T_EQ(wu_state(mine).abandoned, false);
    T_EQ(wu_state(untouched).abandoned, false);
    T_EQ(wu_state(e).set, true);

    /* The wait that takes it says so, and the caller owns it like after any wait. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait_handle(CTX, m1, 0, FALSE), WAIT_ABANDONED_0);
    T_EQ(WAIT_ABANDONED_0, 0x80);
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* it is a success */
    s1 = wu_state(m1);
    T_EQ(s1.owner_thread_id, ME->id);
    T_EQ(s1.recursion, 1);
    T_EQ(s1.abandoned, false);                          /* reported once */
    T_EQ(rt_wait_handle(CTX, m1, INFINITE, FALSE), WAIT_OBJECT_0);      /* the owner's next wait: plain recursion */
    T_EQ(wu_state(m1).recursion, 2);
    T_EQ(rt_mutex_release(CTX, m1), TRUE);
    T_EQ(rt_mutex_release(CTX, m1), TRUE);
    T_EQ(wu_state(m1).owned, false);
    T_EQ(rt_wait_handle(CTX, m1, 0, FALSE), WAIT_OBJECT_0);             /* and it is an ordinary mutex again */
    T_EQ(rt_mutex_release(CTX, m1), TRUE);

    /* In a wait-any the index is added; a lower object that is not signalled does not matter,
     * and a lower one that is signalled wins as always. */
    HANDLE unset = rt_event_create(CTX, FALSE, FALSE, NULL);
    HANDLE any[4] = { unset, e, unset, m2 };
    T_EQ(rt_wait_handles(CTX, 4, any, FALSE, 0, FALSE), WAIT_OBJECT_0 + 1);     /* the event; the mutex is not taken */
    T_EQ(wu_state(m2).abandoned, true);
    T_EQ(rt_wait_handles(CTX, 4, any, FALSE, 0, FALSE), WAIT_ABANDONED_0 + 3);
    T_EQ(wu_state(m2).owner_thread_id, ME->id);
    T_EQ(wu_state(m2).abandoned, false);
    T_EQ(rt_mutex_release(CTX, m2), TRUE);
    T_EQ(rt_wait_handles(CTX, 4, any, FALSE, 0, FALSE), WAIT_OBJECT_0 + 3);     /* once only */
    T_EQ(rt_mutex_release(CTX, m2), TRUE);
    T_EQ(rt_mutex_release(CTX, m2), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
}

TEST(core, wait_exit_abandoned_in_wait_all)
{
    static Worker w;
    HANDLE a = mtx(), b = mtx(), normal = mtx();
    HANDLE manual = rt_event_create(CTX, TRUE, TRUE, NULL), unset = rt_event_create(CTX, FALSE, FALSE, NULL);
    Own o = { { a, b }, { 2, 1 }, 2 };
    if (!exit_with(&w, &o)) return;

    /* A wait-all that cannot finish takes nothing, the abandoned mark included. */
    HANDLE stuck[3] = { a, unset, b };
    T_EQ(rt_wait_handles(CTX, 3, stuck, TRUE, 0, FALSE), WAIT_TIMEOUT);
    T_EQ(rt_wait_handles(CTX, 3, stuck, TRUE, 10, FALSE), WAIT_TIMEOUT);
    T_EQ(wu_state(a).abandoned, true);
    T_EQ(wu_state(a).owned, false);
    T_EQ(wu_state(b).abandoned, true);

    /* One that can: WAIT_ABANDONED_0 with no index, wherever the abandoned mutexes stand. */
    HANDLE all[4] = { manual, normal, a, b };
    T_EQ(rt_wait_handles(CTX, 4, all, TRUE, INFINITE, FALSE), WAIT_ABANDONED_0);
    T_EQ(wu_state(a).owner_thread_id, ME->id);
    T_EQ(wu_state(b).owner_thread_id, ME->id);
    T_EQ(wu_state(normal).owner_thread_id, ME->id);
    T_EQ(wu_state(a).abandoned, false);
    T_EQ(wu_state(b).abandoned, false);
    T_EQ(rt_wait_handles(CTX, 4, all, TRUE, 0, FALSE), WAIT_OBJECT_0);          /* all owned by the caller: plain */
    for (int i = 1; i < 4; i++) {
        T_EQ(rt_mutex_release(CTX, all[i]), TRUE);
        T_EQ(rt_mutex_release(CTX, all[i]), TRUE);
        T_EQ(wu_state(all[i]).owned, false);
    }

    /* Only one of several is abandoned, and it is the last: still WAIT_ABANDONED_0. */
    Own o2 = { { b }, { 1 }, 1 };
    if (!exit_with(&w, &o2)) return;
    T_TRUE(T_LOGGED(RT_CH_THREAD, "exited owning 1 mutex: abandoned"));
    HANDLE last[3] = { a, normal, b };
    T_EQ(rt_wait_handles(CTX, 3, last, TRUE, 0, FALSE), WAIT_ABANDONED_0);
    HANDLE again[3] = { a, normal, b };
    T_EQ(rt_wait_handles(CTX, 3, again, TRUE, 0, FALSE), WAIT_OBJECT_0);
}

/* ---- a thread queued on the mutex is woken by the owner's exit ---------------------------------- */

typedef struct { HANDLE m; int owns, go; } Hold;

static void hold_then_exit(Worker *w) {
    Hold *h = w->arg;
    W_CHECK(w, rt_wait_handle(W_CTX(w), h->m, INFINITE, FALSE) == WAIT_OBJECT_0);
    wu_store(&h->owns, 1);
    while (!wu_load(&h->go)) usleep(200);
    rt_wait_thread_exit(&w->t);
}

static void wait_then_release(Worker *w) {
    Hold *h = w->arg;
    w->result = rt_wait_handle(W_CTX(w), h->m, INFINITE, FALSE);
    w->n[0] = wu_state(h->m).owner_thread_id == w->t.id;
    while (wu_load(&h->go) < 2 + (int)w->index) usleep(200);
    W_CHECK(w, rt_mutex_release(W_CTX(w), h->m) == TRUE);
}

TEST(core, wait_exit_wakes_a_queued_waiter)
{
    static Worker owner, waiter[2];
    static Hold h;
    h.m = mtx();
    wu_start_one(&owner, 0, hold_then_exit, &h);
    T_REQUIRE(EVENTUALLY(wu_load(&h.owns)));
    wu_start_one(&waiter[0], 0, wait_then_release, &h);
    T_REQUIRE(EVENTUALLY(wu_queued(h.m) == 1));
    wu_start_one(&waiter[1], 1, wait_then_release, &h);
    T_REQUIRE(EVENTUALLY(wu_queued(h.m) == 2));
    T_EQ(rt_wait_handle(CTX, h.m, 0, FALSE), WAIT_TIMEOUT);

    /* The owner exits: the thread that has waited longest gets the mutex, abandoned; the
     * other keeps waiting. */
    wu_store(&h.go, 1);
    if (!wu_finish(&owner, 1, "owner")) return;
    T_REQUIRE(EVENTUALLY(wu_queued(h.m) == 1));
    T_REQUIRE(EVENTUALLY(wu_state(h.m).owner_thread_id == waiter[0].t.id));
    T_EQ(wu_state(h.m).abandoned, false);               /* the mark went with the wait that reports it */
    wu_settle();
    T_EQ(wu_load(&waiter[1].done), 0);

    /* It releases: the second one gets an ordinary mutex. */
    wu_store(&h.go, 2);
    if (!wu_finish(&waiter[0], 1, "first waiter")) return;
    T_EQ(waiter[0].result, WAIT_ABANDONED_0);
    T_EQ(waiter[0].n[0], 1);
    T_REQUIRE(EVENTUALLY(wu_state(h.m).owner_thread_id == waiter[1].t.id));
    wu_store(&h.go, 3);
    if (!wu_finish(&waiter[1], 1, "second waiter")) return;
    T_EQ(waiter[1].result, WAIT_OBJECT_0);
    T_EQ(wu_state(h.m).owned, false);
    T_EQ(wu_state(h.m).abandoned, false);
}

/* ---- ownership that never came from a wait -------------------------------------------------------- */

typedef struct { HANDLE made; } Maker;

static void create_owned_and_exit(Worker *w) {
    Maker *m = w->arg;
    m->made = rt_mutex_create(W_CTX(w), TRUE, NULL);    /* the thread has never waited: it has no wait record */
    W_CHECK(w, m->made != NULL);
    W_CHECK(w, w->t.sub[RT_SUB_WAIT] == NULL);
    rt_wait_thread_exit(&w->t);
}

TEST(core, wait_exit_abandons_initial_ownership)
{
    static Worker w;
    Maker m = { NULL };
    wu_start_one(&w, 0, create_owned_and_exit, &m);
    if (!wu_finish(&w, 1, "a thread that only creates")) return;
    T_REQUIRE(m.made != NULL);
    T_EQ(wu_state(m.made).owned, false);
    T_EQ(wu_state(m.made).abandoned, true);
    /* Releasing it is still refused: nobody owns it.  (Windows may answer TRUE here; see the
     * UNSURE note in core/sync.c.) */
    T_EQ(rt_mutex_release(CTX, m.made), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(wu_state(m.made).abandoned, true);
    /* SignalObjectAndWait reports an abandoned mutex like every other wait. */
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL);
    T_EQ(rt_signal_and_wait(CTX, e, m.made, INFINITE, FALSE), WAIT_ABANDONED_0);
    T_EQ(wu_state(e).set, true);
    T_EQ(wu_state(m.made).owner_thread_id, ME->id);
    T_EQ(rt_mutex_release(CTX, m.made), TRUE);
}

/* ---- a mutex that dies while it is owned ----------------------------------------------------------- */

typedef struct { HANDLE m; int owns, go; bool unref_under_lock; } Dying;

static void own_until_told(Worker *w) {
    Dying *d = w->arg;
    W_CHECK(w, rt_wait_handle(W_CTX(w), d->m, INFINITE, FALSE) == WAIT_OBJECT_0);
    if (d->unref_under_lock) {
        /* This thread drops the last reference itself, holding the dispatcher lock. */
        RtObject *o = rt_handle_ref(d->m, RT_K_MUTEX);
        W_CHECK(w, o != NULL && rt_handle_close(d->m) == TRUE);
        rt_dispatcher_lock();
        rt_object_unref(o);
        rt_dispatcher_unlock();
    }
    wu_store(&d->owns, 1);
    while (!wu_load(&d->go)) usleep(200);
    rt_wait_thread_exit(&w->t);
}

TEST(core, wait_exit_after_an_owned_mutex_died)
{
    static Worker w;
    /* The last handle to a mutex is closed while a thread owns it: the mutex is gone, as on
     * Windows.  When the owner exits later there is nothing left of it to abandon. */
    static Dying d;
    d = (Dying){ mtx(), 0, 0, false };
    wu_start_one(&w, 0, own_until_told, &d);
    T_REQUIRE(EVENTUALLY(wu_load(&d.owns)));
    T_EQ(rt_handle_close(d.m), TRUE);
    T_TRUE(rt_handle_ref(d.m, RT_K_ANY) == NULL);
    wu_store(&d.go, 1);
    if (!wu_finish(&w, 1, "owner of a dead mutex")) return;
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "exited owning"));

    /* The same when the last reference is dropped with the dispatcher lock held. */
    d = (Dying){ mtx(), 0, 0, true };
    wu_start_one(&w, 0, own_until_told, &d);
    T_REQUIRE(EVENTUALLY(wu_load(&d.owns)));
    wu_store(&d.go, 1);
    if (!wu_finish(&w, 1, "owner of a mutex it freed under the lock")) return;
    T_TRUE(!T_LOGGED(RT_CH_THREAD, "exited owning"));
    T_EQ(rt_dispatcher_held(), false);

    /* The other order: the owner exits first, the abandoned mutex is closed afterwards. */
    HANDLE m = mtx();
    Own o = { { m }, { 2 }, 1 };
    wu_start_one(&w, 0, own_and_exit, &o);
    if (!wu_finish(&w, 1, "owner")) return;
    T_EQ(wu_state(m).abandoned, true);
    T_EQ(rt_handle_close(m), TRUE);

    /* A named mutex that dies owned frees its name: the next CreateMutex makes a new, free one. */
    const WCHAR *name = wu_w("wait_exit.dies-owned");
    HANDLE n1 = rt_mutex_create(CTX, TRUE, name);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    T_EQ(wu_state(n1).owner_thread_id, ME->id);
    T_EQ(rt_handle_close(n1), TRUE);
    HANDLE n2 = rt_mutex_create(CTX, FALSE, name);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);                /* not ERROR_ALREADY_EXISTS */
    T_EQ(wu_state(n2).owned, false);
    T_EQ(wu_state(n2).abandoned, false);
    T_EQ(rt_mutex_release(CTX, n2), FALSE);             /* the main thread owns nothing of that name any more */
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    /* Mutexes owned by threads that are still alive are untouched by all of this. */
    HANDLE keep = rt_mutex_create(CTX, TRUE, NULL);
    Own none = { { NULL }, { 0 }, 0 };
    wu_start_one(&w, 0, own_and_exit, &none);
    if (!wu_finish(&w, 1, "a thread that owns nothing")) return;
    T_EQ(wu_state(keep).owner_thread_id, ME->id);
    T_EQ(wu_state(keep).recursion, 1);
    T_EQ(wu_state(keep).abandoned, false);
}

/* ---- the rules of the call itself ------------------------------------------------------------------ */

static void wait_for_ever(Worker *w) {
    HANDLE *h = w->arg;
    w->result = rt_wait_handle(W_CTX(w), *h, INFINITE, FALSE);
}

TEST(core, wait_exit_rules)
{
    static Worker a, b, c;
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL);
    RtObject *eo = wu_obj(e);

    /* A thread that never waited and owns nothing may exit; it logs nothing. */
    wu_make_thread(&a, 0);
    T_TRUE(a.t.sub[RT_SUB_WAIT] == NULL);
    rt_wait_thread_exit(&a.t);
    T_TRUE(a.t.sub[RT_SUB_WAIT] != NULL);               /* marked as exited */
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), 0);

    /* A thread's wait record is made by its first wait and freed by its exit. */
    wu_make_thread(&b, 1);
    T_EQ(rt_wait(W_CTX(&b), 1, &eo, false, 0, false), WAIT_TIMEOUT);
    void *record = b.t.sub[RT_SUB_WAIT];
    T_TRUE(record != NULL);
    T_EQ(rt_wait(W_CTX(&b), 1, &eo, false, 0, false), WAIT_TIMEOUT);
    T_TRUE(b.t.sub[RT_SUB_WAIT] == record);             /* the same one each time */
    rt_wait_thread_exit(&b.t);
    T_TRUE(b.t.sub[RT_SUB_WAIT] != record && b.t.sub[RT_SUB_WAIT] != NULL);
    T_TRUE(b.t.sub[RT_SUB_WAIT] == a.t.sub[RT_SUB_WAIT]);       /* the one mark every exited thread carries */

    /* After the exit: no wait, no sleep, no second exit; an APC is refused. */
    T_TRAPS(rt_wait(W_CTX(&b), 1, &eo, false, 0, false), "waits after rt_wait_thread_exit was called for it");
    T_TRAPS(rt_wait_handle(W_CTX(&a), e, 0, FALSE), "waits after rt_wait_thread_exit was called for it");
    T_TRAPS(rt_sleep_ex(W_CTX(&b), 0, FALSE), "waits after rt_wait_thread_exit was called for it");
    T_TRAPS(rt_wait_thread_exit(&b.t), "rt_wait_thread_exit: called twice for thread");
    T_TRAPS(rt_wait_thread_exit(&a.t), "rt_wait_thread_exit: called twice for thread");
    T_TRAPS(rt_wait_thread_exit(NULL), "rt_wait_thread_exit: NULL thread");
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_apc_queue(CTX, &b.t, 0x1000, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_GEN_FAILURE);

    /* Operations that need no wait still work for such a thread (the thread unit calls the
     * exit hook when the thread's own code is over, but a shared object outlives it). */
    T_EQ(rt_event_set(W_CTX(&b), e), TRUE);
    T_EQ(rt_wait(CTX, 1, &eo, false, 0, false), WAIT_OBJECT_0);

    /* Exit of a thread that is inside a wait is not supported, and says so. */
    wu_start_one(&c, 2, wait_for_ever, &e);
    T_REQUIRE(EVENTUALLY(wu_queued(e) == 1));
    T_TRAPS(rt_wait_thread_exit(&c.t), "is inside a wait");
    T_EQ(rt_event_set(CTX, e), TRUE);
    if (!wu_finish(&c, 1, "waiter")) return;
    T_EQ(c.result, WAIT_OBJECT_0);
    rt_wait_thread_exit(&c.t);                          /* and once it is out of the wait, it may */
    T_EQ(wu_queued(e), 0);
}
