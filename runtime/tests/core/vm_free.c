/* rt_vm_free: the rules of VirtualFree (runtime/core/vm.c states each rule where it
 * implements it).  Helpers and the PLAY addresses: vm_util.c.
 */
#include <stdlib.h>

#include "vm_util.c"

/* Reserve `size` at `at`, commit all of it read-write and give every page its own byte. */
static uint64_t filled(uint64_t at, uint64_t size) {
    uint64_t r = rt_vm_alloc(at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    if (!r) return 0;
    for (uint64_t p = 0; p < size / PG; p++) vu_fill(r + p * PG, PG, (uint8_t)(p + 1));
    return r;
}
static bool page_has(uint64_t r, uint64_t p) { return vu_all(r + p * PG, PG, (uint8_t)(p + 1)); }

TEST(core, vm_free_bad_arguments)
{
    const uint64_t limit = rt_machine()->max_address + 1;
    const uint64_t r = filled(PLAY(0), 0x10000);
    T_REQUIRE(r == PLAY(0));

    /* The type is exactly MEM_DECOMMIT or exactly MEM_RELEASE. */
    static const DWORD bad[] = {
        0, MEM_FREE, MEM_COMMIT, MEM_RESERVE, MEM_RESET, MEM_DECOMMIT | MEM_RELEASE,
        MEM_RELEASE | 0x1, MEM_RELEASE | 0x2,                   /* the placeholder flags of Windows 10 */
        MEM_DECOMMIT | MEM_RESERVE, MEM_RELEASE | MEM_COMMIT, MEM_DECOMMIT | 0x80000000, 0x1, 0xFFFFFFFF,
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0, bad[i]);
        FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, bad[i]);
        FREE_FAILS(ERROR_INVALID_PARAMETER, r + 0x1000, 0x1000, bad[i]);
        FREE_FAILS(ERROR_INVALID_PARAMETER, PLAY(1), 0, bad[i]);
        FREE_FAILS(ERROR_INVALID_PARAMETER, 0, 0, bad[i]);
    }

    /* MEM_RELEASE with a size: ERROR_INVALID_PARAMETER, checked before the address is. */
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 1, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0x10000, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r + 0x1000, 0x1000, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, UINT64_MAX, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, PLAY(1), 1, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, 0, 1, MEM_RELEASE);

    /* At or past the highest user address, or a size that reaches past it or wraps:
     * ERROR_INVALID_PARAMETER.  Just below, the address is valid and holds nothing. */
    FREE_FAILS(ERROR_INVALID_PARAMETER, limit, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, limit, 0, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, limit, 0x1000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, UINT64_MAX, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, limit - 0x1000, 0x1001, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, UINT64_MAX, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, UINT64_MAX - r + 1, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, limit - r + 1, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, limit - 0x1000, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_ADDRESS, limit - 0x1000, 0x1000, MEM_DECOMMIT);

    /* None of it touched the region. */
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
    for (uint64_t p = 0; p < 16; p++) T_TRUE(page_has(r, p));
}

