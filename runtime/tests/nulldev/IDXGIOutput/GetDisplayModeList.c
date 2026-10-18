/* Expected values: the two-call protocol and DXGI_ERROR_MORE_DATA are Microsoft's page of
 * IDXGIOutput::GetDisplayModeList (read 2026-10-10); 1920, 1080 and 60 are the window
 * contract's desktop (rt/window.h, RtDesktop's comment); format 28, progressive 1 and
 * scaling 0 are MinGW's dxgiformat.h and dxgitype.h; the 28 bytes and the offsets are
 * DXGI_MODE_DESC's layout (make d3d-layout-check); one mode is the contract's choice. */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIOutput, GetDisplayModeList)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIOutput *output = rt_nulldev_output_new(((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter);
    static const uint32_t want[7] = { 1920, 1080, 60, 1, 28, 1, 0 };   /* Width Height Numerator Denominator Format Scanline Scaling */
    uint32_t raw[7 * 2 + 1];
    UINT count;

    T_CASE("a NULL list returns the number of modes, 1");
    count = 77;
    T_EQ(COM_CALL(IDXGIOutput, GetDisplayModeList, output, 28, 0, &count, NULL), S_OK);
    T_EQ(count, 1);

    T_CASE("a list with room receives the desktop's mode and the count 1");
    for (unsigned n = 1; n <= 2; n++) {
        memset(raw, 0xa5, sizeof raw);
        count = n;
        T_EQ(COM_CALL(IDXGIOutput, GetDisplayModeList, output, 28, 0, &count, (DXGI_MODE_DESC *)raw), S_OK);
        T_EQ(count, 1);
        T_TRUE(!memcmp(raw, want, 28));
        for (int i = 7; i < 15; i++) T_EQ(raw[i], 0xa5a5a5a5u);        /* one mode, nothing after it */
    }

    T_CASE("a list with no room is DXGI_ERROR_MORE_DATA and receives nothing");
    memset(raw, 0xa5, sizeof raw);
    count = 0;
    T_EQ((uint32_t)COM_CALL(IDXGIOutput, GetDisplayModeList, output, 28, 0, &count, (DXGI_MODE_DESC *)raw), 0x887a0003u);
    T_EQ(count, 0);
    T_EQ(raw[0], 0xa5a5a5a5u);

    T_TRAPS(COM_CALL(IDXGIOutput, GetDisplayModeList, output, 87, 0, &count, NULL), "GetDisplayModeList for format 87");
    T_TRAPS(COM_CALL(IDXGIOutput, GetDisplayModeList, output, 28, 2, &count, NULL), "GetDisplayModeList with flags 0x2");
    T_TRAPS(COM_CALL(IDXGIOutput, GetDisplayModeList, output, 28, 0, NULL, NULL), "GetDisplayModeList with a NULL count");
}
