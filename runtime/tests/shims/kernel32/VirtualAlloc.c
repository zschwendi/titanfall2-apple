#include "rt/test.h"
#include "rt/vm.h"

/* A stretch of the VM arena that the runtime never uses at start-up: the explicit-address
 * cases below need addresses that are free, and the low part of the arena is not. */
#define FAR_FREE  (RT_VM_ARENA_BASE + (UINT64_C(200) << 30))

TEST(kernel32, VirtualAlloc)
{
    uint8_t *p, *q;
    uint64_t img;
    int local = 0;

    T_CASE("reserves a region at no address, 64 KB aligned");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_TRUE(((uintptr_t)p & 0xFFFF) == 0);
    T_TRUE(rt_vm_in_vm_arena((uint64_t)(uintptr_t)p));
    T_TRAPS(CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE), "MEM_WRITE_WATCH");
    T_TRAPS(CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE), "MEM_PHYSICAL");

    T_CASE("reserves at a free address, rounded down to 64 KB");
    q = CALL(kernel32, VirtualAlloc, (LPVOID)(uintptr_t)(FAR_FREE + 0x11000), 0x2000, MEM_RESERVE, PAGE_READWRITE);
    T_EQ((uint64_t)(uintptr_t)q, FAR_FREE + 0x10000);

    T_CASE("reserve and commit in one step gives zeroed, writable memory");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_TRUE(((uintptr_t)p & 0xFFFF) == 0);
    T_EQ(p[0x1000], 0);
    T_EQ(p[0x2FFF], 0);
    p[0x2FFF] = 0xA5;
    T_EQ(p[0x2FFF], 0xA5);

    T_CASE("commits a range inside a reserved region, rounded down to the page");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x4000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    q = CALL(kernel32, VirtualAlloc, p + 0x1234, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    T_EQ((uintptr_t)q, (uintptr_t)(p + 0x1000));
    T_EQ(q[0x1FFF], 0);
    q[0] = 1;
    T_EQ(q[0], 1);
    img = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, RT_VM_IMAGE);
    T_TRAPS(CALL(kernel32, VirtualAlloc, (LPVOID)(uintptr_t)img, 0x1000, MEM_COMMIT, PAGE_READWRITE), "MEM_COMMIT on an image or a file view");
    T_TRAPS(CALL(kernel32, VirtualAlloc, (LPVOID)&local, 0x1000, MEM_COMMIT, PAGE_READWRITE), "memory outside both arenas");

    T_CASE("committing a committed page does not fail");
    T_TRUE(CALL(kernel32, VirtualAlloc, q, 0x1000, MEM_COMMIT, PAGE_READWRITE) != NULL);

    T_CASE("commit outside any reservation fails with ERROR_INVALID_ADDRESS");
    T_TRUE(CALL(kernel32, VirtualAlloc, (LPVOID)(uintptr_t)(FAR_FREE + 0x1000), 0x1000, MEM_COMMIT, PAGE_READWRITE) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);

    T_CASE("MEM_RESET succeeds on committed memory");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_TRUE(CALL(kernel32, VirtualAlloc, p, 0x1000, MEM_RESET, PAGE_NOACCESS) == p);
    T_TRAPS(CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESET, PAGE_NOACCESS), "MEM_RESET with no address");
    T_TRAPS(CALL(kernel32, VirtualAlloc, (LPVOID)(uintptr_t)img, 0x1000, MEM_RESET, PAGE_NOACCESS), "MEM_RESET on an image or a file view");

    T_CASE("MEM_LARGE_PAGES fails with ERROR_PRIVILEGE_NOT_HELD");
    T_TRUE(CALL(kernel32, VirtualAlloc, NULL, 0x200000, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_PRIVILEGE_NOT_HELD);

    T_CASE("a protection that is not valid fails with ERROR_INVALID_PARAMETER");
    T_TRUE(CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a size of zero fails with ERROR_INVALID_PARAMETER");
    T_TRUE(CALL(kernel32, VirtualAlloc, NULL, 0, MEM_RESERVE, PAGE_READWRITE) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("MEM_RESET with another flag fails with ERROR_INVALID_PARAMETER");
    T_TRUE(CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESET | MEM_COMMIT, PAGE_READWRITE) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
