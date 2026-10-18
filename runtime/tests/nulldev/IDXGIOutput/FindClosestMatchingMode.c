/* Expected values: Microsoft's page of IDXGIOutput::FindClosestMatchingMode was not read
 * (no fetch available); the one mode is the desktop's, from rt/window.h's contract
 * (1920 by 1080 at 60 Hz, design-null-device.md 3.4); the 28-byte DXGI_MODE_DESC layout is
 * the header's. The stops are the contract's choices. */
#include "rt/test.h"
#include "rt/nulldev.h"
#include "rt/window.h"

TEST(IDXGIOutput, FindClosestMatchingMode)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    IDXGIAdapter1 *adapter = ((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter;
    IDXGIOutput *output = rt_nulldev_output_new(adapter);

    /* the desktop's mode, then 28 bytes of it and nothing after: a guard word follows */
    struct { DXGI_MODE_DESC desc; uint32_t guard; } out;
    const uint32_t guard_value = 0xdeadbeefu;

    T_CASE("format 28 gives the desktop's mode in the format asked for, and S_OK"); /* device is not used: a NULL and a given device answer alike */
    memset(&out, 0xab, sizeof out);
    out.guard = guard_value;
    DXGI_MODE_DESC req;
    memset(&req, 0, sizeof req);
    req.Width = 640;
    req.Height = 480;
    req.RefreshRate.Numerator = 75;
    req.RefreshRate.Denominator = 1;
    req.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    T_EQ(COM_CALL(IDXGIOutput, FindClosestMatchingMode, output, &req, &out.desc, NULL), S_OK);
    T_EQ(out.desc.Width, 1920);
    T_EQ(out.desc.Height, 1080);
    T_EQ(out.desc.RefreshRate.Numerator, 60);
    T_EQ(out.desc.RefreshRate.Denominator, 1);
    T_EQ(out.desc.Format, 28);
    T_EQ(out.desc.ScanlineOrdering, 1);
    T_EQ(out.desc.Scaling, 0);
    T_EQ(out.guard, guard_value);
    T_EQ(rt_com_refs(output), 1);
    /* the same answer when a device is given */
    memset(&out, 0xab, sizeof out);
    out.guard = guard_value;
    T_EQ(COM_CALL(IDXGIOutput, FindClosestMatchingMode, output, &req, &out.desc, (IUnknown *)device), S_OK);
    T_EQ(out.desc.Width, 1920);
    T_EQ(out.desc.Height, 1080);
    T_EQ(out.desc.RefreshRate.Numerator, 60);
    T_EQ(out.desc.Format, 28);
    T_EQ(out.guard, guard_value);

    /* a NULL closest_match: the contract gives the same message as for a NULL mode */
    T_TRAPS(COM_CALL(IDXGIOutput, FindClosestMatchingMode, output, &req, NULL, NULL), "FindClosestMatchingMode with a NULL mode");
    T_TRAPS(COM_CALL(IDXGIOutput, FindClosestMatchingMode, output, NULL, &out.desc, NULL), "FindClosestMatchingMode with a NULL mode");
    req.Format = 0;
    T_TRAPS(COM_CALL(IDXGIOutput, FindClosestMatchingMode, output, &req, &out.desc, NULL), "FindClosestMatchingMode for format");
}
