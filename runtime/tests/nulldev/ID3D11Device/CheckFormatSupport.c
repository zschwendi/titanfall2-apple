/* Expected values: the hex numbers are the "CheckFormatSupport" block of device_probe.out
 * (finding 35, docs/findings/35-dxmt-build.md section 3), written out here, not taken from the
 * body; format numbers are DXGI_FORMAT (rt/win/dxgi.h): 1 R32G32B32A32_TYPELESS, 2 _FLOAT, 28
 * R8G8B8A8_UNORM, 42 R32_UINT, 45 D24_UNORM_S8_UINT, 71 BC1_UNORM.  57 R16_UINT and 16
 * R32G32_FLOAT: lines "57" and "16" of tools/dxmt/format_probe.out (the same device asked about
 * every format number; the game asks about these two at materialsystem_dx11+0x2d379 and
 * +0x2d3d5 and tests bits 0x2000000 and 0x4000 of the answers).  The other numbers of that
 * file, written out here from it: 1, 3, 29, 61, 87 and 115 with their bits; the 34 lines that
 * read "hr=0x80070057 0x00000000" (66, 89, 100 to 114, 116 to 132: E_INVALIDARG, and the probe
 * had put 0xdeadbeef in the word before each call, so the 0 is the device's).  An unmeasured
 * format (0, and anything above 132) and a NULL out-pointer are stops: the contract's choice,
 * since no source gives Windows' answer (Microsoft's page of CheckFormatSupport, read
 * 2026-10-10; Wine's test_format_support). */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CheckFormatSupport)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    UINT s = 0xdeadbeef;

    T_CASE("a measured format gives its bits and S_OK");
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 28, &s), (uint32_t)S_OK);
    T_EQ(s, 0x02fef3f3u);
    s = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 71, &s), (uint32_t)S_OK);
    T_EQ(s, 0x00d213f0u);
    s = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 45, &s), (uint32_t)S_OK);
    T_EQ(s, 0x04f717f0u);
    s = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 42, &s), (uint32_t)S_OK);
    T_EQ(s, 0x02d273ffu);
    s = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 2, &s), (uint32_t)S_OK);
    T_EQ(s, 0x02f6f3fbu);

    T_CASE("the two formats the game asks about beyond the first probe's 32");
    s = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 57, &s), (uint32_t)S_OK);
    T_EQ(s, 0x02f273f7u);
    s = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, 16, &s), (uint32_t)S_OK);
    T_EQ(s, 0x02f6f3fbu);

    T_CASE("more of the measured column");
    static const struct { unsigned format; uint32_t bits; } more[] = {
        { 1, 0x02d273f1u }, { 3, 0x02d273fbu }, { 29, 0x02fef3f1u }, { 61, 0x02f6f3f3u }, { 87, 0x02fef3f3u }, { 115, 0x02f2f3f1u },
    };
    for (unsigned i = 0; i < sizeof more / sizeof *more; i++) {
        s = 0xdeadbeef;
        T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, more[i].format, &s), (uint32_t)S_OK);
        T_EQ(s, more[i].bits);
    }

    T_CASE("a format the device refused gives E_INVALIDARG and 0, as measured");
    static const unsigned refused[34] = {
        66, 89, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,
        116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
    };
    for (unsigned i = 0; i < 34; i++) {
        s = 0xdeadbeef;
        T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckFormatSupport, device, refused[i], &s), (uint32_t)E_INVALIDARG);
        T_EQ(s, 0u);
    }

    T_CASE("a format that was not measured is a stop");
    T_TRAPS(COM_CALL(ID3D11Device, CheckFormatSupport, device, 0, &s), "CheckFormatSupport for format 0");
    T_TRAPS(COM_CALL(ID3D11Device, CheckFormatSupport, device, 133, &s), "CheckFormatSupport for format 133");
    T_TRAPS(COM_CALL(ID3D11Device, CheckFormatSupport, device, 0xffffffffu, &s), "CheckFormatSupport for format 4294967295");
    T_TRAPS(COM_CALL(ID3D11Device, CheckFormatSupport, device, 28, NULL), "CheckFormatSupport with a NULL out-pointer");
}
