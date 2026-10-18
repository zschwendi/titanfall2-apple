/* The handle table used from many threads at once (RUNTIME.md 4.1: "The table is used from
 * many threads at once and a reference taken by one thread must keep the object alive while
 * another closes the handle").  The locking argument is at the top of runtime/core/handle.c;
 * these tests are its evidence:
 *
 *   - a reference taken before a close keeps the object alive until it is dropped
 *     (deterministic ordering, then a stress test with a canary and a unique id per object);
 *   - a handle closed by several threads at once is closed exactly once;
 *   - a named object created by several threads at once is created once;
 *   - a name looked up while the object dies is never a dead object;
 *   - handles handed out concurrently are all different.
 *
 * The threads are plain host pthreads.  They are not guest threads (the thread unit does not
 * exist yet), so they have no thread block of their own: a failing call stores its last error
 * in the process-wide variable of thread.c, which the threads share, so the threads here check
 * return values only.  Checks (T_*) run on the test's own thread after the join. */
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <time.h>

#include "rt/test.h"
#include "rt/handle.h"

typedef struct TObj {
    RtObject obj;
    uint32_t canary;
    uint32_t id;
} TObj;

#define ALIVE 0xA11FE000u
#define DEAD  0xDEADD00Du

static unsigned created_count, destroyed_count, bad_destroy_count, next_id;

static void tobj_destroy(RtObject *o) {
    TObj *t = (TObj *)o;
    /* The first thing a premature destroy would break is the canary of an object another
     * thread still reads. */
    if (__atomic_exchange_n(&t->canary, DEAD, __ATOMIC_ACQ_REL) != ALIVE)
        __atomic_add_fetch(&bad_destroy_count, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&destroyed_count, 1, __ATOMIC_RELAXED);
}
static const RtObjectOps tops = { .name = "test", .destroy = tobj_destroy };

/* Safe on any thread: no T_* checks. */
static TObj *mk_unnamed(void) {
    TObj *t = (TObj *)rt_object_new(RT_K_EVENT, sizeof *t, &tops, NULL, NULL);
    if (!t) return NULL;
    t->id = __atomic_add_fetch(&next_id, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&t->canary, ALIVE, __ATOMIC_RELEASE);
    __atomic_add_fetch(&created_count, 1, __ATOMIC_RELAXED);
    return t;
}

static uint32_t refs_of(RtObject *o) { return __atomic_load_n(&o->refs, __ATOMIC_SEQ_CST); }

static void yield_a_bit(unsigned n) { for (unsigned i = 0; i < n; i++) sched_yield(); }

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

typedef struct { void *(*fn)(void *); void *arg; pthread_t th; } Worker;

static void quiet(uint8_t *saved) { *saved = rt_log_level[RT_CH_FILE]; rt_log_level[RT_CH_FILE] = RT_LL_ERROR; }

/* ---- 1. a reference keeps the object alive while another thread closes the handle ----------- */

static pthread_mutex_t phase_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  phase_cv = PTHREAD_COND_INITIALIZER;
static int phase;
static HANDLE shared_handle;
static int holder_saw_alive_before, holder_saw_alive_after, holder_got_ref, late_ref_failed;

static void set_phase(int p) {
    pthread_mutex_lock(&phase_lock);
    phase = p;
    pthread_cond_broadcast(&phase_cv);
    pthread_mutex_unlock(&phase_lock);
}
static void wait_phase(int p) {
    pthread_mutex_lock(&phase_lock);
    while (phase < p) pthread_cond_wait(&phase_cv, &phase_lock);
    pthread_mutex_unlock(&phase_lock);
}

static void *holder(void *arg) {
    (void)arg;
    RtObject *o = rt_handle_ref(shared_handle, RT_K_EVENT);
    holder_got_ref = o != NULL;
    if (!o) { set_phase(1); return NULL; }
    holder_saw_alive_before = __atomic_load_n(&((TObj *)o)->canary, __ATOMIC_ACQUIRE) == ALIVE;
    set_phase(1);                           /* the reference is taken ...                   */
    wait_phase(2);                          /* ... the main thread closes the handle ...    */
    holder_saw_alive_after = __atomic_load_n(&((TObj *)o)->canary, __ATOMIC_ACQUIRE) == ALIVE;
    late_ref_failed = rt_handle_ref(shared_handle, RT_K_EVENT) == NULL;
    rt_object_unref(o);                     /* ... and only now may the object die          */
    return NULL;
}

