/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsfree
 * RT-CASES:  frees an allocated index; a freed index is given out again, with a zero slot; freeing an index that is not allocated fails; freeing an index of 1,088 or more fails; success does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, BOOL, TlsFree, DWORD dwTlsIndex)
{
    /* The core's free.  On failure it sets ERROR_INVALID_PARAMETER itself (index 1,088 or
     * more, or not allocated); on success it leaves last error alone, so the shim sets nothing. */
    return rt_tls_free(dwTlsIndex);
}
