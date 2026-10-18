#include "rt/test.h"
#include "rt/win/devices.h"

TEST(setupapi, SetupDiDestroyDeviceInfoList)
{
    BOOL r;

    T_CASE("returns TRUE for the empty device information set");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiDestroyDeviceInfoList, RT_HDEVINFO_EMPTY);
    T_EQ(r, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);                  /* success leaves last error alone */

    T_CASE("sets ERROR_INVALID_HANDLE for NULL and for INVALID_HANDLE_VALUE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiDestroyDeviceInfoList, NULL);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiDestroyDeviceInfoList, INVALID_HANDLE_VALUE);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("sets ERROR_INVALID_HANDLE for any other value");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiDestroyDeviceInfoList, (HDEVINFO)(uintptr_t)0x1234);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
}
