/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cotaskmemfree
 * RT-CASES:  NULL has no effect; frees a block from rt_galloc; does not change last error
 * RT-TRAPS:  core: a pointer that is not a live block
 */
#include "rt/shim.h"
#include "rt/heap.h"

/* The memory the guest may hand to CoTaskMemFree comes from the process heap (rt_galloc,
 * docs/RUNTIME.md 5.4).  A NULL pointer has no effect, as the documentation says.  A pointer
 * that is not NULL and is not a live block (a second free, or a pointer rt_galloc did not
 * hand out) is fatal in the core, and the shim adds no guard for it. */
SHIM(ole32, void, CoTaskMemFree, LPVOID pv)
{
    if (pv) rt_heap_free(rt_heap_process(), 0, pv);
}
