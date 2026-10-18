/* rt_vm_protect: the rules of VirtualProtect (runtime/core/vm.c states each rule where it
 * implements it).  What the host pages do under a protection is in vm_host.c.  Helpers and
 * the PLAY addresses: vm_util.c.
 */
#include <stdlib.h>

#include "vm_util.c"

#define OLD_MARK  0x5A5A5A5Au

TEST(core, vm_protect_bad_arguments)
{
    const uint64_t limit = rt_machine()->max_address + 1;
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    DWORD old = OLD_MARK;

    /* An invalid protection: ERROR_INVALID_PARAMETER, whatever else is wrong with the call. */
    for (unsigned i = 0; i < VU_N_BAD_PROTECTS; i++) {
        PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, vu_bad_protects[i], &old);
        PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, vu_bad_protects[i], NULL);
        PROTECT_FAILS(ERROR_INVALID_PARAMETER, PLAY(1), 0x1000, vu_bad_protects[i], &old);
        PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0, vu_bad_protects[i], &old);
    }

    /* No place for the old protection: ERROR_NOACCESS, and nothing is changed. */
    PROTECT_FAILS(ERROR_NOACCESS, r, 0x1000, PAGE_READONLY, NULL);
    PROTECT_FAILS(ERROR_NOACCESS, r, 0x10000, PAGE_NOACCESS, NULL);
    PROTECT_FAILS(ERROR_NOACCESS, PLAY(1), 0x1000, PAGE_READONLY, NULL);
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x10000, PAGE_READWRITE);

    /* Size 0: ERROR_INVALID_PARAMETER. */
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, PLAY(1), 0, PAGE_READONLY, &old);

    /* At or past the highest user address, or a size that reaches past it or wraps. */
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, limit, 0x1000, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, UINT64_MAX, 1, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, limit - 0x1000, 0x1001, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, UINT64_MAX, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, UINT64_MAX - r + 1, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, limit - r + 1, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, limit - 0x1000, 0x1000, PAGE_READONLY, &old);

    /* Copy-on-write protections are not valid for private memory. */
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, PAGE_WRITECOPY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, PAGE_EXECUTE_WRITECOPY, &old);
    PROTECT_FAILS(ERROR_INVALID_PARAMETER, r, 0x1000, PAGE_WRITECOPY | PAGE_GUARD, &old);

    /* A failed call leaves the old-protection variable and the pages alone. */
    T_EQ(old, OLD_MARK);
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
    T_EQ(vu_host_prot(r), PROT_READ | PROT_WRITE);
}

