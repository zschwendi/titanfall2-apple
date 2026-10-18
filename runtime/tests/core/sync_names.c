/* Named events, mutexes and semaphores (docs/RUNTIME.md sections 3.4 and 4.1; rt/sync.h): the
 * Create and Open calls against the one name space all kinds share, the last error they
 * leave, and creation of one name by several threads at once.  The name rules themselves
 * (case, prefixes, the names that stop the run) are the handle unit's and are tested with
 * it (handle_names.c).  Helpers: wait_util.c.
 */
#include "wait_util.c"

#define CTX        T_CTX()
#define ME         RT_THREAD(CTX)
#define SENTINEL   0xABCD1234u

typedef enum { EV, MU, SE } Kind;

/* Create and Open of each kind, with parameters that differ from call to call so that a test
 * can see which call's parameters an object carries. */
static HANDLE create(Ctx *ctx, Kind k, const WCHAR *name, int variant) {
    switch (k) {
    case EV: return rt_event_create(ctx, variant & 1, (variant >> 1) & 1, name);
    case MU: return rt_mutex_create(ctx, variant & 1, name);
    default: return rt_semaphore_create(ctx, 1 + variant, 10 + variant, name);
    }
}
static HANDLE open_kind(Ctx *ctx, Kind k, const WCHAR *name) {
    switch (k) {
    case EV: return rt_event_open(ctx, name);
    case MU: return rt_mutex_open(ctx, name);
    default: return rt_semaphore_open(ctx, name);
    }
}

/* Does the object carry the parameters of create(..., variant) made by thread `maker`? */
static bool carries(HANDLE h, Kind k, int variant, uint32_t maker) {
    RtSyncState s = wu_state(h);
    switch (k) {
    case EV: return s.manual_reset == (bool)(variant & 1) && s.set == (bool)((variant >> 1) & 1);
    case MU: return variant & 1 ? s.owner_thread_id == maker && s.recursion == 1 : !s.owned;
    default: return s.count == 1 + variant && s.maximum == 10 + variant;
    }
}

