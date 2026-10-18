/* The wait core under contention (docs/RUNTIME.md section 4.2): what must hold when several
 * threads wait and signal at once.
 *
 *   - an auto-reset event releases exactly one waiter per Set, a manual-reset event all;
 *   - a token passed round a ring of auto-reset events is never lost (a lost wake-up);
 *   - a semaphore used as a counted queue loses no unit and hands none out twice, with
 *     consumers that time out while units arrive and a maximum that producers run into;
 *   - a kernel mutex excludes, recursively;
 *   - wait-all is atomic: threads taking the same two mutexes in opposite orders do not
 *     deadlock, and a thread never holds one of its objects while it waits for another;
 *   - a wait whose timeout runs out as the signal arrives either took the object or did not,
 *     and says which.
 *
 * Threads and conventions: wait_util.c.  No test starts more than eight threads.
 */
#include "wait_util.c"

#define CTX  T_CTX()

static Worker workers[8];

/* ==== events ====================================================================================== */

typedef struct { HANDLE h; DWORD timeout; } OneWait;

static void wait_one(Worker *w) {
    OneWait *o = w->arg;
    w->result = rt_wait_handle(W_CTX(w), o->h, o->timeout, FALSE);
}

static unsigned count_result(const Worker *w, unsigned n, DWORD r) {
    unsigned c = 0;
    for (unsigned i = 0; i < n; i++) c += wu_load(&w[i].done) && w[i].result == r;
    return c;
}

TEST(core, wait_threads_auto_reset_releases_exactly_one)
{
    enum { N = 8 };
    OneWait o = { rt_event_create(CTX, FALSE, FALSE, NULL), INFINITE };
    wu_start(workers, N, wait_one, &o);
    T_REQUIRE(EVENTUALLY(wu_queued(o.h) == N));

    /* Three Sets one after the other, with eight threads queued: each releases one thread and
     * leaves the event reset, so three threads come back, not one (the Sets do not merge) and
     * not more. */
    for (int i = 0; i < 3; i++) {
        T_EQ(rt_event_set(CTX, o.h), TRUE);
        T_EQ(wu_state(o.h).set, false);
        T_EQ(wu_queued(o.h), N - 1 - (unsigned)i);
    }
    T_REQUIRE(EVENTUALLY(wu_done_count(workers, N) == 3));
    wu_settle();
    T_EQ(wu_done_count(workers, N), 3);

    /* One at a time for the rest. */
    for (unsigned k = 4; k <= N; k++) {
        T_EQ(rt_event_set(CTX, o.h), TRUE);
        T_REQUIRE(EVENTUALLY(wu_done_count(workers, N) == k));
        T_EQ(wu_queued(o.h), N - k);
    }
    wu_settle();
    T_EQ(wu_state(o.h).set, false);                     /* eight Sets, eight threads, nothing left over */
    if (!wu_finish(workers, N, "waiters on an auto-reset event")) return;
    T_EQ(count_result(workers, N, WAIT_OBJECT_0), N);

    /* With nobody waiting the event stays set until one wait consumes it, however often it
     * is set. */
    T_EQ(rt_event_set(CTX, o.h), TRUE);
    T_EQ(rt_event_set(CTX, o.h), TRUE);
    T_EQ(wu_state(o.h).set, true);
    wu_start(workers, 3, wait_one, &o);
    T_REQUIRE(EVENTUALLY(wu_done_count(workers, 3) == 1 && wu_queued(o.h) == 2));
    wu_settle();
    T_EQ(wu_done_count(workers, 3), 1);                 /* two Sets with nobody waiting were one */
    T_EQ(wu_state(o.h).set, false);
    rt_event_set(CTX, o.h);
    rt_event_set(CTX, o.h);
    if (!wu_finish(workers, 3, "late waiters")) return;
}

