/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc
 * RT-CASES:  returns the lowest index that is not allocated; the index it returns has a zero slot; success does not change last error
 * RT-TRAPS:  core: all 1,088 indices allocated
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(kernel32, DWORD, TlsAlloc)
{
    /* The core's allocation.  When all 1,088 indices are in use the core stops with a fatal
     * report instead of returning TLS_OUT_OF_INDEXES: the documentation does not say which last
     * error that failure sets, so no guard is added here. */
    return rt_tls_alloc();
}
