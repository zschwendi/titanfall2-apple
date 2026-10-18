#include "rt/test.h"

TEST(kernel32, DeviceIoControl)
{
    T_TRAPS(CALL(kernel32, DeviceIoControl, (HANDLE)(uintptr_t)0x44, 0x2D1400, NULL, 0, NULL, 0, NULL, NULL), "DeviceIoControl(handle 0x44, code 0x002D1400, 0 bytes in, 0 bytes out)");
}
