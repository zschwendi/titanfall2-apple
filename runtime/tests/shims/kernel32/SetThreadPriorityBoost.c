#include "rt/test.h"
#include "rt/sync.h"

TEST(kernel32, SetThreadPriorityBoost)
{
    HANDLE ev;
    unsigned before;

    /* First in the test: the shim logs once per process, so the count is taken before any call. */
    T_CASE("the log line appears once for two calls");
    before = T_LOG_COUNT(RT_CH_THREAD);
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, RT_PSEUDO_THREAD, TRUE), TRUE);
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, RT_PSEUDO_THREAD, FALSE), TRUE);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD) - before, 1);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "SetThreadPriorityBoost(...): accepted and ignored"));

    T_CASE("accepts the calling thread's pseudo-handle with TRUE and with FALSE");
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, RT_PSEUDO_THREAD, TRUE), TRUE);
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, RT_PSEUDO_THREAD, FALSE), TRUE);

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, NULL, TRUE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ev = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, ev, FALSE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadPriorityBoost, RT_PSEUDO_THREAD, TRUE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
