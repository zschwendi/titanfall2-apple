#include "rt/test.h"
#include "rt/nulldev.h"

/* Expected values.  S_OK and feature level 11_0 (0xb000) for the game's arguments: finding 35
 * (what the native renderer returned on this Mac for a null feature-level list) and
 * Microsoft's page of the function (the default list begins with 11_0).  The ten arguments:
 * docs/handoff/rt-win-queue.md, as a run of the game passed them.  Which classes the two
 * objects are is the do-nothing device's own model (rt/nulldev.h), not Direct3D. */
TEST(d3d11, D3D11CreateDevice)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = (ID3D11DeviceContext *)(uintptr_t)0x0ed0c101;     /* the game's slot holds leftovers */
    D3D_FEATURE_LEVEL level = 0, list[1] = { D3D_FEATURE_LEVEL_11_0 };
    void *got = NULL;

    T_CASE("the call the game makes");
    T_EQ(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7, &device, &level, &context), S_OK);
    T_EQ(level, 0xb000);
    T_REQUIRE(device != NULL && context != NULL);
    T_TRUE((void *)device != (void *)context);
    T_TRUE(rt_com_class_of(device) == &rt_nulldev_device_class);
    T_TRUE(rt_com_class_of(context) == &rt_nulldev_context_class);
    T_EQ(LD64((uint64_t)(uintptr_t)device), rt_iface_ID3D11Device.vtable);           /* what lifted code reads at offset 0 */
    T_EQ(LD64((uint64_t)(uintptr_t)context), rt_iface_ID3D11DeviceContext.vtable);
    T_EQ(rt_com_refs(device), 2);            /* the caller's and the context's: every object a device makes holds it (rt/nulldev.h) */
    T_EQ(rt_com_refs(context), 1);
    /* The three methods every object has work; they are the layer's (rt/com.h). */
    T_EQ(COM_CALL(ID3D11Device, QueryInterface, device, &rt_iface_ID3D11Device.iid, &got), S_OK);
    T_TRUE(got == device);
    T_EQ(COM_CALL(ID3D11Device, AddRef, device), 4);
    T_EQ(COM_CALL(ID3D11Device, Release, device), 3);
    T_EQ(COM_CALL(ID3D11Device, Release, device), 2);
    T_EQ(COM_CALL(ID3D11DeviceContext, AddRef, context), 2);
    T_EQ(COM_CALL(ID3D11DeviceContext, Release, context), 1);
    /* What the other methods do is not this shim's: a method with no body stops and names
     * itself (tests/core/com_objects.c shows that on the test's own interfaces), and each
     * body that is written brings its own test under tests/nulldev. */
    /* A second call makes a second pair. */
    ID3D11Device *device2 = NULL;
    ID3D11DeviceContext *context2 = NULL;
    level = 0;
    T_EQ(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7, &device2, &level, &context2), S_OK);
    T_EQ(level, 0xb000);
    T_TRUE(device2 != device && context2 != context);

    /* Every other combination of arguments stops. */
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, (IDXGIAdapter *)device, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7, &device, &level, &context),
            "D3D11CreateDevice with an adapter");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, 7, &device, &level, &context),
            "D3D11CreateDevice with a driver type other than hardware (0)");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, 5, NULL, 0, NULL, 0, 7, &device, &level, &context),
            "D3D11CreateDevice with a driver type other than hardware (5)");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, (HMODULE)(uintptr_t)0x10000, 0, NULL, 0, 7, &device, &level, &context),
            "D3D11CreateDevice with a software module");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 2, NULL, 0, 7, &device, &level, &context),
            "D3D11CreateDevice with creation flags 0x2");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, list, 1, 7, &device, &level, &context),
            "D3D11CreateDevice with a feature-level list");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 1, 7, &device, &level, &context),
            "D3D11CreateDevice with a feature-level list");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 6, &device, &level, &context),
            "D3D11CreateDevice with an SDK version other than 7 (6)");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7, NULL, &level, &context),
            "D3D11CreateDevice with an out-pointer that is NULL");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7, &device, NULL, &context),
            "D3D11CreateDevice with an out-pointer that is NULL");
    T_TRAPS(CALL(d3d11, D3D11CreateDevice, NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 7, &device, &level, NULL),
            "D3D11CreateDevice with an out-pointer that is NULL");
}
