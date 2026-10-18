#include "rt/test.h"

TEST(kernel32, SuspendThread)
{
    T_TRAPS(CALL(kernel32, SuspendThread, (HANDLE)(uintptr_t)0x44), "SuspendThread(0x44)");
}