TEST(core, wait_threads_manual_reset_releases_all)
{
    enum { N = 8 };
    OneWait o = { rt_event_create(CTX, TRUE, FALSE, NULL), INFINITE };
    wu_start(workers, N, wait_one, &o);
    T_REQUIRE(EVENTUALLY(wu_queued(o.h) == N));
    wu_settle();
    T_EQ(wu_done_count(workers, N), 0);

    T_EQ(rt_event_set(CTX, o.h), TRUE);                 /* one Set */
    T_EQ(wu_queued(o.h), 0);                            /* everybody left the queue inside the call */
    if (!wu_finish(workers, N, "waiters on a manual-reset event")) return;
    T_EQ(count_result(workers, N, WAIT_OBJECT_0), N);
    T_EQ(wu_state(o.h).set, true);                      /* and it is still set */

    /* Later waits pass straight through until it is reset; then they wait again. */
    wu_start(workers, N, wait_one, &o);
    if (!wu_finish(workers, N, "waiters on a set manual-reset event")) return;
    T_EQ(rt_event_reset(CTX, o.h), TRUE);
    o.timeout = 40;
    wu_start(workers, 4, wait_one, &o);
    if (!wu_finish(workers, 4, "waiters on a reset manual-reset event")) return;
    T_EQ(count_result(workers, 4, WAIT_TIMEOUT), 4);
}

/* A token goes round a ring of auto-reset events.  Each thread waits for its own event and
 * sets the next one; the odd threads do both as one call (SignalObjectAndWait).  A wake-up
 * lost anywhere stops the ring for good.  The ring runs for a length of time, not a number
 * of hops (a hop is a thread switch, whose cost on a busy machine nobody knows), and for at
 * least `min` hops. */
typedef struct {
    HANDLE   ev[8];
    unsigned n;
    unsigned hops, min;             /* hops: touched only by the thread that has the token */
    int      finish;                /* the main thread: time is up */
    int      stop;
} Ring;

static void ring_thread(Worker *w) {
    Ring *r = w->arg;
    unsigned me = w->index, next = (me + 1) % r->n;
    DWORD got = rt_wait_handle(W_CTX(w), r->ev[me], INFINITE, FALSE);
    for (;;) {
        W_CHECK(w, got == WAIT_OBJECT_0);
        if (wu_load(&r->stop)) break;
        if (r->hops >= r->min && wu_load(&r->finish)) {         /* the token is here and the run is over */
            wu_store(&r->stop, 1);
            for (unsigned i = 0; i < r->n; i++) W_CHECK(w, rt_event_set(W_CTX(w), r->ev[i]) == TRUE);
            break;
        }
        r->hops++;
        w->n[0]++;
        if (me & 1) {
            got = rt_signal_and_wait(W_CTX(w), r->ev[next], r->ev[me], INFINITE, FALSE);
        } else {
            W_CHECK(w, rt_event_set(W_CTX(w), r->ev[next]) == TRUE);
            got = rt_wait_handle(W_CTX(w), r->ev[me], INFINITE, FALSE);
        }
    }
}

TEST(core, wait_threads_ring_loses_no_wakeup)
{
    static Ring r;
    r.n = 6;
    r.min = 600;
    for (unsigned i = 0; i < r.n; i++) r.ev[i] = rt_event_create(CTX, FALSE, FALSE, NULL);
    wu_start(workers, r.n, ring_thread, &r);
    T_EQ(rt_event_set(CTX, r.ev[0]), TRUE);             /* the token */
    usleep(WU_STRESS_US);
    wu_store(&r.finish, 1);
    if (!wu_finish(workers, r.n, "ring of events")) return;
    T_TRUE(r.hops >= r.min);
    T_EQ(wu_tally(workers, r.n, 0), r.hops);
    /* The token went round in order: thread i made every n-th hop, starting with hop i. */
    for (unsigned i = 0; i < r.n; i++) T_EQ(workers[i].n[0], r.hops / r.n + (i < r.hops % r.n));
    for (unsigned i = 0; i < r.n; i++) T_EQ(wu_queued(r.ev[i]), 0);
}

/* ==== a semaphore as a counted queue ================================================================ */