TEST(core, vm_free_release)
{
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    T_REQUIRE(rt_vm_alloc(r + 0x3000, 0x6000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r + 0x3000);
    T_REQUIRE(rt_vm_alloc(r + 0x1C000, 0x1000, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE) == r + 0x1C000);
    vu_fill(r + 0x3000, 0x6000, 0xEE);

    /* A release needs the base of the reservation: any other address in it, and any
     * address that is in no reservation, is ERROR_INVALID_ADDRESS. */
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x1000, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x3000, 0, MEM_RELEASE);          /* where the committed part starts */
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x10000, 0, MEM_RELEASE);         /* a 64 KB boundary inside it */
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x1FFFF, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r - 1, 0, MEM_RELEASE);               /* free space below */
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x20000, 0, MEM_RELEASE);         /* free space above */
    FREE_FAILS(ERROR_INVALID_ADDRESS, PLAY(1), 0, MEM_RELEASE);             /* never allocated */
    FREE_FAILS(ERROR_INVALID_ADDRESS, 0, 0, MEM_RELEASE);                   /* a null pointer */
    FREE_FAILS(ERROR_INVALID_ADDRESS, 0x1234, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_ADDRESS, 0x10000, 0, MEM_RELEASE);
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x3000);
    Q_COMMITTED(r + 0x3000, r, PAGE_NOACCESS, 0x6000, PAGE_READWRITE);
    T_TRUE(vu_all(r + 0x3000, 0x6000, 0xEE));

    /* It frees the whole reservation whatever state its pages are in: the range is free,
     * no host page of it is accessible or holds memory. */
    FREE_OK(r, 0, MEM_RELEASE);
    Q_FREE(r, VM_END - r);
    for (uint64_t a = r; a < r + 0x20000; a += rt_vm_host_page()) {
        T_EQ(vu_host_prot(a), PROT_NONE);
        T_TRUE(!vu_resident(a));
    }
    T_TRUE(!vu_reads(r + 0x3000));

    /* Gone is gone: a second release, and a decommit, find no reservation. */
    FREE_FAILS(ERROR_INVALID_ADDRESS, r, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r, 0, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x3000, 0x1000, MEM_DECOMMIT);

    /* The range can be allocated again and reads as zero. */
    ALLOC_IS(r, r, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(r, 0x20000));

    /* The address is rounded down to its page before it is compared with the base (what
     * the NT kernel does as ReactOS reimplements it; the documentation asks for the base
     * itself, and this has not been observed on Windows). */
    FREE_OK(r + 0xFFF, 0, MEM_RELEASE);
    Q_FREE(r, VM_END - r);

    /* A release leaves its neighbours alone. */
    uint64_t a = filled(r, 0x10000), b = filled(r + 0x10000, 0x10000), c = filled(r + 0x20000, 0x10000);
    T_REQUIRE(a && b && c);
    FREE_OK(b, 0, MEM_RELEASE);
    Q_COMMITTED(a, a, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
    Q_FREE(b, 0x10000);
    Q_COMMITTED(c, c, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
    for (uint64_t p = 0; p < 16; p++) T_TRUE(page_has(a, p) && page_has(c, p));
    T_EQ(vu_host_prot(b), PROT_NONE);
    T_EQ(vu_host_prot(b + 0xF000), PROT_NONE);
    FREE_OK(a, 0, MEM_RELEASE);
    FREE_OK(c, 0, MEM_RELEASE);

    /* A region whose size is not a whole number of host pages: the release also clears
     * what the guest wrote, through the union, past the end of the region. */
    uint64_t s = filled(PLAY(1), 0x5000);
    T_REQUIRE(s == PLAY(1));
    uint64_t past = s + 0x5000, host_end = (past + rt_vm_host_page() - 1) & ~(rt_vm_host_page() - 1);
    if (past < host_end) vu_fill(past, host_end - past, 0x99);
    FREE_OK(s, 0, MEM_RELEASE);
    ALLOC_IS(s, s, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(s, 0x10000));
    FREE_OK(s, 0, MEM_RELEASE);
}

TEST(core, vm_free_decommit)
{
    const uint64_t r = filled(PLAY(0), 0x20000);                /* 32 pages */
    T_REQUIRE(r == PLAY(0));

    /* A decommit covers every page that holds a byte of the range and leaves those pages
     * reserved; the other pages keep their contents. */
    FREE_OK(r + 0x2FFF, 2, MEM_DECOMMIT);                       /* two bytes: pages 2 and 3 */
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x2000, PAGE_READWRITE);
    Q_RESERVED(r + 0x2000, r, PAGE_READWRITE, 0x2000);
    Q_COMMITTED(r + 0x4000, r, PAGE_READWRITE, 0x1C000, PAGE_READWRITE);
    FREE_OK(r + 0x8123, 1, MEM_DECOMMIT);                       /* one byte: page 8 */
    Q_COMMITTED(r + 0x7000, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0x8000, r, PAGE_READWRITE, 0x1000);
    Q_COMMITTED(r + 0x9000, r, PAGE_READWRITE, 0x17000, PAGE_READWRITE);
    FREE_OK(r + 0xA000, 0x1000, MEM_DECOMMIT);                  /* exactly page 10 */
    Q_COMMITTED(r + 0x9000, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0xA000, r, PAGE_READWRITE, 0x1000);
    FREE_OK(r + 0xC000, 0x1001, MEM_DECOMMIT);                  /* a page and a byte: pages 12 and 13 */
    Q_COMMITTED(r + 0xB000, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0xC000, r, PAGE_READWRITE, 0x2000);
    Q_COMMITTED(r + 0xE000, r, PAGE_READWRITE, 0x12000, PAGE_READWRITE);
    for (uint64_t p = 0; p < 32; p++)
        if (p != 2 && p != 3 && p != 8 && p != 10 && p != 12 && p != 13) T_TRUE(page_has(r, p));

    /* "The function does not fail if you attempt to decommit an uncommitted page": over a
     * mix of committed and reserved pages, and over reserved pages only. */
    FREE_OK(r + 0x1000, 0x4000, MEM_DECOMMIT);                  /* pages 1 to 4; 2 and 3 were decommitted */
    FREE_OK(r + 0x2000, 0x2000, MEM_DECOMMIT);
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_RESERVED(r + 0x1000, r, PAGE_READWRITE, 0x4000);
    Q_COMMITTED(r + 0x5000, r, PAGE_READWRITE, 0x3000, PAGE_READWRITE);
    T_TRUE(page_has(r, 0) && page_has(r, 5));

    /* What was decommitted reads as zero when it is committed again. */
    ALLOC_IS(r + 0x1000, r + 0x1000, 0x4000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(r + 0x1000, 0x4000));
    T_TRUE(page_has(r, 0) && page_has(r, 5));

    /* Up to the last byte of the reservation.  One byte past it the call fails and
     * changes nothing. */
    FREE_FAILS(ERROR_INVALID_PARAMETER, r + 0x1F000, 0x1001, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0x20001, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r + 0x1FFFF, 2, MEM_DECOMMIT);
    Q_COMMITTED(r + 0xE000, r, PAGE_READWRITE, 0x12000, PAGE_READWRITE);
    T_TRUE(page_has(r, 0) && page_has(r, 31));
    FREE_OK(r + 0x1FFFF, 1, MEM_DECOMMIT);
    Q_COMMITTED(r + 0xE000, r, PAGE_READWRITE, 0x11000, PAGE_READWRITE);
    Q_RESERVED(r + 0x1F000, r, PAGE_READWRITE, 0x1000);

    /* Size 0 means the whole reservation and needs its base. */
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x1000, 0, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x10000, 0, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x1FFFF, 0, MEM_DECOMMIT);
    T_TRUE(page_has(r, 0) && page_has(r, 16));
    FREE_OK(r + 0x7FF, 0, MEM_DECOMMIT);                        /* the page of the base: see vm_free_release */
    Q_RESERVED(r, r, PAGE_READWRITE, 0x20000);
    for (uint64_t a = r; a < r + 0x20000; a += rt_vm_host_page()) {
        T_EQ(vu_host_prot(a), PROT_NONE);
        T_TRUE(!vu_resident(a));
    }
    /* Again, by either spelling: nothing is committed, nothing fails. */
    FREE_OK(r, 0, MEM_DECOMMIT);
    FREE_OK(r, 0x20000, MEM_DECOMMIT);
    Q_RESERVED(r, r, PAGE_READWRITE, 0x20000);

    /* The range is still reserved: it cannot be allocated again, and it can be committed. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r, 0x1000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    ALLOC_IS(r, r, 0x20000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(r, 0x20000));

    /* An address in no reservation: ERROR_INVALID_ADDRESS, also when the range would run
     * on into one. */
    FREE_FAILS(ERROR_INVALID_ADDRESS, PLAY(1), 0x1000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, PLAY(1), 0, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r - 0x1000, 0x2000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, r + 0x20000, 0x1000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, 0, 0x1000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, 0, 0, MEM_DECOMMIT);
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x20000, PAGE_READWRITE);

    /* Freeing across reservations fails, however alike and adjacent they are. */
    const uint64_t b = filled(r + 0x20000, 0x10000);
    T_REQUIRE(b == r + 0x20000);
    vu_fill(r, 0x20000, 0x44);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r + 0x1F000, 0x2000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0x30000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r + 0x1FFFF, 2, MEM_DECOMMIT);
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x20000, PAGE_READWRITE);
    Q_COMMITTED(b, b, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
    T_TRUE(vu_all(r, 0x20000, 0x44));
    for (uint64_t p = 0; p < 16; p++) T_TRUE(page_has(b, p));

    /* A small region: its size is its size, the slack up to 64 KB is free space. */
    const uint64_t s = filled(PLAY(1), 0x5000);
    T_REQUIRE(s == PLAY(1));
    FREE_FAILS(ERROR_INVALID_PARAMETER, s, 0x5001, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_ADDRESS, s + 0x5000, 0x1000, MEM_DECOMMIT);
    for (uint64_t p = 0; p < 5; p++) T_TRUE(page_has(s, p));
    FREE_OK(s, 0x5000, MEM_DECOMMIT);
    Q_RESERVED(s, s, PAGE_READWRITE, 0x5000);
}

