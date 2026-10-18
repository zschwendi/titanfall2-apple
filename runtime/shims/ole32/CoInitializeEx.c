/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex
 * RT-CASES:  first call with COINIT_MULTITHREADED returns S_OK; second call with the same model returns S_FALSE; the other model returns RPC_E_CHANGED_MODE and changes nothing; the flags COINIT_DISABLE_OLE1DDE and COINIT_SPEED_OVER_MEMORY are accepted; a non-NULL pvReserved fails with E_INVALIDARG; a preset last error is left alone
 * RT-TRAPS:  core: a bit of dwCoInit outside the four COINIT_ values
 */
#include "rt/shim.h"
#include "rt/thread.h"
#include "rt/win/error.h"

SHIM(ole32, HRESULT, CoInitializeEx, LPVOID pvReserved, DWORD dwCoInit)
{
    /* pvReserved must be NULL (the page).  E_INVALIDARG for another value is from memory: the
     * page gives E_INVALIDARG among the standard returns of this function but not for this
     * parameter.  An HRESULT function: last error is not touched. */
    if (pvReserved != NULL) return E_INVALIDARG;

    /* The core does the rest (rt/thread.h): the thread's count and model, and the results S_OK,
     * S_FALSE and RPC_E_CHANGED_MODE.  The core stops on a bit of dwCoInit outside the four
     * COINIT_ values; the shim adds no guard for it. */
    return rt_com_initialize(ctx, dwCoInit);
}
