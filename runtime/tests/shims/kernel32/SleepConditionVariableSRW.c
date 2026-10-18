#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, SleepConditionVariableSRW)
{
    CONDITION_VARIABLE cv = { 0 };
    SRWLOCK l = { 0 };
    SRWLOCK m = { 0 };
    SRWLOCK n = { 0 };
    SRWLOCK k = { 0 };
    BOOL r;

    T_CASE("a timeout returns FALSE with ERROR_TIMEOUT, exclusive lock");
    rt_srw_acquire_exclusive(&l);
    /* A flag bit other than CONDITION_VARIABLE_LOCKMODE_SHARED is not known: the shim stops (RT-TRAPS). */
    T_TRAPS(CALL(kernel32, SleepConditionVariableSRW, &cv, &l, 10, 2), "Flags other than 0 and CONDITION_VARIABLE_LOCKMODE_SHARED");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, SleepConditionVariableSRW, &cv, &l, 10, 0);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_REQUIRE(rt_srw_try_acquire_shared(&l) == FALSE);      /* held exclusively again */
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == FALSE);
    rt_srw_release_exclusive(&l);

    T_CASE("a timeout returns FALSE with ERROR_TIMEOUT, shared lock");
    rt_srw_acquire_shared(&m);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, SleepConditionVariableSRW, &cv, &m, 10, CONDITION_VARIABLE_LOCKMODE_SHARED);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&m) == FALSE);   /* held shared again, not exclusive */
    T_REQUIRE(rt_srw_try_acquire_shared(&m) == TRUE);       /* ... so a second shared holder gets in */
    rt_srw_release_shared(&m);
    rt_srw_release_shared(&m);

    T_CASE("a zero timeout returns at once with ERROR_TIMEOUT");
    rt_srw_acquire_exclusive(&n);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, SleepConditionVariableSRW, &cv, &n, 0, 0);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_REQUIRE(rt_srw_try_acquire_shared(&n) == FALSE);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&n) == FALSE);
    rt_srw_release_exclusive(&n);

    T_CASE("the lock is held again in the mode it was held before");
    rt_srw_acquire_shared(&k);                              /* two shared holders */
    rt_srw_acquire_shared(&k);
    r = CALL(kernel32, SleepConditionVariableSRW, &cv, &k, 10, CONDITION_VARIABLE_LOCKMODE_SHARED);
    T_EQ(r, FALSE);
    T_REQUIRE(rt_srw_try_acquire_shared(&k) == TRUE);       /* shared, and now three holders ... */
    rt_srw_release_shared(&k);                              /* ... back to two */
    T_REQUIRE(rt_srw_try_acquire_exclusive(&k) == FALSE);
    rt_srw_release_shared(&k);                              /* one holder left */
    T_REQUIRE(rt_srw_try_acquire_exclusive(&k) == FALSE);
    rt_srw_release_shared(&k);                              /* none left */
    T_REQUIRE(rt_srw_try_acquire_exclusive(&k) == TRUE);
    rt_srw_release_exclusive(&k);
}
