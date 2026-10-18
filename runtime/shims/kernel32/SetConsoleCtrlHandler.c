/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/console/setconsolectrlhandler
 * RT-CASES:  sets the Ctrl+C ignore flag with no routine; clears the Ctrl+C ignore flag with no routine; appends a routine to the handler list; removes the entry added last with a routine; the same routine added twice and removed twice; holds up to 64 handlers
 * RT-TRAPS:  core: removing a routine that is not in the list; core: more than 64 entries
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, BOOL, SetConsoleCtrlHandler, PHANDLER_ROUTINE HandlerRoutine, BOOL Add)
{
    return rt_console_ctrl_handler(HandlerRoutine, Add);
}
