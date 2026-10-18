/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue
 * RT-CASES:  a fresh slot reads 0 and clears last error; a value written is read back, all 64 bits; a stored 0 reads back with last error cleared; two slots do not disturb each other; an index that is not allocated reads its slot without a check; an index of 1,088 or more fails with ERROR_INVALID_PARAMETER
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, LPVOID, TlsGetValue, DWORD dwTlsIndex)
{
    /* The core returns the calling thread's value.  It sets last error itself: ERROR_SUCCESS
     * on success, which the documentation requires so that a stored 0 can be told from a
     * failure, and ERROR_INVALID_PARAMETER on failure.  The shim sets nothing. */
    return (LPVOID)(uintptr_t)rt_tls_get(ctx, dwTlsIndex);
}
