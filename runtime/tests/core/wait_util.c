/* Helpers shared by the tests of the wait core and of the event, mutex and semaphore objects
 * (wait*.c and sync*.c in this directory).
 *
 * This is a .c file that the other test files #include.  It would be a header, but the unit
 * that owns these tests may only add files named runtime/tests/core/wait*.c and sync*.c.  The
 * build also compiles it on its own, where it defines nothing that is used: every definition
 * here is static and marked unused.
 *
 * THREADS.  The runtime cannot create guest threads yet, so a test's second and further
 * threads are host pthreads that each own a hand-made guest thread: an RtThread whose Ctx
 * points at its own thread block (thread id at +0x48, last error at +0x68).  That is all the
 * wait core needs of a thread: an identity (a mutex's owner), a slot for its wait record and
 * a place for last error.  These threads are not in the x86 ordering mode and neither is the
 * main thread of a test process, so everything runs under arm64's weak ordering.  They are
 * not guest threads to rt_guest_call (rt_thread_self() is NULL on them), so APCs are only
 * ever delivered on the test's main thread, which is the real guest thread 1.
 *
 * Only the main thread calls the T_ checks (the harness's counters are not thread safe):
 * workers record their first failed check with W_CHECK and the main thread reports it when
 * it collects them.  A lost wake-up or a deadlock shows as a worker that never finishes:
 * every test collects its workers with a deadline and fails if one is still asleep.
 *
 * The machine these tests run on is often very busy (other builds, load averages above 100),
 * so nothing here asserts that something happens QUICKLY.  A test waits for a state it can
 * observe (a thread is queued on an object: rt_wait_queue_length) and not for a length of
 * time; where time is the subject (timeouts) only lower bounds are asserted.
 */
#ifndef RT_TEST_WAIT_UTIL_C
#define RT_TEST_WAIT_UTIL_C

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/handle.h"
#include "rt/sync.h"
#include "rt/time.h"

#define WU  __attribute__((unused)) static

/* How long a test waits for something that must happen before it calls it lost. */
#define WU_DEADLINE_NS  UINT64_C(30000000000)

/* How long a stress test keeps its threads at it, in microseconds.  Such a test runs for this
 * long and for at least a minimum number of rounds, whichever takes longer: a round is often
 * a thread switch, and how long that takes depends on what else the machine is doing. */
#define WU_STRESS_US    300000

typedef struct Worker Worker;
struct Worker {
    RtThread    t;                  /* the hand-made guest thread; t.ctx is its Ctx */
    uint8_t     block[RT_TEB_SIZE] __attribute__((aligned(16)));   /* its thread block */
    pthread_t   host;
    void      (*fn)(Worker *);
    void       *arg;                /* the test's shared state */
    unsigned    index;              /* 0, 1, 2 ... within its group */
    int         done;               /* set when fn has returned */
    unsigned    errors;             /* failed W_CHECKs, and the first of them */
    int         line;
    const char *what;
    DWORD       result;             /* what the worker's wait returned, where a test wants it */
    uint64_t    n[6];               /* tallies, meaning per test */
    uint64_t    rng;
};

#define W_CTX(w)  (&(w)->t.ctx)
#define W_CHECK(w, cond) do { if (!(cond) && !(w)->errors++) { (w)->line = __LINE__; (w)->what = #cond; } } while (0)

WU uint32_t wu_next_id = 0x2000;    /* guest thread ids: multiples of 4, never reused */

WU uint64_t wu_now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
WU int      wu_load(const int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
WU void     wu_store(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

WU uint64_t wu_rnd(Worker *w) {
    w->rng ^= w->rng << 13;
    w->rng ^= w->rng >> 7;
    w->rng ^= w->rng << 17;
    return w->rng;
}

/* Make `w` a guest thread by hand, without starting anything. */
WU void wu_make_thread(Worker *w, unsigned index) {
    memset(w, 0, sizeof *w);
    uint64_t teb = (uint64_t)(uintptr_t)w->block;
    w->t.id = wu_next_id;
    wu_next_id += 4;
    ST64(teb + RT_TEB_SELF, teb);
    ST64(teb + RT_TEB_PROCESS_ID, RT_PROCESS_ID);
    ST64(teb + RT_TEB_THREAD_ID, w->t.id);
    w->t.teb = teb;
    w->t.ctx.gs_base = teb;
    w->t.ctx.mxcsr = 0x1F80;
    w->t.ctx.host = &w->t;
    snprintf(w->t.name, sizeof w->t.name, "worker %u", index);
    w->index = index;
    w->rng = 0x9E3779B97F4A7C15ull * (w->t.id + 1);
}

WU void *wu_main(void *p) {
    Worker *w = p;
    w->fn(w);
    wu_store(&w->done, 1);
    return NULL;
}

/* Start one worker running fn(w) with w->arg = arg. */
WU void wu_start_one(Worker *w, unsigned index, void (*fn)(Worker *), void *arg) {
    wu_make_thread(w, index);
    w->fn = fn;
    w->arg = arg;
    if (pthread_create(&w->host, NULL, wu_main, w)) { perror("pthread_create"); abort(); }
}

/* Start n workers. */
WU void wu_start(Worker *w, unsigned n, void (*fn)(Worker *), void *arg) {
    for (unsigned i = 0; i < n; i++) wu_start_one(&w[i], i, fn, arg);
}

WU unsigned wu_done_count(const Worker *w, unsigned n) {
    unsigned done = 0;
    for (unsigned i = 0; i < n; i++) done += wu_load(&w[i].done) != 0;
    return done;
}

/* Wait for the workers, report what they found, and say whether all of them finished.  One
 * that has not finished by the deadline is asleep for good (a lost wake-up or a deadlock):
 * that fails the test, and the caller returns without it. */
WU bool wu_finish(Worker *w, unsigned n, const char *what) {
    uint64_t deadline = wu_now_ns() + WU_DEADLINE_NS;
    for (;;) {
        unsigned done = wu_done_count(w, n);
        if (done == n) break;
        if (wu_now_ns() > deadline) {
            rt_test_fail(__FILE__, __LINE__, "%s: %u of %u threads never finished: a lost wake-up or a deadlock",
                         what, n - done, n);
            return false;
        }
        usleep(300);
    }
    for (unsigned i = 0; i < n; i++) {
        pthread_join(w[i].host, NULL);
        if (w[i].errors)
            rt_test_fail(__FILE__, w[i].line, "%s: thread %u failed %u check(s), the first: %s", what, i,
                         w[i].errors, w[i].what);
    }
    T_TRUE(n > 0);
    return true;
}

WU uint64_t wu_tally(const Worker *w, unsigned n, unsigned k) {
    uint64_t s = 0;
    for (unsigned i = 0; i < n; i++) s += w[i].n[k];
    return s;
}

/* True once `expr` holds; false if it does not before the deadline.  The first few hundred
 * looks are a yield apart (what is waited for is usually one thread switch away), the rest a
 * fraction of a millisecond. */
#define EVENTUALLY(expr) ({ \
        bool ok_ = false; \
        unsigned looks_ = 0; \
        for (uint64_t t0_ = wu_now_ns(); !(ok_ = (expr)) && wu_now_ns() - t0_ < WU_DEADLINE_NS; ) { \
            if (++looks_ < 300) sched_yield(); else usleep(200); \
        } \
        ok_; })

