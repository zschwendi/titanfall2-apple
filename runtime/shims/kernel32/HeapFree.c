/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapfree
 * RT-CASES:  frees a live block and returns TRUE; NULL is accepted and returns TRUE; HEAP_NO_SERIALIZE is accepted; does not change last error
 * RT-TRAPS:  flag bits that are not in rt/heap.h
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* Every flag bit rt/heap.h knows.  A bit HeapFree does not take but rt/heap.h knows is
 * refused by the core (heap.c check_flags), so only unknown bits are refused here. */
#define HEAP_KNOWN_FLAGS  (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | \
                           HEAP_REALLOC_IN_PLACE_ONLY | HEAP_CREATE_ENABLE_EXECUTE)

/* A pointer that is not a live block of this heap (a double free, a stack address, a block
 * of another heap) is fatal in the core: the documentation calls it heap corruption. */
SHIM(kernel32, BOOL, HeapFree, HANDLE hHeap, DWORD dwFlags, LPVOID lpMem)
{
    if (dwFlags & ~HEAP_KNOWN_FLAGS)
        RT_UNIMPLEMENTED(ctx, "HeapFree with flag bits that are not in rt/heap.h: %#x",
                         dwFlags & ~HEAP_KNOWN_FLAGS);
    return rt_heap_free(hHeap, dwFlags, lpMem);
}
