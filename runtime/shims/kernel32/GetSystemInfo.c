/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsysteminfo
 * RT-CASES:  fills every field from the machine description; the processor count and mask agree with CPUID leaf 1; the level and revision agree with CPUID leaf 1; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/machine.h"

SHIM(kernel32, void, GetSystemInfo, LPSYSTEM_INFO lpSystemInfo)
{
    /* Microsoft gives no return value and no failure, so the structure is always filled and
     * last error is left alone.  The core writes every field from the machine description
     * (RUNTIME.md 8.5).  A NULL buffer is a guest bug: the core stops the run with a report. */
    rt_machine_system_info(lpSystemInfo);
}
