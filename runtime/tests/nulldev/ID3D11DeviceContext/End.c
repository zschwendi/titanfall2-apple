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

TEST(ID3D11DeviceContext, End)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Query *q[4];
    for (UINT k = 0; k < 4; k++) {
        D3D11_QUERY_DESC d = { k, 0 };
        T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, &q[k]), S_OK);
    }
    NdQuery *ev = rt_com_state_of(q[0], &rt_nulldev_query_class), *occ = rt_com_state_of(q[1], &rt_nulldev_query_class);

    T_CASE("End of an event query counts it and stamps the time");
    uint64_t before = rt_nulldev_ticks();
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[0]);
    T_EQ(ev->ended, 1);
    T_TRUE(ev->end_ticks >= before && ev->end_ticks <= rt_nulldev_ticks());
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[0]);
    T_EQ(ev->ended, 2);

    T_CASE("End of a begun occlusion query ends it");
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[1]);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[1]);
    T_EQ(occ->begun, 0);
    T_EQ(occ->ended, 1);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, End, context, NULL), "End of a NULL query");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[1]), "End of an occlusion or timestamp-disjoint query that was not begun");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[3]), "End of an occlusion or timestamp-disjoint query that was not begun");
}
