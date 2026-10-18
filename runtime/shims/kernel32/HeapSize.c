/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsize
 * RT-CASES:  returns the requested size of a live block, exactly; a size of 0 gives 0; NULL and a pointer that is not a block of this heap return (SIZE_T)-1; does not change last error
 * RT-TRAPS:  flag bits that are not in rt/heap.h
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* Every flag bit rt/heap.h knows.  A bit HeapSize does not take but rt/heap.h knows is
 * refused by the core (heap.c check_flags), so only unknown bits are refused here. */
#define HEAP_KNOWN_FLAGS  (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | \
                           HEAP_REALLOC_IN_PLACE_ONLY | HEAP_CREATE_ENABLE_EXECUTE)

/* The failure value (SIZE_T)-1 and no last error are the core's (heap.c rt_heap_size). */
SHIM(kernel32, SIZE_T, HeapSize, HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    if (dwFlags & ~HEAP_KNOWN_FLAGS)
        RT_UNIMPLEMENTED(ctx, "HeapSize with flag bits that are not in rt/heap.h: %#x",
                         dwFlags & ~HEAP_KNOWN_FLAGS);
    return rt_heap_size(hHeap, dwFlags, lpMem);
}
