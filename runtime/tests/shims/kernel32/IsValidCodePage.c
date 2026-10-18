#include "rt/test.h"
#include "rt/win/nls.h"

/* The core's answers.  IsValidCodePage has no documented last-error code and the core never
 * sets one, so the last error is checked to be left alone on both TRUE and FALSE. */
TEST(kernel32, IsValidCodePage)
{
    T_CASE("1252, 437, 20127, 28591 and 65001 are valid");
    T_EQ(CALL(kernel32, IsValidCodePage, 1252), TRUE);
    T_EQ(CALL(kernel32, IsValidCodePage, 437), TRUE);
    T_EQ(CALL(kernel32, IsValidCodePage, 20127), TRUE);
    T_EQ(CALL(kernel32, IsValidCodePage, 28591), TRUE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_UTF8), TRUE);

    T_CASE("0 and the other special values are not valid");
    T_EQ(CALL(kernel32, IsValidCodePage, 0), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_ACP), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_OEMCP), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_MACCP), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_THREAD_ACP), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_SYMBOL), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, CP_UTF7), FALSE);

    T_CASE("code pages the machine lacks are not valid");
    T_EQ(CALL(kernel32, IsValidCodePage, 932), FALSE);
    T_EQ(CALL(kernel32, IsValidCodePage, 0xFFFFFFFFu), FALSE);

    T_CASE("success and failure leave last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, IsValidCodePage, 1252), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, IsValidCodePage, 932), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
