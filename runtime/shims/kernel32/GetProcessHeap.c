/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-getprocessheap
 * RT-CASES:  returns the process heap; the same handle on every call; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* The process heap is made by rt_init before any guest code runs, so the documented NULL
 * failure cannot happen: rt_heap_process stops the process if it is asked too early. */
SHIM(kernel32, HANDLE, GetProcessHeap)
{
    return rt_heap_process();
}
