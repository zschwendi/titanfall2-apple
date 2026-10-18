/* The forward of ID3D11DeviceContext::IASetPrimitiveTopology through the seam (rt/d3dback.h).
 * Expected values: the seam's rules and the topology numbers of rt/win/d3d11.h, not the body.
 * Rule 2: the first argument is the back end's context (the back object of the front context);
 * the topology crosses as the game passed it.  Rule 1: the stops of the body (topology 6 to 9
 * and above 13) come before the back end is reached, so each is a T_BACK_STOPS_BEFORE.  No older
 * nulldev test is changed here: the front keeps the topology, and that is the nulldev test's. */
#include "rt/test_back.h"

BACK_TEST(ID3D11DeviceContext, IASetPrimitiveTopology)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("a triangle list is handed on once, with its topology");
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 4);
    T_EQ(rt_com_calls(&rt_iface_ID3D11DeviceContext, "IASetPrimitiveTopology"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetPrimitiveTopology), 1);
        T_EQ(T_BACK_ARG(IASetPrimitiveTopology, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(IASetPrimitiveTopology, 1), 4);
    }

    T_CASE("the undefined topology 0 is handed on too");
    COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 0);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(IASetPrimitiveTopology), 2);
        T_EQ(T_BACK_ARG(IASetPrimitiveTopology, 0), T_BACK_OF(context));
        T_EQ(T_BACK_ARG(IASetPrimitiveTopology, 1), 0);
    }

    T_CASE("a topology the front stops on (7, a patch list) stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 7));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 6));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 9));
    T_BACK_STOPS_BEFORE(COM_CALL(ID3D11DeviceContext, IASetPrimitiveTopology, context, 14));
    if (T_BACK) T_EQ(T_BACK_COUNT(IASetPrimitiveTopology), 2);
}