/* Producers put an item in a buffer and release one unit; consumers take a unit and then an
 * item.  The producers run for a length of time and at least Q_MIN items each. */
enum { Q_PRODUCERS = 3, Q_CONSUMERS = 4, Q_MIN = 300, Q_PER_PRODUCER = 20000, Q_ITEMS = Q_PRODUCERS * Q_PER_PRODUCER,
       Q_MAX = 8 };

typedef struct {
    HANDLE   sem, lock;             /* units = items in the buffer that nobody has claimed; a kernel mutex */
    /* Guarded by `lock`: plain memory. */
    uint32_t buf[Q_ITEMS];
    unsigned head, tail;
    uint8_t  seen[Q_ITEMS];
    int      inside;
    /* Atomics. */
    unsigned consumed, released, refused, producers_done;
    int      stop;
} Queue;

static void q_lock(Worker *w, Queue *q) {
    W_CHECK(w, rt_wait_handle(W_CTX(w), q->lock, INFINITE, FALSE) == WAIT_OBJECT_0);
    W_CHECK(w, __atomic_exchange_n(&q->inside, 1, __ATOMIC_SEQ_CST) == 0);
}
static void q_unlock(Worker *w, Queue *q) {
    W_CHECK(w, __atomic_exchange_n(&q->inside, 0, __ATOMIC_SEQ_CST) == 1);
    W_CHECK(w, rt_mutex_release(W_CTX(w), q->lock) == TRUE);
}

static void producer(Worker *w) {
    Queue *q = w->arg;
    for (unsigned i = 0; i < Q_PER_PRODUCER && !(i >= Q_MIN && wu_load(&q->stop)); i++) {
        q_lock(w, q);
        W_CHECK(w, q->tail < Q_ITEMS);
        q->buf[q->tail++] = w->index * Q_PER_PRODUCER + i;      /* the item first ... */
        q_unlock(w, q);
        for (;;) {                                              /* ... then its unit */
            LONG prev = -1;
            if (rt_semaphore_release(W_CTX(w), q->sem, 1, &prev)) {
                W_CHECK(w, prev >= 0 && prev < Q_MAX);
                __atomic_add_fetch(&q->released, 1, __ATOMIC_SEQ_CST);
                break;
            }
            /* Full: the release changed nothing and said why; try again. */
            W_CHECK(w, rt_get_last_error(W_CTX(w)) == ERROR_TOO_MANY_POSTS);
            W_CHECK(w, prev == -1);
            __atomic_add_fetch(&q->refused, 1, __ATOMIC_SEQ_CST);
            w->n[1]++;
            sched_yield();
        }
        w->n[0]++;
    }
    __atomic_add_fetch(&q->producers_done, 1, __ATOMIC_SEQ_CST);
}

static bool queue_drained(Queue *q) {
    return __atomic_load_n(&q->producers_done, __ATOMIC_SEQ_CST) == Q_PRODUCERS &&
           __atomic_load_n(&q->consumed, __ATOMIC_SEQ_CST) == __atomic_load_n(&q->released, __ATOMIC_SEQ_CST);
}

static void consumer(Worker *w) {
    Queue *q = w->arg;
    while (!queue_drained(q)) {
        /* Short timeouts, zero included, so that waits keep running out while units arrive. */
        DWORD r = rt_wait_handle(W_CTX(w), q->sem, (DWORD)(wu_rnd(w) % 3), FALSE);
        if (r == WAIT_TIMEOUT) { w->n[1]++; continue; }
        W_CHECK(w, r == WAIT_OBJECT_0);
        if (r != WAIT_OBJECT_0) return;
        /* A unit was taken for this thread, so an item is there for it. */
        q_lock(w, q);
        W_CHECK(w, q->head < q->tail);
        if (q->head < q->tail) {
            uint32_t item = q->buf[q->head++];
            W_CHECK(w, item < Q_ITEMS && q->seen[item] == 0);
            if (item < Q_ITEMS) q->seen[item]++;
        }
        q_unlock(w, q);
        w->n[0]++;
        __atomic_add_fetch(&q->consumed, 1, __ATOMIC_SEQ_CST);
    }
}

