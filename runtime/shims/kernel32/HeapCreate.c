/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapcreate
 * RT-CASES:  a growable heap when the maximum size is 0; a fixed-size heap when the initial size is smaller than the maximum; an initial size of 0 gives a working heap; HEAP_NO_SERIALIZE and HEAP_CREATE_ENABLE_EXECUTE are accepted; HEAP_GENERATE_EXCEPTIONS applies to every call on the heap; a fixed-size heap returns NULL for a block that does not fit its maximum; does not change last error on success
 * RT-TRAPS:  flag bits that are not in rt/heap.h; a size above the guest address space; not smaller than the maximum size
 */
#include "rt/shim.h"
#include "rt/heap.h"
#include "rt/machine.h"

/* Every flag bit rt/heap.h knows.  A bit HeapCreate does not take but rt/heap.h knows is
 * refused by the core (heap.c check_flags), so only unknown bits are refused here. */
#define HEAP_KNOWN_FLAGS  (HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY | \
                           HEAP_REALLOC_IN_PLACE_ONLY | HEAP_CREATE_ENABLE_EXECUTE)

SHIM(kernel32, HANDLE, HeapCreate, DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize)
{
    const RtMachine *mc = rt_machine();

    if (flOptions & ~HEAP_KNOWN_FLAGS)
        RT_UNIMPLEMENTED(ctx, "HeapCreate with flag bits that are not in rt/heap.h: %#x",
                         flOptions & ~HEAP_KNOWN_FLAGS);
    /* The documentation gives no error code for a size this large (the core answers
     * ERROR_NOT_ENOUGH_MEMORY from memory), so it is not guessed here. */
    if (dwInitialSize > mc->max_address || dwMaximumSize > mc->max_address)
        RT_UNIMPLEMENTED(ctx, "HeapCreate with a size above the guest address space: the documentation gives no error code for it");
    /* The documentation says the initial size must be smaller than the maximum size; what
     * Windows does when it is not is not known. */
    if (dwMaximumSize != 0 && dwInitialSize >= dwMaximumSize)
        RT_UNIMPLEMENTED(ctx, "HeapCreate with an initial size %llu not smaller than the maximum size %llu: the documentation says it must be smaller",
                         (unsigned long long)dwInitialSize, (unsigned long long)dwMaximumSize);
    return rt_heap_create(flOptions, dwInitialSize, dwMaximumSize);
}
