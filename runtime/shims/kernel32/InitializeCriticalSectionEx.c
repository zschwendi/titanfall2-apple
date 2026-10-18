/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionex
 * RT-CASES:  initialises a free section with the spin count, whatever the memory held before; flags 0 and CRITICAL_SECTION_NO_DEBUG_INFO both initialise the section; the value 1 that the engine passes initialises the section as 0 does; keeps the low 24 bits of the spin count and drops the high byte; returns TRUE; does not change last error
 * RT-TRAPS:  InitializeCriticalSectionEx with Flags
 */
#include "rt/shim.h"
#include "rt/lock.h"

SHIM(kernel32, BOOL, InitializeCriticalSectionEx, LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount, DWORD Flags)
{
    /* The documentation lists two values for Flags, 0 and CRITICAL_SECTION_NO_DEBUG_INFO, and
     * does not say what any other value does, so any other bit stops the run (RUNTIME.md 9.2).
     * The core stores -1 in DebugInfo whatever the flag: the runtime allocates no debug
     * information in either case, and the core reads DebugInfo only as its initialised marker
     * (rt/lock.h).  The spin count is advisory, as for InitializeCriticalSectionAndSpinCount.
     *
     * FLAGS 1, EXACTLY, IS ANSWERED AS 0 IS.  It is not a documented value, and it is what the
     * game passes: the engine calls InitializeCriticalSectionEx(section, 0, 1) from
     * engine+0x2564ce on a thread it starts in the first frames of its main loop, and goes
     * on to use the section.  The evidence that the call succeeds is the recording of the
     * real game's start (tools/trace/cut.py mission1 calls --fn InitializeCriticalSectionEx
     * --t 19:24, docs/handoff/gate3-run-8.md, entry 1): twice at 21.11 s from that address
     * with arguments (section, 0, 1), result 1 each time, under Wine, where the recording was
     * made.  That Windows itself ignores the low bits of Flags is from memory and not
     * checked.  Every other value that is not documented still stops. */
    if (Flags != 1 && (Flags & ~CRITICAL_SECTION_NO_DEBUG_INFO))
        RT_UNIMPLEMENTED(ctx, "InitializeCriticalSectionEx with Flags 0x%x (only 0 and CRITICAL_SECTION_NO_DEBUG_INFO are documented)", Flags);
    rt_cs_init(lpCriticalSection, dwSpinCount);
    return TRUE;
}
