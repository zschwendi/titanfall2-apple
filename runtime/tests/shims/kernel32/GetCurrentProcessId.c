#include "rt/test.h"

TEST(kernel32, GetCurrentProcessId)
{
    T_CASE("returns the process id stored in the thread block at +0x40");
    T_EQ(CALL(kernel32, GetCurrentProcessId), LD32(T_CTX()->gs_base + 0x40));
    /* There is no host-side copy of the process id to compare with: overwrite the field, so
     * that the shim is shown to read it, then put the original back. */
    uint32_t saved = LD32(T_CTX()->gs_base + 0x40);
    ST32(T_CTX()->gs_base + 0x40, 0x1358);
    T_EQ(CALL(kernel32, GetCurrentProcessId), 0x1358);
    ST32(T_CTX()->gs_base + 0x40, saved);
    T_EQ(CALL(kernel32, GetCurrentProcessId), saved);

    T_CASE("the id is a nonzero multiple of 4");
    DWORD id = CALL(kernel32, GetCurrentProcessId);
    T_TRUE(id != 0);
    T_TRUE(id % 4 == 0);
    T_EQ(id, RT_PROCESS_ID);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetCurrentProcessId);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
