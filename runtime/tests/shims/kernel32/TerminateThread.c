#include "rt/test.h"

TEST(kernel32, TerminateThread)
{
    T_TRAPS(CALL(kernel32, TerminateThread, (HANDLE)(uintptr_t)0x44, 7), "TerminateThread(0x44, 7)");
}