TEST(core, handle_threads_ref_survives_close)
{
    TObj *o = mk_unnamed();
    if (!o) return;
    shared_handle = rt_handle_new(&o->obj);
    phase = 0;
    unsigned before = __atomic_load_n(&destroyed_count, __ATOMIC_SEQ_CST);
    pthread_t th;
    T_REQUIRE(pthread_create(&th, NULL, holder, NULL) == 0);
    wait_phase(1);
    T_EQ(holder_got_ref, 1);

    T_EQ(rt_handle_close(shared_handle), TRUE);                 /* closed while the other thread holds a reference */
    T_EQ(destroyed_count, before);                              /* so the object is still there */
    T_EQ(refs_of(&o->obj), 1);                                  /* the holder's alone */
    set_phase(2);
    pthread_join(th, NULL);
    T_EQ(holder_saw_alive_before, 1);
    T_EQ(holder_saw_alive_after, 1);
    T_EQ(late_ref_failed, 1);                                   /* a lookup after the close finds nothing */
    T_EQ(destroyed_count, before + 1);                          /* it died when the holder let go */
    T_EQ(bad_destroy_count, 0);
}

/* ---- 2. stress: readers reference and use objects while churners close and replace them -------- */

#define CELLS 16
static HANDLE cell[CELLS];
static unsigned churners_done, stop_readers, go_stress, readers_running;
static unsigned reader_refs, reader_misses, reader_bad, dup_ok, held_across_close;
static unsigned churn_closed_ok, churn_closed_bad;

static bool stress_enough(void) {
    return __atomic_load_n(&reader_refs, __ATOMIC_RELAXED) > 1000 &&
           __atomic_load_n(&held_across_close, __ATOMIC_RELAXED) > 50 &&
           __atomic_load_n(&dup_ok, __ATOMIC_RELAXED) > 10;
}

static void *churner(void *arg) {
    unsigned seed = (unsigned)(uintptr_t)arg * 2654435761u + 1;
    while (!__atomic_load_n(&go_stress, __ATOMIC_ACQUIRE)) sched_yield();
    /* At least 30,000 replacements, and on until the readers have really been there (how many
     * of them get CPU time depends on the machine), but not for more than 30 seconds. */
    uint64_t deadline = now_ms() + 30000;
    for (unsigned n = 0; n < 30000 || (!stress_enough() && now_ms() < deadline); n++) {
        if ((n & 7) == 0) sched_yield();                /* let the readers in between the replacements */
        seed = seed * 1103515245u + 12345u;
        unsigned i = (seed >> 8) % CELLS;
        HANDLE old = __atomic_exchange_n(&cell[i], NULL, __ATOMIC_ACQ_REL);     /* claim the cell: nobody else closes it */
        if (!old) { sched_yield(); continue; }
        if (rt_handle_close(old)) __atomic_add_fetch(&churn_closed_ok, 1, __ATOMIC_RELAXED);
        else __atomic_add_fetch(&churn_closed_bad, 1, __ATOMIC_RELAXED);
        TObj *t = mk_unnamed();
        HANDLE h = t ? rt_handle_new(&t->obj) : NULL;
        __atomic_store_n(&cell[i], h, __ATOMIC_RELEASE);
    }
    __atomic_add_fetch(&churners_done, 1, __ATOMIC_ACQ_REL);
    return NULL;
}

