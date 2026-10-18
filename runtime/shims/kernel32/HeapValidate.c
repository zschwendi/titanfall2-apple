/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/heapapi/nf-heapapi-heapvalidate
 * RT-CASES:  a live block is valid; a freed block is invalid; an address 8 bytes into a live block is invalid; the address of a local variable is invalid; a NULL block validates the heap; a NULL heap handle is invalid; HEAP_NO_SERIALIZE is accepted; does not change last error
 * RT-TRAPS:  core: a flag bit other than HEAP_NO_SERIALIZE
 */
#include "rt/shim.h"
#include "rt/heap.h"

SHIM(kernel32, BOOL, HeapValidate, HANDLE hHeap, DWORD dwFlags, LPCVOID lpMem)
{
    /* The core answers FALSE for a bad heap handle and for a block that is not live, as the
     * documentation says, and never sets last error (the documentation: "There is no extended
     * error information for this function").  A flag bit it does not take is fatal in the core. */
    return rt_heap_validate(hHeap, dwFlags, lpMem);
}
