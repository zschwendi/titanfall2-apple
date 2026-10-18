/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/d3d11/nf-d3d11-id3d11device-createquery
 * RT-CASES:  each of the four kinds: S_OK, one reference, the device held, the description kept; a NULL out-pointer validates only: S_FALSE and no object
 * RT-TRAPS:  CreateQuery with a NULL description; CreateQuery of kind 4; CreateQuery with flags
 */
#include "rt/nulldev.h"

/* The four kinds the game creates (finding 31, section 5): event (0), occlusion (1), timestamp
 * (2), timestamp disjoint (3); what each answers is ID3D11DeviceContext/GetData.c.  Any other
 * kind is a stop: its result was never chosen.  Flags: D3D11_QUERY_MISC_PREDICATEHINT is for
 * predicates, which the game does not make: any flag is a stop.  A NULL out-pointer validates
 * and returns S_FALSE without creating (Wine's d3d11 test test_create_query, through
 * docs/reviews/d3d-contract-check.md, point 3; the page, read 2026-10-10 by a subagent, does
 * not say).  S_OK for success is from memory (the page names only E_OUTOFMEMORY). */
COM_METHOD(ID3D11Device, HRESULT, CreateQuery, const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery)
{
    (void)rt_com_state(ctx, This, &rt_nulldev_device_class);
    if (!pQueryDesc) RT_UNIMPLEMENTED(ctx, "CreateQuery with a NULL description");
    if (pQueryDesc->Query > D3D11_QUERY_TIMESTAMP_DISJOINT)
        RT_UNIMPLEMENTED(ctx, "CreateQuery of kind %u, which is none of the four the game was found to use (event, occlusion, "
                         "timestamp, timestamp disjoint); flags 0x%x", (unsigned)pQueryDesc->Query, (unsigned)pQueryDesc->MiscFlags);
    if (pQueryDesc->MiscFlags) RT_UNIMPLEMENTED(ctx, "CreateQuery with flags 0x%x (kind %u)", (unsigned)pQueryDesc->MiscFlags, (unsigned)pQueryDesc->Query);
    if (!ppQuery) {
        RT_BACK_ALONE(ctx, CreateQuery);        /* the call only validated: no object, so the back end is not asked */
        return S_FALSE;
    }
    ID3D11Query *query = rt_nulldev_new(This, &rt_nulldev_query_class);
    ((NdQuery *)rt_com_state(ctx, query, &rt_nulldev_query_class))->desc = *pQueryDesc;
    /* The seam (rt/d3dback.h, rules 3 and 4): the front has its object and its answer; the back
     * end makes its own from the game's description, unchanged. */
    RT_BACK_CREATE(ctx, query, CreateQuery, rt_d3dback_of(This), pQueryDesc);
    *ppQuery = query;
    return S_OK;
}
