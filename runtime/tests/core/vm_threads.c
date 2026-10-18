/* The VM map from several threads at once (docs/RUNTIME.md section 5.2: "used from several
 * threads: one lock").
 *
 * The threads are plain host threads: the map's functions take no Ctx.  They record what
 * went wrong in their own struct and the main thread checks it after joining them, because
 * the test macros are not for several threads.  A fault in a thread kills the test process,
 * which fails the test.  The threads do not look at last error: outside a guest thread it
 * is one process-wide variable.  Helpers: vm_util.c.
 */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "vm_util.c"

enum { MAX_THREADS = 8 };

typedef struct {
    unsigned  index;
    uint64_t  rng;
    int       bad_line;             /* __LINE__ of the first thing that went wrong, or 0 */
    uint64_t  bad_value;
    uint64_t  count;
    uint64_t  arg;
} Worker;

static atomic_int go;

#define W_CHECK(w, cond, value) \
    do { if (!(cond) && !(w)->bad_line) { (w)->bad_line = __LINE__; (w)->bad_value = (uint64_t)(value); } } while (0)

static unsigned w_rnd(Worker *w, unsigned n) { return (unsigned)((vu_next(&w->rng) >> 11) % n); }

static void start_together(void) { while (!atomic_load(&go)) sched_yield(); }

/* The harness captures the `vm` channel at debug level, one line per call of the map.  The
 * threads here make hundreds of thousands of calls, so the channel is turned down while
 * they run: the log's own lock would otherwise be most of what is being tested. */
static uint8_t saved_level;
static void quiet(void)   { saved_level = rt_log_level[RT_CH_VM]; rt_log_level[RT_CH_VM] = RT_LL_WARN; }
static void unquiet(void) { rt_log_level[RT_CH_VM] = saved_level; }

/* Run `fn` on n threads that start together; report what each recorded. */
static void run_workers(unsigned n, void *(*fn)(void *), Worker *w, uint64_t arg) {
    pthread_t tid[MAX_THREADS];
    atomic_store(&go, 0);
    quiet();
    for (unsigned i = 0; i < n; i++) {
        w[i] = (Worker){ .index = i, .rng = 0x9E3779B97F4A7C15 * (i + 1), .arg = arg };
        T_REQUIRE(pthread_create(&tid[i], NULL, fn, &w[i]) == 0);
    }
    atomic_store(&go, 1);
    for (unsigned i = 0; i < n; i++) pthread_join(tid[i], NULL);
    unquiet();
    for (unsigned i = 0; i < n; i++) {
        if (w[i].bad_line)
            rt_test_fail(__FILE__, w[i].bad_line, "thread %u: check failed (value %#llx) after %llu rounds", i,
                         (unsigned long long)w[i].bad_value, (unsigned long long)w[i].count);
        T_EQ(w[i].bad_line, 0);
    }
}

/* ---- each thread with regions of its own ------------------------------------------------- */

