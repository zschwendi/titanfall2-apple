#include "rt/test.h"
#include "rt/lock.h"

TEST(kernel32, WakeConditionVariable)
{
    CONDITION_VARIABLE cv = { 0 };
    SRWLOCK l = { 0 };
    BOOL r;

    T_CASE("with no thread sleeping it has no effect, so a sleep after it still times out");
    CALL(kernel32, WakeConditionVariable, &cv);
    rt_srw_acquire_exclusive(&l);
    r = CALL(kernel32, SleepConditionVariableSRW, &cv, &l, 10, 0);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_TIMEOUT);
    T_REQUIRE(rt_srw_try_acquire_exclusive(&l) == FALSE);   /* the sleep took the lock back */
    rt_srw_release_exclusive(&l);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, WakeConditionVariable, &cv);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
