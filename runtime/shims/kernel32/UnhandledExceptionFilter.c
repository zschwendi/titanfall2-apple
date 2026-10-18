/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-unhandledexceptionfilter
 * RT-CASES:  a NULL EXCEPTION_POINTERS gives the fatal report with no record; an EXCEPTION_POINTERS gives the record and context in the fatal report, and never returns
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, LONG, UnhandledExceptionFilter, EXCEPTION_POINTERS *ExceptionInfo)
{
    /* Plan A (RUNTIME.md 9.3): the core prints the fatal report and aborts; it never returns,
     * so there is no result and no last error to set.  The core reads ExceptionInfo only
     * after it has checked that the guest memory is readable. */
    rt_uef_unhandled(ctx, (uint64_t)(uintptr_t)ExceptionInfo);
}
