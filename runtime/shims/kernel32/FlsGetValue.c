/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fibersapi/nf-fibersapi-flsgetvalue
 * RT-CASES:  a new index reads NULL with last error ERROR_SUCCESS; a value set reads back with last error ERROR_SUCCESS, on two indexes independently; index 0 and index 128 fail with ERROR_INVALID_PARAMETER; an index in range that was never allocated returns the value stored for it
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, PVOID, FlsGetValue, DWORD dwFlsIndex)
{
    /* The core returns the calling thread's value, 0 if it never set one.  It sets last error
     * itself (rt/thread.h): ERROR_SUCCESS when it succeeds, as rt_tls_get does, and
     * ERROR_INVALID_PARAMETER with 0 when the index is 0 or RT_FLS_SLOTS or more.  The shim sets
     * nothing. */
    return (PVOID)(uintptr_t)rt_fls_get(ctx, dwFlsIndex);
}
