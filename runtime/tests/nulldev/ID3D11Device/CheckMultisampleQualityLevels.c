/* Expected values: Wine's test_check_multisample_quality_levels (conformance test d3d11.c) for
 * the order of checks, the E_FAIL range and NULL-before-count; the probe's line "x1: 1 x2: 1 x4: 1
 * x8: 0 x16: 0" of format 28 (finding 35, docs/findings/35-dxmt-build.md section 3) and the
 * render-target bit 0x200000 of the measured CheckFormatSupport bits for 2 and 4 samples; the
 * numbers of formats: DXGI_FORMAT (rt/win/dxgi.h).  Microsoft's page of
 * CheckMultisampleQualityLevels, read 2026-10-10.  Format 1 (not measured) is a stop: the
 * contract's choice, since no source gives Windows' answer for it. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, CheckMultisampleQualityLevels)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    UINT n = 0xdeadbeef;

    T_CASE("format 28 with 1, 2 and 4 samples gives S_OK and 1");
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 1, &n), (uint32_t)S_OK);
    T_EQ(n, 1u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 2, &n), (uint32_t)S_OK);
    T_EQ(n, 1u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 4, &n), (uint32_t)S_OK);
    T_EQ(n, 1u);

    T_CASE("format 28 with 8, 16, 3, 15 and 32 samples gives S_OK and 0");
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 8, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 16, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 3, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 15, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 32, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);

    T_CASE("format 45 with 4 samples and format 40 with 4 samples give 1");
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 45, 4, &n), (uint32_t)S_OK);
    T_EQ(n, 1u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 40, 4, &n), (uint32_t)S_OK);
    T_EQ(n, 1u);

    T_CASE("format 71 with 2 samples gives S_OK and 0, with 1 sample 1");
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 71, 2, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 71, 1, &n), (uint32_t)S_OK);
    T_EQ(n, 1u);

    T_CASE("format 42 with 4 samples gives S_OK and 0");
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 42, 4, &n), (uint32_t)S_OK);
    T_EQ(n, 0u);

    T_CASE("sample count 0 and 33 give E_FAIL and 0");
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 0, &n), (uint32_t)E_FAIL);
    T_EQ(n, 0u);
    n = 0xdeadbeef;
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 33, &n), (uint32_t)E_FAIL);
    T_EQ(n, 0u);

    T_CASE("a NULL pointer gives E_INVALIDARG, with 2 samples or with 0 samples");
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 2, NULL), (uint32_t)E_INVALIDARG);
    T_EQ((uint32_t)COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 28, 0, NULL), (uint32_t)E_INVALIDARG);

    /* Format 0 was not measured; format 66 is one the device refused in CheckFormatSupport
     * (tools/dxmt/format_probe.out), and this method was not measured for it. */
    T_TRAPS(COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 0, 2, &n), "CheckMultisampleQualityLevels for format 0");
    T_TRAPS(COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 66, 2, &n), "CheckMultisampleQualityLevels for format 66");
    T_TRAPS(COM_CALL(ID3D11Device, CheckMultisampleQualityLevels, device, 133, 1, &n), "CheckMultisampleQualityLevels for format 133");
}
