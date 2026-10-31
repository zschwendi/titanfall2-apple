/* The VM map behind VirtualAlloc, VirtualFree, VirtualProtect and VirtualQuery
 * (docs/RUNTIME.md section 5.2, rt/vm.h, runtime/core/vm.c).
 *
 * This file: the arenas, the three cases written for the stand-in map (kept as they were),
 * one walk through a region's whole life, and what the map says about the memory the
 * runtime itself allocated at start-up.  The rules of each call, one by one, are in
 * vm_alloc.c, vm_free.c, vm_protect.c and vm_query.c; the host pages under the guest pages
 * in vm_host.c; a comparison against a model over random operations in vm_model.c; several
 * threads in vm_threads.c.  vm_util.c holds what they share.
 */
#include <unistd.h>

#include "vm_util.c"

static bool all_zero(uint64_t p, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) if (LD8(p + i)) return false;
    return true;
}

TEST(core, vm_arenas)
{
    /* Section 1.2: two fixed ranges, both below 8 TB. */
#if defined(RT_VM_LAYOUT_IOS) && RT_VM_LAYOUT_IOS
    T_EQ(RT_IMAGE_ARENA_BASE, UINT64_C(448) << 30);
    T_EQ(RT_VM_ARENA_BASE, UINT64_C(456) << 30);
#else
    T_EQ(RT_IMAGE_ARENA_BASE, UINT64_C(0x010000000000));
    T_EQ(RT_VM_ARENA_BASE, UINT64_C(0x011000000000));
#endif
    T_TRUE(RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE <= UINT64_C(0x80000000000));
    T_TRUE(RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE <= RT_VM_ARENA_BASE);
    T_TRUE(rt_vm_in_image_arena(RT_IMAGE_ARENA_BASE) && !rt_vm_in_image_arena(RT_IMAGE_ARENA_BASE - 1));
    T_TRUE(rt_vm_in_vm_arena(RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE - 1) && !rt_vm_in_vm_arena(RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE));
    T_TRUE(!rt_vm_in_vm_arena(0) && !rt_vm_in_image_arena(0));
    /* The host page size is asked, never assumed. */
    T_EQ(rt_vm_host_page(), (uint64_t)sysconf(_SC_PAGESIZE));
    /* Everything the runtime gave the first thread is in the VM arena. */
    T_TRUE(rt_vm_in_vm_arena(T_CTX()->gs_base) && rt_vm_in_vm_arena(T_CTX()->r[4]));
    /* The arenas are reserved once, by rt_init. */
    T_TRAPS(rt_vm_init(), "FATAL runtime: rt_vm_init called twice");
}

