#include "rt/test.h"

/* The answer is fixed: no console, so the call fails with ERROR_INVALID_HANDLE, writes neither
 * the count nor the buffer, and logs once that there is no console. */
TEST(kernel32, WriteConsoleW)
{
    WCHAR buf[4];
    DWORD count;
    unsigned i;

    T_CASE("fails with ERROR_INVALID_HANDLE when there is no console");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, WriteConsoleW, (HANDLE)0x44, NULL, 0, NULL, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("writes neither the count nor the buffer");
    for (i = 0; i < 4; i++) buf[i] = (WCHAR)(0x1100 + i);
    count = 0xA5A5A5A5;
    T_EQ(CALL(kernel32, WriteConsoleW, (HANDLE)0x44, buf, 4, &count, NULL), FALSE);
    T_EQ(count, 0xA5A5A5A5);
    for (i = 0; i < 4; i++) T_EQ(buf[i], (WCHAR)(0x1100 + i));

    T_CASE("logs that there is no console");
    T_TRUE(T_LOGGED(RT_CH_FILE, "WriteConsoleW: there is no console"));
}