static void *reader(void *arg) {
    unsigned seed = (unsigned)(uintptr_t)arg * 40503u + 7;
    __atomic_add_fetch(&readers_running, 1, __ATOMIC_ACQ_REL);
    while (!__atomic_load_n(&stop_readers, __ATOMIC_ACQUIRE)) {
        seed = seed * 1103515245u + 12345u;
        HANDLE h = __atomic_load_n(&cell[(seed >> 8) % CELLS], __ATOMIC_ACQUIRE);
        if (!h) continue;
        RtObject *o = rt_handle_ref(h, RT_K_EVENT);
        if (!o) { __atomic_add_fetch(&reader_misses, 1, __ATOMIC_RELAXED); continue; }
        __atomic_add_fetch(&reader_refs, 1, __ATOMIC_RELAXED);
        TObj *t = (TObj *)o;
        uint32_t id = t->id;
        /* The reference must keep the object whole while handles to it are closed and its slot
         * is reused: same canary, same id, and a count of at least our own. */
        bool ok = __atomic_load_n(&t->canary, __ATOMIC_ACQUIRE) == ALIVE && id != 0 && refs_of(o) >= 1;
        yield_a_bit(1 + (seed & 3));
        ok = ok && __atomic_load_n(&t->canary, __ATOMIC_ACQUIRE) == ALIVE && t->id == id;
        {   /* was the handle closed while we held the object? (then the object is ours alone) */
            RtObject *again = rt_handle_ref(h, RT_K_EVENT);
            if (again) rt_object_unref(again);
            else __atomic_add_fetch(&held_across_close, 1, __ATOMIC_RELAXED);
        }
        if ((seed & 7) == 0) {                      /* DuplicateHandle of a handle that may be closing */
            HANDLE d = rt_handle_dup(h);
            if (d) {
                RtObject *o2 = rt_handle_ref(d, RT_K_EVENT);
                ok = ok && o2 == o;
                if (o2) rt_object_unref(o2);
                ok = ok && rt_handle_close(d) == TRUE;
                __atomic_add_fetch(&dup_ok, 1, __ATOMIC_RELAXED);
            }
        }
        if (!ok) __atomic_add_fetch(&reader_bad, 1, __ATOMIC_RELAXED);
        rt_object_unref(o);
    }
    return NULL;
}

TEST(core, handle_threads_stress_ref_close)
{
    uint8_t saved;
    quiet(&saved);
    for (int i = 0; i < CELLS; i++) {
        TObj *t = mk_unnamed();
        cell[i] = t ? rt_handle_new(&t->obj) : NULL;
        T_REQUIRE(cell[i] != NULL);
    }
    enum { NCHURN = 2, NREAD = 6 };
    Worker w[NCHURN + NREAD];
    for (int i = 0; i < NCHURN; i++) w[i] = (Worker){ churner, (void *)(uintptr_t)(i + 1), 0 };
    for (int i = 0; i < NREAD; i++) w[NCHURN + i] = (Worker){ reader, (void *)(uintptr_t)(i + 1), 0 };

    /* Readers run until the churners are done: start all, release the churners once every
     * reader is running, wait for the churners, stop the readers. */
    for (int i = 0; i < NCHURN + NREAD; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, w[i].arg) == 0);
    while (__atomic_load_n(&readers_running, __ATOMIC_ACQUIRE) < NREAD) sched_yield();
    __atomic_store_n(&go_stress, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < NCHURN; i++) pthread_join(w[i].th, NULL);
    __atomic_store_n(&stop_readers, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < NREAD; i++) pthread_join(w[NCHURN + i].th, NULL);

    for (int i = 0; i < CELLS; i++) T_REQUIRE(rt_handle_close(cell[i]) == TRUE);
    rt_log_level[RT_CH_FILE] = saved;
    T_EQ(churn_closed_bad, 0);                  /* every claimed handle was open, and closed once */
    T_TRUE(churn_closed_ok > 1000);
    T_EQ(reader_bad, 0);                        /* no reader ever saw a dead or recycled object */
    T_TRUE(reader_refs > 1000);                 /* the readers did get references while the churn ran ... */
    T_TRUE(held_across_close > 50);             /* ... and the handle was closed under them many times */
    T_TRUE(dup_ok > 10);
    T_EQ(bad_destroy_count, 0);
    T_EQ(destroyed_count, created_count);       /* nothing leaked, nothing destroyed twice */
}

