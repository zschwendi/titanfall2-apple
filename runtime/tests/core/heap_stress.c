/* A long random workload on three heaps at once (the process heap, a growable heap and a fixed-size
 * one), checked against a model kept by the test: what every block holds, how big it is, that no two
 * overlap, which operations must succeed and which must fail, and, for the fixed-size heap, exactly
 * how much room is left.  Single-threaded: the concurrent cases are in heap_threads.c.
 */
#include <malloc/malloc.h>
#include <stdlib.h>

#include "rt/test.h"
#include "rt/heap.h"
#include "rt/machine.h"

static RtHeapBlock *hdr(const void *p) { return (RtHeapBlock *)p - 1; }

static uint64_t rng_next(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545F4914F6CDD1D);
}

static void fill(void *p, uint64_t n, uint8_t seed) {
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + i * 11);
}

static bool check_fill(const void *p, uint64_t n, uint8_t seed) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i] != (uint8_t)(seed + i * 11)) return false;
    return true;
}

static bool all_zero(const void *p, uint64_t n) {
    const uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++) if (b[i]) return false;
    return true;
}

typedef struct { uint8_t *p; uint64_t size; uint8_t seed; } Block;

typedef struct {
    HANDLE   heap;
    uint32_t id;               /* the heap id its blocks carry, learned from the first block */
    uint64_t maximum;          /* fixed-size heaps: the maximum rounded to 4 KB; 0 = growable */
    uint64_t charged;          /* fixed-size heaps: what the model says is in use */
    Block    blocks[300];
    unsigned live;
} Model;

static uint64_t charge(uint64_t size) { return 16 + ((size + 15) & ~UINT64_C(15)); }

static uint64_t pick_size(uint64_t *s) {
    uint64_t r = rng_next(s);
    unsigned k = r % 100;
    r >>= 8;
    if (k < 60) return r % 300;
    if (k < 90) return r % 5000;
    if (k < 99) return r % 70000;
    return r % 1500000;
}

static unsigned failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; if (failures <= 5) rt_test_fail(__FILE__, __LINE__, __VA_ARGS__); } } while (0)

static void verify_block(Model *m, const Block *b) {
    CHECK(((uint64_t)b->p & 15) == 0, "block %p not aligned", (void *)b->p);
    CHECK(hdr(b->p)->magic == RT_HEAP_BLOCK_MAGIC, "block %p lost its magic", (void *)b->p);
    CHECK(hdr(b->p)->heap_id == m->id, "block %p carries heap id %u, not %u", (void *)b->p, hdr(b->p)->heap_id, m->id);
    CHECK(hdr(b->p)->size == b->size, "block %p header size %llu, not %llu", (void *)b->p, (unsigned long long)hdr(b->p)->size, (unsigned long long)b->size);
    CHECK(rt_heap_size(m->heap, 0, b->p) == b->size, "HeapSize of %p differs from the size asked", (void *)b->p);
    CHECK(check_fill(b->p, b->size, b->seed), "contents of %p (%llu bytes) changed", (void *)b->p, (unsigned long long)b->size);
}

static int by_address(const void *a, const void *b) {
    uint64_t x = (uint64_t)((const Block *)a)->p, y = (uint64_t)((const Block *)b)->p;
    return x < y ? -1 : x > y;
}

/* No two live blocks, in any of the heaps, overlap: ranges [p - 16, p + size). */
static void check_no_overlap(Model *models, unsigned nmodels) {
    static Block all[3 * 300];
    unsigned n = 0;
    for (unsigned i = 0; i < nmodels; i++)
        for (unsigned k = 0; k < models[i].live; k++) all[n++] = models[i].blocks[k];
    qsort(all, n, sizeof all[0], by_address);
    for (unsigned i = 1; i < n; i++)
        CHECK((uint64_t)all[i - 1].p + all[i - 1].size <= (uint64_t)all[i].p - 16, "blocks %p and %p overlap",
              (void *)all[i - 1].p, (void *)all[i].p);
}

static void op_alloc(Model *m, uint64_t *rng) {
    if (m->live == 300) return;
    uint64_t size = pick_size(rng);
    static const DWORD flag_sets[] = {0, HEAP_ZERO_MEMORY, HEAP_NO_SERIALIZE, HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY};
    DWORD flags = flag_sets[rng_next(rng) % 4];
    bool fits = !m->maximum || (size < 0xFF000 && m->charged + charge(size) <= m->maximum);
    uint8_t *p = rt_heap_alloc(m->heap, flags, size);
    CHECK((p != NULL) == fits, "alloc of %llu bytes: %s, but the model says it %s", (unsigned long long)size,
          p ? "succeeded" : "failed", fits ? "fits" : "does not fit");
    if (!p) return;
    if (flags & HEAP_ZERO_MEMORY) CHECK(all_zero(p, size), "HEAP_ZERO_MEMORY block of %llu bytes not zero", (unsigned long long)size);
    if (!m->id) m->id = hdr(p)->heap_id;
    Block b = {p, size, (uint8_t)rng_next(rng)};
    fill(p, size, b.seed);
    m->blocks[m->live++] = b;
    if (m->maximum) m->charged += charge(size);
    verify_block(m, &b);
}

static void op_free(Model *m, uint64_t *rng) {
    if (!m->live) return;
    unsigned i = rng_next(rng) % m->live;
    Block b = m->blocks[i];
    verify_block(m, &b);
    CHECK(rt_heap_free(m->heap, rng_next(rng) & 1 ? HEAP_NO_SERIALIZE : 0, b.p) == TRUE, "HeapFree failed");
    if (m->maximum) m->charged -= charge(b.size);
    m->blocks[i] = m->blocks[--m->live];
}

