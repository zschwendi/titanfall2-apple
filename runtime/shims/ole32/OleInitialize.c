/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/ole2/nf-ole2-oleinitialize
 * RT-CASES:  a non-NULL pvReserved fails with E_INVALIDARG; the other model returns RPC_E_CHANGED_MODE and changes nothing; first call returns S_OK; second call returns S_FALSE; a preset last error is left alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"
#include "rt/win/error.h"

SHIM(ole32, HRESULT, OleInitialize, LPVOID pvReserved)
{
    /* pvReserved must be NULL (the page); E_INVALIDARG for another value is from memory, as for
     * CoInitializeEx.  An HRESULT function: last error is not touched.
     *
     * The core does the rest (rt_ole_initialize, rt/thread.h): the apartment-threaded
     * initialisation, then the OLE count, with S_OK for the first OleInitialize of a thread and
     * S_FALSE after it.  OLE_E_WRONGCOMPOBJ (the page: COMPOBJ.DLL and OLE2.DLL disagree) is not
     * returned: this runtime has one COM library and no two versions of it. */
    if (pvReserved != NULL) return E_INVALIDARG;

    return rt_ole_initialize(ctx);
}
