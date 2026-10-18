#include "rt/test.h"

TEST(kernel32, DecodePointer)
{
    int local = 0;
    PVOID p = &local;
    PVOID c = (PVOID)(uintptr_t)0x0000011000001234;

    T_CASE("returns the pointer unchanged");
    T_EQ(CALL(kernel32, DecodePointer, p), p);
    T_EQ(CALL(kernel32, DecodePointer, c), c);

    T_CASE("NULL stays NULL");
    T_TRUE(CALL(kernel32, DecodePointer, NULL) == NULL);

    T_CASE("DecodePointer of EncodePointer gives back the pointer");
    T_EQ(CALL(kernel32, DecodePointer, CALL(kernel32, EncodePointer, p)), p);
    T_EQ(CALL(kernel32, DecodePointer, CALL(kernel32, EncodePointer, c)), c);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, DecodePointer, p);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
