/* rt_vm_query: the rules of VirtualQuery (runtime/core/vm.c states each rule where it
 * implements it).  Helpers and the PLAY addresses: vm_util.c.
 */
#include <stdlib.h>

#include "vm_util.c"

TEST(core, vm_query_limits)
{
    const uint64_t limit = rt_machine()->max_address + 1;
    MEMORY_BASIC_INFORMATION q, mark;
    memset(&mark, 0xA5, sizeof mark);
    T_EQ(sizeof q, 48);

    /* Above the highest user address: the result is 0, last error is
     * ERROR_INVALID_PARAMETER, and the buffer is not written. */
    static const uint64_t bad[] = { UINT64_C(0x7FFFFFF0000), UINT64_C(0x7FFFFFF0001), UINT64_C(0x7FFFFFFFFFF),
                                    UINT64_C(0x80000000000), UINT64_C(0x8000000000000000), UINT64_MAX };
    T_EQ(bad[0], limit);
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        q = mark;
        rt_set_last_error(T_CTX(), VU_MARK);
        T_EQ(rt_vm_query(bad[i], &q), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_TRUE(!memcmp(&q, &mark, sizeof q));
    }

    /* The highest address itself is answered, and the answer ends at the limit. */
    q = vu_query(limit - 1);
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress, limit - 0x1000);
    T_TRUE(q.RegionSize != 0 && (uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize <= limit);

    /* No buffer: 0 and ERROR_NOACCESS. */
    rt_set_last_error(T_CTX(), VU_MARK);
    T_EQ(rt_vm_query(PLAY(0), NULL), 0);
    T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);

    /* Success returns the size of the structure and leaves last error alone. */
    rt_set_last_error(T_CTX(), VU_MARK);
    T_EQ(rt_vm_query(PLAY(0), &q), sizeof q);
    T_EQ(T_LAST_ERROR(), VU_MARK);
    rt_set_last_error(T_CTX(), VU_MARK);
    T_EQ(rt_vm_query(0, &q), sizeof q);
    T_EQ(T_LAST_ERROR(), VU_MARK);
}

