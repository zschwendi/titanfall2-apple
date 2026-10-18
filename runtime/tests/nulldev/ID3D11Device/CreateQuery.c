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

TEST(ID3D11Device, CreateQuery)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    D3D11_QUERY_DESC d = { 0, 0 };
    ID3D11Query *query = NULL;

    T_CASE("each of the four kinds: S_OK, one reference, the device held, the description kept");
    for (UINT k = 0; k < 4; k++) {                      /* event, occlusion, timestamp, timestamp disjoint */
        d.Query = k;
        query = NULL;
        T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, &query), S_OK);
        T_REQUIRE(query != NULL);
        const NdQuery *s = rt_com_state_of(query, &rt_nulldev_query_class);
        T_EQ(s->desc.Query, k);
        T_EQ(s->desc.MiscFlags, 0);
        T_EQ(s->begun, 0);
        T_EQ(s->ended, 0);
        T_TRUE(s->child.device == device);
        T_EQ(rt_com_refs(query), 1);
        T_EQ(rt_com_refs(device), 3 + k);
    }

    T_CASE("a NULL out-pointer validates only: S_FALSE and no object");
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, NULL), S_FALSE);
    T_EQ(rt_com_refs(device), 6);

    T_TRAPS(COM_CALL(ID3D11Device, CreateQuery, device, NULL, &query), "CreateQuery with a NULL description");
    d.Query = 4;                                        /* D3D11_QUERY_PIPELINE_STATISTICS */
    T_TRAPS(COM_CALL(ID3D11Device, CreateQuery, device, &d, &query), "CreateQuery of kind 4");
    d.Query = 1; d.MiscFlags = 1;
    T_TRAPS(COM_CALL(ID3D11Device, CreateQuery, device, &d, &query), "CreateQuery with flags");
}
