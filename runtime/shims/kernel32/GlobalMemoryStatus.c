/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-globalmemorystatus
 * RT-CASES:  fills every member from the machine description; dwMemoryLoad is the per cent of physical memory in use, from 0 to 100; sets dwLength to 56, which the caller need not set; writes only the structure, so the 8 bytes after it are not written; does not change last error
 * RT-TRAPS:  NULL lpBuffer
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

SHIM(kernel32, void, GlobalMemoryStatus, LPMEMORYSTATUS lpBuffer)
{
    /* A NULL pointer has no structure to write to: a stop, not a guess. */
    if (lpBuffer == NULL) RT_UNIMPLEMENTED(ctx, "GlobalMemoryStatus with NULL lpBuffer");

    /* The values are the machine's (machine.h, RUNTIME.md 8.5), the same numbers as
     * GlobalMemoryStatusEx: the older form has SIZE_T members, 8 bytes each in a 64-bit
     * process, so no value is cut.  The commit limit is physical memory (no paging file), and
     * the available virtual address space is the total less the memory in use, since the
     * runtime keeps no count of reserved address space.  The function sets dwLength itself:
     * the size of MEMORYSTATUS, 56 bytes. */
    const RtMachine *m = rt_machine();
    uint64_t used = m->phys_total - m->phys_avail;
    uint64_t total_virtual = m->max_address - m->min_address + 1;

    lpBuffer->dwLength = 56;
    lpBuffer->dwMemoryLoad = (DWORD)(used * 100 / m->phys_total);
    lpBuffer->dwTotalPhys = m->phys_total;
    lpBuffer->dwAvailPhys = m->phys_avail;
    lpBuffer->dwTotalPageFile = m->phys_total;
    lpBuffer->dwAvailPageFile = m->phys_avail;
    lpBuffer->dwTotalVirtual = total_virtual;
    lpBuffer->dwAvailVirtual = total_virtual - used;
}
