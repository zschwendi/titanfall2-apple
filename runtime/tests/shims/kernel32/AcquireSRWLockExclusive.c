#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, AcquireSRWLockExclusive)
{
    SRWLOCK l = { 0 };
    SRWLOCK m = { 0 };

    T_CASE("a free lock is taken exclusively, so no other holder can take it");
    CALL(kernel32, AcquireSRWLockExclusive, &l);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == FALSE);
    T_REQUIRE(rt_srw_try_acquire_shared(&l) == FALSE);
    rt_srw_release_exclusive(&l);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, AcquireSRWLockExclusive, &m);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&m) == FALSE);
    rt_srw_release_exclusive(&m);
}
