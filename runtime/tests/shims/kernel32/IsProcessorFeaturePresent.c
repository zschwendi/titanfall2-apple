#include "rt/test.h"

TEST(kernel32, IsProcessorFeaturePresent)
{
    /* The PF_* numbers of winnt.h are written as numbers: the headers have no names for them.
     * Each comment names the feature; the numbers are those of the Microsoft page (RT-DOC). */

    T_CASE("returns nonzero for a feature the processor has");
    T_TRUE(CALL(kernel32, IsProcessorFeaturePresent, 3) != 0);     /* PF_MMX_INSTRUCTIONS_AVAILABLE */
    T_TRUE(CALL(kernel32, IsProcessorFeaturePresent, 6) != 0);     /* PF_XMMI_INSTRUCTIONS_AVAILABLE: SSE */
    T_TRUE(CALL(kernel32, IsProcessorFeaturePresent, 8) != 0);     /* PF_RDTSC_INSTRUCTION_AVAILABLE */
    T_TRUE(CALL(kernel32, IsProcessorFeaturePresent, 9) != 0);     /* PF_PAE_ENABLED: every x64 processor */
    T_TRUE(CALL(kernel32, IsProcessorFeaturePresent, 10) != 0);    /* PF_XMMI64_INSTRUCTIONS_AVAILABLE: SSE2 */
    T_TRUE(CALL(kernel32, IsProcessorFeaturePresent, 13) != 0);    /* PF_SSE3_INSTRUCTIONS_AVAILABLE */

    T_CASE("returns FALSE for a feature the processor lacks");
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 7), FALSE);     /* PF_3DNOW_INSTRUCTIONS_AVAILABLE */
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 17), FALSE);    /* PF_XSAVE_ENABLED */
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 23), FALSE);    /* PF_FASTFAIL_AVAILABLE */
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 36), FALSE);    /* PF_SSSE3_INSTRUCTIONS_AVAILABLE */
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 39), FALSE);    /* PF_AVX_INSTRUCTIONS_AVAILABLE */
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 40), FALSE);    /* PF_AVX2_INSTRUCTIONS_AVAILABLE */

    T_CASE("returns FALSE for a feature number the machine does not describe and logs it");
    T_EQ(CALL(kernel32, IsProcessorFeaturePresent, 1000), FALSE);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "IsProcessorFeaturePresent(1000)"));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, IsProcessorFeaturePresent, 6);
    CALL(kernel32, IsProcessorFeaturePresent, 7);
    CALL(kernel32, IsProcessorFeaturePresent, 1000);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