TEST(core, vm_protect_states)
{
    DWORD old = OLD_MARK;

    /* Only committed pages can be protected: free and reserved memory are
     * ERROR_INVALID_ADDRESS. */
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, PLAY(1), 0x1000, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, 0, 0x1000, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, 0x10000, 0x1000, PAGE_READONLY, &old);
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0x1000, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0x10000, PAGE_READONLY, &old);

    /* "If the state of any page in the specified region is not committed, the function
     * fails and returns without modifying the access protection of any pages." */
    T_REQUIRE(rt_vm_alloc(r + 0x1000, 0x3000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r + 0x1000);    /* pages 1 to 3 */
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0x2000, PAGE_READONLY, &old);                   /* the first is not */
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0x3000, 0x2000, PAGE_READONLY, &old);          /* the last is not */
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0x3FFF, 0x1002, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0x10000, PAGE_READONLY, &old);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x3000, PAGE_READWRITE);
    T_REQUIRE(rt_vm_free(r + 0x2000, 0x1000, MEM_DECOMMIT));                                /* a hole in the middle */
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0x1000, 0x3000, PAGE_READONLY, &old);
    Q_COMMITTED(r + 0x1000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x3000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    T_EQ(old, OLD_MARK);
    /* The committed pages on either side of the hole can be protected on their own. */
    PROTECT_OK(r + 0x1000, 0x1000, PAGE_READONLY, &old);
    T_EQ(old, PAGE_READWRITE);
    PROTECT_OK(r + 0x3000, 0x1000, PAGE_READONLY, &old);
    T_EQ(old, PAGE_READWRITE);

    /* The pages of one call lie in one reservation: not past its end into free space... */
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    old = OLD_MARK;
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0xF000, 0x1001, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0xFFFF, 2, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r - 0x1000, 0x2000, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0x10000, 0x1000, PAGE_READONLY, &old);
    /* ...and not into the next reservation, however alike and adjacent. */
    const uint64_t b = r + 0x10000;
    T_REQUIRE(rt_vm_alloc(b, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == b);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0xF000, 0x2000, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r + 0xFFFF, 2, PAGE_READONLY, &old);
    PROTECT_FAILS(ERROR_INVALID_ADDRESS, r, 0x20000, PAGE_READONLY, &old);
    T_EQ(old, OLD_MARK);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0x10000, PAGE_READWRITE);
    Q_COMMITTED(b, b, PAGE_READWRITE, 0x10000, PAGE_READWRITE);

    /* Exactly the reservation, to its last byte, is fine. */
    PROTECT_OK(r, 0x10000, PAGE_READONLY, &old);
    T_EQ(old, PAGE_READWRITE);
    PROTECT_OK(r + 0xFFFF, 1, PAGE_READWRITE, &old);
    T_EQ(old, PAGE_READONLY);
    Q_COMMITTED(r, r, PAGE_NOACCESS, 0xF000, PAGE_READONLY);
    Q_COMMITTED(r + 0xF000, r, PAGE_NOACCESS, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(b, b, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
}

TEST(core, vm_protect_old_value)
{
    /* The old protection is the exact value the first page had, modifiers included:
     * through every protection and back. */
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    vu_fill(r, 0x10000, 0x3C);
    DWORD prev = PAGE_READWRITE, old;
    for (unsigned round = 0; round < 2; round++) {
        for (unsigned i = 0; i < VU_N_PRIVATE_PROTECTS; i++) {
            DWORD p = vu_private_protects[round ? VU_N_PRIVATE_PROTECTS - 1 - i : i];
            old = OLD_MARK;
            PROTECT_OK(r, 0x10000, p, &old);
            T_EQ(old, prev);
            Q_COMMITTED(r, r, PAGE_READWRITE, 0x10000, p);
            T_EQ(vu_host_prot(r), vu_access(p));
            prev = p;
        }
    }
    /* The same value again: the old value is that value. */
    PROTECT_OK(r, 0x10000, prev, &old);
    T_EQ(old, prev);
    /* The contents came through all of it, no-access and guard included. */
    PROTECT_OK(r, 0x10000, PAGE_READWRITE, &old);
    T_TRUE(vu_all(r, 0x10000, 0x3C));

    /* The FIRST page of the range decides, whatever the others have. */
    PROTECT_OK(r + 0x4000, 0x1000, PAGE_READONLY, &old);
    PROTECT_OK(r + 0x5000, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old);
    PROTECT_OK(r + 0x6000, 0x1000, PAGE_NOACCESS, &old);
    PROTECT_OK(r + 0x4000, 0x3000, PAGE_EXECUTE_READ, &old);
    T_EQ(old, PAGE_READONLY);
    Q_COMMITTED(r + 0x4000, r, PAGE_READWRITE, 0x3000, PAGE_EXECUTE_READ);
    PROTECT_OK(r + 0x5000, 0x1000, PAGE_READWRITE | PAGE_GUARD, &old);
    PROTECT_OK(r + 0x5000, 0x2000, PAGE_READWRITE, &old);
    T_EQ(old, PAGE_READWRITE | PAGE_GUARD);
    PROTECT_OK(r + 0x3000, 0x3000, PAGE_READWRITE, &old);       /* page 3 was never changed */
    T_EQ(old, PAGE_READWRITE);
    /* An address inside a page: the page that holds it is the first page. */
    PROTECT_OK(r + 0x7000, 0x1000, PAGE_EXECUTE, &old);
    PROTECT_OK(r + 0x7FFF, 0x1000, PAGE_READWRITE, &old);       /* pages 7 and 8 */
    T_EQ(old, PAGE_EXECUTE);
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x10000, PAGE_READWRITE);
    T_TRUE(vu_all(r, 0x10000, 0x3C));
}

