/* The forward of ID3D11DeviceContext::GetData through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 1: the two failures the front answers
 * before the query has ended (DXGI_ERROR_INVALID_CALL, and E_INVALIDARG for a wrong size) are the
 * front's own, so the back end is not reached and rt_d3dback_alone counts them; each stop of the
 * body comes before the back end.  Rule 2: the context and the query cross as their back objects,
 * the bytes by pointer as the game passed them.  Rule 6: with a back end the answer is the back
 * end's, S_OK or S_FALSE, and nothing is invented; any other result is a stop (rule 4).  With
 * `none` the front answers, and the ledger counts the one invented occlusion count. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, GetData)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Query *q = T_BACK_NEW(device, &rt_nulldev_query_class);
    NdQuery *nq = rt_com_state_of(q, &rt_nulldev_query_class);
    nq->desc.Query = D3D11_QUERY_OCCLUSION;
    UINT64 n = 99;

    T_CASE("between Begin and End the front answers alone: DXGI_ERROR_INVALID_CALL, nothing written");
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, sizeof n, 0), DXGI_ERROR_INVALID_CALL);
    T_EQ(n, 99);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GetData), 0);
        T_EQ(rt_d3dback_alone(RT_BACK_GetData), 1);
    }

    T_CASE("after End a wrong size is E_INVALIDARG, also answered by the front alone");
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q);
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, 4, 0), E_INVALIDARG);
    T_EQ(n, 99);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(GetData), 0);
        T_EQ(rt_d3dback_alone(RT_BACK_GetData), 2);
    }

    T_CASE("after End the answer is the back end's: S_OK with the bytes it hands, and nothing is invented");
    if (T_BACK) {
        UINT64 want = 1234;
        rt_d3dback_record_data(&want, sizeof want);
    }
    T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, 8, 0), S_OK);
    if (T_BACK) {
        T_EQ(n, 1234);
        T_EQ(T_BACK_COUNT(GetData), 1);
        T_EQ(T_BACK_ARG(GetData, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(GetData, 1), T_BACK_OF(q));
        T_EQ(T_BACK_ARG(GetData, 2), T_BACK_PTR(&n));
        T_EQ(T_BACK_ARG(GetData, 3), 8);
        T_EQ(T_BACK_ARG(GetData, 4), 0);
        T_EQ(rt_nulldev_invented_count(ND_INV_OCCLUSION), 0);
    } else {
        T_EQ(n, 0);
        T_EQ(rt_nulldev_invented_count(ND_INV_OCCLUSION), 1);
    }

    if (T_BACK) {
        T_CASE("not ready is the back end's answer, handed on; any other result of it is a stop");
        rt_d3dback_record_answer(RT_BACK_GetData, S_FALSE);
        T_EQ(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, 8, 0), S_FALSE);
        rt_d3dback_record_answer(RT_BACK_GetData, E_FAIL);
        T_TRAPS(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, 8, 0),
                "the back end (record) answered 0x80004005 to ID3D11DeviceContext::GetData");
        rt_d3dback_record_answer(RT_BACK_GetData, S_OK);
        T_EQ(T_BACK_COUNT(GetData), 2);
    }

    T_CASE("the stops, each before the back end: GetData of a NULL query, and with flags other than DONOTFLUSH");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GetData, context, NULL, &n, 8, 0));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)q, &n, 8, 2));
    ID3D11Buffer *notq = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, GetData, context, (ID3D11Asynchronous *)notq, &n, 8, 0));
}