/* ---- 3. the same handle closed by several threads: exactly one wins ----------------------------- */

#define CLOSERS 4
static HANDLE target;
static unsigned round_no, arrived, wins, losses;

static void *closer(void *arg) {
    (void)arg;
    unsigned seen = 0;
    for (;;) {
        while (__atomic_load_n(&round_no, __ATOMIC_ACQUIRE) == seen) sched_yield();
        seen = __atomic_load_n(&round_no, __ATOMIC_ACQUIRE);
        if (seen == UINT32_MAX) return NULL;
        if (rt_handle_close(target)) __atomic_add_fetch(&wins, 1, __ATOMIC_RELAXED);
        else __atomic_add_fetch(&losses, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&arrived, 1, __ATOMIC_ACQ_REL);
    }
}

TEST(core, handle_threads_concurrent_close)
{
    uint8_t saved;
    quiet(&saved);
    Worker w[CLOSERS];
    for (int i = 0; i < CLOSERS; i++) w[i] = (Worker){ closer, NULL, 0 };
    for (int i = 0; i < CLOSERS; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, NULL) == 0);
    unsigned bad_rounds = 0, rounds = 3000;
    for (unsigned r = 1; r <= rounds; r++) {
        TObj *t = mk_unnamed();
        target = rt_handle_new(&t->obj);
        wins = losses = arrived = 0;
        __atomic_store_n(&round_no, r, __ATOMIC_RELEASE);
        while (__atomic_load_n(&arrived, __ATOMIC_ACQUIRE) < CLOSERS) sched_yield();
        if (wins != 1 || losses != CLOSERS - 1) bad_rounds++;
    }
    __atomic_store_n(&round_no, UINT32_MAX, __ATOMIC_RELEASE);
    for (int i = 0; i < CLOSERS; i++) pthread_join(w[i].th, NULL);
    rt_log_level[RT_CH_FILE] = saved;
    T_EQ(bad_rounds, 0);
    T_EQ(destroyed_count, rounds);              /* each object died exactly once */
    T_EQ(bad_destroy_count, 0);
}

/* ---- 4. a named object created by several threads at once is created once ------------------------ */

#define CREATORS 8
static unsigned go, ready;
static TObj *got[CREATORS];
static bool got_existed[CREATORS];
static const WCHAR *race_name;

static void *creator(void *arg) {
    unsigned me = (unsigned)(uintptr_t)arg;
    unsigned seen = 0;
    for (;;) {
        while (__atomic_load_n(&go, __ATOMIC_ACQUIRE) == seen) sched_yield();
        seen = __atomic_load_n(&go, __ATOMIC_ACQUIRE);
        if (seen == UINT32_MAX) return NULL;
        bool ex = true;
        got[me] = (TObj *)rt_object_new(RT_K_MUTEX, sizeof(TObj), &tops, race_name, &ex);
        got_existed[me] = ex;
        __atomic_add_fetch(&ready, 1, __ATOMIC_ACQ_REL);
    }
}

TEST(core, handle_threads_concurrent_create_named)
{
    Worker w[CREATORS];
    for (int i = 0; i < CREATORS; i++) w[i] = (Worker){ creator, (void *)(uintptr_t)i, 0 };
    for (int i = 0; i < CREATORS; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, w[i].arg) == 0);
    unsigned bad_rounds = 0, rounds = 1500;
    for (unsigned r = 1; r <= rounds; r++) {
        race_name = (r & 1) ? u"Race" : u"Local\\Race";         /* two spellings of one name */
        ready = 0;
        __atomic_store_n(&go, r, __ATOMIC_RELEASE);
        while (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) < CREATORS) sched_yield();
        unsigned fresh = 0;
        bool same = true;
        for (int i = 0; i < CREATORS; i++) {
            if (!got[i]) { same = false; continue; }
            if (!got_existed[i]) { fresh++; got[i]->canary = ALIVE; got[i]->id = r; __atomic_add_fetch(&created_count, 1, __ATOMIC_RELAXED); }
            if (got[i] != got[0]) same = false;
        }
        if (fresh != 1 || !same || !got[0] || refs_of(&got[0]->obj) != CREATORS) bad_rounds++;
        for (int i = 0; i < CREATORS; i++) if (got[i]) rt_object_unref(&got[i]->obj);
    }
    __atomic_store_n(&go, UINT32_MAX, __ATOMIC_RELEASE);
    for (int i = 0; i < CREATORS; i++) pthread_join(w[i].th, NULL);
    T_EQ(bad_rounds, 0);
    T_EQ(destroyed_count, rounds);
    T_EQ(bad_destroy_count, 0);
    T_TRUE(rt_object_open(RT_K_MUTEX, u"Race") == NULL);
}

