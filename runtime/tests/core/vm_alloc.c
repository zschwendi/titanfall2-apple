/* rt_vm_alloc: the rules of VirtualAlloc, one group per test (runtime/core/vm.c states each
 * rule where it implements it).  Helpers and the PLAY addresses: vm_util.c.
 */
#include <stdlib.h>

#include "vm_util.c"

#define MEM_PHYSICAL_    0x00400000u        /* winnt.h MEM_PHYSICAL; rt/win/memory.h does not define it */
#define MEM_RESET_UNDO_  0x01000000u        /* winnt.h MEM_RESET_UNDO: Windows 8 and later */

static const DWORD requests[] = { MEM_RESERVE, MEM_COMMIT, MEM_RESERVE | MEM_COMMIT, MEM_RESET };
#define N_REQUESTS  (sizeof requests / sizeof requests[0])

TEST(core, vm_alloc_bad_flags)
{
    /* An allocation type that asks for nothing, mixes MEM_RESET with anything, or has a
     * bit that is not an allocation type on Windows 7: ERROR_INVALID_PARAMETER, wherever
     * the address points, and nothing is allocated or changed. */
    static const DWORD bad[] = {
        0,
        MEM_TOP_DOWN, MEM_LARGE_PAGES, MEM_WRITE_WATCH, MEM_PHYSICAL_,          /* modifiers alone */
        MEM_TOP_DOWN | MEM_LARGE_PAGES | MEM_WRITE_WATCH,
        MEM_DECOMMIT, MEM_RELEASE, MEM_FREE, MEM_PRIVATE, MEM_MAPPED, MEM_IMAGE,   /* not allocation types */
        MEM_RESERVE | MEM_DECOMMIT, MEM_COMMIT | MEM_RELEASE, MEM_RESERVE | MEM_COMMIT | MEM_FREE,
        MEM_RESERVE | 0x1, MEM_COMMIT | 0x800, MEM_RESERVE | 0x80000000, MEM_RESERVE | 0x40000,
        MEM_RESET_UNDO_, MEM_RESERVE | MEM_RESET_UNDO_,
        MEM_RESET | MEM_COMMIT, MEM_RESET | MEM_RESERVE, MEM_RESET | MEM_TOP_DOWN, MEM_RESET | MEM_LARGE_PAGES,
        MEM_RESET | MEM_RESERVE | MEM_COMMIT,
        /* Modifiers without the request they belong to, or with each other. */
        MEM_RESERVE | MEM_LARGE_PAGES, MEM_RESERVE | MEM_LARGE_PAGES | MEM_TOP_DOWN,        /* needs MEM_COMMIT */
        MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES | MEM_WRITE_WATCH,
        MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES | MEM_PHYSICAL_,
        MEM_COMMIT | MEM_WRITE_WATCH, MEM_COMMIT | MEM_PHYSICAL_,                           /* need MEM_RESERVE */
        MEM_RESERVE | MEM_COMMIT | MEM_PHYSICAL_,                                           /* must not have MEM_COMMIT */
        MEM_RESERVE | MEM_COMMIT | 0x40000000,          /* AT_ROUND_TO_PAGE, an NT flag that is no allocation type on x64 */
        0xFFFFFFFF,
    };
    uint64_t r = rt_vm_alloc(PLAY(1), 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(r == PLAY(1));
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, bad[i], PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), 0x1000, bad[i], PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, bad[i], PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, bad[i], PAGE_READONLY, RT_VM_IMAGE);
    }
    Q_FREE(PLAY(0), PLAY(1) - PLAY(0));
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x10000);

    /* Size 0 is ERROR_INVALID_PARAMETER for every request. */
    for (unsigned i = 0; i < N_REQUESTS; i++) {
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0, requests[i], PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), 0, requests[i], PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, r, 0, requests[i], PAGE_READWRITE, RT_VM_PRIVATE);
    }
    Q_FREE(PLAY(0), PLAY(1) - PLAY(0));
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x10000);
}

TEST(core, vm_alloc_bad_protect)
{
    /* The protection must be valid in every call: reserve, commit, both, reset. */
    uint64_t r = rt_vm_alloc(PLAY(1), 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(r == PLAY(1));
    T_REQUIRE(rt_vm_alloc(r, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    for (unsigned i = 0; i < VU_N_BAD_PROTECTS; i++) {
        DWORD p = vu_bad_protects[i];
        for (unsigned j = 0; j < N_REQUESTS; j++) {
            if (requests[j] != MEM_RESET)       /* MEM_RESET with address 0 is a different case (vm_alloc_unhandled) */
                ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, requests[j], p, RT_VM_PRIVATE);
            ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), 0x1000, requests[j], p, RT_VM_PRIVATE);
            ALLOC_FAILS(ERROR_INVALID_PARAMETER, r, 0x2000, requests[j], p, RT_VM_PRIVATE);
            ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), 0x1000, requests[j], p, RT_VM_IMAGE);
            ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), 0x1000, requests[j], p, RT_VM_MAPPED);
        }
    }
    Q_FREE(PLAY(0), PLAY(1) - PLAY(0));
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0x1000, r, PAGE_NOACCESS, 0xF000);
}

