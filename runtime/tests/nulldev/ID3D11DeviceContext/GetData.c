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

TEST(ID3D11DeviceContext, GetData)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Query *q[4];
    for (UINT k = 0; k < 4; k++) {
        D3D11_QUERY_DESC d = { k, 0 };
        T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &d, &q[k]), S_OK);
    }
    uint8_t raw[24];
    BOOL done;
    UINT64 count, t0, t1;

    T_CASE("before the first End, and between Begin and End: DXGI_ERROR_INVALID_CALL and nothing written");
    done = 0x5a5a;
    T_EQ((uint32_t)COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[0], &done, sizeof done, 0), 0x887a0001u);
    T_EQ(done, 0x5a5a);
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[1]);
    count = 77;
    T_EQ((uint32_t)COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[1], &count, sizeof count, 0), 0x887a0001u);
    T_EQ(count, 77);

    T_CASE("an event query after End: S_OK and TRUE");
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[0]);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[0], &done, sizeof done, 0), S_OK);
    T_EQ(done, TRUE);
    done = 0;                                           /* and with the flag that withholds a flush */
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[0], &done, sizeof done, 1), S_OK);
    T_EQ(done, TRUE);

    T_CASE("an occlusion query after Begin and End: S_OK and 0 samples");
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[1]);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[1], &count, sizeof count, 0), S_OK);
    T_EQ(count, 0);

    T_CASE("a timestamp query: S_OK and the ticks of its End, never decreasing");
    D3D11_QUERY_DESC td = { D3D11_QUERY_TIMESTAMP, 0 };
    ID3D11Query *later = NULL;
    T_EQ(COM_CALL(ID3D11Device, CreateQuery, device, &td, &later), S_OK);
    uint64_t before = rt_nulldev_ticks();
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[2]);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)later);
    t0 = t1 = 0;
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[2], &t0, sizeof t0, 0), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)later, &t1, sizeof t1, 0), S_OK);
    T_TRUE(t0 >= before);
    T_TRUE(t1 >= t0);
    T_TRUE(t1 <= rt_nulldev_ticks());
    T_EQ(t0, ((NdQuery *)rt_com_state_of(q[2], &rt_nulldev_query_class))->end_ticks);

    T_CASE("a timestamp-disjoint query: 10000000 ticks a second, not disjoint");
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q[3]);
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q[3]);
    memset(raw, 0xa5, sizeof raw);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[3], raw, 16, 0), S_OK);
    static const uint8_t want[16] = { 0x80, 0x96, 0x98, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0 };   /* 10000000 = 0x989680; FALSE; padding */
    T_TRUE(!memcmp(raw, want, 16));
    for (int i = 16; i < 24; i++) T_EQ(raw[i], 0xa5);

    T_CASE("a status check with size 0 is S_OK and writes nothing");
    memset(raw, 0xa5, sizeof raw);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[3], raw, 0, 0), S_OK);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[3], NULL, 0, 0), S_OK);
    for (int i = 0; i < 24; i++) T_EQ(raw[i], 0xa5);

    T_CASE("a wrong size is E_INVALIDARG and writes nothing");
    T_EQ((uint32_t)COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[3], raw, 8, 0), 0x80070057u);
    T_EQ((uint32_t)COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[0], raw, 8, 0), 0x80070057u);
    T_EQ((uint32_t)COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[1], NULL, 8, 0), 0x80070057u);
    for (int i = 0; i < 24; i++) T_EQ(raw[i], 0xa5);

    T_TRAPS(COM_CALL(ID3D11DeviceContext, GetData, context, NULL, &done, sizeof done, 0), "GetData of a NULL query");
    T_TRAPS(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q[0], &done, sizeof done, 2), "GetData with flags 0x2");
}