TEST(core, sync_names_create_and_open)
{
    static const char *const base[3] = { "sync.event", "sync.mutex", "sync.semaphore" };
    static const uint32_t kinds[3] = { RT_K_EVENT, RT_K_MUTEX, RT_K_SEMAPHORE };
    for (int k = 0; k < 3; k++) {
        WCHAR name[64];                                 /* a copy: wu_w's buffers are reused */
        memcpy(name, wu_w(base[k]), sizeof name);
        char local[64], upper[64];
        snprintf(local, sizeof local, "Local\\%s", base[k]);
        snprintf(upper, sizeof upper, "SYNC%s", base[k] + 4);

        /* Nothing has the name yet. */
        rt_set_last_error(CTX, SENTINEL);
        T_TRUE(open_kind(CTX, k, name) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

        /* The first Create makes the object and clears last error. */
        rt_set_last_error(CTX, SENTINEL);
        HANDLE first = create(CTX, k, name, 3);
        T_REQUIRE(first != NULL);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        T_EQ(wu_state(first).kind, kinds[k]);
        T_TRUE(carries(first, k, 3, ME->id));

        /* A second Create returns the same object under a new handle, says so, and its
         * parameters are ignored (variant 0: the other event flags, no initial ownership,
         * other counts). */
        rt_set_last_error(CTX, SENTINEL);
        HANDLE second = create(CTX, k, name, 0);
        T_REQUIRE(second != NULL);
        T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
        T_TRUE(second != first);
        T_TRUE(wu_obj(second) == wu_obj(first));
        T_TRUE(carries(first, k, 3, ME->id));
        /* "Local\" names the same object. */
        rt_set_last_error(CTX, SENTINEL);
        HANDLE local_h = create(CTX, k, wu_w(local), 2);
        T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
        T_TRUE(local_h != NULL && wu_obj(local_h) == wu_obj(first));
        T_TRUE(carries(first, k, 3, ME->id));

        /* Open finds it too and leaves last error alone. */
        rt_set_last_error(CTX, SENTINEL);
        HANDLE opened = open_kind(CTX, k, name);
        T_REQUIRE(opened != NULL);
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_TRUE(wu_obj(opened) == wu_obj(first));
        T_EQ(wu_obj(first)->refs, 4);                   /* one per handle, nothing more */

        /* A Create of an unnamed object is always new and always clears last error, also
         * straight after an ERROR_ALREADY_EXISTS. */
        HANDLE again = create(CTX, k, name, 1);
        T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
        HANDLE unnamed = create(CTX, k, NULL, 0);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        T_TRUE(unnamed != NULL && wu_obj(unnamed) != wu_obj(first));
        T_EQ(rt_handle_close(again), TRUE);

        /* The other kinds can neither create nor open that name: ERROR_INVALID_HANDLE. */
        for (int other = 0; other < 3; other++) {
            if (other == k) continue;
            rt_set_last_error(CTX, SENTINEL);
            T_TRUE(create(CTX, other, name, 0) == NULL);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
            rt_set_last_error(CTX, SENTINEL);
            T_TRUE(create(CTX, other, wu_w(local), 1) == NULL);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
            rt_set_last_error(CTX, SENTINEL);
            T_TRUE(open_kind(CTX, other, name) == NULL);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        }
        T_EQ(wu_obj(first)->refs, 4);                   /* the failed calls kept nothing */
        T_TRUE(carries(first, k, 3, ME->id));           /* and changed nothing */

        /* Names are case-sensitive: another spelling is another object. */
        rt_set_last_error(CTX, SENTINEL);
        HANDLE cased = create(CTX, k, wu_w(upper), 0);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        T_TRUE(cased != NULL && wu_obj(cased) != wu_obj(first));
        T_TRUE(carries(cased, k, 0, ME->id));

        /* Open with no name. */
        rt_set_last_error(CTX, SENTINEL);
        T_TRUE(open_kind(CTX, k, NULL) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

        /* The object and its name live as long as any handle. */
        T_EQ(rt_handle_close(first), TRUE);
        T_EQ(rt_handle_close(second), TRUE);
        T_EQ(rt_handle_close(local_h), TRUE);
        T_TRUE(carries(opened, k, 3, ME->id));
        HANDLE reopened = open_kind(CTX, k, name);
        T_TRUE(reopened != NULL && wu_obj(reopened) == wu_obj(opened));
        T_EQ(rt_handle_close(reopened), TRUE);
        T_EQ(rt_handle_close(opened), TRUE);
        rt_set_last_error(CTX, SENTINEL);
        T_TRUE(open_kind(CTX, k, name) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

        /* The name is free again: Create makes a new object, with ITS parameters. */
        rt_set_last_error(CTX, SENTINEL);
        HANDLE fresh = create(CTX, k, name, 0);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        T_TRUE(fresh != NULL && carries(fresh, k, 0, ME->id));
        /* Now another kind may not take it, but once it is gone, it may. */
        T_EQ(rt_handle_close(fresh), TRUE);
        rt_set_last_error(CTX, SENTINEL);
        HANDLE taken = create(CTX, (k + 1) % 3, name, 0);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
        T_TRUE(taken != NULL);
        T_EQ(wu_state(taken).kind, kinds[(k + 1) % 3]);
        T_EQ(rt_handle_close(taken), TRUE);
    }
}

TEST(core, sync_names_share_state)
{
    /* Two handles to one named object: what is done through one is seen through the other. */
    HANDLE e1 = rt_event_create(CTX, TRUE, FALSE, wu_w("sync.shared-event"));
    HANDLE e2 = rt_event_open(CTX, wu_w("sync.shared-event"));
    T_REQUIRE(e1 && e2);
    T_EQ(rt_event_set(CTX, e2), TRUE);
    T_EQ(rt_wait_handle(CTX, e1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(rt_event_reset(CTX, e1), TRUE);
    T_EQ(rt_wait_handle(CTX, e2, 0, FALSE), WAIT_TIMEOUT);

    HANDLE s1 = rt_semaphore_create(CTX, 0, 3, wu_w("sync.shared-semaphore"));
    HANDLE s2 = rt_semaphore_create(CTX, 3, 3, wu_w("Local\\sync.shared-semaphore"));
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(wu_state(s2).count, 0);                        /* not the 3 of the second call */
    LONG prev = -1;
    T_EQ(rt_semaphore_release(CTX, s2, 2, &prev), TRUE);
    T_EQ(prev, 0);
    T_EQ(rt_wait_handle(CTX, s1, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(s2).count, 1);
    /* Bad counts are refused even though the name exists, and nothing changes. */
    rt_set_last_error(CTX, SENTINEL);
    T_TRUE(rt_semaphore_create(CTX, 5, 3, wu_w("sync.shared-semaphore")) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(wu_state(s1).count, 1);
    T_EQ(wu_state(s1).maximum, 3);

    /* A mutex: asking for initial ownership of one that exists gives none, to its owner (the
     * depth stays 1) or to anyone else. */
    HANDLE m1 = rt_mutex_create(CTX, TRUE, wu_w("sync.shared-mutex"));
    T_EQ(wu_state(m1).recursion, 1);
    HANDLE m2 = rt_mutex_create(CTX, TRUE, wu_w("sync.shared-mutex"));
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(wu_state(m2).recursion, 1);
    T_EQ(wu_state(m2).owner_thread_id, ME->id);
    T_EQ(rt_mutex_release(CTX, m2), TRUE);              /* owned through either handle */
    T_EQ(wu_state(m1).owned, false);
    HANDLE m3 = rt_mutex_create(CTX, TRUE, wu_w("sync.shared-mutex"));
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(wu_state(m3).owned, false);                    /* free, and the caller did not get it */
    T_EQ(rt_mutex_release(CTX, m3), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
}

/* ---- several threads create one name at once ---------------------------------------------------- */

enum { RACERS = 8, ROUNDS = 40 };

typedef struct {
    int      round;                 /* main: the round to run */
    unsigned arrived;               /* workers that have finished the round */
    int      stop;
    Kind     kind;
    WCHAR    name[32];
    HANDLE   got[RACERS];
    DWORD    err[RACERS];
    RtSyncState seen[RACERS];       /* the object as each thread found it straight after its Create */
} NameRace;

static void racer(Worker *w) {
    NameRace *r = w->arg;
    for (int round = 1; ; round++) {
        while (wu_load(&r->round) < round && !wu_load(&r->stop)) sched_yield();
        if (wu_load(&r->stop)) return;
        rt_set_last_error(W_CTX(w), SENTINEL);
        r->got[w->index] = create(W_CTX(w), r->kind, r->name, (int)w->index);
        r->err[w->index] = rt_get_last_error(W_CTX(w));
        if (r->got[w->index]) r->seen[w->index] = wu_state(r->got[w->index]);
        __atomic_add_fetch(&r->arrived, 1, __ATOMIC_SEQ_CST);
    }
}

TEST(core, sync_names_created_once_by_racing_threads)
{
    static Worker w[RACERS];
    static NameRace r;
    wu_start(w, RACERS, racer, &r);
    unsigned winners[RACERS] = { 0 };
    for (int round = 1; round <= ROUNDS; round++) {
        r.kind = (Kind)(round % 3);
        char name[32];
        snprintf(name, sizeof name, "sync.race.%d", round);
        memcpy(r.name, wu_w(name), sizeof r.name);
        __atomic_store_n(&r.arrived, 0, __ATOMIC_SEQ_CST);
        wu_store(&r.round, round);
        if (!EVENTUALLY(__atomic_load_n(&r.arrived, __ATOMIC_SEQ_CST) == RACERS)) {
            T_TRUE(!"the racing threads never finished a round");
            return;
        }
        /* Every thread got a handle to the one object; exactly one of them made it, and the
         * object carries that thread's parameters whole (its event flags, its ownership, its
         * counts), never a mixture and never a blank. */
        unsigned made = 0, existed = 0, winner = 0;
        for (unsigned i = 0; i < RACERS; i++) {
            T_TRUE(r.got[i] != NULL);
            if (!r.got[i]) return;
            T_TRUE(wu_obj(r.got[i]) == wu_obj(r.got[0]));
            if (r.err[i] == ERROR_SUCCESS) { made++; winner = i; }
            else if (r.err[i] == ERROR_ALREADY_EXISTS) existed++;
        }
        T_EQ(made, 1);
        T_EQ(existed, RACERS - 1);
        T_TRUE(carries(r.got[0], r.kind, (int)winner, w[winner].t.id));
        /* No thread ever saw it otherwise: an object that can be found is already complete. */
        RtSyncState now = wu_state(r.got[0]);
        for (unsigned i = 0; i < RACERS; i++) {
            T_EQ(r.seen[i].manual_reset, now.manual_reset);
            T_EQ(r.seen[i].set, now.set);
            T_EQ(r.seen[i].owner_thread_id, now.owner_thread_id);
            T_EQ(r.seen[i].recursion, now.recursion);
            T_EQ(r.seen[i].count, now.count);
            T_EQ(r.seen[i].maximum, now.maximum);
        }
        T_EQ(wu_obj(r.got[0])->refs, RACERS);
        winners[winner]++;
        /* Closing every handle frees the name, even while a racer still owns the mutex. */
        for (unsigned i = 0; i < RACERS; i++) T_EQ(rt_handle_close(r.got[i]), TRUE);
        T_TRUE(open_kind(CTX, r.kind, r.name) == NULL);
    }
    wu_store(&r.stop, 1);
    if (!wu_finish(w, RACERS, "racing creators")) return;
    unsigned total = 0;
    for (unsigned i = 0; i < RACERS; i++) total += winners[i];
    T_EQ(total, ROUNDS);
}