TEST(core, wait_threads_semaphore_is_a_counted_queue)
{
    static Queue q;
    q.sem = rt_semaphore_create(CTX, 0, Q_MAX, NULL);
    q.lock = rt_mutex_create(CTX, FALSE, NULL);
    T_REQUIRE(q.sem && q.lock);
    wu_start(workers, Q_PRODUCERS, producer, &q);
    wu_start(workers + Q_PRODUCERS, Q_CONSUMERS, consumer, &q);
    usleep(WU_STRESS_US);
    wu_store(&q.stop, 1);
    if (!wu_finish(workers, Q_PRODUCERS + Q_CONSUMERS, "producers and consumers")) return;

    /* Every unit released was taken by exactly one wait, and none is left. */
    unsigned items = q.released;
    T_TRUE(items >= Q_PRODUCERS * Q_MIN);
    T_EQ(wu_tally(workers, Q_PRODUCERS, 0), items);
    T_EQ(wu_tally(workers + Q_PRODUCERS, Q_CONSUMERS, 0), items);
    T_EQ(q.consumed, items);
    T_EQ(q.head, items);
    T_EQ(q.tail, items);
    unsigned once = 0, never = 0;
    for (unsigned i = 0; i < Q_ITEMS; i++) { once += q.seen[i] == 1; never += q.seen[i] == 0; }
    T_EQ(once, items);                                  /* each item made was taken once ... */
    T_EQ(never, Q_ITEMS - items);                       /* ... and nothing else was taken */
    RtSyncState s = wu_state(q.sem);
    T_EQ(s.count, 0);
    T_EQ(s.maximum, Q_MAX);
    T_EQ(s.waiters, 0);
    RtSyncState m = wu_state(q.lock);
    T_EQ(m.owned, false);
    T_EQ(m.waiters, 0);
    T_EQ(rt_wait_handle(CTX, q.sem, 0, FALSE), WAIT_TIMEOUT);
    /* Each refused release was counted and retried: the maximum held. */
    T_EQ(wu_tally(workers, Q_PRODUCERS, 1), q.refused);
}

/* ==== mutexes ======================================================================================= */

typedef struct {
    HANDLE   m;
    uint64_t counter;               /* guarded by m: plain memory */
    int      inside;
    int      stop;
    unsigned min, max;              /* rounds per thread: at least, at most */
} MutexShared;

static void mutex_worker(Worker *w) {
    MutexShared *s = w->arg;
    uint32_t me = w->t.id;
    for (unsigned i = 0; i < s->max && !(i >= s->min && wu_load(&s->stop)); i++) {
        unsigned depth = 1 + (unsigned)(wu_rnd(w) % 3);
        if (wu_rnd(w) % 4 == 0) {
            /* Try first; fall back to waiting. */
            DWORD r = rt_wait_handle(W_CTX(w), s->m, 0, FALSE);
            if (r == WAIT_TIMEOUT) { w->n[1]++; r = rt_wait_handle(W_CTX(w), s->m, INFINITE, FALSE); }
            W_CHECK(w, r == WAIT_OBJECT_0);
        } else {
            W_CHECK(w, rt_wait_handle(W_CTX(w), s->m, INFINITE, FALSE) == WAIT_OBJECT_0);
        }
        W_CHECK(w, __atomic_exchange_n(&s->inside, 1, __ATOMIC_SEQ_CST) == 0);
        for (unsigned d = 1; d < depth; d++)            /* the owner's own waits never block */
            W_CHECK(w, rt_wait_handle(W_CTX(w), s->m, d & 1 ? 0 : INFINITE, FALSE) == WAIT_OBJECT_0);
        if ((i & 63) == 0) {
            RtSyncState st = wu_state(s->m);
            W_CHECK(w, st.owned && st.owner_thread_id == me && st.recursion == depth && !st.abandoned);
        }
        uint64_t c = s->counter;
        if ((i & 15) == 0) sched_yield();               /* widen the window a broken lock would expose */
        s->counter = c + 1;
        for (unsigned d = 1; d < depth; d++) W_CHECK(w, rt_mutex_release(W_CTX(w), s->m) == TRUE);
        W_CHECK(w, __atomic_exchange_n(&s->inside, 0, __ATOMIC_SEQ_CST) == 1);
        W_CHECK(w, rt_mutex_release(W_CTX(w), s->m) == TRUE);
        w->n[0]++;
    }
    /* Not the owner any more: one release too many fails and changes nothing. */
    W_CHECK(w, rt_mutex_release(W_CTX(w), s->m) == FALSE);
    W_CHECK(w, rt_get_last_error(W_CTX(w)) == ERROR_NOT_OWNER);
}

