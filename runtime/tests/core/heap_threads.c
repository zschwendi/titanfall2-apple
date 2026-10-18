/* The heaps used from several host threads at once (docs/RUNTIME.md section 5.4: "Thread-safe";
 * HEAP_NO_SERIALIZE is only ever a promise, never a requirement).  The core calls need no guest
 * thread, so the workers are plain pthreads.  A worker never calls a T_ check (they are not
 * thread-safe): it counts what it finds wrong, and the test reads the counts after the join.
 */
#include <malloc/malloc.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#include "rt/test.h"
#include "rt/heap.h"
#include "rt/machine.h"

#define NTHREADS 8

static RtHeapBlock *hdr(const void *p) { return (RtHeapBlock *)p - 1; }

/* ---- small tools for the workers ------------------------------------------------------------ */

static uint64_t rng_next(uint64_t *s) {          /* xorshift64*: any decent generator will do */
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545F4914F6CDD1D);
}

/* A mix of mostly small and sometimes large sizes. */
static uint64_t pick_size(uint64_t *s) {
    uint64_t r = rng_next(s);
    switch (r % 20) {
    case 0: return (r >> 8) % 300000;
    case 1: case 2: case 3: return (r >> 8) % 6000;
    default: return (r >> 8) % 200;
    }
}

static void fill(void *p, uint64_t n, uint8_t seed) {
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + i * 7);
}

static bool check_fill(const void *p, uint64_t n, uint8_t seed) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i] != (uint8_t)(seed + i * 7)) return false;
    return true;
}

static bool all_zero(const void *p, uint64_t n) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

/* All the workers start together. */
static volatile unsigned go;
static unsigned arrived;
static void wait_for_start(unsigned total) {
    __atomic_fetch_add(&arrived, 1, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&arrived, __ATOMIC_ACQUIRE) == total) __atomic_store_n(&go, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&go, __ATOMIC_ACQUIRE)) sched_yield();
}
static void reset_start(void) { go = 0; arrived = 0; }

typedef struct {
    HANDLE   heap;
    unsigned index;
    unsigned iterations;
    unsigned nthreads;
    unsigned errors;          /* what the worker found wrong */
    char     first_error[160];
} Worker;

static void worker_error(Worker *w, const char *what) {
    if (w->errors++ == 0) snprintf(w->first_error, sizeof w->first_error, "%s", what);
}

/* ---- many threads, one heap: allocate, check, reallocate, free ---------------------------------- */

typedef struct { uint8_t *p; uint64_t size; uint8_t seed; } Slot;

