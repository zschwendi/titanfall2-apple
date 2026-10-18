#include <string.h>
#include "rt/test.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_wndmsg_register.  The first name is the one vgui2's load
 * registers (docs/handoff/gate3-load.md). */
TEST(user32, RegisterWindowMessageA)
{
    T_CASE("a message number from 0xC000 to 0xFFFF");
    CALL(kernel32, SetLastError, 0x1234);
    UINT m = CALL(user32, RegisterWindowMessageA, "ShutdownValvePlatform");
    T_TRUE(m >= 0xC000 && m <= 0xFFFF);

    T_CASE("the same string gives the same number, also in other case");
    T_EQ(CALL(user32, RegisterWindowMessageA, "ShutdownValvePlatform"), m);
    T_EQ(CALL(user32, RegisterWindowMessageA, "shutdownVALVEplatform"), m);

    T_CASE("another string gives another number");
    UINT other = CALL(user32, RegisterWindowMessageA, "ShutdownValvePlatform ");
    T_TRUE(other >= 0xC000 && other <= 0xFFFF && other != m);

    T_CASE("does not change last error");
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the core's stops, which the shim does not guard. */
    T_TRAPS(CALL(user32, RegisterWindowMessageA, NULL), "rt_wndmsg_register: not implemented: a NULL message name");
    T_TRAPS(CALL(user32, RegisterWindowMessageA, ""), "rt_wndmsg_register: not implemented: an empty message name");
    static char long_name[257];                 /* 256 characters and the terminator */
    memset(long_name, 'z', 256);
    T_TRAPS(CALL(user32, RegisterWindowMessageA, long_name), "rt_wndmsg_register: not implemented: a message name longer than 255 characters");
    T_TRAPS(CALL(user32, RegisterWindowMessageA, (LPCSTR)0xC123), "rt_wndmsg_register: not implemented: a message name that is an atom (0xc123)");
}
