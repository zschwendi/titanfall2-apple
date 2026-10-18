#include "rt/test.h"
#include "rt/window.h"

TEST(user32, SetCursor)
{
    HCURSOR arrow = RT_HCURSOR_SYSTEM(IDC_ARROW);

    T_CASE("sets a system cursor and returns the previous one, NULL at first");
    T_EQ((uintptr_t)CALL(user32, SetCursor, arrow), 0);
    T_EQ((uintptr_t)CALL(user32, SetCursor, arrow), (uintptr_t)arrow);

    T_CASE("NULL removes the cursor and returns the previous one");
    T_EQ((uintptr_t)CALL(user32, SetCursor, NULL), (uintptr_t)arrow);
    T_EQ((uintptr_t)CALL(user32, SetCursor, NULL), 0);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, SetCursor, arrow);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, SetCursor, (HCURSOR)(uintptr_t)0x1234), "rt_cursor_set: not implemented");
    T_TRAPS(CALL(user32, SetCursor, RT_HCURSOR_SYSTEM(1)), "rt_cursor_set: not implemented");
}
