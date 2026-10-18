/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread
 * RT-CASES:  ends the calling thread, and its thread object becomes signalled with dwExitCode as its exit code; does not return: nothing after the call runs
 * RT-TRAPS:  core: ExitThread on guest thread 1 while other guest threads run
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, void, ExitThread, DWORD dwExitCode)
{
    /* Never returns.  The core does everything Windows does on exit: it unwinds the host stack
     * to the thread's base frame, runs the exit path, and ends the host thread.  On guest thread 1
     * with no other guest thread running it ends the process.  The core makes the other stops
     * fatal itself; a guard here would change which failure the guest sees, so none is added. */
    rt_thread_exit(ctx, dwExitCode);
}
