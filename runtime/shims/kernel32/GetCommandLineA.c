/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-getcommandlinea
 * RT-CASES:  returns a pointer to the command line; the same pointer on every call; the line is not empty and is what rt_cmdline_a() gives; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/env.h"

/* The documentation has no failure return and sets no last error.  The core's command line is
 * one string for the life of the process, so the same pointer comes back on every call.  A
 * command line with a character >= 0x80 is fatal in the core, which is the core's documented
 * behaviour until the locale unit converts code pages. */
SHIM(kernel32, LPSTR, GetCommandLineA)
{
    return rt_cmdline_a();
}