TEST(core, vm_protect_rounding)
{
    /* "A 2-byte range straddling a page boundary causes the protection attributes of both
     * pages to be changed"; a range inside one page changes that page. */
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    DWORD old;
    PROTECT_OK(r + 0x1FFF, 2, PAGE_READONLY, &old);             /* pages 1 and 2 */
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x1000, r, PAGE_READWRITE, 0x2000, PAGE_READONLY);
    Q_COMMITTED(r + 0x3000, r, PAGE_READWRITE, 0xD000, PAGE_READWRITE);
    PROTECT_OK(r + 0x5800, 1, PAGE_NOACCESS, &old);             /* page 5 */
    Q_COMMITTED(r + 0x4000, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x5000, r, PAGE_READWRITE, 0x1000, PAGE_NOACCESS);
    Q_COMMITTED(r + 0x6000, r, PAGE_READWRITE, 0xA000, PAGE_READWRITE);
    PROTECT_OK(r + 0x8000, 0x1000, PAGE_EXECUTE_READ, &old);    /* exactly page 8 */
    Q_COMMITTED(r + 0x7000, r, PAGE_READWRITE, 0x1000, PAGE_READWRITE);
    Q_COMMITTED(r + 0x8000, r, PAGE_READWRITE, 0x1000, PAGE_EXECUTE_READ);
    Q_COMMITTED(r + 0x9000, r, PAGE_READWRITE, 0x7000, PAGE_READWRITE);
    PROTECT_OK(r + 0xA001, 0x1000, PAGE_READONLY | PAGE_GUARD, &old);     /* a page's worth, off by one: 10 and 11 */
    Q_COMMITTED(r + 0xA000, r, PAGE_READWRITE, 0x2000, PAGE_READONLY | PAGE_GUARD);
    Q_COMMITTED(r + 0xC000, r, PAGE_READWRITE, 0x4000, PAGE_READWRITE);
    PROTECT_OK(r + 0xFFFF, 1, PAGE_READONLY, &old);             /* the last byte: page 15 */
    Q_COMMITTED(r + 0xC000, r, PAGE_READWRITE, 0x3000, PAGE_READWRITE);
    Q_COMMITTED(r + 0xF000, r, PAGE_READWRITE, 0x1000, PAGE_READONLY);
    PROTECT_OK(r, 1, PAGE_READONLY, &old);                      /* the first byte: page 0 */
    Q_COMMITTED(r, r, PAGE_READWRITE, 0x3000, PAGE_READONLY);
}

