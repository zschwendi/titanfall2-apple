/* The forward of ID3D11DeviceContext::End through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and the query cross as
 * their back objects.  WHAT A BODY WRITES: every End the front accepts is handed on once.  Rule 1:
 * each stop of the body comes before the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, End)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Query *q = T_BACK_NEW(device, &rt_nulldev_query_class);
    NdQuery *nq = rt_com_state_of(q, &rt_nulldev_query_class);
    nq->desc.Query = D3D11_QUERY_OCCLUSION;
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q);

    T_CASE("End of a begun occlusion query ends it: the record says so, and the back end is handed the query");
    COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q);
    T_EQ(nq->begun, 0);
    T_EQ(nq->ended, 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(End), 1);
        T_EQ(T_BACK_ARG(End, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(End, 1), T_BACK_OF(q));
    }

    T_CASE("the stops, each before the back end: End of a NULL query, and of an occlusion query that was not begun");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, End, context, NULL));
    ID3D11Query *q2 = T_BACK_NEW(device, &rt_nulldev_query_class);
    NdQuery *nq2 = rt_com_state_of(q2, &rt_nulldev_query_class);
    nq2->desc.Query = D3D11_QUERY_OCCLUSION;
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)q2));
    ID3D11Buffer *notq = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, End, context, (ID3D11Asynchronous *)notq));
}
