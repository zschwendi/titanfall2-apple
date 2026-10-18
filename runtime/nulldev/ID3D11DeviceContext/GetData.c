/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata
 * RT-CASES:  an event query after End: S_OK and TRUE; an occlusion query after Begin and End: S_OK and 0 samples; a timestamp query: S_OK and the ticks of its End, never decreasing; a timestamp-disjoint query: 10000000 ticks a second, not disjoint; a status check with size 0 is S_OK and writes nothing; a wrong size is E_INVALIDARG and writes nothing; before the first End, and between Begin and End: DXGI_ERROR_INVALID_CALL and nothing written
 * RT-TRAPS:  GetData of a NULL query; GetData with flags
 */
#include <string.h>

#include "rt/nulldev.h"

/* NEVER "not ready" (S_FALSE): the game re-queues itself while it is told so
 * (materialsystem_dx11+0x158e0; design-null-device.md 3.2), and on a device that queues
 * nothing every command has finished when End returns.  THE ANSWER PER KIND, each the true
 * answer of a device that draws nothing:
 *   event (BOOL)              TRUE: "the GPU is finished processing commands" (D3D11_QUERY)
 *   occlusion (UINT64)        0: the number of samples that passed the depth and stencil tests
 *                             between Begin and End, and nothing was drawn, so none did.  A
 *                             count above 0 would be invented.  What the game does with it:
 *                             finding 31 gives the two creating sites (materialsystem_dx11
 *                             +0x1c0f8, +0x2c065) and not the use; with 0 whatever it tests
 *                             this way is "not visible", so it draws less, on a device where
 *                             drawing does nothing.  The first run that reaches an occlusion
 *                             GetData shows in the count table.
 *   timestamp (UINT64)        the runtime's monotonic clock at the query's last End, in units
 *                             of 100 ns: a real time, never decreasing
 *   timestamp disjoint        Frequency 10,000,000 (those units per second; not 0), Disjoint
 *     (UINT64, BOOL; 16 bytes)  FALSE: the clock never jumps.  The 4 bytes after Disjoint are 0.
 * WHAT WINDOWS DOES AROUND IT (Wine's d3d11 tests test_occlusion_query, test_timestamp_query,
 * test_create_query, as docs/reviews/d3d-contract-check.md, point 3, reads them): DataSize
 * must be 0 or exactly the kind's size (4, 8, 8, 16), else E_INVALIDARG and nothing written;
 * pData NULL with a size: E_INVALIDARG; size 0: a status check, S_OK, nothing written; before
 * the query's first End, and between Begin and End: DXGI_ERROR_INVALID_CALL, nothing written.
 * Flags: D3D11_ASYNC_GETDATA_DONOTFLUSH (1) only withholds a flush, and nothing is queued:
 * no effect; any other bit is a stop. */
COM_METHOD(ID3D11DeviceContext, HRESULT, GetData, ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags)
{
    static const UINT size[4] = { sizeof(BOOL), sizeof(UINT64), sizeof(UINT64), sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT) };
    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if (!pAsync) RT_UNIMPLEMENTED(ctx, "GetData of a NULL query");
    NdQuery *q = rt_com_state(ctx, rt_nulldev_arg(ctx, pAsync, &rt_nulldev_query_class, "pAsync"), &rt_nulldev_query_class);
    if (GetDataFlags & ~1u) RT_UNIMPLEMENTED(ctx, "GetData with flags 0x%x", (unsigned)GetDataFlags);
    /* The two failures are the front's own answers: the back end is not asked (rt/d3dback.h, rule 1). */
    if ((DataSize && DataSize != size[q->desc.Query]) || (DataSize && !pData)) { RT_BACK_ALONE(ctx, GetData); return E_INVALIDARG; }
    if (!q->ended || q->begun) { RT_BACK_ALONE(ctx, GetData); return DXGI_ERROR_INVALID_CALL; }
    /* The seam (rt/d3dback.h, rule 6): with a back end the answer is the back end's, ready or
     * not, and nothing below is invented or counted.  Only S_OK and S_FALSE can be handed to the
     * game; anything else where the front accepted the call is a stop. */
    if (rt_d3dback()) {
        HRESULT hr = RT_BACK_ANSWER(ctx, GetData, c->child.back, rt_d3dback_of(pAsync), pData, DataSize, GetDataFlags);
        if (hr != S_OK && hr != S_FALSE) rt_d3dback_differs(ctx, RT_BACK_GetData, S_OK, hr);
        return hr;
    }
    if (DataSize) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
        memset(&disjoint, 0, sizeof disjoint);
        disjoint.Frequency = ND_TICKS_PER_SECOND;
        disjoint.Disjoint = FALSE;
        if (q->desc.Query == D3D11_QUERY_EVENT) *(BOOL *)pData = TRUE;
        else if (q->desc.Query == D3D11_QUERY_OCCLUSION) *(UINT64 *)pData = q->samples;
        else if (q->desc.Query == D3D11_QUERY_TIMESTAMP) *(UINT64 *)pData = q->end_ticks;
        else memcpy(pData, &disjoint, sizeof disjoint);
    }
    /* Every answer here is one the device cannot truly give: the ledger counts it by kind (a
     * status check of size 0 too: "ready" is the invented part) and logs the first of each. */
    static const uint8_t kind[4] = { ND_INV_EVENT, ND_INV_OCCLUSION, ND_INV_TIMESTAMP, ND_INV_DISJOINT };
    rt_nulldev_invented(ctx, kind[q->desc.Query], q->desc.Query == D3D11_QUERY_OCCLUSION ? q->samples :
                                                  q->desc.Query == D3D11_QUERY_TIMESTAMP ? q->end_ticks :
                                                  q->desc.Query == D3D11_QUERY_EVENT ? 1 : ND_TICKS_PER_SECOND);
    return S_OK;
}