TEST(core, vm_free_decommit_protections)
{
    /* Decommit does not care what protection a page has, and a later commit gives the
     * page the protection of that commit, not the one it had. */
    for (unsigned i = 0; i < VU_N_PRIVATE_PROTECTS; i++) {
        DWORD p = vu_private_protects[i], old;
        const uint64_t r = filled(PLAY(0), 0x10000);
        T_REQUIRE(r == PLAY(0));
        PROTECT_OK(r, 0x10000, p, &old);
        FREE_OK(r + 0x3000, 0x6000, MEM_DECOMMIT);
        Q_COMMITTED(r, r, PAGE_READWRITE, 0x3000, p);
        Q_RESERVED(r + 0x3000, r, PAGE_READWRITE, 0x6000);
        Q_COMMITTED(r + 0x9000, r, PAGE_READWRITE, 0x7000, p);
        ALLOC_IS(r + 0x3000, r + 0x3000, 0x6000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
        T_TRUE(vu_all_zero(r + 0x3000, 0x6000));
        PROTECT_OK(r, 0x10000, PAGE_READWRITE, &old);
        T_EQ(old, p);
        for (uint64_t g = 0; g < 16; g++) T_EQ(page_has(r, g), g < 3 || g >= 9);     /* the rest kept its data */
        FREE_OK(r, 0, MEM_RELEASE);
    }
}

TEST(core, vm_free_types)
{
    /* VirtualFree is for private memory.  A decommit inside an image or a file view fails
     * with ERROR_INVALID_PARAMETER and changes nothing. */
    uint64_t im = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_WRITECOPY, RT_VM_IMAGE);
    uint64_t mv = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_MAPPED);
    T_REQUIRE(im && mv);
    vu_fill(im, 0x10000, 0x21);
    vu_fill(mv, 0x10000, 0x22);
    FREE_FAILS(ERROR_INVALID_PARAMETER, im, 0x1000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, im, 0, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, im + 0x8000, 0x8000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, mv, 0x10000, MEM_DECOMMIT);
    FREE_FAILS(ERROR_INVALID_PARAMETER, mv, 0, MEM_DECOMMIT);
    Q_IS(im, im, im, PAGE_WRITECOPY, 0x10000, MEM_COMMIT, PAGE_WRITECOPY, MEM_IMAGE);
    Q_IS(mv, mv, mv, PAGE_READWRITE, 0x10000, MEM_COMMIT, PAGE_READWRITE, MEM_MAPPED);
    T_TRUE(vu_all(im, 0x10000, 0x21) && vu_all(mv, 0x10000, 0x22));

    /* Nor does a release: the loader and the file unit give such a region back with
     * rt_vm_release, so a guest VirtualFree on a module or on a view removes nothing.  The
     * type is looked at before the address is compared with the base. */
    FREE_FAILS(ERROR_INVALID_PARAMETER, im, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, mv, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, im + 0x1000, 0, MEM_RELEASE);
    FREE_FAILS(ERROR_INVALID_PARAMETER, mv, 0x10000, MEM_RELEASE);
    Q_IS(im, im, im, PAGE_WRITECOPY, 0x10000, MEM_COMMIT, PAGE_WRITECOPY, MEM_IMAGE);
    Q_IS(mv, mv, mv, PAGE_READWRITE, 0x10000, MEM_COMMIT, PAGE_READWRITE, MEM_MAPPED);
    T_TRUE(vu_all(im, 0x10000, 0x21) && vu_all(mv, 0x10000, 0x22));
    RELEASE_OK(im, RT_VM_IMAGE);
    RELEASE_OK(mv, RT_VM_MAPPED);
}

