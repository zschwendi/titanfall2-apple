/* Expected value: 0xb000 (D3D_FEATURE_LEVEL_11_0), the level the native renderer reported on
 * this Mac for a null feature-level list (finding 35, docs/findings/35-dxmt-build.md section 3);
 * written here as the number, not taken from the body. */
#include "rt/test.h"
#include "rt/nulldev.h"

TEST(ID3D11Device, GetFeatureLevel)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);

    T_CASE("the device's feature level, D3D_FEATURE_LEVEL_11_0");
    T_EQ((uint32_t)COM_CALL(ID3D11Device, GetFeatureLevel, device), 0xb000u);
}
