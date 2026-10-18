#include "rt/test.h"

TEST(user32, AllowSetForegroundWindow)
{
    unsigned before = T_LOG_COUNT(RT_CH_WINDOW);

    T_CASE("every process id returns TRUE");
    T_EQ(CALL(user32, AllowSetForegroundWindow, 0), TRUE);
    T_EQ(CALL(user32, AllowSetForegroundWindow, 1234), TRUE);
    T_EQ(CALL(user32, AllowSetForegroundWindow, (DWORD)-1), TRUE);

    T_CASE("the debug line is logged once");
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "AllowSetForegroundWindow(0): no other process exists"));
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW) - before, 1);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, AllowSetForegroundWindow, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(user32, AllowSetForegroundWindow, (DWORD)-1), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