TEST(core, vm_alloc_protections)
{
    /* Every valid protection is recorded exactly: as AllocationProtect by the call that
     * reserves, as Protect by the call that commits.  The host page gets the access the
     * protection stands for (execute counts as read; guard and no-access as nothing). */
    for (unsigned i = 0; i < VU_N_PRIVATE_PROTECTS; i++) {
        DWORD p = vu_private_protects[i], p2 = vu_private_protects[(i * 7 + 3) % VU_N_PRIVATE_PROTECTS];

        uint64_t r = rt_vm_alloc(0, 0x3000, MEM_RESERVE, p, RT_VM_PRIVATE);
        T_REQUIRE(r != 0);
        Q_RESERVED(r, r, p, 0x3000);
        T_EQ(vu_host_prot(r), PROT_NONE);
        ALLOC_IS(r + 0x1000, r + 0x1000, 0x1000, MEM_COMMIT, p2, RT_VM_PRIVATE);
        Q_RESERVED(r, r, p, 0x1000);
        Q_COMMITTED(r + 0x1000, r, p, 0x1000, p2);
        Q_RESERVED(r + 0x2000, r, p, 0x1000);
        T_EQ(vu_host_prot(r), vu_access(p2));
        FREE_OK(r, 0, MEM_RELEASE);

        uint64_t c = rt_vm_alloc(0, 0x2000, MEM_RESERVE | MEM_COMMIT, p, RT_VM_PRIVATE);
        T_REQUIRE(c != 0);
        Q_COMMITTED(c, c, p, 0x2000, p);
        T_EQ(vu_host_prot(c), vu_access(p));
        T_EQ(vu_reads(c), (vu_access(p) & PROT_READ) != 0);
        T_EQ(vu_writes(c + 0x1FFF), (vu_access(p) & PROT_WRITE) != 0);
        if (vu_access(p) & PROT_READ) T_TRUE(vu_all_zero(c, 0x2000));
        FREE_OK(c, 0, MEM_RELEASE);
    }

    /* Copy-on-write protections are not valid for private memory... */
    static const DWORD cow[] = { PAGE_WRITECOPY, PAGE_EXECUTE_WRITECOPY, PAGE_WRITECOPY | PAGE_GUARD,
                                 PAGE_EXECUTE_WRITECOPY | PAGE_NOCACHE };
    uint64_t r = rt_vm_alloc(PLAY(1), 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(r == PLAY(1));
    for (unsigned i = 0; i < sizeof cow / sizeof cow[0]; i++) {
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, MEM_RESERVE, cow[i], RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, MEM_COMMIT, cow[i], RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), 0x1000, MEM_RESERVE | MEM_COMMIT, cow[i], RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, MEM_COMMIT, cow[i], RT_VM_PRIVATE);
    }
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x10000);
    Q_FREE(PLAY(0), PLAY(1) - PLAY(0));

    /* ...and are what an image or a file view is made with.  A copy-on-write page is
     * writable. */
    for (unsigned i = 0; i < sizeof cow / sizeof cow[0]; i++) {
        uint64_t im = rt_vm_alloc(0, 0x2000, MEM_RESERVE | MEM_COMMIT, cow[i], RT_VM_IMAGE);
        T_REQUIRE(im != 0);
        Q_IS(im, im, im, cow[i], 0x2000, MEM_COMMIT, cow[i], MEM_IMAGE);
        T_EQ(vu_host_prot(im), (cow[i] & PAGE_GUARD) ? PROT_NONE : (PROT_READ | PROT_WRITE));
        RELEASE_OK(im, RT_VM_IMAGE);

        uint64_t mv = rt_vm_alloc(0, 0x2000, MEM_RESERVE, PAGE_READONLY, RT_VM_MAPPED);
        T_REQUIRE(mv != 0);
        ALLOC_IS(mv, mv, 0x1000, MEM_COMMIT, cow[i], RT_VM_MAPPED);
        Q_IS(mv, mv, mv, PAGE_READONLY, 0x1000, MEM_COMMIT, cow[i], MEM_MAPPED);
        RELEASE_OK(mv, RT_VM_MAPPED);
    }
}

