/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localfree
 * RT-CASES:  NULL is ignored and returns NULL; a block of the process heap is freed and returns NULL; does not change last error on success
 * RT-TRAPS:  core: a pointer that is not a live block
 */
#include "rt/shim.h"
#include "rt/heap.h"

SHIM(kernel32, HLOCAL, LocalFree, HLOCAL hMem)
{
    if (!hMem) return NULL;
    rt_heap_free(rt_heap_process(), 0, hMem);
    return NULL;
}
