#include "rt/test.h"

TEST(kernel32, EncodePointer)
{
    int local = 0;
    PVOID p = &local;
    PVOID c = (PVOID)(uintptr_t)0x0000011000001234;

    T_CASE("returns the pointer unchanged");
    T_EQ(CALL(kernel32, EncodePointer, p), p);
    T_EQ(CALL(kernel32, EncodePointer, c), c);

    T_CASE("NULL stays NULL");
    T_TRUE(CALL(kernel32, EncodePointer, NULL) == NULL);

    T_CASE("a value with all 64 bits set is returned whole");
    PVOID all = (PVOID)(uintptr_t)0xFFFFFFFFFFFFFFFFull;
    uint64_t got = (uint64_t)(uintptr_t)CALL(kernel32, EncodePointer, all);
    T_EQ(got, 0xFFFFFFFFFFFFFFFFull);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, EncodePointer, p);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