/* ---- 5. a name looked up while its object dies ------------------------------------------------------ */

/* Each round: the main thread makes the object "Churn" (the only reference is its own) and the
 * openers look the name up while the main thread drops its reference.  Three kinds of round, so
 * that the outcome is exact and does not depend on who is scheduled first:
 *   0  the reference is dropped BEFORE the openers are released: the object is gone, every lookup
 *      must miss;
 *   1  the main thread waits until an opener holds the object and drops its own reference then:
 *      that opener's reference must keep the object whole until it lets go, and it dies with it;
 *   2  the reference is dropped right after the release: a pure race, whoever is last destroys
 *      the object and a lookup that comes too late finds nothing, never a half-dead object. */
#define OPENERS 3
static unsigned open_round, open_done, opened_round, opened_total, opened_bad;

static bool opener_check(RtObject *o) {
    /* Fields the core wrote before the object was findable: they must be intact, and the
     * name must still be there, while we hold the reference. */
    return __atomic_load_n(&o->kind, __ATOMIC_ACQUIRE) == RT_K_EVENT && o->ops == &tops && o->name &&
           o->name[0] == 'C' && o->name[4] == 'n' && refs_of(o) >= 1;
}

static void *opener(void *arg) {
    (void)arg;
    unsigned seen = 0;
    for (;;) {
        while (__atomic_load_n(&open_round, __ATOMIC_ACQUIRE) == seen) sched_yield();
        seen = __atomic_load_n(&open_round, __ATOMIC_ACQUIRE);
        if (seen == UINT32_MAX) return NULL;
        RtObject *o = rt_object_open(RT_K_EVENT, u"Churn");
        if (o) {
            __atomic_add_fetch(&opened_total, 1, __ATOMIC_RELAXED);
            __atomic_add_fetch(&opened_round, 1, __ATOMIC_ACQ_REL);
            bool ok = opener_check(o);
            yield_a_bit(seen % 3);                      /* hold it a little, longer in some rounds */
            ok = ok && opener_check(o);
            if (!ok) __atomic_add_fetch(&opened_bad, 1, __ATOMIC_RELAXED);
            rt_object_unref(o);                         /* often the last reference: the name dies here */
        }
        __atomic_add_fetch(&open_done, 1, __ATOMIC_ACQ_REL);
    }
}

