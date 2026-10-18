#include "rt/test.h"
#include "rt/machine.h"

TEST(kernel32, GetVersion)
{
    T_CASE("returns the Windows 7 SP1 value 0x1DB10106");
    DWORD v = CALL(kernel32, GetVersion);
    T_EQ(v, 0x1DB10106u);                           /* 6.1, build 7601 = 0x1DB1 */

    T_CASE("major in the low byte, minor in the next, build in the high word with the top bit clear");
    T_EQ(v & 0xFF, 6);                              /* major */
    T_EQ((v >> 8) & 0xFF, 1);                       /* minor */
    T_EQ(v >> 16, 7601);                            /* build */
    T_EQ(v >> 31, 0);                               /* the NT family: the top bit is clear */
    T_EQ(v & 0xFF, rt_machine()->os_major);         /* and the same numbers as the machine */
    T_EQ((v >> 8) & 0xFF, rt_machine()->os_minor);
    T_EQ(v >> 16, rt_machine()->os_build);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetVersion);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
