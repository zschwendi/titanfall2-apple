/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice
 * RT-CASES:  the call the game makes
 * RT-TRAPS:  an adapter; a driver type other than hardware; a software module; creation flags;
 *            a feature-level list; an SDK version other than 7; an out-pointer that is NULL
 */
#include "rt/shim.h"
#include "rt/nulldev.h"

/* The one call the game makes (materialsystem_dx11+0x10f0d; docs/handoff/rt-win-queue.md has
 * the ten arguments as a run passed them): the default adapter, the hardware driver, no
 * flags, no feature-level list, SDK version 7, and all three out-pointers.  It gets a device
 * and an immediate context of the do-nothing device (rt/nulldev.h), each with one reference.
 *
 * The feature level written is 11_0.  Source: finding 35 (docs/findings/35-dxmt-build.md),
 * what the native renderer reported on this Mac for a null feature-level list.  It agrees
 * with Microsoft's page (RT-DOC, read 2026-10-10): with pFeatureLevels NULL the function
 * uses the array 11_0, 10_1, 10_0, 9_3, 9_2, 9_1, and pFeatureLevel receives "the first
 * D3D_FEATURE_LEVEL from the pFeatureLevels array which succeeded".  The game refuses
 * anything lower (finding 31, section 1).  S_OK is what that run returned as well.
 *
 * The page marks all three out-pointers optional (NULL: nothing is returned there, and with
 * both object pointers NULL no device is made).  The game passes all three; the rest is a stop.
 *
 * Every other combination of arguments is a stop: the answer for it has not been decided. */
SHIM(d3d11, HRESULT, D3D11CreateDevice, IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)
{
    if (pAdapter) RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with an adapter (%p)", (void *)pAdapter);
    if (DriverType != D3D_DRIVER_TYPE_HARDWARE) RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with a driver type other than hardware (%u)", DriverType);
    if (Software) RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with a software module (%p)", (void *)Software);
    if (Flags) RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with creation flags %#x", Flags);
    if (pFeatureLevels || FeatureLevels)
        RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with a feature-level list (%p, %u entries)", (const void *)pFeatureLevels, FeatureLevels);
    if (SDKVersion != D3D11_SDK_VERSION) RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with an SDK version other than 7 (%u)", SDKVersion);
    if (!ppDevice || !pFeatureLevel || !ppImmediateContext)
        RT_UNIMPLEMENTED(ctx, "D3D11CreateDevice with an out-pointer that is NULL (device %p, feature level %p, context %p)",
                         (void *)ppDevice, (void *)pFeatureLevel, (void *)ppImmediateContext);
    rt_nulldev_create(ppDevice, ppImmediateContext);
    *pFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return S_OK;
}
