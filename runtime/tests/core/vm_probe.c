/* rt_vm_probe: the VM map's query that never waits (rt/vm.h; review finding R14). */
#include <pthread.h>

#include "rt/test.h"
#include "rt/vm.h"
#include "rt/../../core/report.h"       /* rt_vm_test_hold_lock */

static void *probe_on_thread(void *arg) {
    return (void *)(uintptr_t)rt_vm_probe((uint64_t)(uintptr_t)arg, NULL);
}

TEST(core, vm_probe)
{
    RtVmProbe p;
    int local = 0;

    /* Outside both arenas: nothing is known and nothing else is filled in. */
    memset(&p, 0xAA, sizeof p);
    T_EQ(rt_vm_probe((uint64_t)(uintptr_t)&local, &p), RT_VM_PROBE_OUTSIDE);
    T_EQ(p.state, RT_VM_PROBE_OUTSIDE);
    T_TRUE(p.base == 0 && p.size == 0 && p.protect == 0 && p.type == 0);
    T_EQ(rt_vm_probe(0, NULL), RT_VM_PROBE_OUTSIDE);                    /* out may be NULL */
    T_EQ(rt_vm_probe(RT_VM_ARENA_BASE - 1, NULL), RT_VM_PROBE_OUTSIDE);
    T_EQ(rt_vm_probe(RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE, NULL), RT_VM_PROBE_OUTSIDE);
    T_EQ(rt_vm_probe(UINT64_C(0xFFFFFFFFFFFFFFFF), NULL), RT_VM_PROBE_OUTSIDE);

    /* A region of 32 guest pages: 2 committed read-write, 1 committed read-only, the rest reserved. */
    uint64_t base = rt_vm_alloc(0, 0x20000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    T_REQUIRE(base != 0);
    T_EQ(rt_vm_alloc(base, 0x2000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE), base);
    T_EQ(rt_vm_alloc(base + 0x4000, 0x1000, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE), base + 0x4000);

    rt_thread_set_last_error(1234);
    T_EQ(rt_vm_probe(base, &p), RT_VM_PROBE_COMMITTED);
    T_TRUE(p.state == RT_VM_PROBE_COMMITTED && p.base == base && p.size == 0x20000);
    T_TRUE(p.protect == PAGE_READWRITE && p.type == RT_VM_PRIVATE);
    T_EQ(rt_vm_probe(base + 0x1FFF, &p), RT_VM_PROBE_COMMITTED);       /* any byte of the page */
    T_EQ(rt_vm_probe(base + 0x4ABC, &p), RT_VM_PROBE_COMMITTED);
    T_TRUE(p.protect == PAGE_READONLY && p.base == base);              /* the protection exactly as recorded */
    T_EQ(rt_vm_probe(base + 0x2000, &p), RT_VM_PROBE_RESERVED);        /* between the two commits */
    T_TRUE(p.base == base && p.size == 0x20000 && p.protect == 0 && p.type == RT_VM_PRIVATE);
    T_EQ(rt_vm_probe(base + 0x1F000, &p), RT_VM_PROBE_RESERVED);       /* above every commit */
    T_EQ(rt_thread_get_last_error(), 1234);                             /* never sets last error */

    /* The lock is held by the calling thread: BUSY, not an abort and not a wait.  (A body
     * that takes the lock the ordinary way dies here: os_unfair_lock aborts on recursion.) */
    rt_vm_test_hold_lock(true);
    memset(&p, 0xAA, sizeof p);
    T_EQ(rt_vm_probe(base, &p), RT_VM_PROBE_BUSY);
    T_TRUE(p.state == RT_VM_PROBE_BUSY && p.base == 0 && p.size == 0 && p.protect == 0 && p.type == 0);
    T_EQ(rt_vm_probe((uint64_t)(uintptr_t)&local, NULL), RT_VM_PROBE_OUTSIDE);  /* decided without the lock */
    /* ... and by another thread: BUSY there too, at once. */
    pthread_t th;
    void *got = NULL;
    T_REQUIRE(pthread_create(&th, NULL, probe_on_thread, (void *)(uintptr_t)base) == 0);
    pthread_join(th, &got);
    T_EQ((uintptr_t)got, RT_VM_PROBE_BUSY);
    rt_vm_test_hold_lock(false);
    T_EQ(rt_vm_probe(base, NULL), RT_VM_PROBE_COMMITTED);              /* the probe left the lock free */
    T_EQ(rt_vm_probe(base, NULL), RT_VM_PROBE_COMMITTED);

    /* Released: the same address is free space of the arena. */
    T_EQ(rt_vm_free(base, 0, MEM_RELEASE), TRUE);
    memset(&p, 0xAA, sizeof p);
    T_EQ(rt_vm_probe(base, &p), RT_VM_PROBE_FREE);
    T_TRUE(p.base == 0 && p.size == 0 && p.protect == 0 && p.type == 0);
}