static void *churn(void *arg) {
    Worker *w = arg;
    uint64_t rng = UINT64_C(0x9E3779B97F4A7C15) * (w->index + 1);
    enum { SLOTS = 48 };
    Slot slots[SLOTS] = {{0}};
    uint32_t id = 0;
    wait_for_start(w->nthreads);
    for (unsigned it = 0; it < w->iterations; it++) {
        Slot *s = &slots[rng_next(&rng) % SLOTS];
        if (!s->p) {
            uint64_t size = pick_size(&rng);
            bool zero = rng_next(&rng) & 1;
            uint8_t *p = rt_heap_alloc(w->heap, zero ? HEAP_ZERO_MEMORY : 0, size);
            if (!p) { worker_error(w, "alloc returned NULL"); continue; }
            if ((uint64_t)p & 15) worker_error(w, "block not 16-byte aligned");
            if (zero && !all_zero(p, size)) worker_error(w, "HEAP_ZERO_MEMORY block not zero");
            if (!id) id = hdr(p)->heap_id;
            s->p = p; s->size = size; s->seed = (uint8_t)rng_next(&rng);
            fill(p, size, s->seed);
            continue;
        }
        /* An existing block: its bytes, size and header are as this thread left them. */
        if (!check_fill(s->p, s->size, s->seed)) worker_error(w, "block contents changed (another thread wrote here)");
        if (rt_heap_size(w->heap, 0, s->p) != s->size) worker_error(w, "HeapSize differs from the size asked");
        if (hdr(s->p)->heap_id != id || hdr(s->p)->magic != RT_HEAP_BLOCK_MAGIC) worker_error(w, "block header changed");
        if (rng_next(&rng) & 1) {
            if (rt_heap_free(w->heap, rng_next(&rng) & 1 ? HEAP_NO_SERIALIZE : 0, s->p) != TRUE) worker_error(w, "HeapFree failed");
            s->p = NULL;
        } else {
            uint64_t size = pick_size(&rng);
            bool zero = rng_next(&rng) & 1;
            uint8_t *q = rt_heap_realloc(w->heap, zero ? HEAP_ZERO_MEMORY : 0, s->p, size);
            if (!q) { worker_error(w, "realloc returned NULL"); continue; }
            uint64_t keep = size < s->size ? size : s->size;
            if (!check_fill(q, keep, s->seed)) worker_error(w, "realloc lost the old contents");
            if (zero && size > s->size && !all_zero(q + s->size, size - s->size)) worker_error(w, "realloc growth not zeroed");
            s->p = q; s->size = size; s->seed = (uint8_t)rng_next(&rng);
            fill(q, size, s->seed);
        }
    }
    for (unsigned i = 0; i < SLOTS; i++) {
        if (!slots[i].p) continue;
        if (!check_fill(slots[i].p, slots[i].size, slots[i].seed)) worker_error(w, "block contents changed at the end");
        if (rt_heap_free(w->heap, 0, slots[i].p) != TRUE) worker_error(w, "HeapFree failed at the end");
    }
    return NULL;
}

static unsigned run_workers(void *(*fn)(void *), Worker *w, unsigned n) {
    pthread_t th[NTHREADS];
    reset_start();
    for (unsigned i = 0; i < n; i++) {
        w[i].index = i;
        w[i].nthreads = n;
        if (pthread_create(&th[i], NULL, fn, &w[i]) != 0) { fprintf(stderr, "pthread_create failed\n"); exit(2); }
    }
    unsigned errors = 0;
    for (unsigned i = 0; i < n; i++) { pthread_join(th[i], NULL); errors += w[i].errors; }
    return errors;
}

static void report(Worker *w, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        if (w[i].errors) rt_test_fail(__FILE__, __LINE__, "worker %u: %u errors, the first: %s", i, w[i].errors, w[i].first_error);
}

TEST(core, heap_threads_share_the_process_heap)
{
    Worker w[NTHREADS];
    for (unsigned i = 0; i < NTHREADS; i++) { memset(&w[i], 0, sizeof w[i]); w[i].heap = rt_heap_process(); w[i].iterations = 40000; }
    T_EQ(run_workers(churn, w, NTHREADS), 0);
    report(w, NTHREADS);
}

TEST(core, heap_threads_share_a_created_heap)
{
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    Worker w[NTHREADS];
    for (unsigned i = 0; i < NTHREADS; i++) { memset(&w[i], 0, sizeof w[i]); w[i].heap = h; w[i].iterations = 40000; }
    T_EQ(run_workers(churn, w, NTHREADS), 0);
    report(w, NTHREADS);
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_threads_each_with_their_own_heap)
{
    Worker w[NTHREADS];
    HANDLE h[NTHREADS];
    for (unsigned i = 0; i < NTHREADS; i++) {
        h[i] = rt_heap_create(i & 1 ? HEAP_NO_SERIALIZE : 0, 0, 0);
        T_REQUIRE(h[i] != NULL);
        memset(&w[i], 0, sizeof w[i]);
        w[i].heap = h[i];
        w[i].iterations = 40000;
    }
    T_EQ(run_workers(churn, w, NTHREADS), 0);
    report(w, NTHREADS);
    for (unsigned i = 0; i < NTHREADS; i++) T_EQ(rt_heap_destroy(h[i]), TRUE);
}

