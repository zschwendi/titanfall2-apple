/* Expected values: finding 35 (docs/findings/35-dxmt-build.md section 3), the probe's lines
 *     adapter: "Apple M3 Pro"   VendorId=0x106b DeviceId=0x0000 SubSysId=0x0 Revision=0
 *     DedicatedVideoMemory=7151124480 DedicatedSystemMemory=0 SharedSystemMemory=0
 *     AdapterLuid=d4070000:01000000        (high part, low part)
 * written here as numbers, not through the ND_ constants.  Offsets and the size 304 are the
 * Windows layout of DXGI_ADAPTER_DESC (MinGW's dxgi.h; make d3d-layout-check). */
#include <string.h>

#include "rt/test.h"
#include "rt/nulldev.h"

TEST(IDXGIAdapter1, GetDesc)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    static const uint16_t name[13] = { 'A', 'p', 'p', 'l', 'e', ' ', 'M', '3', ' ', 'P', 'r', 'o', 0 };
    uint8_t raw[304 + 8], want[304];

    T_CASE("the adapter the native renderer reported on this Mac, every byte of the structure");
    memset(raw, 0xa5, sizeof raw);
    memset(want, 0, sizeof want);
    memcpy(want, name, sizeof name);                                    /* Description[128] at 0 */
    memcpy(want + 256, &(uint32_t){ 0x106b }, 4);                       /* VendorId */
    memcpy(want + 272, &(uint64_t){ UINT64_C(7151124480) }, 8);         /* DedicatedVideoMemory */
    memcpy(want + 296, &(uint32_t){ 0x01000000 }, 4);                   /* AdapterLuid.LowPart */
    memcpy(want + 300, &(uint32_t){ 0xd4070000 }, 4);                   /* AdapterLuid.HighPart */
    T_EQ(COM_CALL(IDXGIAdapter1, GetDesc, adapter, (DXGI_ADAPTER_DESC *)raw), S_OK);
    T_TRUE(!memcmp(raw, want, 304));
    for (int i = 304; i < 312; i++) T_EQ(raw[i], 0xa5);                 /* nothing past the structure */

    T_TRAPS(COM_CALL(IDXGIAdapter1, GetDesc, adapter, NULL), "GetDesc with a NULL description");
}
