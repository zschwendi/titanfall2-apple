/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/debugapi/nf-debugapi-debugbreak
 * RT-CASES:  the call never returns and gives the fatal report FATAL guest called DebugBreak
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, void, DebugBreak)
{
    /* Plan A (RUNTIME.md 9.4): no debugger is ever attached.  The core prints the fatal
     * report and aborts; it never returns, so there is no result and no last error. */
    rt_debug_break(ctx);
}