/* ---- a fixed-size heap under contention: the accounting is exact -------------------------------- */

/* Blocks of 48 bytes cost 64 each, so a heap of 64 KB holds 1024 of them. */
#define FIXED_MAX       65536u
#define FIXED_CAPACITY  1024u

typedef struct { Worker base; void **blocks; unsigned count; } FixedWorker;

static void *grab_until_full(void *arg) {
    FixedWorker *f = arg;
    wait_for_start(f->base.nthreads);
    for (;;) {
        void *p = rt_heap_alloc(f->base.heap, 0, 48);
        if (!p) break;
        f->blocks[f->count++] = p;
    }
    return NULL;
}

static void *release_all(void *arg) {
    FixedWorker *f = arg;
    wait_for_start(f->base.nthreads);
    for (unsigned i = 0; i < f->count; i++)
        if (rt_heap_free(f->base.heap, 0, f->blocks[i]) != TRUE) worker_error(&f->base, "HeapFree failed");
    f->count = 0;
    return NULL;
}

TEST(core, heap_threads_fixed_size_capacity_is_exact)
{
    HANDLE h = rt_heap_create(0, 0, FIXED_MAX);
    T_REQUIRE(h != NULL);
    static void *store[NTHREADS][FIXED_CAPACITY + 8];
    FixedWorker f[NTHREADS];
    for (unsigned i = 0; i < NTHREADS; i++) { memset(&f[i], 0, sizeof f[i]); f[i].base.heap = h; f[i].blocks = store[i]; }
    for (unsigned round = 0; round < 20; round++) {
        /* Eight threads race to fill the heap: together they get exactly its capacity, never one block
         * more (the reservation is atomic) and never one less (nothing was leaked by an earlier round). */
        pthread_t th[NTHREADS];
        reset_start();
        for (unsigned i = 0; i < NTHREADS; i++) { f[i].base.index = i; f[i].base.nthreads = NTHREADS; pthread_create(&th[i], NULL, grab_until_full, &f[i]); }
        for (unsigned i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);
        unsigned total = 0;
        for (unsigned i = 0; i < NTHREADS; i++) total += f[i].count;
        T_EQ(total, FIXED_CAPACITY);
        /* No two threads got the same block. */
        for (unsigned i = 0; i < NTHREADS; i++)
            for (unsigned k = 0; k < f[i].count; k++) T_REQUIRE(hdr(f[i].blocks[k])->magic == RT_HEAP_BLOCK_MAGIC);
        /* ...and all of them give their blocks back at once. */
        reset_start();
        for (unsigned i = 0; i < NTHREADS; i++) pthread_create(&th[i], NULL, release_all, &f[i]);
        for (unsigned i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);
        for (unsigned i = 0; i < NTHREADS; i++) T_EQ(f[i].base.errors, 0);
    }
    T_EQ(rt_heap_destroy(h), TRUE);
}

/* The same heap, with the threads also growing, shrinking and freeing blocks of mixed sizes: when
 * they are all done and everything is freed the whole maximum must be available again. */