TEST(core, vm_alloc_address_limits)
{
    const uint64_t limit = rt_machine()->max_address + 1;       /* 0x7FFFFFF0000 (section 5.1) */

    for (unsigned i = 0; i < N_REQUESTS; i++) {
        DWORD t = requests[i];
        /* A non-null address in the first 64 KB: ERROR_INVALID_PARAMETER. */
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 1, 0x1000, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0x1000, 0x1000, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0xFFFF, 0x20000, t, PAGE_READWRITE, RT_VM_PRIVATE);
        /* 64 KB itself is a valid address.  It is outside the arenas, where nothing can
         * be reserved and nothing is reserved: ERROR_INVALID_ADDRESS. */
        ALLOC_FAILS(ERROR_INVALID_ADDRESS, 0x10000, 0x1000, t, PAGE_READWRITE, RT_VM_PRIVATE);

        /* At or past the highest user address: ERROR_INVALID_PARAMETER.  Just below it
         * the range is valid, and outside the arenas. */
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, limit, 0x1000, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, limit + 0x10000, 1, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, limit - 0x1000, 0x1001, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_ADDRESS, limit - 0x1000, 0x1000, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, UINT64_MAX, 1, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, UINT64_MAX - 0xFFF, 0x1000, t, PAGE_READWRITE, RT_VM_PRIVATE);
        /* A size that wraps the address or reaches past the limit. */
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), UINT64_MAX, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), UINT64_MAX - PLAY(0) + 1, t, PAGE_READWRITE, RT_VM_PRIVATE);
        ALLOC_FAILS(ERROR_INVALID_PARAMETER, PLAY(0), limit - PLAY(0) + 1, t, PAGE_READWRITE, RT_VM_PRIVATE);
        if (t != MEM_RESET) {
            ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, UINT64_MAX, t, PAGE_READWRITE, RT_VM_PRIVATE);
            ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, limit + 1, t, PAGE_READWRITE, RT_VM_PRIVATE);
        }
    }

    /* A size that is valid and that no gap can hold: ERROR_NOT_ENOUGH_MEMORY. */
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, limit, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, RT_VM_ARENA_SIZE + 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, RT_VM_ARENA_SIZE, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, RT_VM_ARENA_SIZE, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, RT_VM_ARENA_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    /* The same size at an address: the range is valid and is not free. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, PLAY(0), limit - PLAY(0), MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, RT_VM_ARENA_BASE, RT_VM_ARENA_SIZE, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_FREE(PLAY(0), VM_END - PLAY(0));
}

TEST(core, vm_alloc_arena_edges)
{
    /* The last 64 KB of the VM arena can be reserved, and its last page used. */
    const uint64_t top = VM_END - 0x10000;
    ALLOC_IS(top, top, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(top, top, PAGE_NOACCESS, 0x10000);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, VM_END - 0x1000, 0x1001, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_IS(VM_END - 0x1000, VM_END - 1, 1, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ST8(VM_END - 1, 0x77);
    T_EQ(LD8(VM_END - 1), 0x77);
    Q_COMMITTED(VM_END - 0x1000, top, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    FREE_OK(top, 0, MEM_RELEASE);

    /* One byte more leaves the arena: the range is not free. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, top, 0x10001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, VM_END - 1, 2, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, VM_END, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_FREE(top, 0x10000);

    /* Between the arenas, and across the start of one, is host address space. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, RT_VM_ARENA_BASE - 0x10000, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, IMAGE_END, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, RT_IMAGE_ARENA_BASE - 0x10000, 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, RT_IMAGE_ARENA_BASE - 1, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);

    /* The image arena takes requests at an address (rt/vm.h: either arena), of any type... */
    const uint64_t itop = IMAGE_END - 0x10000;
    ALLOC_IS(itop, itop, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);
    Q_IS(itop, itop, itop, PAGE_NOACCESS, 0x10000, MEM_RESERVE, 0, MEM_IMAGE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, itop - 0x10000, 0x10001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);
    RELEASE_OK(itop, RT_VM_IMAGE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, itop, 0x10001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);
    ALLOC_IS(itop, itop + 0xFFFF, 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ST8(IMAGE_END - 1, 0x66);
    T_EQ(LD8(IMAGE_END - 1), 0x66);
    Q_COMMITTED(IMAGE_END - 0x1000, itop, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    FREE_OK(itop, 0, MEM_RELEASE);
    Q_FREE(itop, 0x10000);

    /* ...but with no address, a region always comes from the VM arena, whatever its type. */
    uint64_t im = rt_vm_alloc(0, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);
    T_TRUE(rt_vm_in_vm_arena(im));
    Q_IS(im, im, im, PAGE_NOACCESS, 0x1000, MEM_RESERVE, 0, MEM_IMAGE);
    RELEASE_OK(im, RT_VM_IMAGE);
}

TEST(core, vm_alloc_rounding)
{
    const uint64_t x = PLAY(0);

    /* With an address: the base is rounded DOWN to 64 KB, and the region runs to the end
     * of the last page that holds a requested byte. */
    ALLOC_IS(x, x + 0xFFFF, 2, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(x, x, PAGE_NOACCESS, 0x11000);
    Q_FREE(x + 0x11000, VM_END - (x + 0x11000));
    FREE_OK(x, 0, MEM_RELEASE);
    ALLOC_IS(x, x + 0xFFFF, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(x, x, PAGE_NOACCESS, 0x10000);
    FREE_OK(x, 0, MEM_RELEASE);
    ALLOC_IS(x, x + 1, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(x, x, PAGE_NOACCESS, 0x1000);
    FREE_OK(x, 0, MEM_RELEASE);
    /* Reserve and commit at an address commits the whole region, from the rounded base. */
    ALLOC_IS(x, x + 0x2345, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_COMMITTED(x, x, PAGE_READWRITE, 0x4000, PAGE_READWRITE);
    T_TRUE(vu_all_zero(x, 0x4000));
    ST8(x, 1);
    ST8(x + 0x3FFF, 2);
    FREE_OK(x, 0, MEM_RELEASE);

    /* With no address: the size is rounded up to a page and the base is 64 KB aligned. */
    static const struct { uint64_t size, pages; } sizes[] = {
        { 1, 1 }, { 0xFFF, 1 }, { 0x1000, 1 }, { 0x1001, 2 }, { 0xFFFC, 16 }, { 0x10000, 16 }, { 0x10001, 17 },
        { 0x123456, 0x124 },
    };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint64_t a = rt_vm_alloc(0, sizes[i].size, MEM_RESERVE, PAGE_READONLY, RT_VM_PRIVATE);
        T_REQUIRE(a != 0);
        T_EQ(a & 0xFFFF, 0);
        Q_RESERVED(a, a, PAGE_READONLY, sizes[i].pages * PG);
        FREE_OK(a, 0, MEM_RELEASE);
        a = rt_vm_alloc(0, sizes[i].size, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);     /* no address: reserve and commit */
        T_REQUIRE(a != 0);
        T_EQ(a & 0xFFFF, 0);
        Q_COMMITTED(a, a, PAGE_READWRITE, sizes[i].pages * PG, PAGE_READWRITE);
        T_TRUE(vu_all_zero(a, sizes[i].pages * PG));
        FREE_OK(a, 0, MEM_RELEASE);
    }

    /* A commit covers every page that holds a byte of the range, and returns the address
     * rounded down to a page. */
    uint64_t r = rt_vm_alloc(x, 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(r == x);
    ALLOC_IS(r + 0x1000, r + 0x1FFF, 2, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);     /* two bytes, two pages */
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x1000);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x2000, PAGE_READWRITE);
    Q_RESERVED(r + 0x3000, r, PAGE_NOACCESS, 0x1D000);
    ALLOC_IS(r + 0x8000, r + 0x8FFF, 1, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);     /* the last byte of a page */
    Q_COMMITTED(r + 0x8000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0x9000, r, PAGE_NOACCESS, 0x17000);
    ALLOC_IS(r + 0xA000, r + 0xA000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);    /* exactly one page */
    Q_COMMITTED(r + 0xA000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0xB000, r, PAGE_NOACCESS, 0x15000);
    ALLOC_IS(r + 0xC000, r + 0xC001, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);    /* a page's worth, off by one */
    Q_COMMITTED(r + 0xC000, r, PAGE_NOACCESS, 0x2000, PAGE_READWRITE);
    /* Up to the last byte of the region, and not one further. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r + 0x1FFFF, 2, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_RESERVED(r + 0xE000, r, PAGE_NOACCESS, 0x12000);
    ALLOC_IS(r + 0x1F000, r + 0x1FFFF, 1, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_COMMITTED(r + 0x1F000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    /* The whole region in one call. */
    ALLOC_IS(r, r, 0x20000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x20000, PAGE_READWRITE);
    FREE_OK(r, 0, MEM_RELEASE);
}

TEST(core, vm_alloc_conflicts)
{
    /* A requested range must be entirely free. */
    const uint64_t x = PLAY(0) + 0x100000;
    ALLOC_IS(x, x, 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);                /* the same base */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE); /* the same range */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x + 0x10000, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);      /* inside */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x + 0x1F000, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);      /* its last page */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x + 0x8000, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x - 0x10000, 0x10001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);     /* one byte into it */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x - 0x20000, 0x50000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);     /* around it */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x + 0x1FFFF, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);     /* starts in its last byte */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_IMAGE);                  /* whatever the type */
    Q_RESERVED(x, x, PAGE_NOACCESS, 0x20000);
    Q_FREE(x - 0x20000, 0x20000);
    /* Exact fits on both sides. */
    ALLOC_IS(x - 0x10000, x - 0x10000, 0x10000, MEM_RESERVE, PAGE_READONLY, RT_VM_PRIVATE);
    ALLOC_IS(x + 0x20000, x + 0x20000, 0x10000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_RESERVED(x - 0x10000, x - 0x10000, PAGE_READONLY, 0x10000);
    Q_RESERVED(x, x, PAGE_NOACCESS, 0x20000);
    Q_RESERVED(x + 0x20000, x + 0x20000, PAGE_READWRITE, 0x10000);

    /* A commit never spans two reservations, however alike and adjacent they are, and a
     * commit that fails commits nothing. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x - 0x1000, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x + 0x1F000, 0x1001, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x - 0x10000, 0x40000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x - 0x1000, 0x2000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(x - 0x10000, x - 0x10000, PAGE_READONLY, 0x10000);
    Q_RESERVED(x, x, PAGE_NOACCESS, 0x20000);
    Q_RESERVED(x + 0x20000, x + 0x20000, PAGE_READWRITE, 0x10000);
    T_EQ(vu_host_prot(x - 0x1000), PROT_NONE);
    T_EQ(vu_host_prot(x), PROT_NONE);

    /* MEM_COMMIT without MEM_RESERVE at an address needs the whole range reserved:
     * ERROR_INVALID_ADDRESS in free space. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, x + 0x40000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_FREE(x + 0x40000, VM_END - (x + 0x40000));

    /* The 64 KB rule.  A region of one page owns its whole 64 KB of address space: the 15
     * pages after it are free, and can neither be reserved (the base rounds down onto the
     * region) nor committed (they are not reserved). */
    const uint64_t y = x + 0x100000;
    ALLOC_IS(y, y, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, y + 0x1000, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, y + 0xF000, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, y + 0x1000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, y, 0x1001, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_IS(y + 0x10000, y + 0x10000, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_COMMITTED(y, y, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_FREE(y + 0x1000, 0xF000);
    Q_RESERVED(y + 0x10000, y + 0x10000, PAGE_NOACCESS, 0x1000);
}

TEST(core, vm_alloc_commit)
{
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);

    /* Commit yields zeros, in pieces, in any order. */
    ALLOC_IS(r + 0x5000, r + 0x5000, 0x3000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(r + 0x5000, 0x3000));
    vu_fill(r + 0x5000, 0x3000, 0xAB);
    ALLOC_IS(r + 0x2000, r + 0x2000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(r + 0x2000, 0x1000));
    vu_fill(r + 0x2000, 0x1000, 0xCD);
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x2000);
    Q_COMMITTED(r + 0x2000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0x3000, r, PAGE_NOACCESS, 0x2000);
    Q_COMMITTED(r + 0x5000, r, PAGE_NOACCESS, 0x3000, PAGE_READWRITE);
    Q_RESERVED(r + 0x8000, r, PAGE_NOACCESS, 0x8000);

    /* Committing a page that is committed already does not fail and keeps its contents.
     * The pages in between become committed and zero.  Every page of the range gets the
     * protection of this call (what the Windows kernel does; the documentation is silent). */
    ALLOC_IS(r + 0x1000, r + 0x1000, 0x8000, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE);
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x1000);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x8000, PAGE_READONLY);
    Q_RESERVED(r + 0x9000, r, PAGE_NOACCESS, 0x7000);
    T_TRUE(vu_all_zero(r + 0x1000, 0x1000));
    T_TRUE(vu_all(r + 0x2000, 0x1000, 0xCD));
    T_TRUE(vu_all_zero(r + 0x3000, 0x2000));
    T_TRUE(vu_all(r + 0x5000, 0x3000, 0xAB));
    T_TRUE(vu_all_zero(r + 0x8000, 0x1000));
    /* The same call again changes nothing. */
    ALLOC_IS(r + 0x1000, r + 0x1000, 0x8000, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x8000, PAGE_READONLY);
    T_TRUE(vu_all(r + 0x5000, 0x3000, 0xAB));
    /* And back to read-write over part of it: three runs. */
    ALLOC_IS(r + 0x4000, r + 0x4000, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x3000, PAGE_READONLY);
    Q_COMMITTED(r + 0x4000, r, PAGE_NOACCESS, 0x2000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x6000, r, PAGE_NOACCESS, 0x3000, PAGE_READONLY);
    T_TRUE(vu_all(r + 0x5000, 0x3000, 0xAB));

    /* MEM_RESERVE | MEM_COMMIT on a range that is reserved already is a new reservation
     * of a range that is not free: ERROR_INVALID_ADDRESS (a common mistake on Windows too). */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r + 0x9000, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_RESERVED(r + 0x9000, r, PAGE_NOACCESS, 0x7000);
    FREE_OK(r, 0, MEM_RELEASE);
}

