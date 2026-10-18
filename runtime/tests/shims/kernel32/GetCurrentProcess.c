#include "rt/test.h"

TEST(kernel32, GetCurrentProcess)
{
    T_CASE("returns the pseudo-handle for the current process, all 64 bits set");
    HANDLE h = CALL(kernel32, GetCurrentProcess);
    T_EQ((uint64_t)(uintptr_t)h, UINT64_C(0xFFFFFFFFFFFFFFFF));
    T_TRUE(h == RT_PSEUDO_PROCESS);

    T_CASE("returns the same value on every call");
    T_TRUE(CALL(kernel32, GetCurrentProcess) == h);
    T_TRUE(CALL(kernel32, GetCurrentProcess) == h);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetCurrentProcess);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
