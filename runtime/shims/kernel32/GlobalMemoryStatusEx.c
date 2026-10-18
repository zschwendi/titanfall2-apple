/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatusex
 * RT-CASES:  fills every member from the machine description; dwMemoryLoad is the per cent of physical memory in use, from 0 to 100; writes only the structure, so dwLength stays 64 and the 8 bytes after it are not written; returns TRUE and the same answer on every call; does not change last error
 * RT-TRAPS:  dwLength other than 64; NULL lpBuffer
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

SHIM(kernel32, BOOL, GlobalMemoryStatusEx, LPMEMORYSTATUSEX lpBuffer)
{
    /* The caller must set dwLength to the size of the structure, 64 (the documentation says it
     * "must" be set and names no error code for another value), and a NULL pointer has no
     * structure to read: both are stops, not guesses. */
    if (lpBuffer == NULL) RT_UNIMPLEMENTED(ctx, "GlobalMemoryStatusEx with NULL lpBuffer");
    if (lpBuffer->dwLength != 64)
        RT_UNIMPLEMENTED(ctx, "GlobalMemoryStatusEx with dwLength other than 64: %u", (unsigned)lpBuffer->dwLength);

    /* The values are the machine's (machine.h, RUNTIME.md 8.5): host memory as total, half of it
     * available, 8 TB of address space.  The commit limit is physical memory (no paging file),
     * and the available virtual address space is the total less the memory in use, since the
     * runtime keeps no count of reserved address space.  The extended value is reserved: 0. */
    const RtMachine *m = rt_machine();
    uint64_t used = m->phys_total - m->phys_avail;
    uint64_t total_virtual = m->max_address - m->min_address + 1;

    lpBuffer->dwMemoryLoad = (DWORD)(used * 100 / m->phys_total);
    lpBuffer->ullTotalPhys = m->phys_total;
    lpBuffer->ullAvailPhys = m->phys_avail;
    lpBuffer->ullTotalPageFile = m->phys_total;
    lpBuffer->ullAvailPageFile = m->phys_avail;
    lpBuffer->ullTotalVirtual = total_virtual;
    lpBuffer->ullAvailVirtual = total_virtual - used;
    lpBuffer->ullAvailExtendedVirtual = 0;
    return TRUE;
}