TEST(core, wait_threads_mutex_excludes)
{
    enum { N = 6 };
    static MutexShared s;
    s.m = rt_mutex_create(CTX, FALSE, NULL);
    s.min = 200;
    s.max = 200000;
    wu_start(workers, N, mutex_worker, &s);
    usleep(WU_STRESS_US);
    wu_store(&s.stop, 1);
    if (!wu_finish(workers, N, "threads on one mutex")) return;
    T_TRUE(s.counter >= (uint64_t)N * s.min);
    T_EQ(s.counter, wu_tally(workers, N, 0));           /* no increment was lost */
    RtSyncState st = wu_state(s.m);
    T_EQ(st.owned, false);
    T_EQ(st.recursion, 0);
    T_EQ(st.waiters, 0);
}

/* ==== wait-all ====================================================================================== */

typedef struct {
    HANDLE   a, b;
    uint64_t under_a, under_b;      /* guarded by a and by b: plain memory */
    int      in_a, in_b;
    int      stop;
    unsigned min, max;              /* rounds per thread: at least, at most */
} Pair;

/* Threads 0..3 take both mutexes in one wait-all, the even ones as (a, b) and the odd ones as
 * (b, a).  Threads 4 and 5 take one mutex each, so that the two are free at different times. */
static void pair_worker(Worker *w) {
    Pair *p = w->arg;
    uint32_t me = w->t.id;
    for (unsigned i = 0; i < p->max && !(i >= p->min && wu_load(&p->stop)); i++) {
        if (w->index < 4) {
            HANDLE h[2] = { w->index & 1 ? p->b : p->a, w->index & 1 ? p->a : p->b };
            W_CHECK(w, rt_wait_handles(W_CTX(w), 2, h, TRUE, INFINITE, FALSE) == WAIT_OBJECT_0);
            W_CHECK(w, __atomic_exchange_n(&p->in_a, 1, __ATOMIC_SEQ_CST) == 0);
            W_CHECK(w, __atomic_exchange_n(&p->in_b, 1, __ATOMIC_SEQ_CST) == 0);
            if ((i & 31) == 0) {
                RtSyncState sa = wu_state(p->a), sb = wu_state(p->b);
                W_CHECK(w, sa.owner_thread_id == me && sa.recursion == 1);
                W_CHECK(w, sb.owner_thread_id == me && sb.recursion == 1);
            }
            uint64_t ca = p->under_a, cb = p->under_b;
            if ((i & 7) == 0) sched_yield();
            p->under_a = ca + 1;
            p->under_b = cb + 1;
            W_CHECK(w, __atomic_exchange_n(&p->in_a, 0, __ATOMIC_SEQ_CST) == 1);
            W_CHECK(w, __atomic_exchange_n(&p->in_b, 0, __ATOMIC_SEQ_CST) == 1);
            /* Release in either order. */
            W_CHECK(w, rt_mutex_release(W_CTX(w), h[i & 1]) == TRUE);
            W_CHECK(w, rt_mutex_release(W_CTX(w), h[!(i & 1)]) == TRUE);
        } else {
            bool is_a = w->index == 4;
            HANDLE h = is_a ? p->a : p->b;
            W_CHECK(w, rt_wait_handle(W_CTX(w), h, INFINITE, FALSE) == WAIT_OBJECT_0);
            W_CHECK(w, __atomic_exchange_n(is_a ? &p->in_a : &p->in_b, 1, __ATOMIC_SEQ_CST) == 0);
            uint64_t c = is_a ? p->under_a : p->under_b;
            if ((i & 7) == 0) sched_yield();
            if (is_a) p->under_a = c + 1; else p->under_b = c + 1;
            W_CHECK(w, __atomic_exchange_n(is_a ? &p->in_a : &p->in_b, 0, __ATOMIC_SEQ_CST) == 1);
            W_CHECK(w, rt_mutex_release(W_CTX(w), h) == TRUE);
        }
        w->n[0]++;
    }
}