/* Time for a thread that could wrongly get through to do so. */
WU void wu_settle(void) { usleep(20000); }

/* ---- objects ------------------------------------------------------------------------------------ */

/* The object behind an open handle, borrowed: the handle keeps it alive. */
WU RtObject *wu_obj(HANDLE h) {
    RtObject *o = rt_handle_ref(h, RT_K_ANY);
    if (!o) { fprintf(stderr, "wu_obj: %p is not an open handle\n", h); abort(); }
    rt_object_unref(o);
    return o;
}

WU RtSyncState wu_state(HANDLE h) {
    RtSyncState s;
    rt_sync_query(wu_obj(h), &s);
    return s;
}

/* Threads queued on the object behind `h`. */
WU unsigned wu_queued(HANDLE h) { return rt_wait_queue_length(wu_obj(h)); }

/* A UTF-16 copy of an ASCII string, in one of eight static buffers used in turn. */
WU const WCHAR *wu_w(const char *s) {
    static WCHAR bufs[8][300];
    static unsigned at;
    WCHAR *b = bufs[at++ % 8];
    size_t i = 0;
    for (; s[i] && i < 299; i++) b[i] = (WCHAR)(unsigned char)s[i];
    b[i] = 0;
    return b;
}

/* Kinds of the tests' own, as another unit would define them (rt/handle.h).
 *   wu_plain_ops   not waitable: no is_signaled
 *   wu_flag_ops    waitable, nothing taken (what a thread object will be: signalled once it
 *                  has exited, and it stays so) */
typedef struct WuFlag {
    RtObject obj;
    bool     signaled;              /* dispatcher lock */
    unsigned asked;                 /* times is_signaled ran */
} WuFlag;

WU bool wu_flag_is_signaled(RtObject *o, struct RtThread *t) {
    (void)t;
    ((WuFlag *)o)->asked++;
    return ((WuFlag *)o)->signaled;
}

WU const RtObjectOps wu_plain_ops = { .name = "test-plain" };
WU const RtObjectOps wu_flag_ops = { .name = "test-flag", .is_signaled = wu_flag_is_signaled };

WU RtObject *wu_new_plain(void) {
    RtObject *o = rt_object_new(RT_K_FILE, sizeof *o, &wu_plain_ops, NULL, NULL);
    if (!o) abort();
    return o;
}

WU WuFlag *wu_new_flag(void) {
    WuFlag *f = (WuFlag *)rt_object_new(RT_K_THREAD, sizeof *f, &wu_flag_ops, NULL, NULL);
    if (!f) abort();
    return f;
}

/* What the owner of the kind does when the object becomes signalled. */
WU void wu_flag_signal(WuFlag *f) {
    rt_dispatcher_lock();
    f->signaled = true;
    rt_signal(&f->obj);
    rt_dispatcher_unlock();
}

/*   wu_count_ops   waitable, one unit taken per wait: a semaphore whose state the test
 *                  changes by hand, under the dispatcher lock, as the owner of a kind does */
typedef struct WuCount {
    RtObject obj;
    int      units;                 /* dispatcher lock */
} WuCount;

WU bool wu_count_is_signaled(RtObject *o, struct RtThread *t) { (void)t; return ((WuCount *)o)->units > 0; }
WU void wu_count_consume(RtObject *o, struct RtThread *t) { (void)t; ((WuCount *)o)->units--; }
WU const RtObjectOps wu_count_ops = { .name = "test-count", .is_signaled = wu_count_is_signaled,
                                      .consume = wu_count_consume };

WU WuCount *wu_new_count(int units) {
    WuCount *c = (WuCount *)rt_object_new(RT_K_PIPE, sizeof *c, &wu_count_ops, NULL, NULL);
    if (!c) abort();
    c->units = units;
    return c;
}

/* Milliseconds on the runtime's monotonic clock, the one timeouts are measured on. */
WU uint64_t wu_rt_ms(void) { return rt_time_mono_100ns() / 10000; }

#endif /* RT_TEST_WAIT_UTIL_C */
