/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapdestroy
 * RT-CASES:  destroys a heap and returns TRUE; a heap's blocks go with it, without HeapFree; does not change last error on success; the process heap fails with ERROR_INVALID_HANDLE and nothing is destroyed
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* A handle that is not a live heap (already destroyed, or not a heap at all) is fatal in the
 * core: heap.c heap_ref names it.  The process heap is the one handle the documentation
 * says not to pass here: the core fails the call with ERROR_INVALID_HANDLE and destroys
 * nothing (rt/heap.h; decided by the planner 2026-10-09, docs/RUNTIME.md 5.4.  The code is
 * from memory of kernel32; not verified on Windows). */
SHIM(kernel32, BOOL, HeapDestroy, HANDLE hHeap)
{
    return rt_heap_destroy(hHeap);
}