TEST(core, wait_threads_wait_all_in_opposite_orders)
{
    static Pair p;
    p.a = rt_mutex_create(CTX, FALSE, NULL);
    p.b = rt_mutex_create(CTX, FALSE, NULL);
    p.min = 200;
    p.max = 200000;
    wu_start(workers, 6, pair_worker, &p);
    usleep(WU_STRESS_US);
    wu_store(&p.stop, 1);
    /* With two ordinary locks taken in opposite orders this deadlocks within moments. */
    if (!wu_finish(workers, 6, "wait-all on two mutexes in opposite orders")) return;
    uint64_t pairs = wu_tally(workers, 4, 0);
    T_TRUE(pairs >= 4 * p.min);
    T_EQ(p.under_a, pairs + workers[4].n[0]);           /* the four pair threads and the a-only thread */
    T_EQ(p.under_b, pairs + workers[5].n[0]);
    T_EQ(wu_state(p.a).owned, false);
    T_EQ(wu_state(p.b).owned, false);
    T_EQ(wu_queued(p.a), 0);
    T_EQ(wu_queued(p.b), 0);
}

typedef struct { HANDLE h[3]; unsigned n; int release, has_both; } AllWait;

static void all_wait(Worker *w) {
    AllWait *a = w->arg;
    w->result = rt_wait_handles(W_CTX(w), a->n, a->h, TRUE, INFINITE, FALSE);
    w->n[0] = wu_state(a->h[0]).owner_thread_id == w->t.id;
    w->n[1] = wu_state(a->h[1]).owner_thread_id == w->t.id;
    wu_store(&a->has_both, 1);                          /* now wait to be told to let go */
    while (!wu_load(&a->release)) usleep(200);
    W_CHECK(w, rt_mutex_release(W_CTX(w), a->h[0]) == TRUE);
    W_CHECK(w, rt_mutex_release(W_CTX(w), a->h[1]) == TRUE);
}

TEST(core, wait_threads_wait_all_holds_nothing_while_waiting)
{
    HANDLE a = rt_mutex_create(CTX, FALSE, NULL), b = rt_mutex_create(CTX, TRUE, NULL);  /* the main thread owns b */
    static AllWait aw;
    aw = (AllWait){ { a, b, NULL }, 2, 0, 0 };
    static Worker w;
    wu_start_one(&w, 0, all_wait, &aw);
    T_REQUIRE(EVENTUALLY(wu_queued(a) == 1 && wu_queued(b) == 1));

    /* The worker waits for a and b.  a is free and it has NOT taken it: anyone else can. */
    T_EQ(wu_state(a).owned, false);
    T_EQ(rt_wait_handle(CTX, a, 0, FALSE), WAIT_OBJECT_0);
    T_EQ(wu_state(a).owner_thread_id, RT_THREAD(CTX)->id);
    /* Giving a back does not complete the wait (b is still owned) and a stays free. */
    T_EQ(rt_mutex_release(CTX, a), TRUE);
    wu_settle();
    T_EQ(wu_load(&w.done), 0);
    T_EQ(wu_state(a).owned, false);
    T_EQ(wu_queued(a), 1);

    /* Giving b back completes it: the worker gets both in that step. */
    T_EQ(rt_mutex_release(CTX, b), TRUE);
    T_EQ(wu_state(a).owner_thread_id, w.t.id);
    T_EQ(wu_state(b).owner_thread_id, w.t.id);
    T_EQ(wu_queued(a), 0);
    T_EQ(wu_queued(b), 0);
    T_REQUIRE(EVENTUALLY(wu_load(&aw.has_both)));
    T_EQ(w.result, WAIT_OBJECT_0);
    T_EQ(w.n[0], 1);
    T_EQ(w.n[1], 1);
    T_EQ(rt_wait_handle(CTX, a, 0, FALSE), WAIT_TIMEOUT);       /* owned by the worker now */
    T_EQ(rt_mutex_release(CTX, a), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_OWNER);
    wu_store(&aw.release, 1);
    if (!wu_finish(&w, 1, "wait-all worker")) return;
    T_EQ(wu_state(a).owned, false);
    T_EQ(wu_state(b).owned, false);
}

