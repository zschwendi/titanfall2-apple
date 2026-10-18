/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter
 * RT-CASES:  returns the filter stored before and stores the new one; stores 0; two calls in a row give the first value back; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/crash.h"

SHIM(kernel32, LPTOP_LEVEL_EXCEPTION_FILTER, SetUnhandledExceptionFilter, LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
{
    /* The core stores the filter and returns the one it replaces (0 if none).  The filter is a
     * guest code address and is never called (PLAN A); the core cannot fail and sets no last
     * error, so neither does this shim. */
    return rt_uef_set(lpTopLevelExceptionFilter);
}