TEST(core, vm_protect_types)
{
    /* On an image or a file view copy-on-write protections are valid, are recorded as
     * given, and come back as the old value. */
    static const RtVmType types[] = { RT_VM_IMAGE, RT_VM_MAPPED };
    static const DWORD kinds[] = { MEM_IMAGE, MEM_MAPPED };
    for (unsigned i = 0; i < 2; i++) {
        DWORD old = OLD_MARK;
        uint64_t v = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, types[i]);
        T_REQUIRE(v != 0);
        PROTECT_OK(v, 0x10000, PAGE_WRITECOPY, &old);
        T_EQ(old, PAGE_READONLY);
        Q_IS(v, v, v, PAGE_READONLY, 0x10000, MEM_COMMIT, PAGE_WRITECOPY, kinds[i]);
        vu_fill(v, 0x10000, 0x77);                              /* copy-on-write is writable */
        PROTECT_OK(v + 0x1000, 0x1000, PAGE_EXECUTE_WRITECOPY, &old);
        T_EQ(old, PAGE_WRITECOPY);
        PROTECT_OK(v, 0x10000, PAGE_EXECUTE_READ, &old);
        T_EQ(old, PAGE_WRITECOPY);
        PROTECT_OK(v + 0x1000, 0x1000, PAGE_READWRITE, &old);
        T_EQ(old, PAGE_EXECUTE_READ);
        Q_IS(v + 0x1000, v + 0x1000, v, PAGE_READONLY, 0x1000, MEM_COMMIT, PAGE_READWRITE, kinds[i]);
        T_TRUE(vu_all(v, 0x10000, 0x77));
        /* PAGE_NOCACHE and PAGE_WRITECOMBINE cannot be set on such pages; PAGE_GUARD can. */
        old = OLD_MARK;
        PROTECT_FAILS(ERROR_INVALID_PARAMETER, v, 0x1000, PAGE_READWRITE | PAGE_NOCACHE, &old);
        PROTECT_FAILS(ERROR_INVALID_PARAMETER, v, 0x1000, PAGE_READONLY | PAGE_WRITECOMBINE, &old);
        T_EQ(old, OLD_MARK);
        Q_IS(v, v, v, PAGE_READONLY, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READ, kinds[i]);
        PROTECT_OK(v, 0x1000, PAGE_READONLY | PAGE_GUARD, &old);
        T_EQ(old, PAGE_EXECUTE_READ);
        PROTECT_OK(v, 0x1000, PAGE_EXECUTE_READ, &old);
        T_EQ(old, PAGE_READONLY | PAGE_GUARD);
        T_REQUIRE(rt_vm_release(v, types[i]));
    }

    /* What the loader will do to a module image once it is built: protect it.  A native
     * module's image is one region, committed read-write by the registry. */
    RtModule *m = rt_module_find("kernel32.dll");
    T_REQUIRE(m != NULL);
    DWORD old = OLD_MARK;
    uint16_t mz = LD16(m->base);
    PROTECT_OK(m->base, m->size, PAGE_READONLY, &old);
    T_EQ(old, PAGE_READWRITE);
    Q_IS(m->base, m->base, m->base, PAGE_READWRITE, m->size, MEM_COMMIT, PAGE_READONLY, MEM_IMAGE);
    T_EQ(LD16(m->base), mz);
    T_TRUE(vu_reads(m->base) && !vu_writes(m->base) && !vu_writes(m->base + m->size - 1));
    PROTECT_OK(m->base, m->size, PAGE_READWRITE, &old);
    T_EQ(old, PAGE_READONLY);
}

TEST(core, vm_protect_unhandled)
{
    /* Host memory outside the arenas: VirtualQuery calls it committed, Windows would
     * protect it, this map cannot. */
    void *heap = malloc(0x10000);
    int on_stack = 0;
    DWORD old = OLD_MARK;
    T_REQUIRE(heap != NULL);
    T_TRAPS(rt_vm_protect((uint64_t)(uintptr_t)heap, 0x1000, PAGE_READONLY, &old),
            "FATAL unimplemented: rt_vm_protect on host memory outside both arenas (address 0x");
    T_TRAPS(rt_vm_protect((uint64_t)(uintptr_t)&on_stack, 4, PAGE_EXECUTE_READWRITE, &old),
            "rt_vm_protect on host memory outside both arenas");
    T_TRAPS(rt_vm_protect((uint64_t)(uintptr_t)&rt_vm_protect, 4, PAGE_EXECUTE_READWRITE, &old),
            "rt_vm_protect on host memory outside both arenas");
    T_EQ(old, OLD_MARK);
    memset(heap, 1, 0x10000);                                   /* still writable */
    free(heap);
}