/* ==== a timeout that runs out as the signal arrives ================================================== */

typedef struct {
    HANDLE sem, mutex;
    int    round;                   /* main: the round the worker should run now */
    int    finished;                /* worker: the last round it has finished */
    int    all;                     /* this round is a wait-all on { mutex, sem } */
    int    stop;
    DWORD  timeout;
} Race;

static void race_worker(Worker *w) {
    Race *r = w->arg;
    for (int round = 1; ; round++) {
        while (wu_load(&r->round) < round && !wu_load(&r->stop)) sched_yield();
        if (wu_load(&r->stop)) return;
        if (wu_load(&r->all)) {
            HANDLE h[2] = { r->mutex, r->sem };
            w->result = rt_wait_handles(W_CTX(w), 2, h, TRUE, r->timeout, FALSE);
            w->n[2] = wu_state(r->mutex).owner_thread_id == w->t.id;
            if (w->result == WAIT_OBJECT_0) W_CHECK(w, rt_mutex_release(W_CTX(w), r->mutex) == TRUE);
        } else {
            w->result = rt_wait_handle(W_CTX(w), r->sem, r->timeout, FALSE);
        }
        wu_store(&r->finished, round);
    }
}

TEST(core, wait_threads_timeout_races_signal)
{
    static Race r;
    static Worker w;
    r.sem = rt_semaphore_create(CTX, 0, 1, NULL);
    r.mutex = rt_mutex_create(CTX, FALSE, NULL);
    r.timeout = 2;
    wu_start_one(&w, 0, race_worker, &r);

    /* Each round the worker waits 2 ms for the semaphore and the main thread releases one unit
     * about 2 ms after it started the round.  The delay follows the outcome (earlier after a
     * timeout, later after a success), so the release keeps landing around the moment the
     * wait gives up.  Whatever happens, the unit is in exactly one place afterwards.  The
     * rounds go on until each outcome has been seen ten times. */
    unsigned took = 0, timed_out = 0, rounds = 0;
    uint64_t delay_ns = 2000000;
    enum { MIN_ROUNDS = 120, MAX_ROUNDS = 3000 };
    for (int round = 1; round <= MAX_ROUNDS && !(round > MIN_ROUNDS && took >= 10 && timed_out >= 10); round++) {
        rounds++;
        wu_store(&r.all, round % 4 == 0);
        uint64_t t0 = wu_now_ns();
        wu_store(&r.round, round);
        while (wu_now_ns() - t0 < delay_ns) { }
        T_EQ(rt_semaphore_release(CTX, r.sem, 1, NULL), TRUE);
        if (!EVENTUALLY(wu_load(&r.finished) == round)) { T_TRUE(!"the worker never finished its round"); return; }

        RtSyncState s = wu_state(r.sem);
        if (w.result == WAIT_OBJECT_0) {
            took++;
            T_EQ(s.count, 0);                           /* the wait took the unit */
            if (r.all) T_EQ(w.n[2], 1);                 /* ... and the mutex with it */
            delay_ns += delay_ns / 8 + 20000;
        } else {
            timed_out++;
            T_EQ(w.result, WAIT_TIMEOUT);
            T_EQ(s.count, 1);                           /* the wait gave up: the unit is still there */
            if (r.all) T_EQ(w.n[2], 0);                 /* ... and it never held the mutex */
            T_EQ(rt_wait_handle(CTX, r.sem, 0, FALSE), WAIT_OBJECT_0);
            delay_ns -= delay_ns / 8;
        }
        T_EQ(s.waiters, 0);
        T_EQ(wu_state(r.mutex).owned, false);
        T_EQ(wu_state(r.mutex).waiters, 0);
        if (delay_ns > 200000000) delay_ns = 200000000;     /* a very busy machine: up to 0.2 s */
    }
    wu_store(&r.stop, 1);
    if (!wu_finish(&w, 1, "racing waiter")) return;
    T_EQ(took + timed_out, rounds);
    /* Both outcomes were seen, so the release really did land on both sides of the timeout. */
    T_TRUE(took >= 10);
    T_TRUE(timed_out >= 10);
}