static void *own_regions(void *arg) {
    Worker *w = arg;
    uint8_t tag = (uint8_t)(0x10 + w->index);
    start_together();
    for (unsigned round = 0; round < 1500 && !w->bad_line; round++, w->count++) {
        unsigned pages = 1 + w_rnd(w, 40);
        uint64_t r = rt_vm_alloc(0, pages * PG, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
        W_CHECK(w, r != 0 && (r & 0xFFFF) == 0 && rt_vm_in_vm_arena(r), r);
        if (!r) break;
        unsigned a = w_rnd(w, pages), n = 1 + w_rnd(w, pages - a);
        uint64_t at = r + a * PG;
        W_CHECK(w, rt_vm_alloc(at, n * PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == at, at);
        /* Fresh memory is zero; if another thread had been given an overlapping region
         * its bytes would show here or ours would vanish below. */
        W_CHECK(w, vu_all_zero(at, n * PG), at);
        vu_fill(at, n * PG, tag);
        sched_yield();
        W_CHECK(w, vu_all(at, n * PG, tag), at);

        MEMORY_BASIC_INFORMATION q;
        W_CHECK(w, rt_vm_query(at, &q) == sizeof q, at);
        W_CHECK(w, (uint64_t)(uintptr_t)q.AllocationBase == r && q.RegionSize == n * PG && q.State == MEM_COMMIT &&
                   q.Protect == PAGE_READWRITE && q.Type == MEM_PRIVATE, q.RegionSize);
        DWORD old = 0;
        W_CHECK(w, rt_vm_protect(at, n * PG, PAGE_READONLY, &old) && old == PAGE_READWRITE, old);
        W_CHECK(w, vu_all(at, n * PG, tag), at);
        if (n > 1) {
            W_CHECK(w, rt_vm_free(at, PG, MEM_DECOMMIT), at);
            W_CHECK(w, vu_all(at + PG, (n - 1) * PG, tag), at);
            W_CHECK(w, rt_vm_alloc(at, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == at, at);
            W_CHECK(w, vu_all_zero(at, PG), at);
        }
        W_CHECK(w, rt_vm_free(r, 0, MEM_RELEASE), r);
    }
    return NULL;
}

TEST(core, vm_threads_own_regions)
{
    /* Eight threads reserve, commit, write, query, protect, decommit and release regions
     * of their own, all through first fit, all at the bottom of the same arena. */
    Worker w[MAX_THREADS];
    run_workers(8, own_regions, w, 0);
    for (unsigned i = 0; i < 8; i++) T_EQ(w[i].count, 1500);
    /* Everything was released: first fit is back where it started. */
    uint64_t a = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(a != 0);
    T_TRUE(rt_vm_free(a, 0, MEM_RELEASE));
    Q_FREE(a, VM_END - a);
}

/* ---- threads that share host pages -------------------------------------------------------- */

enum { SHARED_PAGES = 64 };

/* Thread t owns the guest pages of the region whose index is t modulo the number of
 * threads, so with four threads and four guest pages per host page every host page is
 * shared by all of them. */
static void *shared_host_pages_worker(void *arg) {
    Worker *w = arg;
    const uint64_t r = PLAY(0);
    const unsigned nthreads = (unsigned)w->arg;
    start_together();
    for (unsigned round = 0; round < 6000 && !w->bad_line; round++, w->count++) {
        uint64_t page = r + (w->index + nthreads * w_rnd(w, SHARED_PAGES / nthreads)) * PG;
        uint8_t tag = (uint8_t)(1 + w_rnd(w, 255));
        DWORD old = 0;
        /* While this runs, the other threads commit, protect and decommit the other guest
         * pages of the same host page.  Our page must stay ours: zero when committed,
         * then exactly what we wrote, readable and writable whenever its own protection
         * says so. */
        W_CHECK(w, rt_vm_alloc(page, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == page, page);
        W_CHECK(w, vu_all_zero(page, PG), page);
        vu_fill(page, PG, tag);
        if (w_rnd(w, 4) == 0) sched_yield();
        W_CHECK(w, vu_all(page, PG, tag), page);
        W_CHECK(w, rt_vm_protect(page, PG, PAGE_READONLY, &old) && old == PAGE_READWRITE, old);
        W_CHECK(w, vu_all(page, PG, tag), page);
        if (w_rnd(w, 4) == 0) {
            /* Through no-access and back: the data must survive whatever the neighbours do. */
            W_CHECK(w, rt_vm_protect(page, PG, PAGE_NOACCESS, &old) && old == PAGE_READONLY, old);
            sched_yield();
            W_CHECK(w, rt_vm_protect(page, PG, PAGE_READONLY, &old) && old == PAGE_NOACCESS, old);
            W_CHECK(w, vu_all(page, PG, tag), page);
        }
        W_CHECK(w, rt_vm_protect(page, PG, PAGE_READWRITE, &old) && old == PAGE_READONLY, old);
        vu_fill(page, PG, (uint8_t)~tag);
        W_CHECK(w, vu_all(page, PG, (uint8_t)~tag), page);
        MEMORY_BASIC_INFORMATION q;
        W_CHECK(w, rt_vm_query(page, &q) == sizeof q && q.State == MEM_COMMIT && q.Protect == PAGE_READWRITE &&
                   (uint64_t)(uintptr_t)q.AllocationBase == r, q.State);
        W_CHECK(w, rt_vm_free(page, PG, MEM_DECOMMIT), page);
        W_CHECK(w, rt_vm_query(page, &q) == sizeof q && q.State == MEM_RESERVE && q.Protect == 0, q.State);
    }
    return NULL;
}

TEST(core, vm_threads_shared_host_pages)
{
    const unsigned per = (unsigned)(rt_vm_host_page() / PG);
    const unsigned nthreads = per >= 2 && per <= MAX_THREADS ? per : 4;
    Worker w[MAX_THREADS];
    T_REQUIRE(rt_vm_alloc(PLAY(0), SHARED_PAGES * PG, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == PLAY(0));
    run_workers(nthreads, shared_host_pages_worker, w, nthreads);
    for (unsigned i = 0; i < nthreads; i++) T_EQ(w[i].count, 6000);
    /* Every thread decommitted what it committed: the region is reserved and holds nothing. */
    Q_RESERVED(PLAY(0), PLAY(0), PAGE_NOACCESS, SHARED_PAGES * PG);
    for (uint64_t a = PLAY(0); a < PLAY(0) + SHARED_PAGES * PG; a += rt_vm_host_page()) {
        T_EQ(vu_host_prot(a), PROT_NONE);
        T_TRUE(!vu_resident(a));
    }
}

/* ---- queries while another thread changes the map ----------------------------------------- */

static atomic_int stop_flipping;
static atomic_int flipping;       /* the flipper has finished a round (or given up): the askers may start */

static void *flipper(void *arg) {
    Worker *w = arg;
    const uint64_t r = PLAY(0);
    start_together();
    while (!atomic_load(&stop_flipping) && !w->bad_line) {
        unsigned a = w_rnd(w, 32), n = 1 + w_rnd(w, 32 - a);
        DWORD old;
        switch (w_rnd(w, 4)) {
        case 0:  W_CHECK(w, rt_vm_alloc(r + a * PG, n * PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) != 0, a); break;
        case 1:  W_CHECK(w, rt_vm_alloc(r + a * PG, n * PG, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE) != 0, a); break;
        case 2:  W_CHECK(w, rt_vm_free(r + a * PG, n * PG, MEM_DECOMMIT), a); break;
        default: (void)rt_vm_protect(r + a * PG, n * PG, PAGE_READWRITE, &old); break;   /* fails when a page is not committed */
        }
        /* A second region that comes and goes next to the first. */
        if (w_rnd(w, 8) == 0) {
            uint64_t b = rt_vm_alloc(r + 0x20000, 0x8000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
            W_CHECK(w, b == r + 0x20000, b);
            W_CHECK(w, rt_vm_free(b, 0, MEM_RELEASE), b);
        }
        w->count++;
        atomic_store(&flipping, 1);
    }
    atomic_store(&flipping, 1);
    return NULL;
}

static void *asker(void *arg) {
    Worker *w = arg;
    const uint64_t r = PLAY(0);
    if (w->index == 0) return flipper(arg);
    start_together();
    /* Wait for the flipper's first round.  On a loaded machine the five askers used to finish
     * all their rounds before the flipper was scheduled once, and the test then failed its own
     * "the flipper did something" check without having tested anything. */
    while (!atomic_load(&flipping)) sched_yield();
    for (unsigned round = 0; round < 40000 && !w->bad_line; round++, w->count++) {
        MEMORY_BASIC_INFORMATION q;
        uint64_t at = r + w_rnd(w, 0x30000);
        W_CHECK(w, rt_vm_query(at, &q) == sizeof q, at);
        uint64_t base = (uint64_t)(uintptr_t)q.BaseAddress, abase = (uint64_t)(uintptr_t)q.AllocationBase;
        W_CHECK(w, base == (at & ~UINT64_C(0xFFF)) && q.RegionSize >= PG && (q.RegionSize & 0xFFF) == 0, q.RegionSize);
        if (at < r + 0x20000) {
            /* The first region is always there: every answer is one of its possible runs. */
            W_CHECK(w, abase == r && q.Type == MEM_PRIVATE && q.AllocationProtect == PAGE_NOACCESS, abase);
            W_CHECK(w, base + q.RegionSize <= r + 0x20000, q.RegionSize);
            W_CHECK(w, (q.State == MEM_RESERVE && q.Protect == 0) ||
                       (q.State == MEM_COMMIT && (q.Protect == PAGE_READWRITE || q.Protect == PAGE_READONLY)), q.Protect);
        } else if (q.State == MEM_FREE) {
            W_CHECK(w, abase == 0 && q.Protect == PAGE_NOACCESS && q.Type == 0, abase);
        } else {
            /* The second region, caught while it exists: whole and committed. */
            W_CHECK(w, abase == r + 0x20000 && q.State == MEM_COMMIT && q.Protect == PAGE_READWRITE &&
                       base + q.RegionSize == r + 0x28000, q.RegionSize);
        }
    }
    return NULL;
}

TEST(core, vm_threads_query_while_changing)
{
    /* One thread changes a region's pages and creates and releases a neighbour; five
     * others ask about them.  Every answer is a state the map was in at some moment:
     * never half an update. */
    Worker w[MAX_THREADS];
    T_REQUIRE(rt_vm_alloc(PLAY(0), 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == PLAY(0));
    atomic_store(&stop_flipping, 0);
    atomic_store(&flipping, 0);

    pthread_t tid[6];
    atomic_store(&go, 0);
    quiet();
    for (unsigned i = 0; i < 6; i++) {
        w[i] = (Worker){ .index = i, .rng = 0xD1B54A32D192ED03 * (i + 1) };
        T_REQUIRE(pthread_create(&tid[i], NULL, asker, &w[i]) == 0);
    }
    atomic_store(&go, 1);
    for (unsigned i = 1; i < 6; i++) pthread_join(tid[i], NULL);
    atomic_store(&stop_flipping, 1);
    pthread_join(tid[0], NULL);
    unquiet();
    for (unsigned i = 0; i < 6; i++) {
        if (w[i].bad_line)
            rt_test_fail(__FILE__, w[i].bad_line, "thread %u: check failed (value %#llx) after %llu rounds", i,
                         (unsigned long long)w[i].bad_value, (unsigned long long)w[i].count);
        T_EQ(w[i].bad_line, 0);
    }
    T_TRUE(w[0].count > 0);
    for (unsigned i = 1; i < 6; i++) T_EQ(w[i].count, 40000);
}

/* ---- races for the same thing -------------------------------------------------------------- */

static atomic_int owners;
static atomic_ullong wins;

static void *same_address(void *arg) {
    Worker *w = arg;
    const uint64_t x = PLAY(0);
    start_together();
    for (unsigned round = 0; round < 20000 && !w->bad_line; round++, w->count++) {
        /* Everyone asks for the same range.  Whoever gets it must be alone with it. */
        if (rt_vm_alloc(x + w_rnd(w, 0x10000), 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == x) {
            W_CHECK(w, atomic_fetch_add(&owners, 1) == 0, 1);
            W_CHECK(w, LD64(x) == 0, LD64(x));
            ST64(x, w->index + 1);
            sched_yield();
            W_CHECK(w, LD64(x) == w->index + 1, LD64(x));
            atomic_fetch_add(&wins, 1);
            atomic_fetch_sub(&owners, 1);
            W_CHECK(w, rt_vm_free(x, 0, MEM_RELEASE), x);
        }
    }
    return NULL;
}

TEST(core, vm_threads_same_address)
{
    Worker w[MAX_THREADS];
    atomic_store(&owners, 0);
    atomic_store(&wins, 0);
    run_workers(8, same_address, w, 0);
    T_TRUE(atomic_load(&wins) > 0);
    T_EQ(atomic_load(&owners), 0);
    Q_FREE(PLAY(0), VM_END - PLAY(0));
}

enum { KEEP = 400 };
static uint64_t kept[MAX_THREADS][KEEP];

static void *first_fit_keep(void *arg) {
    Worker *w = arg;
    start_together();
    for (unsigned i = 0; i < KEEP; i++, w->count++) {
        uint64_t size = (1 + w_rnd(w, 3)) * 0x10000 - w_rnd(w, 0x1000);
        kept[w->index][i] = rt_vm_alloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
        W_CHECK(w, kept[w->index][i] != 0, i);
        if (kept[w->index][i]) ST64(kept[w->index][i], kept[w->index][i]);
    }
    return NULL;
}

static atomic_ullong released;

static void *release_race(void *arg) {
    Worker *w = arg;
    start_together();
    /* Every thread tries to release every region: each release must succeed exactly once. */
    for (unsigned t = 0; t < 8; t++)
        for (unsigned i = 0; i < KEEP; i++, w->count++)
            if (rt_vm_free(kept[(t + w->index) % 8][i], 0, MEM_RELEASE)) atomic_fetch_add(&released, 1);
    return NULL;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

TEST(core, vm_threads_first_fit)
{
    /* Eight threads take 400 regions each through first fit and keep them: 3,200 regions,
     * all different, none overlapping, each still holding what its owner wrote. */
    Worker w[MAX_THREADS];
    static uint64_t all[MAX_THREADS * KEEP];
    run_workers(8, first_fit_keep, w, 0);
    unsigned n = 0;
    for (unsigned t = 0; t < 8; t++) for (unsigned i = 0; i < KEEP; i++) all[n++] = kept[t][i];
    qsort(all, n, sizeof all[0], cmp_u64);
    bool ok = true;
    for (unsigned i = 0; i < n; i++) {
        MEMORY_BASIC_INFORMATION q;
        if (!all[i] || (all[i] & 0xFFFF) || !rt_vm_in_vm_arena(all[i]) || LD64(all[i]) != all[i]) { ok = false; break; }
        if (rt_vm_query(all[i], &q) != sizeof q || (uint64_t)(uintptr_t)q.AllocationBase != all[i] || q.State != MEM_COMMIT) { ok = false; break; }
        if (i + 1 < n && all[i] + q.RegionSize > all[i + 1]) { ok = false; break; }
    }
    T_TRUE(ok);
    /* First fit left no hole a region would fit in: the regions are packed from the
     * lowest one up, at most 64 KB minus a page of slack after each. */
    for (unsigned i = 0; ok && i + 1 < n; i++) {
        MEMORY_BASIC_INFORMATION q;
        T_REQUIRE(rt_vm_query(all[i], &q) == sizeof q);
        T_REQUIRE(all[i + 1] == ((all[i] + q.RegionSize + 0xFFFF) & ~UINT64_C(0xFFFF)));
    }

    atomic_store(&released, 0);
    run_workers(8, release_race, w, 0);
    T_EQ(atomic_load(&released), n);
    Q_FREE(all[0], VM_END - all[0]);
}
