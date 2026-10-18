#include <stdint.h>
#include <string.h>
#include "rt/test.h"
#include "rt/win/devices.h"

TEST(setupapi, SetupDiGetDeviceInterfaceDetailA)
{
    unsigned char buf[64];
    DWORD req;
    SP_DEVICE_INTERFACE_DATA d;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)buf;
    BOOL r;
    size_t i;

    T_CASE("a set other than the empty one fails with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiGetDeviceInterfaceDetailA, NULL, NULL, NULL, 0, NULL, NULL);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiGetDeviceInterfaceDetailA, INVALID_HANDLE_VALUE, NULL, NULL, 0, NULL, NULL);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("the empty set with no interface data fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiGetDeviceInterfaceDetailA, RT_HDEVINFO_EMPTY, NULL, NULL, 0, NULL, NULL);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("the detail buffer and RequiredSize are kept");
    memset(buf, 0xA5, sizeof buf);
    memset(&req, 0xA5, sizeof req);
    r = CALL(setupapi, SetupDiGetDeviceInterfaceDetailA, NULL, NULL, detail, sizeof buf, &req, NULL);
    T_EQ(r, FALSE);
    for (i = 0; i < sizeof buf; i++)
        T_EQ(buf[i], 0xA5);
    T_EQ(req, 0xA5A5A5A5u);
    r = CALL(setupapi, SetupDiGetDeviceInterfaceDetailA, RT_HDEVINFO_EMPTY, NULL, detail, sizeof buf, &req, NULL);
    T_EQ(r, FALSE);
    for (i = 0; i < sizeof buf; i++)
        T_EQ(buf[i], 0xA5);
    T_EQ(req, 0xA5A5A5A5u);

    memset(&d, 0, sizeof d);
    d.cbSize = 32;
    T_TRAPS(CALL(setupapi, SetupDiGetDeviceInterfaceDetailA, RT_HDEVINFO_EMPTY, &d, detail, sizeof buf, &req, NULL),
            "an interface this set never gave out");
}