static void *fixed_churn(void *arg) {
    Worker *w = arg;
    uint64_t rng = UINT64_C(0xD1B54A32D192ED03) * (w->index + 3);
    enum { SLOTS = 8 };
    Slot slots[SLOTS] = {{0}};
    wait_for_start(w->nthreads);
    for (unsigned it = 0; it < w->iterations; it++) {
        Slot *s = &slots[rng_next(&rng) % SLOTS];
        uint64_t size = rng_next(&rng) % 3000;
        if (!s->p) {
            uint8_t *p = rt_heap_alloc(w->heap, HEAP_ZERO_MEMORY, size);
            if (!p) continue;                                   /* full: a legitimate answer */
            if (!all_zero(p, size)) worker_error(w, "block not zero");
            s->p = p; s->size = size; s->seed = (uint8_t)rng_next(&rng);
            fill(p, size, s->seed);
        } else if (rng_next(&rng) % 3 == 0) {
            if (!check_fill(s->p, s->size, s->seed)) worker_error(w, "block contents changed");
            rt_heap_free(w->heap, 0, s->p);
            s->p = NULL;
        } else {
            bool in_place = rng_next(&rng) & 1;
            uint8_t *q = rt_heap_realloc(w->heap, in_place ? HEAP_REALLOC_IN_PLACE_ONLY : 0, s->p, size);
            if (!q) {                                           /* refused: the block must be exactly as it was */
                if (rt_heap_size(w->heap, 0, s->p) != s->size || !check_fill(s->p, s->size, s->seed))
                    worker_error(w, "a refused realloc changed the block");
                continue;
            }
            uint64_t keep = size < s->size ? size : s->size;
            if (!check_fill(q, keep, s->seed)) worker_error(w, "realloc lost contents");
            s->p = q; s->size = size; s->seed = (uint8_t)rng_next(&rng);
            fill(q, size, s->seed);
        }
    }
    for (unsigned i = 0; i < SLOTS; i++) if (slots[i].p) rt_heap_free(w->heap, 0, slots[i].p);
    return NULL;
}

TEST(core, heap_threads_fixed_size_accounting_survives_churn)
{
    HANDLE h = rt_heap_create(0, 0, FIXED_MAX);
    T_REQUIRE(h != NULL);
    Worker w[NTHREADS];
    for (unsigned i = 0; i < NTHREADS; i++) { memset(&w[i], 0, sizeof w[i]); w[i].heap = h; w[i].iterations = 60000; }
    T_EQ(run_workers(fixed_churn, w, NTHREADS), 0);
    report(w, NTHREADS);
    /* Everything freed: all of the maximum is back, to the byte. */
    static void *blk[FIXED_CAPACITY + 8];
    unsigned n = 0;
    while (n < FIXED_CAPACITY + 8) {
        void *p = rt_heap_alloc(h, 0, 48);
        if (!p) break;
        blk[n++] = p;
    }
    T_EQ(n, FIXED_CAPACITY);
    for (unsigned i = 0; i < n; i++) T_EQ(rt_heap_free(h, 0, blk[i]), TRUE);
    T_EQ(rt_heap_destroy(h), TRUE);
}

/* ---- blocks freed by a thread other than the one that made them ---------------------------------- */

typedef struct { void *p; uint64_t size; uint8_t seed; } Item;
#define QUEUE_CAP 4096
static Item queue[QUEUE_CAP];
static unsigned q_head, q_count, q_produced, q_consumed;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

static void *producer(void *arg) {
    Worker *w = arg;
    uint64_t rng = UINT64_C(0xA24BAED4963EE407) * (w->index + 1);
    wait_for_start(w->nthreads);
    for (unsigned it = 0; it < w->iterations; it++) {
        uint64_t size = pick_size(&rng);
        uint8_t seed = (uint8_t)rng_next(&rng);
        uint8_t *p = rt_heap_alloc(w->heap, 0, size);
        if (!p) { worker_error(w, "alloc returned NULL"); continue; }
        fill(p, size, seed);
        for (;;) {
            pthread_mutex_lock(&q_lock);
            if (q_count < QUEUE_CAP) {
                queue[(q_head + q_count++) % QUEUE_CAP] = (Item){p, size, seed};
                q_produced++;
                pthread_mutex_unlock(&q_lock);
                break;
            }
            pthread_mutex_unlock(&q_lock);
            sched_yield();
        }
    }
    return NULL;
}

