/* Expected values: Microsoft's page of XInputGetState (read 2026-10-10: "If the controller is not
 * connected, the return value is ERROR_DEVICE_NOT_CONNECTED"; the index "can be a value" from
 * 0 to 3; "The function does not use SetLastError") and Microsoft's list of system error
 * codes 1000 to 1299 for the number 1167.  That nothing is written and that an index above
 * 3 and a NULL pointer stop are this runtime's choices where the page is silent. */
#include <string.h>

#include "rt/test.h"
#include "rt/gamepad.h"

TEST(xinput1_3, XInputGetState)
{
    struct { uint8_t pre[8]; uint8_t s[32]; uint8_t post[8]; } box;   /* larger than any of XInput's structures */
    unsigned i;

    T_CASE("no controller in slot 0: ERROR_DEVICE_NOT_CONNECTED (1167)");
    memset(&box, 0xa5, sizeof box);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, box.s), 1167);

    T_CASE("the same for slots 1, 2 and 3");
    T_EQ(CALL(xinput1_3, XInputGetState, 1, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputGetState, 2, box.s), 1167);
    T_EQ(CALL(xinput1_3, XInputGetState, 3, box.s), 1167);

    T_CASE("nothing is written to the structure");
    for (i = 0; i < sizeof box; i++) T_EQ(((uint8_t *)&box)[i], 0xa5);

    T_CASE("last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, box.s), 1167);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("connected controller uses native snapshot");
    RtGamepad pad = { .buttons=RT_PAD_A | RT_PAD_START | RT_PAD_L3,
        .left_trigger=255, .right_trigger=128, .left_x=-32768,
        .left_y=32767, .right_x=1234, .right_y=-2345 };
    rt_gamepad_publish(0, &pad);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, box.s), 0);
    RtGamepadState state;
    memcpy(&state, box.s, sizeof state);
    T_TRUE(state.packet != 0);
    T_EQ(state.gamepad.buttons, 0x1050);
    T_EQ(state.gamepad.left_x, -32768);
    T_EQ(state.gamepad.left_y, 32767);
    T_EQ(state.gamepad.right_trigger, 128);
    T_EQ(T_LAST_ERROR(), 0x1234);
    for (i = sizeof state; i < sizeof box.s; i++) T_EQ(box.s[i], 0xa5);
    for (i = 0; i < 8; i++) { T_EQ(box.pre[i], 0xa5); T_EQ(box.post[i], 0xa5); }
    uint32_t packet=state.packet;
    rt_gamepad_publish(0, &pad);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, &state), 0);
    T_EQ(state.packet, packet);
    pad.buttons=0;
    rt_gamepad_publish(0, &pad);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, &state), 0);
    T_TRUE(state.packet != packet);
    rt_gamepad_disconnect(0);
    memset(box.s, 0xa5, sizeof box.s);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, box.s), 1167);
    for (i = 0; i < sizeof box.s; i++) T_EQ(box.s[i], 0xa5);
    memset(&pad, 0, sizeof pad);
    rt_gamepad_publish(0, &pad);
    T_EQ(CALL(xinput1_3, XInputGetState, 0, &state), 0);
    T_EQ(state.gamepad.buttons, 0);
    T_EQ(state.gamepad.left_trigger, 0);
    T_TRUE(state.packet != packet);
    rt_gamepad_disconnect(0);

    T_TRAPS(CALL(xinput1_3, XInputGetState, 4, box.s), "XInputGetState with user index 4, above 3");
    T_TRAPS(CALL(xinput1_3, XInputGetState, 0, NULL), "XInputGetState with a NULL pState");
}
