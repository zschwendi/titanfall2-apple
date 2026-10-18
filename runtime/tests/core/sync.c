/* Events, mutexes and semaphores through their handles (docs/RUNTIME.md section 4.2; rt/sync.h):
 * what each call does, what it returns and every documented failure, as the shims of the
 * family will see them.  Names and concurrent creation are in sync_names.c,
 * SignalObjectAndWait and PulseEvent with threads in sync_signal.c, the wait core itself in
 * wait*.c.  Helpers: wait_util.c.
 */
#include "wait_util.c"

#define CTX        T_CTX()
#define ME         RT_THREAD(CTX)
#define SENTINEL   0xABCD1234u

static DWORD poll(HANDLE h) { return rt_wait_handle(CTX, h, 0, FALSE); }

/* Handles that name nothing: NULL, small integers, a made-up value, a closed handle, and the
 * all-ones value zero-extended (which is not the pseudo-handle). */
static unsigned bad_handles(HANDLE *out) {
    HANDLE closed = rt_event_create(CTX, FALSE, FALSE, NULL);
    rt_handle_close(closed);
    unsigned n = 0;
    out[n++] = NULL;
    out[n++] = (HANDLE)(uintptr_t)1;
    out[n++] = (HANDLE)(uintptr_t)4;
    out[n++] = (HANDLE)(uintptr_t)0x12345678;
    out[n++] = (HANDLE)(uintptr_t)0xFFFFFFFFu;
    out[n++] = closed;
    return n;
}

/* ==== events ====================================================================================== */

