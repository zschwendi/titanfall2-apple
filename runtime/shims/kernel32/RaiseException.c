/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-raiseexception
 * RT-CASES:  names the calling thread with the thread-name exception; names another thread: logged and ignored; a malformed thread-name exception is logged and ignored; DBG_PRINTEXCEPTION_C prints its text on guest.debug; DBG_PRINTEXCEPTION_WIDE_C prints its ANSI text on guest.debug; the printed text is limited to the length given; a print code of another shape logs nothing; every other code ends the run with a fatal report; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, void, RaiseException, DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR *lpArguments)
{
    /* The core returns for the three codes a debugger would continue past, and ends the run
     * with a fatal report for every other code.  It never touches last error. */
    rt_raise_exception(ctx, dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);
}
