/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-unmapviewoffile
 * RT-CASES:  a view unmapped once gives TRUE and then FALSE with ERROR_INVALID_ADDRESS, as NULL does; the byte after a view fails with ERROR_INVALID_ADDRESS; a VirtualAlloc region fails with ERROR_INVALID_ADDRESS; an address inside a view unmaps the whole view; what was written through a shared view stays in the file after the unmap; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, UnmapViewOfFile, LPCVOID lpBaseAddress)
{
    /* The core finds the view that contains the address, drops its reference to the section, and
     * sets ERROR_INVALID_ADDRESS itself when no view contains it (rt_view_unmap, rt/file.h).  The
     * address goes on unchanged. */
    return rt_view_unmap((uint64_t)(uintptr_t)lpBaseAddress);
}
