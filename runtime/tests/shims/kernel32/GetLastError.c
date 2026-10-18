#include "rt/test.h"

TEST(kernel32, GetLastError)
{
    T_CASE("returns the value SetLastError stored");
    CALL(kernel32, SetLastError, ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetLastError), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0xC0000005u);
    T_EQ(CALL(kernel32, GetLastError), 0xC0000005u);

    T_CASE("reads the thread block field at +0x68");
    ST32(T_CTX()->gs_base + 0x68, 0x12345678);          /* what inlined guest code would write */
    T_EQ(CALL(kernel32, GetLastError), 0x12345678);

    T_CASE("leaves the value in place");
    T_EQ(CALL(kernel32, GetLastError), 0x12345678);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), 0x12345678);
}