TEST(core, vm_alloc_and_free)
{
    /* Reserve and commit: 64 KB aligned, in the VM arena, zero-filled, writable. */
    uint64_t p = rt_vm_alloc(0, 0x5000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(p != 0);
    T_EQ(p & 0xFFFF, 0);
    T_TRUE(rt_vm_in_vm_arena(p));
    T_TRUE(all_zero(p, 0x5000));
    ST64(p, 0x1122334455667788);
    ST8(p + 0x4FFF, 0x99);
    T_EQ(LD64(p), 0x1122334455667788);

    /* A second allocation does not overlap the first and is 64 KB aligned too. */
    uint64_t q = rt_vm_alloc(0, 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(q != 0);
    T_TRUE(q >= p + 0x10000 || q + 0x10000 <= p);
    T_EQ(q & 0xFFFF, 0);

    /* MEM_COMMIT alone with no address means reserve and commit. */
    uint64_t c = rt_vm_alloc(0, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(c != 0);
    ST32(c, 1);

    /* At a requested address, if the range is free; the base is rounded down to 64 KB. */
    uint64_t want = RT_VM_ARENA_BASE + (UINT64_C(8) << 30);
    T_EQ(rt_vm_alloc(want + 0x2345, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), want);
    ST8(want + 0x2345, 7);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_alloc(want, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), 0);     /* taken */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_alloc(RT_VM_ARENA_BASE - 0x10000, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), 0);   /* outside the arenas */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);

    /* Bad arguments fail with a Win32 error; they do not trap. */
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_alloc(0, 0, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_alloc(0, 0x1000, 0, PAGE_READWRITE, RT_VM_PRIVATE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    /* Release needs the region base and size 0. */
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_free(p + 0x1000, 0, MEM_RELEASE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_free(p, 0x5000, MEM_RELEASE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(rt_vm_free(p, 0, MEM_RELEASE), TRUE);
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_free(p, 0, MEM_RELEASE), FALSE);                 /* already gone */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);

    /* First fit: the freed range is handed out again, and it reads as zero again. */
    uint64_t again = rt_vm_alloc(0, 0x5000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    T_EQ(again, p);
    T_TRUE(all_zero(again, 0x5000));
    T_EQ(rt_vm_free(q, 0, MEM_RELEASE), TRUE);
    T_EQ(rt_vm_free(c, 0, MEM_RELEASE), TRUE);
    T_EQ(rt_vm_free(want, 0, MEM_RELEASE), TRUE);
    T_EQ(rt_vm_free(again, 0, MEM_RELEASE), TRUE);
}

TEST(core, vm_reserve_then_commit)
{
    /* How a guest stack is made: reserve a region, commit part of it. */
    uint64_t r = rt_vm_alloc(0, 0x40000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(r != 0);
    T_EQ(rt_vm_alloc(r + 0x10000, 0x8000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), r + 0x10000);
    T_TRUE(all_zero(r + 0x10000, 0x8000));
    ST64(r + 0x17FF8, 5);
    /* Committing again is harmless and keeps the contents. */
    T_EQ(rt_vm_alloc(r + 0x10000, 0x1000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), r + 0x10000);
    T_EQ(LD64(r + 0x17FF8), 5);
    /* The address returned for a commit is rounded down to the guest page. */
    T_EQ(rt_vm_alloc(r + 0x20123, 0x10, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), r + 0x20000);
    /* A commit must lie inside one region. */
    rt_set_last_error(T_CTX(), 0);
    T_EQ(rt_vm_alloc(r + 0x3F000, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    T_EQ(rt_vm_free(r, 0, MEM_RELEASE), TRUE);
}

TEST(core, vm_region_life)
{
    /* One region from reservation to release.  The sequence is the one Wine's kernel32
     * test suite runs against real Windows (test_VirtualAlloc in its virtual.c); where an
     * expected value below is one that test checks, the comment says "Windows:".  (Checked
     * against a summary of the current test, not copied from it.) */
    uint64_t r = rt_vm_alloc(0, 0xFFFC, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_REQUIRE(r != 0);
    T_EQ(r & 0xFFFF, 0);

    /* Windows: a reservation is MEM_RESERVE with Protect 0, the protection of the call as
     * AllocationProtect, and its size rounded up to pages (0xFFFC -> 0x10000). */
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x10000);

    /* Windows: VirtualProtect on memory that is not committed fails with ERROR_INVALID_ADDRESS. */
    DWORD old = 0x5A5A;
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0xFFFC, PAGE_READONLY, &old);
    T_EQ(old, 0x5A5A);

    /* Windows: committing one page with PAGE_NOACCESS reports MEM_COMMIT, PAGE_NOACCESS,
     * a run of one page, and AllocationProtect unchanged. */
    ALLOC_IS(r, r, 0x1000, MEM_COMMIT, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x1000, PAGE_NOACCESS);
    Q_RESERVED(r + 0x1000, r, PAGE_NOACCESS, 0xF000);

    /* Windows: a range that is only partly committed still fails, and nothing changes. */
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0xFFFC, PAGE_READONLY, &old);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x1000, PAGE_NOACCESS);

    /* Windows: the old protection comes back exactly, call after call. */
    PROTECT_OK(r, 0x1000, PAGE_READONLY, &old);
    T_EQ(old, PAGE_NOACCESS);
    PROTECT_OK(r, 0x1000, PAGE_READWRITE, &old);
    T_EQ(old, PAGE_READONLY);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    T_TRUE(vu_all_zero(r, 0x1000));
    vu_fill(r, 20, 0x55);
    T_EQ(LD32(r), 0x55555555);

    /* Windows: MEM_RESET returns the address and leaves state and protection alone (the
     * data may or may not survive on Windows; here it does). */
    ALLOC_IS(r, r, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_EQ(LD32(r), 0x55555555);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    /* Windows: MEM_RESET of pages that are only reserved succeeds too and commits nothing. */
    ALLOC_IS(r + 0x1000, r + 0x1000, 0x1000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);
    Q_RESERVED(r + 0x1000, r, PAGE_NOACCESS, 0xF000);
    /* Windows: MEM_RESET past the end of the reservation is ERROR_INVALID_ADDRESS. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r + 0xF000, 0x2000, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE);

    /* Windows: an invalid protection is ERROR_INVALID_PARAMETER in every call. */
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, MEM_RESERVE, 0, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, 0, 0x1000, MEM_COMMIT, 0, RT_VM_PRIVATE);
    ALLOC_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, MEM_COMMIT, PAGE_READONLY | PAGE_EXECUTE, RT_VM_PRIVATE);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, PAGE_READWRITE | PAGE_EXECUTE_WRITECOPY, &old);

    /* Windows: a free type of 0 or MEM_FREE is ERROR_INVALID_PARAMETER. */
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0x10000, 0);
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 0, MEM_FREE);

    /* Windows: decommitting the whole range by size works; the region stays reserved. */
    FREE_OK(r, 0x10000, MEM_DECOMMIT);
    Q_RESERVED(r, r, PAGE_NOACCESS, 0x10000);

    /* Windows: MEM_RELEASE with a size is ERROR_INVALID_PARAMETER; with 0 it frees. */
    FREE_FAILS(ERROR_INVALID_PARAMETER, r, 1, MEM_RELEASE);
    FREE_OK(r, 0, MEM_RELEASE);
    T_EQ(vu_query(r).State, MEM_FREE);

    /* Windows: the same address can be allocated again at once. */
    ALLOC_IS(r, r, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE, RT_VM_PRIVATE);
    T_TRUE(vu_all_zero(r, 0x1000));
    /* Not from that test, but what the documented rounding implies: an address in the same
     * 64 KB as an existing region is rounded down onto it, and that range is not free. */
    ALLOC_FAILS(ERROR_INVALID_ADDRESS, r + 0x1000, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE, RT_VM_PRIVATE);
    FREE_OK(r, 0, MEM_RELEASE);
}

