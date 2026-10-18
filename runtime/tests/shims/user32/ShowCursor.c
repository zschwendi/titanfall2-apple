#include "rt/test.h"
#include "rt/window.h"

TEST(user32, ShowCursor)
{
    T_CASE("counts FALSE down and TRUE up from 0, returning the new counter");
    T_EQ(CALL(user32, ShowCursor, FALSE), -1);
    T_EQ(CALL(user32, ShowCursor, TRUE), 0);
    T_EQ(CALL(user32, ShowCursor, TRUE), 1);
    T_EQ(CALL(user32, ShowCursor, FALSE), 0);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, ShowCursor, TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(user32, ShowCursor, 2), "a bShow that is neither TRUE nor FALSE: 2");
    T_TRAPS(CALL(user32, ShowCursor, -1), "a bShow that is neither TRUE nor FALSE: -1");
}
