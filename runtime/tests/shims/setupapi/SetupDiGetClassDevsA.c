#include "rt/test.h"
#include "rt/win/devices.h"

TEST(setupapi, SetupDiGetClassDevsA)
{
    GUID hid = RT_GUID_DEVINTERFACE_HID;
    HDEVINFO r;
    unsigned logs;

    logs = T_LOG_COUNT(RT_CH_IMPORT);               /* before the first call */

    T_CASE("returns RT_HDEVINFO_EMPTY for the HID class with flags 0x12, on two calls in a row");
    r = CALL(setupapi, SetupDiGetClassDevsA, &hid, NULL, NULL, 0x12);
    T_EQ(r, RT_HDEVINFO_EMPTY);
    r = CALL(setupapi, SetupDiGetClassDevsA, &hid, NULL, NULL, 0x12);
    T_EQ(r, RT_HDEVINFO_EMPTY);

    T_CASE("logs once that no device of the class is there");
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT) - logs, 1);
    T_LOGGED(RT_CH_IMPORT, "SetupDiGetClassDevsA: no device of class {4D1E55B2-F16F-11CF-88CB-001111000030}");

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(setupapi, SetupDiGetClassDevsA, &hid, NULL, NULL, 0x12);
    T_EQ(r, RT_HDEVINFO_EMPTY);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops: each refused query is a fatal report that names the values it was given. */
    T_TRAPS(CALL(setupapi, SetupDiGetClassDevsA, NULL, NULL, NULL, 0x12),
            "SetupDiGetClassDevsA with ClassGuid NULL");
    T_TRAPS(CALL(setupapi, SetupDiGetClassDevsA, &hid, "HID", NULL, 0x12),
            "SetupDiGetClassDevsA with Enumerator");
    T_TRAPS(CALL(setupapi, SetupDiGetClassDevsA, &hid, NULL, NULL, 0x16),
            "SetupDiGetClassDevsA with Flags 0x16: a bit outside");
    T_TRAPS(CALL(setupapi, SetupDiGetClassDevsA, &hid, NULL, NULL, 0x02),
            "SetupDiGetClassDevsA with Flags 0x2 without DIGCF_DEVICEINTERFACE");
}
