/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/objbase/nf-objbase-coinitialize
 * RT-CASES:  first call returns S_OK; second call returns S_FALSE; the model is apartment-threaded, so COINIT_MULTITHREADED then returns RPC_E_CHANGED_MODE and changes nothing; a non-NULL pvReserved fails with E_INVALIDARG; a preset last error is left alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"
#include "rt/win/com.h"
#include "rt/win/error.h"

SHIM(ole32, HRESULT, CoInitialize, LPVOID pvReserved)
{
    /* The same check of pvReserved as CoInitializeEx (the page: "This parameter is reserved and
     * must be NULL"; E_INVALIDARG for another value is from memory).  CoInitialize is
     * CoInitializeEx with COINIT_APARTMENTTHREADED (the page).  An HRESULT function: last error
     * is not touched.  The core has no stop for this call. */
    if (pvReserved != NULL) return E_INVALIDARG;

    return rt_com_initialize(ctx, COINIT_APARTMENTTHREADED);
}
