/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgiobject-getparent
 * RT-CASES:  the adapter's factory as IDXGIFactory1, with one more reference
 * RT-TRAPS:  GetParent for; GetParent with riid
 */
#include "rt/nulldev.h"

/* The parent of an adapter is the factory that enumerated it (finding 31, section 2: the
 * game's way to the factory, at materialsystem_dx11+0x10fa1; Microsoft's page of
 * IDXGIFactory::CreateSwapChain describes the same walk, device to adapter to factory, for
 * finding the factory that made a device; from memory).  Stops as in IDXGIDevice1/GetParent.c. */
COM_METHOD(IDXGIAdapter1, HRESULT, GetParent, REFIID riid, void **parent)
{
    NdAdapter *s = rt_com_state(ctx, This, &rt_nulldev_adapter_class);
    return rt_nulldev_get_parent(ctx, s->factory, riid, parent);
}
