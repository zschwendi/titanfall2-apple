#include "rt/test.h"
#include "rt/vm.h"

/* A stretch of the VM arena that the runtime never uses at start-up (see VirtualAlloc.c). */
#define FAR_FREE    (RT_VM_ARENA_BASE + (UINT64_C(200) << 30))
/* Above the highest address the guest can use (RUNTIME.md section 5.1: up to 0x7FFFFFEFFFF). */
#define ABOVE_USER  UINT64_C(0x7FFFFFF0000)

TEST(kernel32, VirtualQuery)
{
    uint8_t *p;
    MEMORY_BASIC_INFORMATION mbi;
    MEMORY_BASIC_INFORMATION big[2];
    uint64_t img, map;

    T_CASE("describes a committed private region: base, allocation base, state, protection, type and size");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_TRUE(p != NULL);
    memset(&mbi, 0xAA, sizeof mbi);
    T_EQ(CALL(kernel32, VirtualQuery, p, &mbi, sizeof mbi), sizeof mbi);
    T_EQ((uintptr_t)mbi.BaseAddress, (uintptr_t)p);
    T_EQ((uintptr_t)mbi.AllocationBase, (uintptr_t)p);
    T_EQ(mbi.AllocationProtect, PAGE_READWRITE);
    T_EQ(mbi.State, MEM_COMMIT);
    T_EQ(mbi.Protect, PAGE_READWRITE);
    T_EQ(mbi.Type, MEM_PRIVATE);
    T_EQ(mbi.RegionSize, 0x2000);

    T_CASE("describes reserved pages as MEM_RESERVE with the size of the region");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x3000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualQuery, p + 0x1000, &mbi, sizeof mbi), sizeof mbi);
    T_EQ((uintptr_t)mbi.BaseAddress, (uintptr_t)(p + 0x1000));
    T_EQ((uintptr_t)mbi.AllocationBase, (uintptr_t)p);
    T_EQ(mbi.AllocationProtect, PAGE_READWRITE);
    T_EQ(mbi.State, MEM_RESERVE);
    T_EQ(mbi.RegionSize, 0x2000);

    T_CASE("describes free space as MEM_FREE");
    T_EQ(CALL(kernel32, VirtualQuery, (LPCVOID)(uintptr_t)(FAR_FREE + 0x1234), &mbi, sizeof mbi), sizeof mbi);
    T_EQ(mbi.State, MEM_FREE);
    T_EQ((uintptr_t)mbi.BaseAddress, (uintptr_t)(FAR_FREE + 0x1000));
    T_EQ(mbi.RegionSize, RT_VM_ARENA_SIZE - (UINT64_C(200) << 30) - 0x1000);

    T_CASE("rounds the address down to the page");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualQuery, p + 0x1234, &mbi, sizeof mbi), sizeof mbi);
    T_EQ((uintptr_t)mbi.BaseAddress, (uintptr_t)(p + 0x1000));
    T_EQ(mbi.RegionSize, 0x1000);

    T_CASE("reports an image as MEM_IMAGE and a file view as MEM_MAPPED");
    img = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, RT_VM_IMAGE);
    T_TRUE(img != 0);
    T_EQ(CALL(kernel32, VirtualQuery, (LPCVOID)(uintptr_t)(img + 0x2000), &mbi, sizeof mbi), sizeof mbi);
    T_EQ(mbi.Type, MEM_IMAGE);
    T_EQ((uintptr_t)mbi.AllocationBase, (uintptr_t)img);
    T_EQ(mbi.Protect, PAGE_READONLY);
    map = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, RT_VM_MAPPED);
    T_TRUE(map != 0);
    T_EQ(CALL(kernel32, VirtualQuery, (LPCVOID)(uintptr_t)(map + 0x2000), &mbi, sizeof mbi), sizeof mbi);
    T_EQ(mbi.Type, MEM_MAPPED);
    T_EQ((uintptr_t)mbi.AllocationBase, (uintptr_t)map);

    T_CASE("returns 48, the size of the structure, and writes nothing past it");
    memset(big, 0xCC, sizeof big);
    T_EQ(CALL(kernel32, VirtualQuery, p, &big[0], sizeof big), sizeof(MEMORY_BASIC_INFORMATION));
    T_EQ(sizeof(MEMORY_BASIC_INFORMATION), 48);
    T_EQ(((uint8_t *)big)[48], 0xCC);
    T_EQ(((uint8_t *)big)[95], 0xCC);
    T_TRAPS(CALL(kernel32, VirtualQuery, p, NULL, sizeof big), "a NULL buffer");

    T_CASE("a buffer shorter than the structure fails with ERROR_BAD_LENGTH and is not written");
    memset(big, 0xCC, sizeof big);
    rt_set_last_error(T_CTX(), 0x1234);
    T_EQ(CALL(kernel32, VirtualQuery, p, &big[0], sizeof(MEMORY_BASIC_INFORMATION) - 1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_BAD_LENGTH);
    T_EQ(((uint8_t *)big)[0], 0xCC);
    T_EQ(((uint8_t *)big)[47], 0xCC);
    rt_set_last_error(T_CTX(), 0x1234);
    T_EQ(CALL(kernel32, VirtualQuery, p, NULL, 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_BAD_LENGTH);

    T_CASE("an address above the highest accessible address fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, VirtualQuery, (LPCVOID)(uintptr_t)ABOVE_USER, &mbi, sizeof mbi), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, VirtualQuery, p, &mbi, sizeof mbi), sizeof mbi);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
