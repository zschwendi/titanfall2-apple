/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-virtualquery
 * RT-CASES:  describes a committed private region: base, allocation base, state, protection, type and size; describes reserved pages as MEM_RESERVE with the size of the region; describes free space as MEM_FREE; rounds the address down to the page; reports an image as MEM_IMAGE and a file view as MEM_MAPPED; returns 48, the size of the structure, and writes nothing past it; an address above the highest accessible address fails with ERROR_INVALID_PARAMETER; a buffer shorter than the structure fails with ERROR_BAD_LENGTH and is not written; does not change last error on success
 * RT-TRAPS:  a NULL buffer
 */
#include "rt/shim.h"
#include "rt/vm.h"

SHIM(kernel32, SIZE_T, VirtualQuery, LPCVOID lpAddress, PMEMORY_BASIC_INFORMATION lpBuffer, SIZE_T dwLength)
{
    /* A buffer shorter than the structure: 0 and ERROR_BAD_LENGTH, which is what Windows maps
     * the system call's STATUS_INFO_LENGTH_MISMATCH to (the documentation does not name the
     * code; not verified).  It is checked before the buffer pointer, as the length is all the
     * system call can check without writing.  The documentation names no failure for a buffer
     * it cannot write to, so that one is refused by name rather than given a guessed code. */
    if (dwLength < sizeof(MEMORY_BASIC_INFORMATION)) {
        rt_set_last_error(ctx, ERROR_BAD_LENGTH);
        return 0;
    }
    if (lpBuffer == NULL)
        RT_UNIMPLEMENTED(ctx, "VirtualQuery with a NULL buffer: the failure is not documented");

    return rt_vm_query((uint64_t)(uintptr_t)lpAddress, lpBuffer);
}
