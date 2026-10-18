#include "rt/test.h"
#include "rt/window.h"
#include "rt/win/user.h"

TEST(user32, DestroyCursor)
{
    T_CASE("a system cursor (RT_HCURSOR_SYSTEM of an IDC_ value): returns TRUE, the cursor stays usable");
    T_EQ(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(IDC_ARROW)), TRUE);
    T_EQ(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(IDC_SIZEALL)), TRUE);
    T_EQ(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(IDC_HELP)), TRUE);
    rt_cursor_set(RT_HCURSOR_SYSTEM(IDC_ARROW));          /* still a cursor: does not stop */

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0xdeadbeef);
    T_EQ(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(IDC_ARROW)), TRUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_EQ(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(IDC_SIZEALL)), TRUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);
    T_EQ(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(IDC_HELP)), TRUE);
    T_EQ(T_LAST_ERROR(), 0xdeadbeef);

    /* RT-TRAPS: a handle that is not a system cursor is never guessed. RT_HCURSOR_SYSTEM(32647)
     * is a number between the two ranges: it is not an IDC_ value. */
    T_TRAPS(CALL(user32, DestroyCursor, (HCURSOR)0x1234), "not a system cursor");
    T_TRAPS(CALL(user32, DestroyCursor, RT_HCURSOR_SYSTEM(32647)), "not a system cursor");
}