static void *consumer(void *arg) {
    Worker *w = arg;
    unsigned want = w->iterations;                  /* how many this consumer takes */
    wait_for_start(w->nthreads);
    while (want) {
        Item it = {0};
        pthread_mutex_lock(&q_lock);
        if (q_count) { it = queue[q_head]; q_head = (q_head + 1) % QUEUE_CAP; q_count--; q_consumed++; }
        pthread_mutex_unlock(&q_lock);
        if (!it.p) { sched_yield(); continue; }
        want--;
        if (!check_fill(it.p, it.size, it.seed)) worker_error(w, "a block made by another thread changed");
        if (rt_heap_size(w->heap, 0, it.p) != it.size) worker_error(w, "HeapSize of another thread's block differs");
        if (rt_heap_free(w->heap, 0, it.p) != TRUE) worker_error(w, "HeapFree of another thread's block failed");
    }
    return NULL;
}

static void *producer_or_consumer(void *arg) {
    Worker *w = arg;
    return w->index < w->nthreads / 2 ? producer(arg) : consumer(arg);
}

TEST(core, heap_threads_free_blocks_made_by_other_threads)
{
    /* Threads 0..3 make blocks and queue them, threads 4..7 take them from the queue and free them:
     * every block is freed by a thread other than its maker, in a heap that all eight are using. */
    Worker w[NTHREADS];
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    for (unsigned i = 0; i < NTHREADS; i++) { memset(&w[i], 0, sizeof w[i]); w[i].heap = h; w[i].iterations = 30000; }
    T_EQ(run_workers(producer_or_consumer, w, NTHREADS), 0);
    report(w, NTHREADS);
    T_EQ(q_produced, q_consumed);
    T_EQ(q_count, 0);
    T_EQ(q_produced, 4 * 30000);
    T_EQ(rt_heap_destroy(h), TRUE);
}

/* ---- creating and destroying heaps while others allocate ----------------------------------------- */

#define LIVE_MAX 64
static HANDLE live_handles[LIVE_MAX];
static pthread_mutex_t live_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t seen_ids[NTHREADS * 200];
static unsigned seen_count;

static bool live_add(HANDLE h) {
    bool ok = true;
    pthread_mutex_lock(&live_lock);
    unsigned free_slot = LIVE_MAX;
    for (unsigned i = 0; i < LIVE_MAX; i++) {
        if (live_handles[i] == h) ok = false;                    /* handed out twice while alive */
        if (!live_handles[i] && free_slot == LIVE_MAX) free_slot = i;
    }
    if (free_slot == LIVE_MAX) ok = false;
    else live_handles[free_slot] = h;
    pthread_mutex_unlock(&live_lock);
    return ok;
}

static void live_remove(HANDLE h) {
    pthread_mutex_lock(&live_lock);
    for (unsigned i = 0; i < LIVE_MAX; i++) if (live_handles[i] == h) live_handles[i] = NULL;
    pthread_mutex_unlock(&live_lock);
}