TEST(core, vm_alloc_first_fit)
{
    /* With no address, the lowest gap that is big enough, at a 64 KB aligned base. */
    uint64_t a = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    uint64_t b = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    uint64_t c = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(a != 0 && b == a + 0x10000 && c == a + 0x20000);
    T_TRUE(rt_vm_in_vm_arena(a));
    FREE_OK(b, 0, MEM_RELEASE);

    /* A gap of exactly 64 KB: too small by one byte, then an exact fit. */
    uint64_t d = rt_vm_alloc(0, 0x10001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_EQ(d, c + 0x10000);
    ALLOC_IS(b, 0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    FREE_OK(b, 0, MEM_RELEASE);
    ALLOC_IS(b, 0, 0xF001, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    FREE_OK(b, 0, MEM_RELEASE);

    /* The slack between the end of a region and the next 64 KB boundary is never handed
     * out: d is 17 pages long, so the next region starts 128 KB above it. */
    ALLOC_IS(b, 0, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(d + 0x20000, 0, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(d + 0x30000, 0, 0xFFFF, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);

    /* The lowest gap wins over a better-fitting one higher up. */
    FREE_OK(a, 0, MEM_RELEASE);
    FREE_OK(b, 0, MEM_RELEASE);                                  /* a and b: one gap of 128 KB */
    FREE_OK(d + 0x20000, 0, MEM_RELEASE);                        /* 64 KB */
    ALLOC_IS(a, 0, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(b, 0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(d + 0x20000, 0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    /* A gap of two granules takes a region of two, and not one byte more. */
    FREE_OK(a, 0, MEM_RELEASE);
    FREE_OK(b, 0, MEM_RELEASE);
    uint64_t e = rt_vm_alloc(0, 0x20001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_EQ(e, d + 0x40000);
    ALLOC_IS(a, 0, 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);

    /* MEM_TOP_DOWN is accepted and ignored: still the lowest gap. */
    FREE_OK(a, 0, MEM_RELEASE);
    ALLOC_IS(a, 0, 0x8000, MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(a + 0x1000, a + 0x1000, 0x1000, MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_IS(a + 0x10000, 0, 0x1000, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_COMMITTED(a + 0x1000, a, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(a + 0x10000, a + 0x10000, PAGE_READWRITE, 0x1000, PAGE_READWRITE);

    /* A request at an address does not disturb first fit, and first fit goes around it. */
    FREE_OK(a + 0x10000, 0, MEM_RELEASE);
    FREE_OK(a, 0, MEM_RELEASE);
    ALLOC_IS(a + 0x10000, a + 0x10000, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(a, 0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    FREE_OK(a, 0, MEM_RELEASE);
    ALLOC_IS(e + 0x30000, 0, 0x10001, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
}

TEST(core, vm_alloc_fills_the_arena)
{
    /* Find where the free tail of the VM arena starts by asking the map. */
    uint64_t used_end = RT_VM_ARENA_BASE;
    for (uint64_t at = RT_VM_ARENA_BASE; at < VM_END; ) {
        MEMORY_BASIC_INFORMATION q = vu_query(at);
        T_REQUIRE(q.RegionSize != 0);
        at += q.RegionSize;
        if (q.State != MEM_FREE) used_end = at;
    }
    const uint64_t base = (used_end + 0xFFFF) & ~UINT64_C(0xFFFF), tail = VM_END - base;
    T_REQUIRE(tail > (UINT64_C(200) << 30));

    /* One byte too many does not fit; the exact size does, at the lowest free address,
     * and reaches the last page of the arena. */
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, tail + 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(base, 0, tail, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(base, base, PAGE_NOACCESS, tail);
    Q_RESERVED(VM_END - 0x1000, base, PAGE_NOACCESS, 0x1000);

    /* The arena is full: ERROR_NOT_ENOUGH_MEMORY for the smallest request. */
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, 1, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_NOT_ENOUGH_MEMORY, 0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    /* The image arena is another matter. */
    ALLOC_IS(IMAGE_END - 0x10000, IMAGE_END - 0x10000, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    FREE_OK(IMAGE_END - 0x10000, 0, MEM_RELEASE);

    /* A reservation of a couple of hundred GiB behaves like any other: commit at both
     * ends and in the middle, query, decommit. */
    const uint64_t mid = base + (tail / 2 & ~UINT64_C(0xFFF));
    ALLOC_IS(base, base, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_IS(mid, mid, 0x1000, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE);
    ALLOC_IS(VM_END - 0x1000, VM_END - 1, 1, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    ST64(base, 0x1111);
    ST64(VM_END - 8, 0x2222);
    T_EQ(LD64(mid), 0);
    Q_COMMITTED(base, base, PAGE_NOACCESS, 0x2000, PAGE_READWRITE);
    Q_RESERVED(base + 0x2000, base, PAGE_NOACCESS, mid - base - 0x2000);
    Q_COMMITTED(mid, base, PAGE_NOACCESS, 0x1000, PAGE_READONLY);
    Q_RESERVED(mid + 0x1000, base, PAGE_NOACCESS, VM_END - 0x1000 - (mid + 0x1000));
    Q_COMMITTED(VM_END - 0x1000, base, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    FREE_OK(base, 0, MEM_DECOMMIT);
    Q_RESERVED(base, base, PAGE_NOACCESS, tail);
    T_EQ(vu_host_prot(base), PROT_NONE);
    T_EQ(vu_host_prot(mid), PROT_NONE);
    T_EQ(vu_host_prot(VM_END - 0x1000), PROT_NONE);

    FREE_OK(base, 0, MEM_RELEASE);
    Q_FREE(base, tail);
    ALLOC_IS(base, 0, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
}

TEST(core, vm_alloc_large_pages_and_reset)
{
    /* MEM_LARGE_PAGES: the process does not hold the privilege (section 5.2). */
    ALLOC_FAILS(ERROR_PRIVILEGE_NOT_HELD, 0, 0x200000, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_PRIVILEGE_NOT_HELD, PLAY(0), 0x200000, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_PRIVILEGE_NOT_HELD, 0, 0x200000, MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_PRIVILEGE_NOT_HELD, 0, 0x200000, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES | MEM_TOP_DOWN, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_FREE(PLAY(0), VM_END - PLAY(0));
    /* A call that is invalid anyway says so first. */
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x200000, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, 0, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0x1000, 0x200000, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE, RT_VM_PRIVATE);

    const uint64_t r = PLAY(1);
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    ALLOC_FAILS(ERROR_PRIVILEGE_NOT_HELD, r, 0x1000, MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE, RT_VM_PRIVATE);
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x10000);

    /* MEM_RESET succeeds and does nothing (section 5.2): pages keep their state, their
     * protection and, here, their contents.  The range is rounded to pages and must lie
     * in one reservation; the protection is checked and otherwise ignored. */
    T_REQUIRE(rt_vm_alloc(r + 0x1000, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r + 0x1000);
    vu_fill(r + 0x1000, 0x2000, 0x5C);
    ALLOC_IS(r + 0x1000, r + 0x1234, 0x10, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(r + 0x1000, r + 0x1FFF, 2, MEM_RESET, PAGE_READONLY, RT_VM_PRIVATE);
    ALLOC_IS(r, r, 0x10000, MEM_RESET, PAGE_EXECUTE_READWRITE, RT_VM_PRIVATE);      /* reserved and committed pages alike */
    ALLOC_IS(r + 0xF000, r + 0xFFFF, 1, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_TRUE(vu_all(r + 0x1000, 0x2000, 0x5C));
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x1000);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x2000, PAGE_READWRITE);
    Q_RESERVED(r + 0x3000, r, PAGE_NOACCESS, 0xD000);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r, 0x10001, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r + 0xFFFF, 2, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r + 0x10000, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);   /* free space */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, PLAY(0), 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_TRUE(vu_all(r + 0x1000, 0x2000, 0x5C));
    FREE_OK(r, 0, MEM_RELEASE);
}

TEST(core, vm_alloc_types)
{
    /* How the loader will build an image: reserve the whole image as RT_VM_IMAGE, then
     * commit each section with its own protection.  Sections are 4 KB aligned, so several
     * share a host page (docs/RUNTIME.md section 0). */
    const uint64_t im = RT_IMAGE_ARENA_BASE + (UINT64_C(3) << 30);
    ALLOC_IS(im, im, 0x8000, MEM_RESERVE, PAGE_EXECUTE_WRITECOPY, RT_VM_IMAGE);
    Q_IS(im, im, im, PAGE_EXECUTE_WRITECOPY, 0x8000, MEM_RESERVE, 0, MEM_IMAGE);
    ALLOC_IS(im, im, 0x1000, MEM_COMMIT, PAGE_READONLY, RT_VM_IMAGE);                   /* headers */
    ALLOC_IS(im + 0x1000, im + 0x1000, 0x2000, MEM_COMMIT, PAGE_EXECUTE_READ, RT_VM_IMAGE);  /* .text */
    ALLOC_IS(im + 0x3000, im + 0x3000, 0x1000, MEM_COMMIT, PAGE_READONLY, RT_VM_IMAGE);      /* .rdata */
    ALLOC_IS(im + 0x4000, im + 0x4000, 0x3000, MEM_COMMIT, PAGE_WRITECOPY, RT_VM_IMAGE);     /* .data */
    Q_IS(im, im, im, PAGE_EXECUTE_WRITECOPY, 0x1000, MEM_COMMIT, PAGE_READONLY, MEM_IMAGE);
    Q_IS(im + 0x1000, im + 0x1000, im, PAGE_EXECUTE_WRITECOPY, 0x2000, MEM_COMMIT, PAGE_EXECUTE_READ, MEM_IMAGE);
    Q_IS(im + 0x2abc, im + 0x2000, im, PAGE_EXECUTE_WRITECOPY, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READ, MEM_IMAGE);
    Q_IS(im + 0x3000, im + 0x3000, im, PAGE_EXECUTE_WRITECOPY, 0x1000, MEM_COMMIT, PAGE_READONLY, MEM_IMAGE);
    Q_IS(im + 0x4000, im + 0x4000, im, PAGE_EXECUTE_WRITECOPY, 0x3000, MEM_COMMIT, PAGE_WRITECOPY, MEM_IMAGE);
    Q_IS(im + 0x7000, im + 0x7000, im, PAGE_EXECUTE_WRITECOPY, 0x1000, MEM_RESERVE, 0, MEM_IMAGE);
    vu_fill(im + 0x4000, 0x3000, 0x11);                         /* .data is writable */
    T_TRUE(vu_all_zero(im, 0x4000));
    RELEASE_OK(im, RT_VM_IMAGE);
    T_EQ(vu_query(im).State, MEM_FREE);

    /* A file view: MEM_MAPPED, in the VM arena, by any of the three ways to make a region. */
    uint64_t v = rt_vm_alloc(0, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, RT_VM_MAPPED);
    T_REQUIRE(v != 0);
    Q_IS(v, v, v, PAGE_READONLY, 0x3000, MEM_COMMIT, PAGE_READONLY, MEM_MAPPED);
    uint64_t w = rt_vm_alloc(0, 0x3000, MEM_COMMIT, PAGE_READWRITE, RT_VM_MAPPED);
    T_REQUIRE(w != 0);
    Q_IS(w, w, w, PAGE_READWRITE, 0x3000, MEM_COMMIT, PAGE_READWRITE, MEM_MAPPED);
    ALLOC_IS(PLAY(0), PLAY(0) + 0x10, 0x10, MEM_RESERVE, PAGE_READWRITE, RT_VM_MAPPED);
    Q_IS(PLAY(0), PLAY(0), PLAY(0), PAGE_READWRITE, 0x1000, MEM_RESERVE, 0, MEM_MAPPED);
    ALLOC_IS(PLAY(0), PLAY(0), 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_MAPPED);
    Q_IS(PLAY(0), PLAY(0), PLAY(0), PAGE_READWRITE, 0x1000, MEM_COMMIT, PAGE_READWRITE, MEM_MAPPED);
    RELEASE_OK(v, RT_VM_MAPPED);
    RELEASE_OK(w, RT_VM_MAPPED);
    RELEASE_OK(PLAY(0), RT_VM_MAPPED);
}

TEST(core, vm_alloc_unhandled)
{
    /* Calls Windows might honour and this map does not handle end in the unimplemented
     * report with the offending values; none returns a plausible answer. */
    T_TRAPS(rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE, RT_VM_PRIVATE),
            "FATAL unimplemented: rt_vm_alloc: MEM_WRITE_WATCH (allocation type 0x202000, address 0, size 0x10000)");
    T_TRAPS(rt_vm_alloc(PLAY(0), 0x10000, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE, RT_VM_PRIVATE),
            "rt_vm_alloc: MEM_WRITE_WATCH (allocation type 0x203000");
    T_TRAPS(rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_PHYSICAL_, PAGE_READWRITE, RT_VM_PRIVATE),
            "FATAL unimplemented: rt_vm_alloc: MEM_PHYSICAL (allocation type 0x402000");
    /* (With any protection but PAGE_READWRITE that request is simply invalid.) */
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x10000, MEM_RESERVE | MEM_PHYSICAL_, PAGE_READONLY, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x10000, MEM_RESERVE | MEM_PHYSICAL_, PAGE_READWRITE | PAGE_GUARD, RT_VM_PRIVATE);
    T_TRAPS(rt_vm_alloc(0, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE),
            "FATAL unimplemented: rt_vm_alloc: MEM_RESET with address 0 (size 0x1000)");

    /* A guest VirtualAlloc (RT_VM_PRIVATE) that commits or resets pages of an image or of
     * a file view, and the reverse. */
    uint64_t im = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_READONLY, RT_VM_IMAGE);
    uint64_t mv = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, RT_VM_MAPPED);
    uint64_t pv = rt_vm_alloc(0, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(im && mv && pv);
    T_TRAPS(rt_vm_alloc(im, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE),
            "FATAL unimplemented: rt_vm_alloc: MEM_COMMIT with type private inside a region of type image (address 0x");
    T_TRAPS(rt_vm_alloc(mv + 0x2000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE),
            "MEM_COMMIT with type private inside a region of type mapped");
    T_TRAPS(rt_vm_alloc(pv, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_IMAGE),
            "MEM_COMMIT with type image inside a region of type private");
    T_TRAPS(rt_vm_alloc(im, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_MAPPED),
            "MEM_COMMIT with type mapped inside a region of type image");
    T_TRAPS(rt_vm_alloc(im, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE),
            "FATAL unimplemented: rt_vm_alloc(MEM_RESET) inside a region of type image");
    T_TRAPS(rt_vm_alloc(mv, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_MAPPED),
            "rt_vm_alloc(MEM_RESET) inside a region of type mapped");
    /* None of that changed anything. */
    Q_IS(im, im, im, PAGE_READONLY, 0x10000, MEM_RESERVE, 0, MEM_IMAGE);
    Q_IS(mv, mv, mv, PAGE_READONLY, 0x10000, MEM_COMMIT, PAGE_READONLY, MEM_MAPPED);
    Q_RESERVED(pv, pv, PAGE_NOACCESS, 0x10000);

    /* Host memory outside the arenas: VirtualQuery calls it committed, so a commit there
     * would succeed on Windows; this map cannot do it and must not call it an invalid
     * address. */
    void *heap = malloc(0x10000);
    int on_stack = 0;
    T_REQUIRE(heap != NULL);
    T_TRAPS(rt_vm_alloc((uint64_t)(uintptr_t)heap, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE),
            "FATAL unimplemented: rt_vm_alloc on host memory outside both arenas (address 0x");
    T_TRAPS(rt_vm_alloc((uint64_t)(uintptr_t)&on_stack, 4, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE),
            "rt_vm_alloc on host memory outside both arenas");
    T_TRAPS(rt_vm_alloc((uint64_t)(uintptr_t)heap, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE),
            "rt_vm_alloc(MEM_RESET) on host memory outside both arenas");
    /* Reserving there is not a case for the report: the range is simply not free. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, (uint64_t)(uintptr_t)heap, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, (uint64_t)(uintptr_t)heap, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    free(heap);

    /* A type that is not one of the three is a bug in the caller. */
    T_TRAPS(rt_vm_alloc(0, 0x1000, MEM_RESERVE, PAGE_NOACCESS, (RtVmType)0), "FATAL runtime: rt_vm_alloc: 0 is not an RtVmType");
    T_TRAPS(rt_vm_alloc(0, 0x1000, MEM_RESERVE, PAGE_NOACCESS, (RtVmType)4), "rt_vm_alloc: 4 is not an RtVmType");
}
