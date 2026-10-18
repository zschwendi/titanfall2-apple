/* Helpers shared by the tests of the VM map (vm.c and vm_*.c in this directory).
 *
 * This is a .c file that the other test files #include.  It would be a header, but the unit
 * that owns these tests may only add files named runtime/tests/core/vm*.c.  The build also
 * compiles it on its own, where it defines nothing that is used: every definition here is
 * static and marked unused.
 */
#ifndef RT_TEST_VM_UTIL_C
#define RT_TEST_VM_UTIL_C

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/machine.h"
#include "rt/vm.h"

#define VU  __attribute__((unused)) static

#define VM_END     (RT_VM_ARENA_BASE + RT_VM_ARENA_SIZE)
#define IMAGE_END  (RT_IMAGE_ARENA_BASE + RT_IMAGE_ARENA_SIZE)

/* Addresses well away from everything the runtime allocates at start-up and from what
 * first fit hands out (both are at the bottom of the VM arena), so that a test decides
 * what its neighbours are.  Every TEST runs in its own process, so tests may reuse them. */
#define PLAY(n)    (RT_VM_ARENA_BASE + (UINT64_C(16) << 30) + (uint64_t)(n) * (UINT64_C(1) << 30))

#define PG  UINT64_C(0x1000)                    /* the guest page */

/* Every protection VirtualAlloc accepts for private memory. */
VU const DWORD vu_private_protects[] = {
    PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE, PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE,
    PAGE_READONLY | PAGE_GUARD, PAGE_READWRITE | PAGE_GUARD, PAGE_EXECUTE | PAGE_GUARD,
    PAGE_EXECUTE_READ | PAGE_GUARD, PAGE_EXECUTE_READWRITE | PAGE_GUARD,
    PAGE_READONLY | PAGE_NOCACHE, PAGE_READWRITE | PAGE_NOCACHE, PAGE_EXECUTE_READWRITE | PAGE_NOCACHE,
    PAGE_READONLY | PAGE_WRITECOMBINE, PAGE_READWRITE | PAGE_WRITECOMBINE, PAGE_EXECUTE_READ | PAGE_WRITECOMBINE,
};
#define VU_N_PRIVATE_PROTECTS  (sizeof vu_private_protects / sizeof vu_private_protects[0])

/* Values that are not a protection: no base value, two base values, a modifier on
 * PAGE_NOACCESS, two modifiers, bits Windows 7 does not define. */
VU const DWORD vu_bad_protects[] = {
    0, PAGE_GUARD, PAGE_NOCACHE, PAGE_WRITECOMBINE, PAGE_READWRITE | PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE,
    PAGE_READONLY | PAGE_EXECUTE, PAGE_READONLY | PAGE_READWRITE, PAGE_NOACCESS | PAGE_READONLY,
    PAGE_READWRITE | PAGE_EXECUTE_WRITECOPY, PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY, 0xFF, 0x3,
    PAGE_NOACCESS | PAGE_GUARD, PAGE_NOACCESS | PAGE_NOCACHE, PAGE_NOACCESS | PAGE_WRITECOMBINE,
    PAGE_READWRITE | PAGE_GUARD | PAGE_NOCACHE, PAGE_READWRITE | PAGE_GUARD | PAGE_WRITECOMBINE,
    PAGE_READWRITE | PAGE_NOCACHE | PAGE_WRITECOMBINE,
    0x800, PAGE_READWRITE | 0x800, PAGE_READWRITE | 0x10000, PAGE_READWRITE | 0x40000000,
    PAGE_READWRITE | 0x80000000, 0xFFFFFFFF,
};
#define VU_N_BAD_PROTECTS  (sizeof vu_bad_protects / sizeof vu_bad_protects[0])

/* What a committed page with protection `p` lets the host do, written out independently of
 * the map's own table: guard and no-access pages nothing; read-only and the execute
 * protections without write, read; everything writable, read and write. */
VU int vu_access(DWORD p) {
    if (p & PAGE_GUARD) return PROT_NONE;
    switch (p & 0xFF) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: case PAGE_EXECUTE: case PAGE_EXECUTE_READ: return PROT_READ;
    default: return PROT_READ | PROT_WRITE;
    }
}

/* True if all n bytes at p are v: the first is, and every byte equals the one before it. */
VU bool vu_all(uint64_t p, uint64_t n, uint8_t v) {
    const uint8_t *b = (const uint8_t *)(uintptr_t)p;
    return n == 0 || (b[0] == v && memcmp(b, b + 1, n - 1) == 0);
}
VU bool vu_all_zero(uint64_t p, uint64_t n) { return vu_all(p, n, 0); }
VU void vu_fill(uint64_t p, uint64_t n, uint8_t v) { memset((void *)(uintptr_t)p, v, n); }

/* The protection the kernel has for the host page that holds `addr` (PROT_READ and
 * PROT_WRITE bits), or -1 if nothing is mapped there. */
VU int vu_host_prot(uint64_t addr) {
    mach_vm_address_t a = addr;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(mach_task_self(), &a, &size, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &object);
    if (kr != KERN_SUCCESS || a > addr) return -1;
    return info.protection & (PROT_READ | PROT_WRITE | PROT_EXEC);
}