TEST(core, vm_release)
{
    /* rt_vm_release: the owner of a region gives the whole region back, by its exact base
     * and its type (rt/vm.h). */
    uint64_t im = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_WRITECOPY, RT_VM_IMAGE);
    uint64_t mv = rt_vm_alloc(0, 0x13000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_MAPPED);
    uint64_t pv = rt_vm_alloc(0, 0x20000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(im && mv && pv);
    T_REQUIRE(rt_vm_alloc(pv + 0x3000, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == pv + 0x3000);
    vu_fill(im, 0x10000, 0x31);
    vu_fill(mv, 0x13000, 0x32);
    vu_fill(pv + 0x3000, 0x2000, 0x33);

    /* The wrong type: ERROR_INVALID_PARAMETER, in every combination, and nothing changes. */
    RELEASE_FAILS(ERROR_INVALID_PARAMETER, im, RT_VM_MAPPED);
    RELEASE_FAILS(ERROR_INVALID_PARAMETER, im, RT_VM_PRIVATE);
    RELEASE_FAILS(ERROR_INVALID_PARAMETER, mv, RT_VM_IMAGE);
    RELEASE_FAILS(ERROR_INVALID_PARAMETER, mv, RT_VM_PRIVATE);
    RELEASE_FAILS(ERROR_INVALID_PARAMETER, pv, RT_VM_IMAGE);
    RELEASE_FAILS(ERROR_INVALID_PARAMETER, pv, RT_VM_MAPPED);
    /* Not exactly a base: ERROR_INVALID_ADDRESS.  Nothing is rounded, unlike VirtualFree;
     * the address is looked at before the type. */
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, mv + 1, RT_VM_MAPPED);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, mv + 0x1000, RT_VM_MAPPED);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, mv + 0x10000, RT_VM_MAPPED);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, mv + 0x10000, RT_VM_PRIVATE);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, pv + 0xFFF, RT_VM_PRIVATE);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, PLAY(3), RT_VM_PRIVATE);                   /* free space */
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, 0, RT_VM_PRIVATE);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, 0x1000, RT_VM_IMAGE);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, rt_machine()->max_address + 1, RT_VM_MAPPED);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, UINT64_MAX, RT_VM_MAPPED);
    int on_stack = 0;                                                               /* host memory: no report, an error */
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, (uint64_t)(uintptr_t)&on_stack & ~UINT64_C(0xFFFF), RT_VM_PRIVATE);
    Q_IS(im, im, im, PAGE_WRITECOPY, 0x10000, MEM_COMMIT, PAGE_WRITECOPY, MEM_IMAGE);
    Q_IS(mv, mv, mv, PAGE_READWRITE, 0x13000, MEM_COMMIT, PAGE_READWRITE, MEM_MAPPED);
    Q_COMMITTED(pv + 0x3000, pv, PAGE_NOACCESS, 0x2000, PAGE_READWRITE);
    T_TRUE(vu_all(im, 0x10000, 0x31) && vu_all(mv, 0x13000, 0x32) && vu_all(pv + 0x3000, 0x2000, 0x33));

    /* The right type: the whole region is free, whatever the state of its pages, last error
     * is left alone, and a second release finds no region. */
    RELEASE_OK(mv, RT_VM_MAPPED);
    T_EQ(vu_query(mv).State, MEM_FREE);
    T_EQ(vu_query(mv + 0x12000).State, MEM_FREE);
    RELEASE_FAILS(ERROR_INVALID_ADDRESS, mv, RT_VM_MAPPED);
    RELEASE_OK(im, RT_VM_IMAGE);
    RELEASE_OK(pv, RT_VM_PRIVATE);
    T_EQ(vu_query(im).State, MEM_FREE);
    T_EQ(vu_query(pv).State, MEM_FREE);
    T_EQ(vu_query(pv + 0x3000).State, MEM_FREE);

    /* What was released reads as zero when the range is used again (the host pages were
     * replaced, which is also what removes a file the owner had mapped over a view). */
    uint64_t again = rt_vm_alloc(mv, 0x13000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(again == mv);
    T_TRUE(vu_all(again, 0x13000, 0));
    /* A private region may go back either way. */
    FREE_OK(again, 0, MEM_RELEASE);

    T_TRAPS(rt_vm_release(mv, (RtVmType)0), "FATAL runtime: rt_vm_release: 0 is not an RtVmType");
    T_TRAPS(rt_vm_release(mv, (RtVmType)4), "rt_vm_release: 4 is not an RtVmType");
}

TEST(core, vm_free_unhandled)
{
    /* Host memory outside the arenas is reported by VirtualQuery as committed; freeing it
     * is not something this map can do, or may answer with a made-up error. */
    void *heap = malloc(0x10000);
    int on_stack = 0;
    T_REQUIRE(heap != NULL);
    T_TRAPS(rt_vm_free((uint64_t)(uintptr_t)heap, 0, MEM_RELEASE),
            "FATAL unimplemented: rt_vm_free on host memory outside both arenas (address 0x");
    T_TRAPS(rt_vm_free((uint64_t)(uintptr_t)heap, 0x1000, MEM_DECOMMIT), "rt_vm_free on host memory outside both arenas");
    T_TRAPS(rt_vm_free((uint64_t)(uintptr_t)&on_stack, 0, MEM_DECOMMIT), "rt_vm_free on host memory outside both arenas");
    memset(heap, 1, 0x10000);                                   /* still there */
    free(heap);
}