/* The same moment, made to happen every time.  The race test above shows that nothing is lost
 * when a release lands near a timeout, but the two rarely overlap exactly.  Here they must:
 * the test holds the dispatcher lock from before the worker's deadline until well after it,
 * so the worker's time runs out while it cannot get the lock to say so; then, still holding
 * the lock, the test signals the object.  The signal therefore ends the wait first and takes
 * a unit for the worker, and the worker, when it finally gets the lock to time out, has to
 * find that out and report the object, not WAIT_TIMEOUT. */
typedef struct { RtObject *objs[2]; unsigned n; bool all; DWORD timeout; } TimedWait;

static void timed_wait(Worker *w) {
    TimedWait *t = w->arg;
    w->result = rt_wait(W_CTX(w), t->n, t->objs, t->all, t->timeout, false);
}

TEST(core, wait_threads_signal_wins_at_the_timeout)
{
    static Worker w;
    enum { TIMEOUT_MS = 50, HOLD_PAST_MS = 60 };
    for (int all = 0; all < 2; all++) {
        for (int rep = 0; rep < 2; rep++) {
            WuCount *c = wu_new_count(0), *ready = wu_new_count(1);
            /* Wait-any on { c }; wait-all on { ready, c }, where `ready` has its unit already. */
            TimedWait tw = { { all ? &ready->obj : &c->obj, &c->obj }, all ? 2 : 1, all, TIMEOUT_MS };
            wu_start_one(&w, 0, timed_wait, &tw);
            T_REQUIRE(EVENTUALLY(rt_wait_queue_length(&c->obj) == 1));
            uint64_t queued_at = rt_time_mono_100ns();

            rt_dispatcher_lock();
            while (rt_time_mono_100ns() < queued_at + (uint64_t)(TIMEOUT_MS + HOLD_PAST_MS) * 10000) usleep(1000);
            /* Its timeout is long past and it is still queued: it cannot leave without the lock. */
            T_EQ(rt_wait_queue_length(&c->obj), 1);
            T_EQ(wu_load(&w.done), 0);
            c->units = 1;
            rt_signal(&c->obj);
            T_EQ(rt_wait_queue_length(&c->obj), 0);     /* the signal ended the wait ... */
            T_EQ(c->units, 0);                          /* ... and took the unit for the worker */
            if (all) T_EQ(ready->units, 0);
            rt_dispatcher_unlock();

            if (!wu_finish(&w, 1, "a wait whose timeout ran out under the lock")) return;
            T_EQ(w.result, WAIT_OBJECT_0);              /* so that is what the wait reports */
            T_EQ(c->units, 0);
            T_EQ(rt_wait_queue_length(&ready->obj), 0);
            rt_object_unref(&c->obj);
            rt_object_unref(&ready->obj);
        }
    }
}
