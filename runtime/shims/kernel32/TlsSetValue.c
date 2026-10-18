/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlssetvalue
 * RT-CASES:  a value written is read back, all 64 bits; two slots do not disturb each other; success leaves last error alone; an index that is not allocated stores its value; an index of 1,088 or more fails with ERROR_INVALID_PARAMETER
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, TlsSetValue, DWORD dwTlsIndex, LPVOID lpTlsValue)
{
    /* The core stores the value in the calling thread's slot.  On failure it sets last error
     * itself (ERROR_INVALID_PARAMETER); on success it leaves last error alone, as the
     * documentation requires.  The shim sets nothing. */
    return rt_tls_set(ctx, dwTlsIndex, (uint64_t)(uintptr_t)lpTlsValue);
}