TEST(core, vm_startup_regions)
{
    /* What rt_init allocated is described like any other memory. */
    Ctx *ctx = T_CTX();
    RtThread *t = RT_THREAD(ctx);

    /* The guest stack (section 5.3): one region; 64 KB that is reserved and never
     * committed, then the stack, committed whole.  The C runtime's stack code asks
     * VirtualQuery about exactly this. */
    Q_RESERVED(t->stack_region, t->stack_region, PAGE_NOACCESS, 0x10000);
    Q_COMMITTED(t->stack_limit, t->stack_region, PAGE_NOACCESS, t->stack_base - t->stack_limit, PAGE_READWRITE);
    MEMORY_BASIC_INFORMATION sp = vu_query(ctx->r[4]);
    T_EQ((uint64_t)(uintptr_t)sp.BaseAddress, ctx->r[4] & ~UINT64_C(0xFFF));
    T_EQ((uint64_t)(uintptr_t)sp.AllocationBase, LD64(ctx->gs_base + RT_TEB_DEALLOC_STACK));
    T_EQ((uint64_t)(uintptr_t)sp.BaseAddress + sp.RegionSize, t->stack_base);
    T_EQ(sp.State, MEM_COMMIT);
    T_EQ(vu_host_prot(t->stack_region), PROT_NONE);             /* the guard really has no access */
    T_TRUE(!vu_reads(t->stack_limit - 1) && vu_writes(t->stack_limit));
    T_TRUE((uint64_t)(uintptr_t)vu_query(t->stack_base).AllocationBase != t->stack_region);    /* the top is past its end */

    /* The thread block and the process block: committed, read-write, private. */
    Q_COMMITTED(t->teb, t->teb, PAGE_READWRITE, RT_TEB_REGION_SIZE, PAGE_READWRITE);
    uint64_t peb = rt_process_block();
    Q_COMMITTED(peb, peb, PAGE_READWRITE, RT_PEB_REGION_SIZE, PAGE_READWRITE);

    /* A native module is an image: MEM_IMAGE, and AllocationBase is the module base
     * wherever in the image the address lies. */
    RtModule *m = rt_module_find("kernel32.dll");
    T_REQUIRE(m != NULL);
    Q_IS(m->base, m->base, m->base, PAGE_READWRITE, m->size, MEM_COMMIT, PAGE_READWRITE, MEM_IMAGE);
    Q_IS(m->base + 0x12345, m->base + 0x12000, m->base, PAGE_READWRITE, m->size - 0x12000, MEM_COMMIT,
         PAGE_READWRITE, MEM_IMAGE);

    /* The limits the map uses are the ones the guest is told (section 5.1). */
    const RtMachine *mc = rt_machine();
    T_EQ(mc->page_size, 0x1000);
    T_EQ(mc->granularity, 0x10000);
    T_EQ(mc->max_address, UINT64_C(0x7FFFFFEFFFF));
    MEMORY_BASIC_INFORMATION q;
    T_EQ(rt_vm_query(mc->max_address, &q), sizeof q);
    rt_set_last_error(ctx, VU_MARK);
    T_EQ(rt_vm_query(mc->max_address + 1, &q), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
}
