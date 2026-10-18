/* Expected values: Microsoft's pages of D3D11_QUERY, GetData and
 * D3D11_QUERY_DATA_TIMESTAMP_DISJOINT (what each kind returns: a BOOL, a UINT64 count of
 * samples, a UINT64 timestamp, a frequency and a BOOL) and Wine's d3d11 conformance tests as
 * the contract's check reads them (docs/reviews/d3d-contract-check.md, point 3): the data size
 * per kind (4, 8, 8, 16), E_INVALIDARG (0x80070057) for another size, DXGI_ERROR_INVALID_CALL
 * (0x887a0001) before the first End and between Begin and End, S_FALSE for a NULL out-pointer
 * of CreateQuery.  The values themselves (TRUE, 0 samples, 10,000,000 ticks a second, not
 * disjoint) are this device's stated choice (ID3D11DeviceContext/GetData.c). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11DeviceContext, Begin)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Query *q[4];
    for (UINT k = 0; k < 4; k++) {
        D3D11_QUERY_DESC d = { k, 0 };
        T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, &q[k]), S_OK);
    }
    NdQuery *occ = rt_com_state_of(q[1], &rt_nulldev_query_class);

    T_CASE("Begin of an occlusion query records that it began");
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[1]);
    T_EQ(occ->begun, 1);
    T_EQ(occ->ended, 0);

    T_CASE("after End it can begin again");
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[1]);
    T_EQ(occ->begun, 0);
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[1]);
    T_EQ(occ->begun, 1);
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[3]);      /* timestamp disjoint begins too */
    T_EQ(((NdQuery *)rt_com_state_of(q[3], &rt_nulldev_query_class))->begun, 1);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, Begin, context, NULL), "Begin of a NULL query");

    T_CASE("Begin of an event or a timestamp query does nothing: the query has not begun and answers as without it");
    /* Microsoft, D3D11_QUERY (read 2026-10-11), D3D11_QUERY_EVENT and D3D11_QUERY_TIMESTAMP: "When
     * using this type of query, ID3D11DeviceContext::Begin is disabled."; an event query's
     * GetData "will return S_OK, and pData will point to a BOOL with a value of TRUE", a
     * timestamp's "returns a UINT64" (G9 of docs/reviews/d3d-code-review.md, whose test this is). */
    BOOL finished = FALSE;
    UINT64 ticks = 0;
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[0]);
    T_EQ(((NdQuery *)rt_com_state_of(q[0], &rt_nulldev_query_class))->begun, 0);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[0]);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[0], &finished, 4, 0), S_OK);
    T_EQ(finished, TRUE);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[2]);
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[2]);     /* after its End: still nothing */
    T_EQ(((NdQuery *)rt_com_state_of(q[2], &rt_nulldev_query_class))->begun, 0);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[2], &ticks, 8, 0), S_OK);
    T_TRUE(ticks != 0);
    T_TRAPS(COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[1]), "Begin of a query that has begun and not ended");
}
