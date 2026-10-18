#include "rt/test.h"
#include "rt/window.h"

/* Expected values: docs/handoff/rt-contract-input.md, rows K22, K34 and K35: the scan codes of
 * Microsoft's "Keyboard Input Overview" (Scan 1 Make), the answers for the two Shift keys and
 * the two Control keys that Wine's conformance test test_key_map asserts, and MapVirtualKey's
 * page for the capital of a letter by type 2.  The core's test (tests/core/win_keymap.c) goes
 * through the whole table. */
TEST(user32, MapVirtualKeyExA)
{
    const HKL us = CALL(user32, LoadKeyboardLayoutA, "00000409", 0);

    T_CASE("map type 0 gives the scan code of a virtual key");
    T_EQ(CALL(user32, MapVirtualKeyExA, 'W', 0, us), 0x11);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x20, 0, us), 0x39);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x1B, 0, NULL), 0x01);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x10, 0, us), 0x2A);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0xA1, 0, us), 0x36);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0xA3, 0, us), 0x1D);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x26, 0, us), 0x48);

    T_CASE("map types 1 and 3 give the virtual key of a scan code");
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x11, 1, us), 'W');
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x2A, 1, us), 0x10);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x36, 1, us), 0x10);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x2A, 3, us), 0xA0);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x36, 3, us), 0xA1);
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x1D, 3, NULL), 0xA2);

    T_CASE("map type 2 gives the unshifted character");
    T_EQ(CALL(user32, MapVirtualKeyExA, 'W', 2, us), 'W');
    T_EQ(CALL(user32, MapVirtualKeyExA, '5', 2, us), '5');
    T_EQ(CALL(user32, MapVirtualKeyExA, 0xBF, 2, us), '/');
    T_EQ(CALL(user32, MapVirtualKeyExA, 0x70, 2, us), 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, MapVirtualKeyExA, 'A', 0, us), 0x1E);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: all four are stops of the core (rt_key_map). */
    T_TRAPS(CALL(user32, MapVirtualKeyExA, 'A', 0, (HKL)(uintptr_t)0x04070407), "only the US layout exists");
    T_TRAPS(CALL(user32, MapVirtualKeyExA, 'A', 4, us), "map type 4");
    T_TRAPS(CALL(user32, MapVirtualKeyExA, 0x60, 0, us), "virtual key 0x60 by map type 0");
    T_TRAPS(CALL(user32, MapVirtualKeyExA, 0x47, 1, us), "scan code 0x47 of the numeric keypad block");
    T_TRAPS(CALL(user32, MapVirtualKeyExA, 0x7F, 3, us), "scan code 0x7f by map type 3");
}
