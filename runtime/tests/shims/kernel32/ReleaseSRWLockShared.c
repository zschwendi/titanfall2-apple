#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, ReleaseSRWLockShared)
{
    SRWLOCK l = { 0 };
    SRWLOCK m = { 0 };
    SRWLOCK n = { 0 };

    T_CASE("releasing one of two shared holders keeps the lock shared");
    rt_srw_acquire_shared(&l);
    rt_srw_acquire_shared(&l);
    CALL(kernel32, ReleaseSRWLockShared, &l);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == FALSE);   /* the other holder is still in */
    rt_srw_release_shared(&l);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == TRUE);    /* and now nobody is */
    rt_srw_release_exclusive(&l);

    T_CASE("releasing the last shared holder frees the lock");
    rt_srw_acquire_shared(&m);
    CALL(kernel32, ReleaseSRWLockShared, &m);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&m) == TRUE);
    rt_srw_release_exclusive(&m);

    T_CASE("does not change last error");
    rt_srw_acquire_shared(&n);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, ReleaseSRWLockShared, &n);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&n) == TRUE);
    rt_srw_release_exclusive(&n);
}
