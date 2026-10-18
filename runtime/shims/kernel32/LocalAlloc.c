/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localalloc
 * RT-CASES:  LMEM_FIXED returns a writable block of the requested size; LPTR zeroes the requested bytes; a size of 0 gives a pointer; a request that cannot be met returns NULL and sets ERROR_NOT_ENOUGH_MEMORY; does not change last error on success
 * RT-TRAPS:  flags outside LMEM_ZEROINIT
 */
#include "rt/shim.h"
#include "rt/heap.h"

SHIM(kernel32, HLOCAL, LocalAlloc, UINT uFlags, SIZE_T uBytes)
{
    LPVOID p;

    if (uFlags & ~LMEM_ZEROINIT)
        RT_UNIMPLEMENTED(ctx, "LocalAlloc with flags outside LMEM_ZEROINIT: %#x", uFlags & ~LMEM_ZEROINIT);
    p = rt_heap_alloc(rt_heap_process(), uFlags & LMEM_ZEROINIT ? HEAP_ZERO_MEMORY : 0, uBytes);
    return p;
}