/* True if the host page that holds `addr` has memory behind it now. */
VU bool vu_resident(uint64_t addr) {
    uint64_t hp = rt_vm_host_page();
    char vec = 0;
    if (mincore((void *)(uintptr_t)(addr & ~(hp - 1)), hp, &vec) != 0) return false;
    return (vec & MINCORE_INCORE) != 0;
}

/* Touch one byte and say whether the host let it happen.  A write stores back the byte it
 * read, so the memory is unchanged either way. */
VU sigjmp_buf vu_jmp;
VU void vu_on_fault(int sig) { (void)sig; siglongjmp(vu_jmp, 1); }
VU bool vu_touch(uint64_t addr, bool write) {
    struct sigaction sa, old_bus, old_segv;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = vu_on_fault;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGBUS, &sa, &old_bus);
    sigaction(SIGSEGV, &sa, &old_segv);
    volatile bool ok = false;
    if (sigsetjmp(vu_jmp, 1) == 0) {
        volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;
        uint8_t v = *p;
        if (write) *p = v;
        ok = true;
    }
    sigaction(SIGBUS, &old_bus, NULL);
    sigaction(SIGSEGV, &old_segv, NULL);
    return ok;
}
VU bool vu_reads(uint64_t addr)  { return vu_touch(addr, false); }
VU bool vu_writes(uint64_t addr) { return vu_touch(addr, true); }

/* rt_vm_query that must succeed.  The buffer starts out as garbage, so a field the call
 * forgets shows; the two reserved fields must come back as zero. */
VU MEMORY_BASIC_INFORMATION vu_query(uint64_t addr) {
    MEMORY_BASIC_INFORMATION m;
    memset(&m, 0xA5, sizeof m);
    T_EQ(rt_vm_query(addr, &m), sizeof m);
    T_EQ(m.PartitionId, 0);
    T_EQ(m.Reserved_, 0);
    T_EQ(m.Reserved2_, 0);
    return m;
}

/* The whole answer of VirtualQuery for `addr`. */
#define Q_IS(addr, base_, alloc_base_, alloc_protect_, size_, state_, protect_, type_) \
    do { MEMORY_BASIC_INFORMATION q_ = vu_query(addr); \
         T_EQ((uint64_t)(uintptr_t)q_.BaseAddress, (uint64_t)(base_)); \
         T_EQ((uint64_t)(uintptr_t)q_.AllocationBase, (uint64_t)(alloc_base_)); \
         T_EQ(q_.AllocationProtect, (DWORD)(alloc_protect_)); \
         T_EQ(q_.RegionSize, (uint64_t)(size_)); \
         T_EQ(q_.State, (DWORD)(state_)); \
         T_EQ(q_.Protect, (DWORD)(protect_)); \
         T_EQ(q_.Type, (DWORD)(type_)); } while (0)

/* The three states of private memory.  `addr` must be page aligned (BaseAddress == addr). */
#define Q_FREE(addr, size)  Q_IS(addr, addr, 0, 0, size, MEM_FREE, PAGE_NOACCESS, 0)
#define Q_RESERVED(addr, alloc_base, alloc_protect, size) \
    Q_IS(addr, addr, alloc_base, alloc_protect, size, MEM_RESERVE, 0, MEM_PRIVATE)
#define Q_COMMITTED(addr, alloc_base, alloc_protect, size, protect) \
    Q_IS(addr, addr, alloc_base, alloc_protect, size, MEM_COMMIT, protect, MEM_PRIVATE)

/* A call that must fail with exactly this last error.  Last error is set to a marker first,
 * so a failure that forgets to set it shows. */
#define VU_MARK  0xDEADBEEFu
#define ALLOC_FAILS(err, ...) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_alloc(__VA_ARGS__), 0); T_EQ(T_LAST_ERROR(), (DWORD)(err)); } while (0)
#define FREE_FAILS(err, ...) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_free(__VA_ARGS__), FALSE); T_EQ(T_LAST_ERROR(), (DWORD)(err)); } while (0)
#define PROTECT_FAILS(err, ...) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_protect(__VA_ARGS__), FALSE); T_EQ(T_LAST_ERROR(), (DWORD)(err)); } while (0)

/* A call that must succeed, and must leave last error alone (docs/RUNTIME.md section 3.4). */
#define ALLOC_IS(expected, ...) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_alloc(__VA_ARGS__), (uint64_t)(expected)); T_EQ(T_LAST_ERROR(), VU_MARK); } while (0)
#define FREE_OK(...) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_free(__VA_ARGS__), TRUE); T_EQ(T_LAST_ERROR(), VU_MARK); } while (0)
#define RELEASE_OK(base, type) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_release((base), (type)), TRUE); T_EQ(T_LAST_ERROR(), VU_MARK); } while (0)
#define RELEASE_FAILS(err, base, type) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_release((base), (type)), FALSE); T_EQ(T_LAST_ERROR(), (DWORD)(err)); } while (0)
#define PROTECT_OK(...) \
    do { rt_set_last_error(T_CTX(), VU_MARK); T_EQ(rt_vm_protect(__VA_ARGS__), TRUE); T_EQ(T_LAST_ERROR(), VU_MARK); } while (0)

/* A deterministic generator for the tests that pick operations at random. */
VU uint64_t vu_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return *state = x;
}

#endif /* RT_TEST_VM_UTIL_C */
