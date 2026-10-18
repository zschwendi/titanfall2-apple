#include <string.h>
#include "rt/test.h"
#include "rt/heap.h"

TEST(kernel32, LocalAlloc)
{
    HANDLE h = rt_heap_process();
    uint8_t *p;
    unsigned nonzero;

    T_CASE("LMEM_FIXED returns a writable block of the requested size");
    p = CALL(kernel32, LocalAlloc, LMEM_FIXED, 16);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 16);
    memset(p, 0x5A, 16);                                    /* all 16 bytes are usable */
    T_TRUE(p[0] == 0x5A && p[15] == 0x5A);
    T_TRUE(CALL(kernel32, LocalFree, p) == NULL);
    T_EQ(rt_heap_validate(h, 0, p), FALSE);                 /* freed: no longer a live block */

    T_CASE("LPTR zeroes the requested bytes");
    p = rt_heap_alloc(h, 0, 64);                            /* dirty a block the zone may hand out again */
    T_TRUE(p != NULL);
    memset(p, 0xAB, 64);
    T_EQ(rt_heap_free(h, 0, p), TRUE);
    p = CALL(kernel32, LocalAlloc, LPTR, 64);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 64);
    nonzero = 0;
    for (unsigned i = 0; i < 64; i++) if (p[i]) nonzero++;
    T_EQ(nonzero, 0);
    T_TRUE(CALL(kernel32, LocalFree, p) == NULL);

    T_CASE("a size of 0 gives a pointer");
    p = CALL(kernel32, LocalAlloc, LMEM_FIXED, 0);
    T_TRUE(p != NULL);
    T_EQ(rt_heap_size(h, 0, p), 0);
    T_TRUE(CALL(kernel32, LocalFree, p) == NULL);

    /* A failed LocalAlloc sets ERROR_NOT_ENOUGH_MEMORY (the page names no code; the core's
     * rt_heap_alloc gives this code for a request it cannot meet, 4 TiB or more). */
    T_CASE("a request that cannot be met returns NULL and sets ERROR_NOT_ENOUGH_MEMORY");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    p = CALL(kernel32, LocalAlloc, LMEM_FIXED, (SIZE_T)1 << 42);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_NOT_ENOUGH_MEMORY);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, LocalAlloc, LPTR, 16);
    T_TRUE(p != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(kernel32, LocalFree, p) == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* A flag outside LMEM_ZEROINIT is not implemented: LMEM_MOVEABLE gives a handle, not a pointer. */
    T_TRAPS(CALL(kernel32, LocalAlloc, LMEM_MOVEABLE, 16), "flags outside LMEM_ZEROINIT");
}
