/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc
 * RT-CASES:  grows a block and keeps its contents; shrinks a block and keeps its contents; HEAP_ZERO_MEMORY zeroes the growth and nothing else; HEAP_REALLOC_IN_PLACE_ONLY fails with NULL and leaves the block unchanged when it cannot stay put; a size too large for any block returns NULL and leaves the block unchanged; does not change last error; a NULL block returns NULL, sets last error to ERROR_SUCCESS and changes nothing; a size of 0 succeeds and returns a live block of size 0
 * RT-TRAPS:  flag bits that are not in rt/heap.h
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* Every flag bit rt/heap.h knows.  A bit HeapReAlloc does not take but rt/heap.h knows is
 * refused by the core (heap.c check_flags), so only unknown bits are refused here. */
#define HEAP_KNOWN_FLAGS  (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | \
                           HEAP_REALLOC_IN_PLACE_ONLY | HEAP_CREATE_ENABLE_EXECUTE)

SHIM(kernel32, LPVOID, HeapReAlloc, HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes)
{
    if (dwFlags & ~HEAP_KNOWN_FLAGS)
        RT_UNIMPLEMENTED(ctx, "HeapReAlloc with flag bits that are not in rt/heap.h: %#x",
                         dwFlags & ~HEAP_KNOWN_FLAGS);
    /* A NULL block is not a stop: Windows returns NULL with last error ERROR_SUCCESS and changes
     * nothing (Wine kernel32/tests/heap.c test_HeapCreate: NULL returned; the last error check is
     * todo_wine, Windows does it and Wine did not yet).  The core takes only a live block, so the
     * shim answers here and does not call it. */
    if (!lpMem) {
        rt_set_last_error(ctx, ERROR_SUCCESS);
        return NULL;
    }
    /* A size of 0 succeeds: the result is a live block of size 0, as HeapAlloc(0) gives, and
     * is not freed (rt/heap.h; decided by the planner 2026-10-09, docs/RUNTIME.md 5.4.  The
     * documentation does not say; Wine heap.c test_HeapCreate: HeapSize of the result is 0). */
    return rt_heap_realloc(hHeap, dwFlags, lpMem, dwBytes);
}
