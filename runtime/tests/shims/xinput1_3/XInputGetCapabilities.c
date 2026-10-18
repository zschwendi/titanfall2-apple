/* Expected values: Microsoft's page of XInputGetCapabilities (read 2026-10-10: "If the controller is not
 * connected, the return value is ERROR_DEVICE_NOT_CONNECTED"; the index "can be a value" from
 * 0 to 3; "The function does not use SetLastError") and Microsoft's list of system error
 * codes 1000 to 1299 for the number 1167.  That nothing is written and that an index above
 * 3 and a NULL pointer stop are this runtime's choices where the page is silent. */
#include <string.h>

#include "rt/test.h"

TEST(xinput1_3, XInputGetCapabilities)
{
    struct { uint8_t pre[8]; uint8_t s[32]; uint8_t post[8]; } box;   /* larger than any of XInput's structures */
    unsigned i;

    T_CASE("no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167)");
    memset(&box, 0xa5, sizeof box);
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 0, 1, box.s), 1167);

    T_CASE("the same for slots 1, 2 and 3");
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 1, 1, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 2, 1, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 3, 1, box.s), 1167);

    T_CASE("nothing is written to the structure");
    for (i = 0; i < sizeof box; i++) T_EQ(((uint8_t *)&box)[i], 0xa5);

    T_CASE("last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 0, 1, box.s), 1167);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the flag XINPUT_FLAG_GAMEPAD (1), as the game passes it, changes nothing");
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 0, 0, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputGetCapabilities, 0, 1, box.s), 1167);
    for (i = 0; i < sizeof box; i++) T_EQ(((uint8_t *)&box)[i], 0xa5);

    T_TRAPS(CALL(xinput1_3, XInputGetCapabilities, 0, 2, box.s), "XInputGetCapabilities with flags 0x2, neither 0 nor XINPUT_FLAG_GAMEPAD");

    T_TRAPS(CALL(xinput1_3, XInputGetCapabilities, 4, 1, box.s), "XInputGetCapabilities with user index 4, above 3");
    T_TRAPS(CALL(xinput1_3, XInputGetCapabilities, 0, 1, NULL), "XInputGetCapabilities with a NULL pCapabilities");
}
