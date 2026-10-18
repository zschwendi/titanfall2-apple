#include "rt/test.h"

TEST(kernel32, GetCurrentThreadId)
{
    T_CASE("returns the thread id stored in the thread block at +0x48");
    T_EQ(CALL(kernel32, GetCurrentThreadId), LD32(T_CTX()->gs_base + 0x48));
    T_EQ(CALL(kernel32, GetCurrentThreadId), RT_THREAD(T_CTX())->id);
    /* Overwrite the field, so that the shim is shown to read it, then put the original back. */
    uint32_t saved = LD32(T_CTX()->gs_base + 0x48);
    ST32(T_CTX()->gs_base + 0x48, 0x2468);
    T_EQ(CALL(kernel32, GetCurrentThreadId), 0x2468);
    ST32(T_CTX()->gs_base + 0x48, saved);
    T_EQ(CALL(kernel32, GetCurrentThreadId), saved);

    T_CASE("the id of the first thread is a nonzero multiple of 4");
    DWORD id = CALL(kernel32, GetCurrentThreadId);
    T_TRUE(id != 0);
    T_TRUE(id % 4 == 0);
    T_EQ(id, RT_FIRST_THREAD_ID);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetCurrentThreadId);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