TEST(core, handle_threads_name_vs_destroy)
{
    Worker w[OPENERS];
    for (int i = 0; i < OPENERS; i++) w[i] = (Worker){ opener, NULL, 0 };
    for (int i = 0; i < OPENERS; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, NULL) == 0);
    unsigned rounds = 6000, existed_wrong = 0, not_dead = 0, hit_after_drop = 0, no_hit_while_held = 0;
    for (unsigned r = 1; r <= rounds; r++) {
        bool ex = true;
        TObj *t = (TObj *)rt_object_new(RT_K_EVENT, sizeof *t, &tops, u"Churn", &ex);
        if (!t || ex) { existed_wrong++; continue; }    /* the previous round's object must be completely gone */
        t->canary = ALIVE;
        __atomic_add_fetch(&created_count, 1, __ATOMIC_RELAXED);
        open_done = 0;
        opened_round = 0;
        switch (r % 3) {
        case 0:
            rt_object_unref(&t->obj);
            __atomic_store_n(&open_round, r, __ATOMIC_RELEASE);
            break;
        case 1:
            __atomic_store_n(&open_round, r, __ATOMIC_RELEASE);
            while (__atomic_load_n(&opened_round, __ATOMIC_ACQUIRE) == 0) sched_yield();
            rt_object_unref(&t->obj);
            break;
        default:
            __atomic_store_n(&open_round, r, __ATOMIC_RELEASE);
            rt_object_unref(&t->obj);
            break;
        }
        while (__atomic_load_n(&open_done, __ATOMIC_ACQUIRE) < OPENERS) sched_yield();
        if (r % 3 == 0 && opened_round != 0) hit_after_drop++;
        if (r % 3 == 1 && opened_round == 0) no_hit_while_held++;
        if (__atomic_load_n(&destroyed_count, __ATOMIC_ACQUIRE) != created_count) not_dead++;
    }
    __atomic_store_n(&open_round, UINT32_MAX, __ATOMIC_RELEASE);
    for (int i = 0; i < OPENERS; i++) pthread_join(w[i].th, NULL);
    T_EQ(existed_wrong, 0);
    T_EQ(not_dead, 0);                                  /* after every round, nothing is left alive */
    T_EQ(hit_after_drop, 0);                            /* a name never outlives its object */
    T_EQ(no_hit_while_held, 0);
    T_EQ(opened_bad, 0);
    T_EQ(bad_destroy_count, 0);
    T_EQ(destroyed_count, created_count);
    T_EQ(created_count, rounds);
    T_TRUE(opened_total >= rounds / 3);                 /* at least one hit in every round of kind 1 */
    T_TRUE(rt_object_open(RT_K_EVENT, u"Churn") == NULL);
}

/* ---- 6. handles handed out concurrently are all different ---------------------------------------------- */

#define ALLOCATORS 6
#define PER_ALLOC 4000
static HANDLE alloc_out[ALLOCATORS][PER_ALLOC];
static unsigned alloc_go;

static void *allocator(void *arg) {
    unsigned me = (unsigned)(uintptr_t)arg;
    while (!__atomic_load_n(&alloc_go, __ATOMIC_ACQUIRE)) sched_yield();
    for (unsigned i = 0; i < PER_ALLOC; i++) {
        TObj *t = mk_unnamed();
        alloc_out[me][i] = t ? rt_handle_new(&t->obj) : NULL;
        if ((i % 5) == 4 && alloc_out[me][i - 2]) {         /* close some as we go: slots are reused concurrently */
            if (rt_handle_close(alloc_out[me][i - 2])) alloc_out[me][i - 2] = (HANDLE)(uintptr_t)1;
        }
    }
    return NULL;
}

TEST(core, handle_threads_unique_handles)
{
    Worker w[ALLOCATORS];
    for (int i = 0; i < ALLOCATORS; i++) w[i] = (Worker){ allocator, (void *)(uintptr_t)i, 0 };
    for (int i = 0; i < ALLOCATORS; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, w[i].arg) == 0);
    __atomic_store_n(&alloc_go, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < ALLOCATORS; i++) pthread_join(w[i].th, NULL);

    /* the handles still open must all be different, valid, and of the right shape */
    static uint64_t open_handles[ALLOCATORS * PER_ALLOC];
    unsigned n = 0, shape_bad = 0, null_handles = 0;
    for (int t = 0; t < ALLOCATORS; t++)
        for (unsigned i = 0; i < PER_ALLOC; i++) {
            uint64_t v = (uint64_t)(uintptr_t)alloc_out[t][i];
            if (v == 1) continue;                   /* closed by its thread */
            if (v == 0) { null_handles++; continue; }
            if ((v & 3) || (v >> 31)) shape_bad++;
            open_handles[n++] = v;
        }
    T_EQ(null_handles, 0);
    T_EQ(shape_bad, 0);
    T_TRUE(n > ALLOCATORS * PER_ALLOC / 2);
    /* sort (heap sort would be overkill: use qsort-free insertion on blocks via simple shell sort) */
    for (unsigned gap = n / 2; gap > 0; gap /= 2)
        for (unsigned i = gap; i < n; i++) {
            uint64_t x = open_handles[i]; unsigned j = i;
            while (j >= gap && open_handles[j - gap] > x) { open_handles[j] = open_handles[j - gap]; j -= gap; }
            open_handles[j] = x;
        }
    unsigned dups = 0;
    for (unsigned i = 1; i < n; i++) if (open_handles[i] == open_handles[i - 1]) dups++;
    T_EQ(dups, 0);
    for (unsigned i = 0; i < n; i++) T_REQUIRE(rt_handle_close((HANDLE)(uintptr_t)open_handles[i]) == TRUE);
    T_EQ(destroyed_count, created_count);
    T_EQ(bad_destroy_count, 0);
}

