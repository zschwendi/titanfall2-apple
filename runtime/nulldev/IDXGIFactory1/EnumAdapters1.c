/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgifactory1-enumadapters1
 * RT-CASES:  index 0 is the one adapter, a new object with one reference; index 1 and above is DXGI_ERROR_NOT_FOUND; a NULL out-pointer is DXGI_ERROR_INVALID_CALL
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* RT-DOC, read 2026-10-10: "Returns S_OK if successful; otherwise, returns
 * DXGI_ERROR_NOT_FOUND if the index is greater than or equal to the number of adapters in the
 * local system, or DXGI_ERROR_INVALID_CALL if ppAdapter parameter is NULL"; on success
 * "EnumAdapters1 increments the adapter interface's reference count"; index 0 is "the adapter
 * with the output on which the desktop primary is displayed".
 *
 * How many adapters: 1, what the native renderer reported on this Mac (finding 35,
 * docs/findings/35-dxmt-build.md section 3: "EnumAdapters1 count = 1").
 *
 * Two things the page does not say, decided by the contract (docs/handoff/d3d-contract.md):
 * each call makes a new adapter object (rt/nulldev.h), and on DXGI_ERROR_NOT_FOUND the
 * out-pointer is set to NULL (what Wine's dxgi does; from memory). */
COM_METHOD(IDXGIFactory1, HRESULT, EnumAdapters1, UINT Adapter, IDXGIAdapter1 **ppAdapter)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_factory_class);
    if (!ppAdapter) return DXGI_ERROR_INVALID_CALL;
    if (Adapter >= ND_ADAPTER_COUNT) {
        *ppAdapter = NULL;
        return DXGI_ERROR_NOT_FOUND;
    }
    *ppAdapter = rt_nulldev_adapter_new(This);
    return S_OK;
}
