/* The forward of IDXGIDevice1::SetMaximumFrameLatency through the seam (rt/d3dback.h).
 * Expected values: the seam's rules and the method's notes, not the body.  Rule 2: argument 0 is
 * the device's own back object (the front device's back pointer); the latency crosses as the game
 * passed it, 0 included.  With no back end the front stores the number itself: 0 stores 3.
 * Rule 1: the stop for 17 (above 16) comes before the back end is reached.  Rule 4: a result of
 * the back end other than S_OK is a stop that names the method. */
#include "rt/test_back.h"

BACK_TEST(IDXGIDevice1, SetMaximumFrameLatency)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIDevice1 *dx = rt_com_face(device, &rt_iface_IDXGIDevice1);
    NdDevice *s = rt_com_state_of(device, &rt_nulldev_device_class);

    T_CASE("a latency is handed on once, with the device's back object and the number");
    T_EQ(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dx, 2), S_OK);
    T_EQ(rt_com_calls(&rt_iface_IDXGIDevice1, "SetMaximumFrameLatency"), 1);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(SetMaximumFrameLatency), 1);
        T_EQ(T_BACK_ARG(SetMaximumFrameLatency, 0), T_BACK_OF(device));
        T_EQ(T_BACK_ARG(SetMaximumFrameLatency, 1), 2);
    }

    T_CASE("0 is handed on as 0, while the front stores its default 3");
    T_EQ(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dx, 0), S_OK);
    if (T_BACK) {
        T_EQ(T_BACK_COUNT(SetMaximumFrameLatency), 2);
        T_EQ(T_BACK_ARG(SetMaximumFrameLatency, 1), 0);
    }
    T_EQ(s->max_frame_latency, 3);

    if (T_BACK) {
        T_CASE("a result of the back end other than S_OK is a stop that names the method");
        rt_d3dback_record_answer(RT_BACK_SetMaximumFrameLatency, E_FAIL);
        T_TRAPS(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dx, 2),
                "the back end (record) answered 0x80004005 to IDXGIDevice1::SetMaximumFrameLatency");
        rt_d3dback_record_answer(RT_BACK_SetMaximumFrameLatency, S_OK);
    }

    T_CASE("a latency above 16 stops before the back end is reached");
    T_BACK_STOPS_BEFORE(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dx, 17));
}