/* ---- 7. reference counts under contention ------------------------------------------------------------- */

#define REFTHREADS 6
static RtObject *shared_obj;
static unsigned ref_go, ref_done;

static void *ref_hammer(void *arg) {
    (void)arg;
    while (!__atomic_load_n(&ref_go, __ATOMIC_ACQUIRE)) sched_yield();
    for (unsigned i = 0; i < 100000; i++) {
        rt_object_ref(shared_obj);
        rt_object_unref(shared_obj);
    }
    __atomic_add_fetch(&ref_done, 1, __ATOMIC_ACQ_REL);
    return NULL;
}

TEST(core, handle_threads_refcount_integrity)
{
    TObj *o = mk_unnamed();
    if (!o) return;
    shared_obj = &o->obj;
    Worker w[REFTHREADS];
    for (int i = 0; i < REFTHREADS; i++) w[i] = (Worker){ ref_hammer, NULL, 0 };
    for (int i = 0; i < REFTHREADS; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, NULL) == 0);
    __atomic_store_n(&ref_go, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < REFTHREADS; i++) pthread_join(w[i].th, NULL);
    T_EQ(refs_of(&o->obj), 1);                      /* not one increment or decrement was lost */
    T_EQ(destroyed_count, 0);
    rt_object_unref(&o->obj);
    T_EQ(destroyed_count, 1);
}

/* Each round: the object has one reference per thread, and all the threads drop theirs at the
 * same moment.  It must be destroyed once, by one of them, and not before the last. */
static unsigned drop_round, drop_arrived;
static RtObject *drop_obj;

static void *dropper(void *arg) {
    (void)arg;
    unsigned seen = 0;
    for (;;) {
        while (__atomic_load_n(&drop_round, __ATOMIC_ACQUIRE) == seen) sched_yield();
        seen = __atomic_load_n(&drop_round, __ATOMIC_ACQUIRE);
        if (seen == UINT32_MAX) return NULL;
        rt_object_unref(drop_obj);
        __atomic_add_fetch(&drop_arrived, 1, __ATOMIC_ACQ_REL);
    }
}

TEST(core, handle_threads_last_unref)
{
    Worker w[REFTHREADS];
    for (int i = 0; i < REFTHREADS; i++) w[i] = (Worker){ dropper, NULL, 0 };
    for (int i = 0; i < REFTHREADS; i++) T_REQUIRE(pthread_create(&w[i].th, NULL, w[i].fn, NULL) == 0);
    unsigned rounds = 4000, early = 0;
    for (unsigned r = 1; r <= rounds; r++) {
        TObj *o = mk_unnamed();
        drop_obj = &o->obj;
        for (int i = 1; i < REFTHREADS; i++) rt_object_ref(&o->obj);        /* one per thread, the first is the creator's */
        drop_arrived = 0;
        __atomic_store_n(&drop_round, r, __ATOMIC_RELEASE);
        while (__atomic_load_n(&drop_arrived, __ATOMIC_ACQUIRE) < REFTHREADS) sched_yield();
        if (__atomic_load_n(&destroyed_count, __ATOMIC_ACQUIRE) != r) early++;
    }
    __atomic_store_n(&drop_round, UINT32_MAX, __ATOMIC_RELEASE);
    for (int i = 0; i < REFTHREADS; i++) pthread_join(w[i].th, NULL);
    T_EQ(early, 0);
    T_EQ(destroyed_count, rounds);
    T_EQ(bad_destroy_count, 0);
}