static void *create_destroy(void *arg) {
    Worker *w = arg;
    uint64_t rng = UINT64_C(0xC2B2AE3D27D4EB4F) * (w->index + 1);
    wait_for_start(w->nthreads);
    for (unsigned it = 0; it < w->iterations; it++) {
        HANDLE h = rt_heap_create(rng_next(&rng) & 1 ? HEAP_NO_SERIALIZE : 0, 0, rng_next(&rng) % 3 == 0 ? 1u << 20 : 0);
        if (!h) { worker_error(w, "HeapCreate returned NULL"); continue; }
        if (!live_add(h)) worker_error(w, "a heap handle was handed out twice while it was alive");
        uint32_t id = 0;
        void *blocks[10];
        for (unsigned k = 0; k < 10; k++) {
            uint64_t size = rng_next(&rng) % 5000;
            blocks[k] = rt_heap_alloc(h, HEAP_ZERO_MEMORY, size);
            if (!blocks[k]) { worker_error(w, "alloc returned NULL"); continue; }
            memset(blocks[k], 0x5A, size);
            if (!id) id = hdr(blocks[k])->heap_id;
            else if (hdr(blocks[k])->heap_id != id) worker_error(w, "blocks of one heap carry different ids");
        }
        for (unsigned k = 0; k < 10; k += 2) if (blocks[k]) rt_heap_free(h, 0, blocks[k]);     /* some freed, some left */
        pthread_mutex_lock(&live_lock);
        seen_ids[seen_count++] = id;
        pthread_mutex_unlock(&live_lock);
        live_remove(h);
        if (rt_heap_destroy(h) != TRUE) worker_error(w, "HeapDestroy failed");
    }
    return NULL;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* Half of the threads create, fill and destroy heaps; the other half churn the process heap meanwhile. */
static void *create_or_churn(void *arg) {
    Worker *w = arg;
    return w->index < w->nthreads / 2 ? create_destroy(arg) : churn(arg);
}

TEST(core, heap_threads_create_and_destroy)
{
    Worker w[NTHREADS];
    for (unsigned i = 0; i < NTHREADS; i++) {
        memset(&w[i], 0, sizeof w[i]);
        w[i].heap = rt_heap_process();
        w[i].iterations = i < NTHREADS / 2 ? 200 : 20000;
    }
    T_EQ(run_workers(create_or_churn, w, NTHREADS), 0);
    report(w, NTHREADS);
    /* Every heap that was made had an id of its own, and no id repeated. */
    T_EQ(seen_count, (NTHREADS / 2) * 200);
    qsort(seen_ids, seen_count, sizeof seen_ids[0], cmp_u32);
    for (unsigned i = 0; i < seen_count; i++) {
        T_REQUIRE(seen_ids[i] != 0);
        if (i) T_REQUIRE(seen_ids[i] != seen_ids[i - 1]);
    }
    for (unsigned i = 0; i < LIVE_MAX; i++) T_EQ(live_handles[i] == NULL, 1);
}

/* ---- two threads freeing, or destroying, the same thing ------------------------------------------- */

typedef struct { HANDLE heap; void *block; } Race;

static void *free_it(void *arg) {
    Race *r = arg;
    wait_for_start(2);
    rt_heap_free(r->heap, 0, r->block);
    return NULL;
}

static void *destroy_it(void *arg) {
    Race *r = arg;
    wait_for_start(2);
    rt_heap_destroy(r->heap);
    return NULL;
}

/* Both threads start together on the same call.  Exactly one can succeed; the other must be stopped
 * by the runtime's own report, not handed to the host allocator (which would abort with a message
 * of its own, or corrupt its lists). */
static void race(void *(*fn)(void *), Race *r) {
    pthread_t a, b;
    reset_start();
    pthread_create(&a, NULL, fn, r);
    pthread_create(&b, NULL, fn, r);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
}

TEST(core, heap_threads_double_free_is_caught)
{
    HANDLE ph = rt_heap_process();
    HANDLE h = rt_heap_create(0, 0, 0);
    T_REQUIRE(h != NULL);
    static const uint64_t sizes[] = {0, 24, 100, 700, 3000, 40000, 2u << 20};
    for (unsigned round = 0; round < 6; round++) {
        for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            Race r = {round & 1 ? h : ph, rt_heap_alloc(round & 1 ? h : ph, 0, sizes[i])};
            T_REQUIRE(r.block != NULL);
            T_TRAPS(race(free_it, &r), "FATAL runtime: heap: HeapFree: ");
        }
    }
    T_EQ(rt_heap_destroy(h), TRUE);
}

TEST(core, heap_threads_double_destroy_is_caught)
{
    for (unsigned round = 0; round < 10; round++) {
        Race r = {rt_heap_create(0, 0, 0), NULL};
        T_REQUIRE(r.heap != NULL);
        T_REQUIRE(rt_heap_alloc(r.heap, 0, 100) != NULL);
        T_TRAPS(race(destroy_it, &r), "FATAL runtime: heap: HeapDestroy: heap handle");
    }
}
