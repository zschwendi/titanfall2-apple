#include "rt/test.h"
#include "rt/win/file.h"

TEST(kernel32, GetStdHandle)
{
    HANDLE hin, hout, herr;

    T_CASE("the three standard handles are different values, neither NULL nor INVALID_HANDLE_VALUE, and the same on every call");
    hin  = CALL(kernel32, GetStdHandle, STD_INPUT_HANDLE);
    hout = CALL(kernel32, GetStdHandle, STD_OUTPUT_HANDLE);
    herr = CALL(kernel32, GetStdHandle, STD_ERROR_HANDLE);
    T_TRUE(hin != NULL && hin != INVALID_HANDLE_VALUE);
    T_TRUE(hout != NULL && hout != INVALID_HANDLE_VALUE);
    T_TRUE(herr != NULL && herr != INVALID_HANDLE_VALUE);
    T_TRUE(hin != hout);
    T_TRUE(hout != herr);
    T_TRUE(hin != herr);
    T_EQ(CALL(kernel32, GetStdHandle, STD_INPUT_HANDLE), hin);
    T_EQ(CALL(kernel32, GetStdHandle, STD_OUTPUT_HANDLE), hout);
    T_EQ(CALL(kernel32, GetStdHandle, STD_ERROR_HANDLE), herr);

    T_CASE("another number gives INVALID_HANDLE_VALUE and ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, GetStdHandle, 0), INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetStdHandle, 1), INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetStdHandle, (DWORD)-9), INVALID_HANDLE_VALUE);   /* just above STD_INPUT_HANDLE */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetStdHandle, (DWORD)-13), INVALID_HANDLE_VALUE);  /* just below STD_ERROR_HANDLE */
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetStdHandle, STD_INPUT_HANDLE), hin);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetStdHandle, STD_OUTPUT_HANDLE), hout);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetStdHandle, STD_ERROR_HANDLE), herr);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
