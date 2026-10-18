/* Expected values: Microsoft's page of XInputSetState (read 2026-10-10: "If the controller is not
 * connected, the return value is ERROR_DEVICE_NOT_CONNECTED"; the index "can be a value" from
 * 0 to 3; "The function does not use SetLastError") and Microsoft's list of system error
 * codes 1000 to 1299 for the number 1167.  That nothing is written and that an index above
 * 3 and a NULL pointer stop are this runtime's choices where the page is silent. */
#include <string.h>

#include "rt/test.h"

TEST(xinput1_3, XInputSetState)
{
    struct { uint8_t pre[8]; uint8_t s[32]; uint8_t post[8]; } box;   /* larger than any of XInput's structures */
    unsigned i;

    T_CASE("no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167)");
    memset(&box, 0xa5, sizeof box);
    T_EQ(CALL(xinput1_3, XInputSetState, 0, box.s), 1167);

    T_CASE("the same for slots 1, 2 and 3");
    T_EQ(CALL(xinput1_3, XInputSetState, 1, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputSetState, 2, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputSetState, 3, box.s), 1167);

    T_CASE("nothing is written to the structure");
    for (i = 0; i < sizeof box; i++) T_EQ(((uint8_t *)&box)[i], 0xa5);

    T_CASE("last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(xinput1_3, XInputSetState, 0, box.s), 1167);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(xinput1_3, XInputSetState, 4, box.s), "XInputSetState with user index 4, above 3");
    T_TRAPS(CALL(xinput1_3, XInputSetState, 0, NULL), "XInputSetState with a NULL pVibration");
}
