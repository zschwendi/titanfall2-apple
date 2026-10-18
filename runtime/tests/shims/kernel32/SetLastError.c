#include "rt/test.h"

TEST(kernel32, SetLastError)
{
    T_CASE("stores the value in the thread block at +0x68");
    CALL(kernel32, SetLastError, ERROR_FILE_NOT_FOUND);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    CALL(kernel32, SetLastError, 0xFFFFFFFFu);
    T_EQ(T_LAST_ERROR(), 0xFFFFFFFFu);

    T_CASE("uses only the low 32 bits of the argument");
    /* The harness fills the upper half of rcx with garbage on every call; the 4 bytes after
     * the field must stay as they were. */
    ST32(T_CTX()->gs_base + 0x6C, 0xA5A5A5A5);
    CALL(kernel32, SetLastError, 0x80070057u);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), 0x80070057u);
    T_EQ(LD32(T_CTX()->gs_base + 0x6C), 0xA5A5A5A5u);

    T_CASE("zero is stored like any other value");
    CALL(kernel32, SetLastError, ERROR_SUCCESS);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
}
