/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-getfeaturelevel
 * RT-CASES:  the device's feature level, D3D_FEATURE_LEVEL_11_0
 * RT-TRAPS:  none
 */
#include "rt/nulldev.h"

/* The level is the one D3D11CreateDevice reported on this Mac for a null feature-level list
 * (finding 35, docs/findings/35-dxmt-build.md section 3); it is stored in NdDevice at creation
 * as ND_FEATURE_LEVEL.  Microsoft's page was not read (no fetch available); the method's
 * meaning, "the feature level of the hardware device", is from memory. */
COM_METHOD(ID3D11Device, D3D_FEATURE_LEVEL, GetFeatureLevel)
{
    NdDevice *s = rt_com_state(ctx, This, &rt_nulldev_device_class);
    return s->level;
}
