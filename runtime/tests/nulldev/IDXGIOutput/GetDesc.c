/* Expected values: the device name is finding 35's (docs/findings/35-dxmt-window.md section 4:
 * output 0: "\\.\DISPLAY1" ... attached=1); the rectangle {0, 0, 1920, 1080} and the monitor
 * handle 0x10001 are the window contract's desktop (rt/window.h: RtDesktop's comment,
 * RT_HMONITOR_PRIMARY; the numbers come from finding 30's trace); rotation 1 is
 * DXGI_MODE_ROTATION_IDENTITY (MinGW's dxgitype.h).  Offsets and the size 96 are the Windows
 * layout of DXGI_OUTPUT_DESC (MinGW's dxgi.h; make d3d-layout-check). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIOutput, GetDesc)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIOutput *output = rt_nulldev_output_new(adapter);
    static const uint16_t name[13] = { '\\', '\\', '.', '\\', 'D', 'I', 'S', 'P', 'L', 'A', 'Y', '1', 0 };
    static const int32_t rect[4] = { 0, 0, 1920, 1080 };
    uint8_t raw[96 + 8], want[96];

    T_CASE("the one output is the desktop of rt/window.h, every byte of the structure");
    memset(raw, 0xa5, sizeof raw);
    memset(want, 0, sizeof want);
    memcpy(want, name, sizeof name);                                    /* DeviceName[32] at 0 */
    memcpy(want + 64, rect, 16);                                        /* DesktopCoordinates */
    memcpy(want + 80, &(uint32_t){ 1 }, 4);                             /* AttachedToDesktop */
    memcpy(want + 84, &(uint32_t){ 1 }, 4);                             /* Rotation: identity */
    memcpy(want + 88, &(uint64_t){ 0x10001 }, 8);                       /* Monitor */
    T_EQ(COM_CALL(IDXGIOutput, GetDesc, output, (DXGI_OUTPUT_DESC *)raw), S_OK);
    T_TRUE(!memcmp(raw, want, 96));
    for (int i = 96; i < 104; i++) T_EQ(raw[i], 0xa5);                  /* nothing past the structure */

    T_TRAPS(COM_CALL(IDXGIOutput, GetDesc, output, NULL), "GetDesc with a NULL description");
}
