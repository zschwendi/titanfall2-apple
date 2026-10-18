#include "rt/test.h"

TEST(user32, GetKeyboardLayoutList)
{
    HKL list[2];

    T_CASE("nBuff 0 gives the count");
    memset(list, 0xA5, sizeof list);
    T_EQ(CALL(user32, GetKeyboardLayoutList, 0, list), 1);
    T_EQ((uintptr_t)list[0], 0xA5A5A5A5A5A5A5A5ULL);
    T_EQ((uintptr_t)list[1], 0xA5A5A5A5A5A5A5A5ULL);
    T_EQ(CALL(user32, GetKeyboardLayoutList, 0, NULL), 1);

    T_CASE("nBuff 2 with a list gets the layout first");
    memset(list, 0xA5, sizeof list);
    T_EQ(CALL(user32, GetKeyboardLayoutList, 2, list), 1);
    T_EQ(list[0], (HKL)0x04090409);
    T_EQ((uintptr_t)list[1], 0xA5A5A5A5A5A5A5A5ULL);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetKeyboardLayoutList, 0, NULL);
    CALL(user32, GetKeyboardLayoutList, 2, list);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: a negative count and a buffer with no list are never guessed. */
    T_TRAPS(CALL(user32, GetKeyboardLayoutList, -1, list), "negative nBuff -1");
    T_TRAPS(CALL(user32, GetKeyboardLayoutList, 1, NULL), "NULL list with nBuff 1");
}
