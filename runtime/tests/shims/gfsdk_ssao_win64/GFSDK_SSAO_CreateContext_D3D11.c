/* Expected values: NVIDIA's public header GFSDK_SSAO.h (version 4.0, github.com/NVIDIAGameWorks/
 * HBAOPlus): GFSDK_SSAO_D3D_FEATURE_LEVEL_NOT_SUPPORTED is the fifteenth enumerator of
 * GFSDK_SSAO_Status, 14.  That the context pointer is left alone is what the game relies on
 * (materialsystem_dx11+0x288c2 tests it for NULL); the header does not say what the library
 * does with it on failure. */
#include "rt/test.h"
#include "rt/log.h"

TEST(gfsdk_ssao_win64, GFSDK_SSAO_CreateContext_D3D11)
{
    int device = 0;                                     /* any non-NULL pointer: the function never looks behind it */
    void *context = NULL, *kept = &device;
    uint32_t version[4] = { 3, 0, 2, 0x11a49d1 };

    T_CASE("answers GFSDK_SSAO_D3D_FEATURE_LEVEL_NOT_SUPPORTED (14) and leaves the context pointer as it was");
    T_EQ(CALL(gfsdk_ssao_win64, GFSDK_SSAO_CreateContext_D3D11, &device, &context, NULL, version), 14);
    T_TRUE(context == NULL);
    T_EQ(CALL(gfsdk_ssao_win64, GFSDK_SSAO_CreateContext_D3D11, &device, &kept, NULL, version), 14);
    T_TRUE(kept == &device);

    T_CASE("logs one warning line");
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "GFSDK_SSAO_CreateContext_D3D11: no ambient-occlusion library here"));

    T_TRAPS(CALL(gfsdk_ssao_win64, GFSDK_SSAO_CreateContext_D3D11, NULL, &context, NULL, version), "GFSDK_SSAO_CreateContext_D3D11 with a NULL device or a NULL context pointer");
    T_TRAPS(CALL(gfsdk_ssao_win64, GFSDK_SSAO_CreateContext_D3D11, &device, NULL, NULL, version), "GFSDK_SSAO_CreateContext_D3D11 with a NULL device or a NULL context pointer");
}
