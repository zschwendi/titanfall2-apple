/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversion
 * RT-CASES:  returns the Windows 7 SP1 value 0x1DB10106; major in the low byte, minor in the next, build in the high word with the top bit clear; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/machine.h"

SHIM(kernel32, DWORD, GetVersion)
{
    /* The documented packing: the low word holds the major version in its low byte and the
     * minor version in its high byte; the high word holds the build number, and its top bit
     * is clear (the Windows NT family).  The three numbers are the machine's (machine.h). */
    const RtMachine *m = rt_machine();
    return (m->os_major & 0xFFu) | ((m->os_minor & 0xFFu) << 8) | ((m->os_build & 0x7FFFu) << 16);
}