TEST(core, vm_query_free)
{
    /* Free space: MEM_FREE, PAGE_NOACCESS, no allocation base, no type.  BaseAddress is the
     * queried address rounded down to its page and the size counts forward from there, to
     * the next region or to the end of the arena. */
    Q_FREE(PLAY(0), VM_END - PLAY(0));
    Q_IS(PLAY(0) + 0x1234, PLAY(0) + 0x1000, 0, 0, VM_END - PLAY(0) - 0x1000, MEM_FREE, PAGE_NOACCESS, 0);
    Q_IS(PLAY(0) + 0xFFF, PLAY(0), 0, 0, VM_END - PLAY(0), MEM_FREE, PAGE_NOACCESS, 0);
    Q_FREE(VM_END - 0x1000, 0x1000);                            /* the last page of the arena */
    Q_IS(VM_END - 1, VM_END - 0x1000, 0, 0, 0x1000, MEM_FREE, PAGE_NOACCESS, 0);

    const uint64_t r = PLAY(0) + 0x100000;
    T_REQUIRE(rt_vm_alloc(r, 0x3000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    Q_FREE(PLAY(0), 0x100000);                                  /* ends where the region begins */
    Q_FREE(r - 0x1000, 0x1000);
    Q_IS(r - 1, r - 0x1000, 0, 0, 0x1000, MEM_FREE, PAGE_NOACCESS, 0);
    Q_RESERVED(r, r, PAGE_READWRITE, 0x3000);
    /* The 13 pages between the end of the region and the next 64 KB boundary are free,
     * and one run with the free space after them. */
    Q_FREE(r + 0x3000, VM_END - (r + 0x3000));
    T_REQUIRE(rt_vm_alloc(r + 0x10000, 0x1000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE) == r + 0x10000);
    Q_FREE(r + 0x3000, 0xD000);
    Q_FREE(r + 0xF000, 0x1000);
    T_REQUIRE(rt_vm_free(r, 0, MEM_RELEASE));
    Q_FREE(PLAY(0), 0x110000);                                  /* the two free runs are one again */

    /* The image arena: its free space ends at its own end. */
    Q_FREE(IMAGE_END - 0x10000, 0x10000);
    Q_FREE(IMAGE_END - 0x1000, 0x1000);
    MEMORY_BASIC_INFORMATION q = vu_query(IMAGE_END - (UINT64_C(1) << 30));
    T_EQ(q.State, MEM_FREE);
    T_EQ(q.RegionSize, UINT64_C(1) << 30);
}

TEST(core, vm_query_runs)
{
    const uint64_t r = PLAY(0);
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, 0x20000, MEM_RESERVE, PAGE_READONLY, RT_VM_PRIVATE) == r);

    /* A reservation with nothing committed: one run, counted forward from the page asked
     * about, never backward. */
    Q_RESERVED(r, r, PAGE_READONLY, 0x20000);
    Q_RESERVED(r + 0x1000, r, PAGE_READONLY, 0x1F000);
    Q_RESERVED(r + 0x1F000, r, PAGE_READONLY, 0x1000);
    Q_IS(r + 0x1FFFF, r + 0x1F000, r, PAGE_READONLY, 0x1000, MEM_RESERVE, 0, MEM_PRIVATE);
    Q_IS(r + 0x4ABC, r + 0x4000, r, PAGE_READONLY, 0x1C000, MEM_RESERVE, 0, MEM_PRIVATE);

    /* Committed pages make three runs.  AllocationBase is the base of the reservation and
     * AllocationProtect the protection it was reserved with, for every page of it. */
    T_REQUIRE(rt_vm_alloc(r + 0x4000, 0x8000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE));     /* pages 4 to 11 */
    Q_RESERVED(r, r, PAGE_READONLY, 0x4000);
    Q_RESERVED(r + 0x3000, r, PAGE_READONLY, 0x1000);
    Q_COMMITTED(r + 0x4000, r, PAGE_READONLY, 0x8000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x7000, r, PAGE_READONLY, 0x5000, PAGE_READWRITE);
    Q_COMMITTED(r + 0xB000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    Q_IS(r + 0xBFFF, r + 0xB000, r, PAGE_READONLY, 0x1000, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE);
    Q_RESERVED(r + 0xC000, r, PAGE_READONLY, 0x14000);

    /* A run ends where the protection changes; a modifier is a change. */
    T_REQUIRE(rt_vm_protect(r + 0x6000, 0x2000, PAGE_READONLY, &old));                          /* pages 6 and 7 */
    Q_COMMITTED(r + 0x4000, r, PAGE_READONLY, 0x2000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x5000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x6000, r, PAGE_READONLY, 0x2000, PAGE_READONLY);
    Q_COMMITTED(r + 0x7000, r, PAGE_READONLY, 0x1000, PAGE_READONLY);
    Q_COMMITTED(r + 0x8000, r, PAGE_READONLY, 0x4000, PAGE_READWRITE);
    T_REQUIRE(rt_vm_protect(r + 0x9000, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old));
    T_REQUIRE(rt_vm_protect(r + 0xA000, 0x1000, PAGE_READWRITE | PAGE_NOCACHE, &old));
    Q_COMMITTED(r + 0x8000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x9000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE | PAGE_GUARD);
    Q_COMMITTED(r + 0xA000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE | PAGE_NOCACHE);
    Q_COMMITTED(r + 0xB000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    /* ...and where the state changes. */
    T_REQUIRE(rt_vm_free(r + 0x5000, 0x1000, MEM_DECOMMIT));
    Q_COMMITTED(r + 0x4000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0x5000, r, PAGE_READONLY, 0x1000);
    Q_COMMITTED(r + 0x6000, r, PAGE_READONLY, 0x2000, PAGE_READONLY);
    /* Runs that become alike merge. */
    T_REQUIRE(rt_vm_protect(r + 0x6000, 0x6000, PAGE_EXECUTE_READ, &old));
    Q_COMMITTED(r + 0x6000, r, PAGE_READONLY, 0x6000, PAGE_EXECUTE_READ);
    T_REQUIRE(rt_vm_free(r + 0x4000, 0x1000, MEM_DECOMMIT));
    Q_RESERVED(r, r, PAGE_READONLY, 0x6000);

    /* A commit above everything committed so far, at the very end of the reservation. */
    T_REQUIRE(rt_vm_alloc(r + 0x1F000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE));
    Q_RESERVED(r + 0xC000, r, PAGE_READONLY, 0x13000);
    Q_COMMITTED(r + 0x1F000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    T_EQ(vu_query(r + 0x20000).State, MEM_FREE);

    /* A run never crosses from one reservation into the next, however alike they are. */
    const uint64_t b = r + 0x20000;
    T_REQUIRE(rt_vm_alloc(b, 0x10000, MEM_RESERVE, PAGE_READONLY, RT_VM_PRIVATE) == b);
    T_REQUIRE(rt_vm_alloc(b, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == b);
    Q_COMMITTED(r + 0x1F000, r, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(b, b, PAGE_READONLY, 0x1000, PAGE_READWRITE);
    T_REQUIRE(rt_vm_free(r + 0x1F000, 0x1000, MEM_DECOMMIT) && rt_vm_free(b, 0x1000, MEM_DECOMMIT));
    Q_RESERVED(r + 0xC000, r, PAGE_READONLY, 0x14000);
    Q_RESERVED(b, b, PAGE_READONLY, 0x10000);
    /* Nor into the free space after it. */
    T_REQUIRE(rt_vm_free(b, 0, MEM_RELEASE));
    Q_RESERVED(r + 0x1F000, r, PAGE_READONLY, 0x1000);
    Q_FREE(b, VM_END - b);

    /* Stepping by RegionSize from the base walks the reservation run by run and ends
     * exactly at its end. */
    static const struct { uint64_t pages; DWORD state, protect; } want[] = {
        { 6, MEM_RESERVE, 0 }, { 6, MEM_COMMIT, PAGE_EXECUTE_READ }, { 20, MEM_RESERVE, 0 },
    };
    uint64_t at = r;
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
        Q_IS(at, at, r, PAGE_READONLY, want[i].pages * PG, want[i].state, want[i].protect, MEM_PRIVATE);
        at += want[i].pages * PG;
    }
    T_EQ(at, r + 0x20000);
}

static void here(void) { }

TEST(core, vm_query_host_memory)
{
    /* Outside both arenas the answer comes from the kernel: host memory is committed and
     * private, with the protection the host gives it (section 5.2). */
    int on_stack = 1;
    uint64_t a = (uint64_t)(uintptr_t)&on_stack;
    MEMORY_BASIC_INFORMATION q = vu_query(a);
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress, a & ~UINT64_C(0xFFF));
    T_TRUE((uint64_t)(uintptr_t)q.AllocationBase != 0 && (uint64_t)(uintptr_t)q.AllocationBase <= (a & ~UINT64_C(0xFFF)));
    T_TRUE(q.RegionSize != 0 && (q.RegionSize & 0xFFF) == 0);
    T_TRUE((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize > a);
    T_EQ(q.State, MEM_COMMIT);
    T_EQ(q.Protect, PAGE_READWRITE);
    T_EQ(q.AllocationProtect, PAGE_READWRITE);
    T_EQ(q.Type, MEM_PRIVATE);

    void *heap = malloc(100);
    T_REQUIRE(heap != NULL);
    q = vu_query((uint64_t)(uintptr_t)heap);
    T_EQ(q.State, MEM_COMMIT);
    T_EQ(q.Protect, PAGE_READWRITE);
    T_EQ(q.Type, MEM_PRIVATE);
    free(heap);

    q = vu_query((uint64_t)(uintptr_t)&here);                   /* the program's own code */
    T_EQ(q.State, MEM_COMMIT);
    T_EQ(q.Protect, PAGE_EXECUTE_READ);
    T_EQ(q.Type, MEM_PRIVATE);

    /* A host mapping of three host pages with no access, then the middle one made
     * read-write, then read-only: each protection is reported, at 4 KB precision. */
    const uint64_t hp = rt_vm_host_page();
    void *m = mmap(NULL, 3 * hp, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    T_REQUIRE(m != MAP_FAILED);
    const uint64_t base = (uint64_t)(uintptr_t)m, mid = base + hp;
    T_TRUE(!rt_vm_in_vm_arena(base) && !rt_vm_in_image_arena(base));    /* the host maps nothing into the arenas */
    q = vu_query(mid);
    T_EQ(q.State, MEM_COMMIT);
    T_EQ(q.Protect, PAGE_NOACCESS);
    T_EQ(q.Type, MEM_PRIVATE);
    T_REQUIRE(mprotect((void *)(uintptr_t)mid, hp, PROT_READ | PROT_WRITE) == 0);
    Q_IS(mid, mid, mid, PAGE_READWRITE, hp, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE);
    Q_IS(mid + 0x1234, mid + 0x1000, mid, PAGE_READWRITE, hp - 0x1000, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE);
    q = vu_query(base);                                         /* the page before: ends where the middle begins */
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, mid);
    T_EQ(q.Protect, PAGE_NOACCESS);
    q = vu_query(mid + hp);                                     /* the page after: starts its own run */
    T_EQ((uint64_t)(uintptr_t)q.AllocationBase, mid + hp);
    T_EQ(q.Protect, PAGE_NOACCESS);
    T_REQUIRE(mprotect((void *)(uintptr_t)mid, hp, PROT_READ) == 0);
    Q_IS(mid, mid, mid, PAGE_READONLY, hp, MEM_COMMIT, PAGE_READONLY, MEM_PRIVATE);
    /* Unmapped host address space is free. */
    T_REQUIRE(munmap(m, 3 * hp) == 0);
    q = vu_query(mid);
    T_EQ(q.State, MEM_FREE);
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress, mid);
    T_EQ((uint64_t)(uintptr_t)q.AllocationBase, 0);
    T_EQ(q.Protect, PAGE_NOACCESS);
    T_EQ(q.Type, 0);
    T_TRUE(q.RegionSize >= 2 * hp);

    /* The first 64 KB are free, as on Windows, and so is the rest of the host's unmapped
     * low memory. */
    Q_IS(0, 0, 0, 0, 0x10000, MEM_FREE, PAGE_NOACCESS, 0);
    Q_IS(0xFFFF, 0xF000, 0, 0, 0x1000, MEM_FREE, PAGE_NOACCESS, 0);
    q = vu_query(0x10000);
    T_EQ(q.State, MEM_FREE);
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress, 0x10000);
    T_TRUE(0x10000 + q.RegionSize <= RT_IMAGE_ARENA_BASE);

    /* An answer about host memory never reaches into an arena, and one about an arena
     * never comes from the kernel: just outside each edge. */
    q = vu_query(RT_IMAGE_ARENA_BASE - 0x1000);
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, RT_IMAGE_ARENA_BASE);
    q = vu_query(IMAGE_END);
    T_TRUE((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize <= RT_VM_ARENA_BASE);
    T_TRUE(q.State == MEM_FREE || (uint64_t)(uintptr_t)q.AllocationBase >= IMAGE_END);
    q = vu_query(RT_VM_ARENA_BASE - 0x1000);
    T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, RT_VM_ARENA_BASE);
    q = vu_query(VM_END);
    T_TRUE((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize <= rt_machine()->max_address + 1);
    T_TRUE(q.State == MEM_FREE || (uint64_t)(uintptr_t)q.AllocationBase >= VM_END);
    /* The arenas are host mappings with no access; the map, not the kernel, says what is
     * in them: free space is MEM_FREE, not "committed, no access". */
    T_EQ(vu_host_prot(PLAY(0)), PROT_NONE);
    T_EQ(vu_query(PLAY(0)).State, MEM_FREE);
}

TEST(core, vm_query_host_edges)
{
    /* Host mappings right against the edges of the arenas and of the guest's address
     * range.  The kernel may merge such a mapping with the arena's own (both have no
     * access), or report one that reaches past the limit; the answer must still stop at
     * the edge.  Each mapping is asked for at an address; where the kernel puts it
     * somewhere else there is nothing to check for that edge. */
    const uint64_t hp = rt_vm_host_page(), limit = rt_machine()->max_address + 1;
    MEMORY_BASIC_INFORMATION q;
    unsigned placed = 0;

    uint64_t want = RT_VM_ARENA_BASE - hp;                      /* just below the VM arena */
    void *m = mmap((void *)(uintptr_t)want, hp, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    T_REQUIRE(m != MAP_FAILED);
    if ((uint64_t)(uintptr_t)m == want) {
        placed++;
        q = vu_query(want);
        T_EQ(q.State, MEM_COMMIT);
        T_EQ(q.Protect, PAGE_NOACCESS);
        T_EQ(q.Type, MEM_PRIVATE);
        T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, RT_VM_ARENA_BASE);
        T_TRUE((uint64_t)(uintptr_t)q.AllocationBase >= IMAGE_END && (uint64_t)(uintptr_t)q.AllocationBase <= want);
    }
    /* The arena's first page is still the map's to describe (the process block is there). */
    T_EQ((uint64_t)(uintptr_t)vu_query(RT_VM_ARENA_BASE).AllocationBase, RT_VM_ARENA_BASE);

    want = VM_END;                                              /* just above it */
    m = mmap((void *)(uintptr_t)want, hp, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    T_REQUIRE(m != MAP_FAILED);
    if ((uint64_t)(uintptr_t)m == want) {
        placed++;
        q = vu_query(want + 0x1000);
        T_EQ(q.State, MEM_COMMIT);
        T_EQ(q.Protect, PAGE_NOACCESS);
        T_EQ((uint64_t)(uintptr_t)q.BaseAddress, want + 0x1000);
        T_EQ((uint64_t)(uintptr_t)q.AllocationBase, VM_END);    /* not somewhere inside the arena */
        T_TRUE((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize <= limit);
    }
    Q_FREE(VM_END - 0x1000, 0x1000);                            /* and the arena's last page is free, not "no access" */

    want = RT_IMAGE_ARENA_BASE - hp;                            /* just below the image arena */
    m = mmap((void *)(uintptr_t)want, hp, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    T_REQUIRE(m != MAP_FAILED);
    if ((uint64_t)(uintptr_t)m == want) {
        placed++;
        q = vu_query(want);
        T_EQ(q.State, MEM_COMMIT);
        T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, RT_IMAGE_ARENA_BASE);
    }

    want = limit - hp;                                          /* across the highest user address */
    m = mmap((void *)(uintptr_t)want, 2 * hp, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    T_REQUIRE(m != MAP_FAILED);
    if ((uint64_t)(uintptr_t)m == want) {
        placed++;
        Q_IS(limit - 1, limit - 0x1000, want, PAGE_READWRITE, 0x1000, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE);
        Q_IS(want, want, want, PAGE_READWRITE, hp, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE);
        /* Free space below it ends where it begins. */
        q = vu_query(want - 0x1000);
        if (q.State == MEM_FREE) T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, want);
        T_REQUIRE(munmap(m, 2 * hp) == 0);
    }
    /* With the next host mapping lying beyond the limit, free space ends at the limit. */
    want = limit + 64 * hp;
    m = mmap((void *)(uintptr_t)want, hp, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    T_REQUIRE(m != MAP_FAILED);
    if ((uint64_t)(uintptr_t)m == want) {
        placed++;
        q = vu_query(limit - 0x1000);
        if (q.State == MEM_FREE) T_EQ((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize, limit);
        T_TRUE((uint64_t)(uintptr_t)q.BaseAddress + q.RegionSize <= limit);
    }
    /* On this system the kernel honours all five requests; if it ever honours none, this
     * test checks nothing and should say so. */
    T_TRUE(placed > 0);
    rt_log(RT_CH_VM, RT_LL_INFO, "vm_query_host_edges: %u of 5 host mappings were placed where asked", placed);
}

TEST(core, vm_query_walk)
{
    /* A program that walks its address space with VirtualQuery, adding RegionSize each
     * time, gets from 0 to the limit in a sensible number of steps, sees every region
     * exactly once and never an answer that contradicts the one before. */
    const uint64_t limit = rt_machine()->max_address + 1;
    const uint64_t r = PLAY(0), im = IMAGE_END - 0x100000;
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, 0x30000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    T_REQUIRE(rt_vm_alloc(r + 0x4000, 0x5000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE));
    T_REQUIRE(rt_vm_protect(r + 0x6000, 0x1000, PAGE_READONLY, &old));
    T_REQUIRE(rt_vm_alloc(im, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READ, RT_VM_IMAGE) == im);

    static struct { uint64_t addr, size, alloc_base; DWORD state, protect, type; bool seen; } want[] = {
        { 0, 0x4000, 0, MEM_RESERVE, 0, MEM_PRIVATE, false },
        { 0x4000, 0x2000, 0, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE, false },
        { 0x6000, 0x1000, 0, MEM_COMMIT, PAGE_READONLY, MEM_PRIVATE, false },
        { 0x7000, 0x2000, 0, MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE, false },
        { 0x9000, 0x27000, 0, MEM_RESERVE, 0, MEM_PRIVATE, false },
        { 0x30000, 0, 0, MEM_FREE, PAGE_NOACCESS, 0, false },          /* the free tail of the VM arena */
        { 1, 0x2000, 0, MEM_COMMIT, PAGE_EXECUTE_READ, MEM_IMAGE, false },
    };
    const unsigned nwant = sizeof want / sizeof want[0];
    for (unsigned i = 0; i < nwant; i++) {
        bool image = want[i].addr == 1;
        want[i].addr = image ? im : r + want[i].addr;
        want[i].alloc_base = want[i].state == MEM_FREE ? 0 : image ? im : r;
        if (want[i].state == MEM_FREE) want[i].size = VM_END - want[i].addr;
    }

    uint64_t at = 0;
    unsigned steps = 0;
    while (at < limit) {
        MEMORY_BASIC_INFORMATION q;
        T_REQUIRE(rt_vm_query(at, &q) == sizeof q);
        uint64_t qbase = (uint64_t)(uintptr_t)q.BaseAddress, abase = (uint64_t)(uintptr_t)q.AllocationBase;
        T_REQUIRE(qbase == at);
        T_REQUIRE(q.RegionSize != 0 && (q.RegionSize & 0xFFF) == 0 && q.RegionSize <= limit - at);
        if (q.State == MEM_FREE) {
            T_REQUIRE(abase == 0 && q.AllocationProtect == 0 && q.Protect == PAGE_NOACCESS && q.Type == 0);
        } else {
            T_REQUIRE(q.State == MEM_COMMIT || q.State == MEM_RESERVE);
            T_REQUIRE(q.Type == MEM_PRIVATE || q.Type == MEM_MAPPED || q.Type == MEM_IMAGE);
            T_REQUIRE(abase != 0 && abase <= at);
            T_REQUIRE((q.State == MEM_RESERVE) == (q.Protect == 0));
            /* What is in an arena is described by the map, with an allocation base in the
             * same arena; what is outside is the host's and never claims to be an image. */
            T_REQUIRE(rt_vm_in_vm_arena(at) == rt_vm_in_vm_arena(abase));
            T_REQUIRE(rt_vm_in_image_arena(at) == rt_vm_in_image_arena(abase));
            if (!rt_vm_in_vm_arena(at) && !rt_vm_in_image_arena(at)) T_REQUIRE(q.Type == MEM_PRIVATE && q.State == MEM_COMMIT);
        }
        /* A run never straddles the edge of an arena. */
        T_REQUIRE(rt_vm_in_vm_arena(at) == rt_vm_in_vm_arena(at + q.RegionSize - 1));
        T_REQUIRE(rt_vm_in_image_arena(at) == rt_vm_in_image_arena(at + q.RegionSize - 1));
        for (unsigned i = 0; i < nwant; i++) {
            if (want[i].addr != at) continue;
            T_EQ(q.RegionSize, want[i].size);
            T_EQ(abase, want[i].alloc_base);
            T_EQ(q.State, want[i].state);
            T_EQ(q.Protect, want[i].protect);
            T_EQ(q.Type, want[i].type);
            want[i].seen = true;
        }
        at += q.RegionSize;
        T_REQUIRE(++steps < 1000000);
    }
    T_EQ(at, limit);
    for (unsigned i = 0; i < nwant; i++) T_TRUE(want[i].seen);
}
