#include "rt/test.h"
#include "rt/vm.h"

/* A stretch of the VM arena that the runtime never uses at start-up (see VirtualAlloc.c). */
#define FAR_FREE  (RT_VM_ARENA_BASE + (UINT64_C(200) << 30))

TEST(kernel32, VirtualFree)
{
    uint8_t *p, *m;
    uint64_t img;
    int local = 0;

    T_CASE("releases a region reserved by VirtualAlloc, with any state of its pages");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, MEM_RELEASE), TRUE);
    m = CALL(kernel32, VirtualAlloc, NULL, 0x4000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(m != NULL);
    T_TRUE(CALL(kernel32, VirtualAlloc, m + 0x1000, 0x1000, MEM_COMMIT, PAGE_READWRITE) != NULL);
    T_EQ(CALL(kernel32, VirtualFree, m, 0, MEM_RELEASE), TRUE);
    img = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READONLY, RT_VM_IMAGE);
    T_TRAPS(CALL(kernel32, VirtualFree, (LPVOID)(uintptr_t)img, 0, MEM_RELEASE), "VirtualFree on an image or a file view");
    T_TRAPS(CALL(kernel32, VirtualFree, (LPVOID)&local, 0, MEM_RELEASE), "VirtualFree on memory outside both arenas");

    T_CASE("decommits committed pages, which read as zero when committed again");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_TRUE(p != NULL);
    p[0x10] = 0x77;
    T_EQ(p[0x10], 0x77);
    T_EQ(CALL(kernel32, VirtualFree, p, 0x1000, MEM_DECOMMIT), TRUE);
    T_TRUE(CALL(kernel32, VirtualAlloc, p, 0x1000, MEM_COMMIT, PAGE_READWRITE) == p);
    T_EQ(p[0x10], 0);
    T_TRAPS(CALL(kernel32, VirtualFree, (LPVOID)(uintptr_t)img, 0x1000, MEM_DECOMMIT), "VirtualFree on an image or a file view");

    T_CASE("decommitting a page that is not committed does not fail");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x2000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualFree, p, 0x1000, MEM_DECOMMIT), TRUE);

    T_CASE("MEM_RELEASE with a size fails with ERROR_INVALID_PARAMETER");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualFree, p, 0x1000, MEM_RELEASE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, MEM_RELEASE), TRUE);

    T_CASE("MEM_RELEASE at an address that is not a region base fails with ERROR_INVALID_ADDRESS");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x2000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualFree, p + 0x1000, 0, MEM_RELEASE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, MEM_RELEASE), TRUE);

    T_CASE("an address in no reservation fails with ERROR_INVALID_ADDRESS");
    T_EQ(CALL(kernel32, VirtualFree, (LPVOID)(uintptr_t)(FAR_FREE + 0x80000), 0, MEM_RELEASE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);

    T_CASE("a free type that is neither MEM_DECOMMIT nor MEM_RELEASE fails with ERROR_INVALID_PARAMETER");
    p = CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, MEM_DECOMMIT | MEM_RELEASE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, MEM_RELEASE), TRUE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    T_TRUE(p != NULL);
    T_EQ(CALL(kernel32, VirtualFree, p, 0, MEM_RELEASE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