static void op_realloc(Model *m, uint64_t *rng) {
    if (!m->live) return;
    unsigned i = rng_next(rng) % m->live;
    Block *b = &m->blocks[i];
    verify_block(m, b);
    uint64_t size = pick_size(rng);
    /* Resizes near the old size are the interesting ones: the same, one more, one less, up to the next 16. */
    switch (rng_next(rng) % 6) {
    case 0: size = b->size; break;
    case 1: size = b->size + 1; break;
    case 2: size = b->size ? b->size - 1 : 0; break;
    case 3: size = b->size + (rng_next(rng) % 40); break;
    default: break;
    }
    static const DWORD flag_sets[] = {0, HEAP_ZERO_MEMORY, HEAP_REALLOC_IN_PLACE_ONLY, HEAP_REALLOC_IN_PLACE_ONLY | HEAP_ZERO_MEMORY,
                                      HEAP_NO_SERIALIZE | HEAP_ZERO_MEMORY};
    DWORD flags = flag_sets[rng_next(rng) % 5];
    uint64_t room = malloc_size(hdr(b->p));                    /* what the block really holds, header included */
    bool grow = size > b->size;
    bool limit_ok = !m->maximum || (size < 0xFF000 && (!grow || m->charged - charge(b->size) + charge(size) <= m->maximum));
    bool in_place_ok = !(flags & HEAP_REALLOC_IN_PLACE_ONLY) || !grow || 16 + size <= room;
    bool should_work = limit_ok && in_place_ok;

    uint8_t *old = b->p;
    uint64_t old_size = b->size;
    uint8_t old_seed = b->seed;
    uint8_t *q = rt_heap_realloc(m->heap, flags, old, size);
    CHECK((q != NULL) == should_work, "realloc %llu -> %llu with flags %#x: %s, but the model says it %s",
          (unsigned long long)old_size, (unsigned long long)size, flags, q ? "succeeded" : "failed",
          should_work ? "must work" : "must fail");
    if (!q) {                                                   /* a failure leaves the block exactly as it was */
        CHECK(rt_heap_size(m->heap, 0, old) == old_size && check_fill(old, old_size, old_seed), "a failed realloc changed the block");
        return;
    }
    if ((flags & HEAP_REALLOC_IN_PLACE_ONLY)) CHECK(q == old, "HEAP_REALLOC_IN_PLACE_ONLY moved the block");
    uint64_t keep = size < old_size ? size : old_size;
    CHECK(check_fill(q, keep, old_seed), "realloc %llu -> %llu lost the old contents", (unsigned long long)old_size, (unsigned long long)size);
    if ((flags & HEAP_ZERO_MEMORY) && grow)
        CHECK(all_zero(q + old_size, size - old_size), "realloc %llu -> %llu: the added bytes are not zero", (unsigned long long)old_size, (unsigned long long)size);
    if (m->maximum) m->charged = m->charged - charge(old_size) + charge(size);
    b->p = q;
    b->size = size;
    b->seed = (uint8_t)rng_next(rng);
    fill(q, size, b->seed);
    verify_block(m, b);
}

static void run_model(unsigned steps, uint64_t seed) {
    HANDLE grow = rt_heap_create(0, 0, 0);
    HANDLE fixed = rt_heap_create(HEAP_NO_SERIALIZE, 0, 300000);
    T_REQUIRE(grow != NULL && fixed != NULL);
    Model models[3];
    memset(models, 0, sizeof models);
    models[0].heap = rt_heap_process();
    models[1].heap = grow;
    models[2].heap = fixed;
    models[2].maximum = (300000 + 4095) & ~UINT64_C(4095);
    uint64_t rng = seed;
    for (unsigned step = 0; step < steps; step++) {
        Model *m = &models[rng_next(&rng) % 3];
        switch (rng_next(&rng) % 8) {
        case 0: case 1: case 2: op_alloc(m, &rng); break;
        case 3: case 4: op_free(m, &rng); break;
        case 5: case 6: op_realloc(m, &rng); break;
        default:
            if (m->live) verify_block(m, &m->blocks[rng_next(&rng) % m->live]);
            break;
        }
        if (step % 2000 == 1999) {
            check_no_overlap(models, 3);
            for (unsigned i = 0; i < 3; i++) for (unsigned k = 0; k < models[i].live; k++) verify_block(&models[i], &models[i].blocks[k]);
        }
        if (failures > 5) break;
    }
    T_EQ(failures, 0);
    /* The fixed-size heap's books balance: free everything and all of its room is there. */
    for (unsigned i = 0; i < 3; i++)
        while (models[i].live) op_free(&models[i], &rng);
    T_EQ(models[2].charged, 0);
    uint64_t room = models[2].maximum, got = 0;
    for (;;) {
        void *p = rt_heap_alloc(fixed, 0, 48);
        if (!p) break;
        got += 64;
    }
    T_EQ(got, room - room % 64);
    T_EQ(rt_heap_destroy(grow), TRUE);
    T_EQ(rt_heap_destroy(fixed), TRUE);
}

TEST(core, heap_stress_random_workload)
{
    run_model(60000, UINT64_C(0x123456789ABCDEF1));
}

TEST(core, heap_stress_random_workload_other_seed)
{
    run_model(60000, UINT64_C(0x0F1E2D3C4B5A6978));
}
