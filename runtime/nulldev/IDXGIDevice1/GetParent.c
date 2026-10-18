/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiobject-getparent
 * RT-CASES:  the device's adapter as IDXGIAdapter1, with one more reference
 * RT-TRAPS:  GetParent for; GetParent with riid
 */
#include "rt/nulldev.h"

/* The parent of a DXGI device is its adapter (finding 31, section 2: this is how the game
 * reaches the adapter, at materialsystem_dx11+0x10f84; Microsoft, "IDXGIDevice interface":
 * the device's adapter is what GetAdapter and GetParent give; from memory).  The page of
 * GetParent (RT-DOC, read 2026-10-10) says only "Gets the parent of the object" and "Returns
 * one of the DXGI_ERROR values": it names no result for an interface the parent lacks, so
 * that is a stop (rt_nulldev_get_parent), and so is a NULL pointer. */
COM_METHOD(IDXGIDevice1, HRESULT, GetParent, REFIID riid, void **parent)
{
    NdDevice *s = rt_com_state(ctx, This, &rt_nulldev_device_class);
    return rt_nulldev_get_parent(ctx, s->adapter, riid, parent);
}
