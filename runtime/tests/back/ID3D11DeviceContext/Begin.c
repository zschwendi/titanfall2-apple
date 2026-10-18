/* The forward of ID3D11DeviceContext::Begin through the seam (rt/d3dback.h).
 * Expected values: the seam's rules, not the body.  Rule 2: the context and the query cross as
 * their back objects.  WHAT A BODY WRITES: every Begin the front accepts is handed on once, the
 * event query's too, so an event Begin is counted with the back end.  The event query's `begun`
 * stays 0 because the D3D11_QUERY page says Begin is disabled for an event query (the body cites
 * that page; the record itself is the body's, and this is the one line that takes it from there).
 * Rule 1: each stop of the body comes before the back end is reached. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, Begin)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    ID3D11Query *q = T_BACK_NEW(device, &rt_nulldev_query_class);
    NdQuery *nq = rt_com_state_of(q, &rt_nulldev_query_class);
    nq->desc.Query = D3D11_QUERY_OCCLUSION;
    ID3D11Query *ev = T_BACK_NEW(device, &rt_nulldev_query_class);
    NdQuery *ne = rt_com_state_of(ev, &rt_nulldev_query_class);
    ne->desc.Query = D3D11_QUERY_EVENT;

    T_CASE("Begin of an occlusion query records that it began, and the back end is handed the query");
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q);
    T_EQ(nq->begun, 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Begin), 1);
        T_EQ(T_BACK_ARG(Begin, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(Begin, 1), T_BACK_OF(q));
    }

    T_CASE("Begin of an event query is handed on with a back end, and records nothing: the query has not begun");
    COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)ev);
    T_EQ(ne->begun, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(Begin), 2);
        T_EQ(T_BACK_ARG(Begin, 1), T_BACK_OF(ev));
    }

    T_CASE("the stops, each before the back end: Begin of a NULL query, and a second Begin before End");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Begin, context, NULL));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)q));
    ID3D11Buffer *notq = T_BACK_NEW(device, &rt_nulldev_buffer_class);
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, Begin, context, (ID3D11Asynchronous *)notq));
}
