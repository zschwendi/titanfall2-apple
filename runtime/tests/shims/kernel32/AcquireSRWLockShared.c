#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, AcquireSRWLockShared)
{
    SRWLOCK l = { 0 };
    SRWLOCK m = { 0 };

    T_CASE("a free lock is taken shared: another shared holder is allowed and an exclusive holder is refused");
    CALL(kernel32, AcquireSRWLockShared, &l);
    T_REQUIRE(rt_srw_try_acquire_shared(&l) == TRUE);       /* a second shared holder: two now */
    rt_srw_release_shared(&l);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == FALSE);   /* one shared holder left: no exclusive */
    rt_srw_release_shared(&l);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, AcquireSRWLockShared, &m);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&m) == FALSE);
    rt_srw_release_shared(&m);
}
