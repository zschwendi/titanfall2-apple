#include "rt/test.h"
#include "rt/win/user.h"

TEST(user32, ChangeDisplaySettingsA)
{
    /* A stand-in for a DEVMODE: the shim never reads the mode, so any non-NULL pointer does. */
    DWORD mode[8] = {0};

    T_CASE("return to the default mode (NULL, 0)");
    T_EQ(CALL(user32, ChangeDisplaySettingsA, NULL, 0), DISP_CHANGE_SUCCESSFUL);
    T_EQ(CALL(user32, ChangeDisplaySettingsA, NULL, 0), DISP_CHANGE_SUCCESSFUL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, ChangeDisplaySettingsA, NULL, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a display mode change. The stops are the two calls that are not the default
     * mode: a non-NULL mode with flags 0, and no mode with flags 4 (not 0, so not the default). */
    T_TRAPS(CALL(user32, ChangeDisplaySettingsA, mode, 0), "a display mode change (flags 0x0)");
    T_TRAPS(CALL(user32, ChangeDisplaySettingsA, NULL, 4), "a display mode change (flags 0x4)");
}
