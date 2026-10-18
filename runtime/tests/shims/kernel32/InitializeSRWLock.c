#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, InitializeSRWLock)
{
    SRWLOCK l;
    uint64_t bits;

    T_CASE("the lock is unlocked whatever its bytes were before");
    memset(&l, 0xFF, sizeof l);
    CALL(kernel32, InitializeSRWLock, &l);
    memcpy(&bits, &l, sizeof bits);
    T_EQ(bits, 0);

    T_CASE("the lock can be acquired at once afterwards");
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == TRUE);
    rt_srw_release_exclusive(&l);
    T_REQUIRE(rt_srw_try_acquire_shared(&l) == TRUE);
    rt_srw_release_shared(&l);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, InitializeSRWLock, &l);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
