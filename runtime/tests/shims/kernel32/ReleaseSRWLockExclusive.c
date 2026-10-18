#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, ReleaseSRWLockExclusive)
{
    SRWLOCK l = { 0 };
    SRWLOCK m = { 0 };

    T_CASE("releasing an exclusive lock frees it for the next holder");
    rt_srw_acquire_exclusive(&l);
    CALL(kernel32, ReleaseSRWLockExclusive, &l);
    T_REQUIRE(rt_srw_try_acquire_shared(&l) == TRUE);       /* free: a shared holder gets in */
    rt_srw_release_shared(&l);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == TRUE);    /* and then an exclusive one */
    rt_srw_release_exclusive(&l);

    T_CASE("does not change last error");
    rt_srw_acquire_exclusive(&m);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, ReleaseSRWLockExclusive, &m);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&m) == TRUE);
    rt_srw_release_exclusive(&m);
}