TEST(core, sync_event_states)
{
    /* The four ways to create one. */
    HANDLE auto_unset = rt_event_create(CTX, FALSE, FALSE, NULL), auto_set = rt_event_create(CTX, FALSE, TRUE, NULL);
    HANDLE man_unset = rt_event_create(CTX, TRUE, FALSE, NULL), man_set = rt_event_create(CTX, TRUE, TRUE, NULL);
    T_REQUIRE(auto_unset && auto_set && man_unset && man_set);
    RtSyncState s = wu_state(auto_unset);
    T_EQ(s.kind, RT_K_EVENT);
    T_EQ(s.manual_reset, false);
    T_EQ(s.set, false);
    T_EQ(s.waiters, 0);
    T_EQ(wu_state(auto_set).manual_reset, false);
    T_EQ(wu_state(auto_set).set, true);
    T_EQ(wu_state(man_unset).manual_reset, true);
    T_EQ(wu_state(man_unset).set, false);
    T_EQ(wu_state(man_set).manual_reset, true);
    T_EQ(wu_state(man_set).set, true);
    /* A BOOL is true when it is not zero. */
    HANDLE odd = rt_event_create(CTX, 2, 0x100, NULL);
    T_EQ(wu_state(odd).manual_reset, true);
    T_EQ(wu_state(odd).set, true);
    HANDLE neg = rt_event_create(CTX, -1, -1, NULL);
    T_EQ(wu_state(neg).manual_reset, true);
    T_EQ(wu_state(neg).set, true);

    /* Auto-reset: one wait takes it. */
    T_EQ(poll(auto_unset), WAIT_TIMEOUT);
    T_EQ(poll(auto_set), WAIT_OBJECT_0);
    T_EQ(poll(auto_set), WAIT_TIMEOUT);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_event_set(CTX, auto_unset), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);                     /* success leaves last error alone */
    T_EQ(rt_event_set(CTX, auto_unset), TRUE);          /* setting a set event changes nothing */
    T_EQ(wu_state(auto_unset).set, true);
    T_EQ(poll(auto_unset), WAIT_OBJECT_0);
    T_EQ(poll(auto_unset), WAIT_TIMEOUT);               /* two Sets, one wait satisfied */
    T_EQ(rt_event_set(CTX, auto_unset), TRUE);
    T_EQ(rt_event_reset(CTX, auto_unset), TRUE);
    T_EQ(poll(auto_unset), WAIT_TIMEOUT);

    /* Manual-reset: waits take nothing; only ResetEvent resets it. */
    T_EQ(poll(man_unset), WAIT_TIMEOUT);
    for (int i = 0; i < 3; i++) T_EQ(poll(man_set), WAIT_OBJECT_0);
    T_EQ(rt_wait_handle(CTX, man_set, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(man_set).set, true);
    T_EQ(rt_event_reset(CTX, man_set), TRUE);
    T_EQ(rt_event_reset(CTX, man_set), TRUE);           /* resetting a reset event changes nothing */
    T_EQ(poll(man_set), WAIT_TIMEOUT);
    T_EQ(rt_event_set(CTX, man_set), TRUE);
    T_EQ(poll(man_set), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    /* PulseEvent with nobody waiting leaves the event reset, whatever it was. */
    T_EQ(rt_event_pulse(CTX, auto_unset), TRUE);
    T_EQ(wu_state(auto_unset).set, false);
    T_EQ(rt_event_pulse(CTX, man_set), TRUE);
    T_EQ(wu_state(man_set).set, false);
    rt_event_set(CTX, auto_unset);
    T_EQ(rt_event_pulse(CTX, auto_unset), TRUE);
    T_EQ(wu_state(auto_unset).set, false);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    /* The same through the object, for core units that hold one. */
    RtObject *o = wu_obj(auto_unset);
    rt_event_set_object(o);
    T_EQ(wu_state(auto_unset).set, true);
    rt_event_set_object(o);
    rt_event_reset_object(o);
    T_EQ(wu_state(auto_unset).set, false);
    rt_event_reset_object(o);
    T_EQ(poll(auto_unset), WAIT_TIMEOUT);
    T_EQ(T_LAST_ERROR(), SENTINEL);

    /* Every handle is its own object, and closing one leaves the others. */
    T_TRUE(wu_obj(auto_unset) != wu_obj(auto_set));
    T_EQ(rt_handle_close(auto_set), TRUE);
    T_EQ(rt_event_set(CTX, auto_unset), TRUE);
    T_EQ(poll(auto_unset), WAIT_OBJECT_0);
}

TEST(core, sync_event_bad_handles)
{
    HANDLE bad[8];
    unsigned n = bad_handles(bad);
    HANDLE m = rt_mutex_create(CTX, TRUE, NULL), s = rt_semaphore_create(CTX, 1, 2, NULL);
    for (unsigned i = 0; i < n; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_event_set(CTX, bad[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_event_reset(CTX, bad[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_event_pulse(CTX, bad[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }
    /* A handle to another kind is refused the same way, and that object is not touched. */
    HANDLE other[2] = { m, s };
    for (int i = 0; i < 2; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_event_set(CTX, other[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_event_reset(CTX, other[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_event_pulse(CTX, other[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }
    T_EQ(wu_state(m).recursion, 1);
    T_EQ(wu_state(s).count, 1);
    /* No reference was kept on the way out of a failed call. */
    T_EQ(wu_obj(m)->refs, 1);
    T_EQ(wu_obj(s)->refs, 1);
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL);
    rt_event_set(CTX, e);
    rt_event_reset(CTX, e);
    rt_event_pulse(CTX, e);
    T_EQ(wu_obj(e)->refs, 1);                           /* nor by a successful one */
}

/* ==== mutexes ======================================================================================= */

static void try_release(Worker *w) {
    HANDLE *m = w->arg;
    rt_set_last_error(W_CTX(w), SENTINEL);
    w->result = (DWORD)rt_mutex_release(W_CTX(w), *m);
    w->n[0] = rt_get_last_error(W_CTX(w));
    w->n[1] = rt_wait_handle(W_CTX(w), *m, 0, FALSE);   /* and it cannot take it either */
}

TEST(core, sync_mutex_ownership)
{
    static Worker w;
    /* Created free. */
    HANDLE m = rt_mutex_create(CTX, FALSE, NULL);
    T_REQUIRE(m != NULL);
    RtSyncState s = wu_state(m);
    T_EQ(s.kind, RT_K_MUTEX);
    T_EQ(s.owned, false);
    T_EQ(s.owner_thread_id, 0);
    T_EQ(s.recursion, 0);
    T_EQ(s.abandoned, false);

    /* Releasing a mutex nobody owns fails. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_mutex_release(CTX, m), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(wu_state(m).owned, false);

    /* A wait makes the caller the owner; each further wait deepens it; each release undoes one. */
    T_EQ(poll(m), WAIT_OBJECT_0);
    T_EQ(wu_state(m).owner_thread_id, ME->id);
    T_EQ(wu_state(m).recursion, 1);
    T_EQ(rt_wait_handle(CTX, m, INFINITE, FALSE), WAIT_OBJECT_0);
    T_EQ(poll(m), WAIT_OBJECT_0);
    T_EQ(wu_state(m).recursion, 3);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_mutex_release(CTX, m), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(wu_state(m).recursion, 2);
    T_EQ(wu_state(m).owner_thread_id, ME->id);          /* still owned after one release of three */

    /* Another thread can neither release it nor take it while it is owned, at any depth. */
    wu_start_one(&w, 0, try_release, &m);
    if (!wu_finish(&w, 1, "a thread that does not own the mutex")) return;
    T_EQ(w.result, FALSE);
    T_EQ(w.n[0], ERROR_NOT_OWNER);
    T_EQ(w.n[1], WAIT_TIMEOUT);
    T_EQ(wu_state(m).recursion, 2);
    T_EQ(wu_state(m).owner_thread_id, ME->id);

    T_EQ(rt_mutex_release(CTX, m), TRUE);
    T_EQ(wu_state(m).recursion, 1);
    T_EQ(rt_mutex_release(CTX, m), TRUE);
    T_EQ(wu_state(m).owned, false);
    T_EQ(wu_state(m).owner_thread_id, 0);
    T_EQ(rt_mutex_release(CTX, m), FALSE);              /* one release too many */
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(wu_state(m).owned, false);
    T_EQ(wu_state(m).recursion, 0);

    /* Created owned: the creator owns it with depth 1, without any wait. */
    HANDLE owned = rt_mutex_create(CTX, TRUE, NULL);
    T_EQ(wu_state(owned).owner_thread_id, ME->id);
    T_EQ(wu_state(owned).recursion, 1);
    HANDLE owned2 = rt_mutex_create(CTX, 0x100, NULL);  /* any nonzero BOOL */
    T_EQ(wu_state(owned2).recursion, 1);
    wu_start_one(&w, 0, try_release, &owned);
    if (!wu_finish(&w, 1, "a thread that does not own the mutex")) return;
    T_EQ(w.result, FALSE);
    T_EQ(w.n[0], ERROR_NOT_OWNER);
    T_EQ(w.n[1], WAIT_TIMEOUT);
    T_EQ(poll(owned), WAIT_OBJECT_0);
    T_EQ(wu_state(owned).recursion, 2);
    T_EQ(rt_mutex_release(CTX, owned), TRUE);
    T_EQ(rt_mutex_release(CTX, owned), TRUE);
    T_EQ(rt_mutex_release(CTX, owned), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);

    /* Once it is free the other thread takes it: its own release then fails only because the
     * test's worker releases first and waits second. */
    wu_start_one(&w, 0, try_release, &owned);
    if (!wu_finish(&w, 1, "a thread that finds the mutex free")) return;
    T_EQ(w.result, FALSE);
    T_EQ(w.n[0], ERROR_NOT_OWNER);
    T_EQ(w.n[1], WAIT_OBJECT_0);
    T_EQ(wu_state(owned).owner_thread_id, w.t.id);
    T_EQ(poll(owned), WAIT_TIMEOUT);                    /* now the main thread is the one locked out */
    T_EQ(rt_mutex_release(CTX, owned), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    T_EQ(wu_obj(owned)->refs, 1);
}

TEST(core, sync_mutex_bad_handles)
{
    HANDLE bad[8];
    unsigned n = bad_handles(bad);
    for (unsigned i = 0; i < n; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_mutex_release(CTX, bad[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL), s = rt_semaphore_create(CTX, 0, 2, NULL);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_mutex_release(CTX, e), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);         /* not ERROR_NOT_OWNER: it is not a mutex at all */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_mutex_release(CTX, s), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(wu_state(e).set, false);
    T_EQ(wu_state(s).count, 0);
}

/* ==== semaphores ==================================================================================== */

TEST(core, sync_semaphore_create_limits)
{
    /* Valid: 0 <= initial <= maximum, maximum > 0. */
    static const struct { LONG initial, max; } ok[] = {
        { 0, 1 }, { 1, 1 }, { 0, 5 }, { 3, 5 }, { 5, 5 }, { 0, INT32_MAX },
        { INT32_MAX, INT32_MAX }, { INT32_MAX - 1, INT32_MAX },
    };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        rt_set_last_error(CTX, SENTINEL);
        HANDLE h = rt_semaphore_create(CTX, ok[i].initial, ok[i].max, NULL);
        T_TRUE(h != NULL);
        if (!h) continue;
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);            /* a fresh creation clears last error */
        RtSyncState s = wu_state(h);
        T_EQ(s.kind, RT_K_SEMAPHORE);
        T_EQ(s.count, ok[i].initial);
        T_EQ(s.maximum, ok[i].max);
        T_EQ(poll(h), ok[i].initial > 0 ? WAIT_OBJECT_0 : WAIT_TIMEOUT);
    }
    /* Invalid: each side of each limit. */
    static const struct { LONG initial, max; } bad[] = {
        { 0, 0 }, { 0, -1 }, { 0, INT32_MIN }, { -1, 1 }, { INT32_MIN, 1 }, { 2, 1 }, { 6, 5 },
        { INT32_MAX, INT32_MAX - 1 }, { 1, 0 }, { -1, -1 }, { -1, 0 }, { -2, -1 },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_TRUE(rt_semaphore_create(CTX, bad[i].initial, bad[i].max, NULL) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(CTX, SENTINEL);
        T_TRUE(rt_semaphore_create(CTX, bad[i].initial, bad[i].max, wu_w("sync.never-made")) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }
    rt_set_last_error(CTX, SENTINEL);
    T_TRUE(rt_semaphore_open(CTX, wu_w("sync.never-made")) == NULL);    /* and no object was left behind */
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
}

TEST(core, sync_semaphore_release)
{
    HANDLE s = rt_semaphore_create(CTX, 3, 5, NULL);
    LONG prev = -77;

    /* The release count must be above zero. */
    static const LONG nonpositive[] = { 0, -1, INT32_MIN };
    for (int i = 0; i < 3; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_semaphore_release(CTX, s, nonpositive[i], &prev), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(prev, -77);                                /* not written on failure */
        T_EQ(wu_state(s).count, 3);
    }

    /* Exact fit: 3 + 2 = 5, the maximum.  The previous count is reported. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_semaphore_release(CTX, s, 2, &prev), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(prev, 3);
    T_EQ(wu_state(s).count, 5);

    /* One past the maximum: refused, nothing changes, nothing is reported. */
    prev = -77;
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_semaphore_release(CTX, s, 1, &prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(prev, -77);
    T_EQ(wu_state(s).count, 5);
    T_EQ(rt_semaphore_release(CTX, s, INT32_MAX, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(wu_state(s).count, 5);

    /* Each satisfied wait takes one unit; at zero waits block. */
    for (int i = 5; i > 0; i--) {
        T_EQ(poll(s), WAIT_OBJECT_0);
        T_EQ(wu_state(s).count, i - 1);
    }
    T_EQ(poll(s), WAIT_TIMEOUT);
    T_EQ(rt_wait_handle(CTX, s, 10, FALSE), WAIT_TIMEOUT);
    T_EQ(wu_state(s).count, 0);

    /* From zero: the previous count is 0; NULL for the previous count is allowed. */
    prev = -77;
    T_EQ(rt_semaphore_release(CTX, s, 1, &prev), TRUE);
    T_EQ(prev, 0);
    T_EQ(rt_semaphore_release(CTX, s, 3, NULL), TRUE);
    T_EQ(wu_state(s).count, 4);
    T_EQ(rt_semaphore_release(CTX, s, 2, &prev), FALSE);        /* 4 + 2 > 5: all or nothing */
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(wu_state(s).count, 4);
    T_EQ(rt_semaphore_release(CTX, s, 1, &prev), TRUE);
    T_EQ(prev, 4);
    T_EQ(wu_state(s).maximum, 5);

    /* The largest counts: the sum is not allowed to wrap. */
    HANDLE big = rt_semaphore_create(CTX, 1, INT32_MAX, NULL);
    T_EQ(rt_semaphore_release(CTX, big, INT32_MAX, &prev), FALSE);      /* 1 + (2^31 - 1) would wrap */
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(wu_state(big).count, 1);
    T_EQ(rt_semaphore_release(CTX, big, INT32_MAX - 1, &prev), TRUE);   /* exactly to the maximum */
    T_EQ(prev, 1);
    T_EQ(wu_state(big).count, INT32_MAX);
    T_EQ(rt_semaphore_release(CTX, big, 1, &prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(poll(big), WAIT_OBJECT_0);
    T_EQ(wu_state(big).count, INT32_MAX - 1);

    /* A maximum of one. */
    HANDLE one = rt_semaphore_create(CTX, 0, 1, NULL);
    T_EQ(rt_semaphore_release(CTX, one, 2, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(rt_semaphore_release(CTX, one, 1, NULL), TRUE);
    T_EQ(rt_semaphore_release(CTX, one, 1, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TOO_MANY_POSTS);
    T_EQ(wu_state(one).count, 1);
    T_EQ(wu_obj(one)->refs, 1);
}

TEST(core, sync_semaphore_bad_handles)
{
    HANDLE bad[8];
    unsigned n = bad_handles(bad);
    LONG prev = -77;
    for (unsigned i = 0; i < n; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_semaphore_release(CTX, bad[i], 1, &prev), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }
    HANDLE e = rt_event_create(CTX, FALSE, FALSE, NULL), m = rt_mutex_create(CTX, TRUE, NULL);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_semaphore_release(CTX, e, 1, &prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_semaphore_release(CTX, m, 1, &prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(prev, -77);
    T_EQ(wu_state(e).set, false);
    T_EQ(wu_state(m).recursion, 1);
    /* A bad handle is reported before a bad count. */
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_semaphore_release(CTX, NULL, 0, &prev), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}

/* ==== waiting on handles ============================================================================ */

TEST(core, sync_wait_handle_failures)
{
    HANDLE bad[8];
    unsigned n = bad_handles(bad);
    HANDLE e = rt_event_create(CTX, FALSE, TRUE, NULL), s = rt_semaphore_create(CTX, 2, 2, NULL);
    HANDLE m = rt_mutex_create(CTX, FALSE, NULL);

    for (unsigned i = 0; i < n; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_wait_handle(CTX, bad[i], INFINITE, FALSE), WAIT_FAILED);        /* at once, even with no timeout */
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        /* In an array, at the front, in the middle and at the end: nothing is taken from the
         * good ones and no reference to them is kept. */
        for (unsigned pos = 0; pos < 3; pos++) {
            HANDLE h[3] = { e, s, m };
            h[pos] = bad[i];
            rt_set_last_error(CTX, SENTINEL);
            T_EQ(rt_wait_handles(CTX, 3, h, FALSE, INFINITE, FALSE), WAIT_FAILED);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
            rt_set_last_error(CTX, SENTINEL);
            T_EQ(rt_wait_handles(CTX, 3, h, TRUE, 0, TRUE), WAIT_FAILED);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        }
    }
    T_EQ(wu_state(e).set, true);
    T_EQ(wu_state(s).count, 2);
    T_EQ(wu_state(m).owned, false);
    T_EQ(wu_obj(e)->refs, 1);
    T_EQ(wu_obj(s)->refs, 1);
    T_EQ(wu_obj(m)->refs, 1);

    /* The count: 0 and 65 are refused before any handle is looked at (the array may even be
     * NULL), 64 is the most. */
    HANDLE many[MAXIMUM_WAIT_OBJECTS + 1];
    for (unsigned i = 0; i <= MAXIMUM_WAIT_OBJECTS; i++) many[i] = rt_event_create(CTX, TRUE, TRUE, NULL);
    static const DWORD counts[] = { 0, MAXIMUM_WAIT_OBJECTS + 1, 1000, 0xFFFFFFFFu };
    for (unsigned i = 0; i < 4; i++) {
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_wait_handles(CTX, counts[i], many, FALSE, 0, FALSE), WAIT_FAILED);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_wait_handles(CTX, counts[i], NULL, TRUE, INFINITE, FALSE), WAIT_FAILED);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(CTX, SENTINEL);
        T_EQ(rt_wait_handles(CTX, counts[i], bad, FALSE, 0, FALSE), WAIT_FAILED);      /* the count comes first */
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait_handles(CTX, MAXIMUM_WAIT_OBJECTS, many, TRUE, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_wait_handles(CTX, MAXIMUM_WAIT_OBJECTS, many + 1, FALSE, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    for (unsigned i = 0; i <= MAXIMUM_WAIT_OBJECTS; i++) T_EQ(wu_obj(many[i])->refs, 1);

    /* An object that is in the handle table but cannot be waited on. */
    HANDLE plain = rt_handle_new(wu_new_plain());
    T_REQUIRE(plain != NULL);
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait_handle(CTX, plain, 0, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    HANDLE with_plain[2] = { e, plain };
    rt_set_last_error(CTX, SENTINEL);
    T_EQ(rt_wait_handles(CTX, 2, with_plain, FALSE, 0, FALSE), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(wu_state(e).set, true);                        /* the signalled event before it was not taken */
    T_EQ(wu_obj(plain)->refs, 1);

    /* A NULL array with a count in range is not a failure Windows returns: it faults. */
    T_TRAPS(rt_wait_handles(CTX, 2, NULL, FALSE, 0, FALSE), "a wait on 2 handles with a NULL handle array");
    T_TRAPS(rt_wait_handle(NULL, e, 0, FALSE), "rt_wait_handles: no ctx");

    /* The low two bits of a handle are ignored, as everywhere. */
    T_EQ(rt_wait_handle(CTX, (HANDLE)((uintptr_t)e | 3), 0, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(e).set, false);
}

/* ---- a handle closed while a thread waits on it -------------------------------------------------- */

typedef struct { HANDLE h; } Closing;

static void wait_infinite(Worker *w) {
    Closing *c = w->arg;
    w->result = rt_wait_handle(W_CTX(w), c->h, INFINITE, FALSE);
}

static unsigned destroyed;
static bool counted_is_signaled(RtObject *o, struct RtThread *t) { (void)t; return ((WuFlag *)o)->signaled; }
static void counted_destroy(RtObject *o) { (void)o; __atomic_add_fetch(&destroyed, 1, __ATOMIC_SEQ_CST); }
static const RtObjectOps counted_ops = { .name = "test-counted", .destroy = counted_destroy,
                                         .is_signaled = counted_is_signaled };

TEST(core, sync_handle_closed_while_waiting)
{
    /* The waiter holds a reference for the length of its wait, so closing the handle under it
     * neither frees the object nor disturbs the wait; the object dies when the wait is over. */
    static Worker w[2];
    WuFlag *f = (WuFlag *)rt_object_new(RT_K_THREAD, sizeof *f, &counted_ops, NULL, NULL);
    T_REQUIRE(f != NULL);
    rt_object_ref(&f->obj);                             /* the test's own, to signal it with */
    Closing c = { rt_handle_new(&f->obj) };
    T_REQUIRE(c.h != NULL);
    wu_start(w, 2, wait_infinite, &c);
    T_REQUIRE(EVENTUALLY(rt_wait_queue_length(&f->obj) == 2));
    T_EQ(f->obj.refs, 4);                               /* the handle, the test, two waiters */

    T_EQ(rt_handle_close(c.h), TRUE);
    T_EQ(f->obj.refs, 3);
    T_EQ(destroyed, 0);
    T_EQ(rt_wait_handle(CTX, c.h, 0, FALSE), WAIT_FAILED);      /* the handle is gone for everyone else */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    wu_settle();
    T_EQ(wu_done_count(w, 2), 0);                       /* the waits go on */
    T_EQ(rt_wait_queue_length(&f->obj), 2);

    wu_flag_signal(f);
    if (!wu_finish(w, 2, "waiters on a closed handle")) return;
    T_EQ(w[0].result, WAIT_OBJECT_0);
    T_EQ(w[1].result, WAIT_OBJECT_0);
    T_EQ(f->obj.refs, 1);                               /* each waiter gave its reference back */
    T_EQ(destroyed, 0);
    rt_object_unref(&f->obj);
    T_EQ(destroyed, 1);
}
