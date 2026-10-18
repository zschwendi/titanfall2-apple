/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess
 * RT-CASES:  exits with the given code, 0 and 7; a code of 0xC0000409 exits with status 255
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, void, ExitProcess, UINT uExitCode)
{
    /* Never returns and never fails: the core runs the exit sequence and ends the process. */
    rt_process_exit(ctx, uExitCode);
}
