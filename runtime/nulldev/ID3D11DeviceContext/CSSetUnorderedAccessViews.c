/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cssetunorderedaccessviews
 * RT-CASES:  two slots with initial counts 0 and 0xffffffff; two slots with NULL counts; a count of 0 with a NULL array; slot 7 is the last slot
 * RT-TRAPS:  CSSetUnorderedAccessViews for slots; CSSetUnorderedAccessViews with a NULL array; an object of class
 */
#include "rt/nulldev.h"

COM_METHOD(ID3D11DeviceContext, void, CSSetUnorderedAccessViews, UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView const **ppUnorderedAccessViews, const UINT *pUAVInitialCounts)
{
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if ((uint64_t)StartSlot + NumUAVs > D3D11_PS_CS_UAV_REGISTER_COUNT)
        RT_UNIMPLEMENTED(ctx, "CSSetUnorderedAccessViews for slots %u to %u", StartSlot, StartSlot + NumUAVs - 1);
    if (NumUAVs > 0 && !ppUnorderedAccessViews)
        RT_UNIMPLEMENTED(ctx, "CSSetUnorderedAccessViews with a NULL array");
    for (UINT i = 0; i < NumUAVs; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppUnorderedAccessViews[i], &rt_nulldev_uav_class, "ppUnorderedAccessViews");
    if (rt_d3dback()) {
        void *back[D3D11_PS_CS_UAV_REGISTER_COUNT];
        for (UINT i = 0; i < NumUAVs; i++) back[i] = rt_d3dback_of((void *)ppUnorderedAccessViews[i]);
        RT_BACK(ctx, CSSetUnorderedAccessViews, c->child.back, StartSlot, NumUAVs, ppUnorderedAccessViews ? back : NULL, pUAVInitialCounts);
    }
}
