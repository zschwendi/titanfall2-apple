/* Expected values: the notes of this unit (Microsoft, from memory: default 3, range 1 to 16,
 * 0 resets to the default; the page was not read): 3 at first, 1 after 1, 16 after 16, 3 after
 * 0, S_OK each time.  The stop for 17 is this runtime's: the page names no failure code for it
 * in the notes.  The values never come from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIDevice1, SetMaximumFrameLatency)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGIDevice1 *dxgi = NULL;
    rt_nulldev_create(&device, &context);
    T_EQ(COM_CALL(ID3D11Device, QueryInterface, device, &rt_iface_IDXGIDevice1.iid, (void **)&dxgi), S_OK);
    NdDevice *s = rt_com_state_of(device, &rt_nulldev_device_class);

    T_CASE("1 to 16 is stored and S_OK");
    T_EQ(s->max_frame_latency, 3);
    T_EQ(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dxgi, 1), S_OK);
    T_EQ(s->max_frame_latency, 1);
    T_EQ(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dxgi, 16), S_OK);
    T_EQ(s->max_frame_latency, 16);

    T_CASE("0 stores the default 3 and S_OK");
    T_EQ(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dxgi, 0), S_OK);
    T_EQ(s->max_frame_latency, 3);

    T_TRAPS(COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dxgi, 17), "SetMaximumFrameLatency(");
}
