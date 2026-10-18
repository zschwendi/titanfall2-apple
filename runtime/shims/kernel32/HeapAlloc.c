/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc
 * RT-CASES:  returns a 16-byte aligned block of the requested size; HEAP_ZERO_MEMORY zeroes the requested bytes; a size of 0 gives a valid block; HEAP_NO_SERIALIZE is accepted; a request that cannot be met returns NULL and sets ERROR_NOT_ENOUGH_MEMORY; HEAP_GENERATE_EXCEPTIONS makes that failure fatal; does not change last error on success
 * RT-TRAPS:  flag bits that are not in rt/heap.h
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* Every flag bit rt/heap.h knows.  A bit HeapAlloc does not take but rt/heap.h knows is
 * refused by the core (heap.c check_flags), so only unknown bits are refused here. */
#define HEAP_KNOWN_FLAGS  (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | \
                           HEAP_REALLOC_IN_PLACE_ONLY | HEAP_CREATE_ENABLE_EXECUTE)

SHIM(kernel32, LPVOID, HeapAlloc, HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes)
{
    if (dwFlags & ~HEAP_KNOWN_FLAGS)
        RT_UNIMPLEMENTED(ctx, "HeapAlloc with flag bits that are not in rt/heap.h: %#x",
                         dwFlags & ~HEAP_KNOWN_FLAGS);
    return rt_heap_alloc(hHeap, dwFlags, dwBytes);
}
