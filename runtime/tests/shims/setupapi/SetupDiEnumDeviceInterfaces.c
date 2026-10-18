#include <stdint.h>
#include <string.h>
#include "rt/test.h"
#include "rt/win/devices.h"

TEST(setupapi, SetupDiEnumDeviceInterfaces)
{
    SP_DEVICE_INTERFACE_DATA d;
    unsigned char *bytes = (unsigned char *)&d;
    BOOL r;
    size_t i;

    T_CASE("index 0 of the empty set fails with ERROR_NO_MORE_ITEMS");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiEnumDeviceInterfaces, RT_HDEVINFO_EMPTY, NULL, NULL, 0, &d);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_ITEMS);

    T_CASE("index 3 of the empty set fails with ERROR_NO_MORE_ITEMS");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiEnumDeviceInterfaces, RT_HDEVINFO_EMPTY, NULL, NULL, 3, &d);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_ITEMS);

    T_CASE("the structure is not written");
    memset(&d, 0xA5, sizeof d);
    r = CALL(setupapi, SetupDiEnumDeviceInterfaces, RT_HDEVINFO_EMPTY, NULL, NULL, 0, &d);
    T_EQ(r, FALSE);
    for (i = 0; i < sizeof d; i++)
        T_EQ(bytes[i], 0xA5);

    T_CASE("a set other than the empty one fails with ERROR_INVALID_HANDLE");
    memset(&d, 0xA5, sizeof d);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiEnumDeviceInterfaces, NULL, NULL, NULL, 0, &d);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    for (i = 0; i < sizeof d; i++)
        T_EQ(bytes[i], 0xA5);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiEnumDeviceInterfaces, (HDEVINFO)(uintptr_t)0x1234u, NULL, NULL, 0, &d);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}
