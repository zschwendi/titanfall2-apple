#include "rt/test.h"
#include "rt/sync.h"
#include "rt/thread.h"

/* The seven THREAD_PRIORITY_ values the core accepts, in the order they are stored.  Each one
 * differs from the one before it, so a read-back that returns a stale value fails. */
static const int levels[7] = {
    THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
    THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST,
    THREAD_PRIORITY_TIME_CRITICAL,
};

TEST(kernel32, SetThreadPriority)
{
    HANDLE ev;
    unsigned i;

    T_CASE("accepts the calling thread's pseudo-handle and stores each of the seven values, which GetThreadPriority reads back");
    for (i = 0; i < 7; i++) {
        T_EQ(CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, levels[i]), TRUE);
        T_EQ(CALL(kernel32, GetThreadPriority, RT_PSEUDO_THREAD), levels[i]);
    }

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE and leave the priority as it was");
    T_EQ(CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, THREAD_PRIORITY_HIGHEST), TRUE);
    T_EQ(CALL(kernel32, SetThreadPriority, NULL, THREAD_PRIORITY_ABOVE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ev = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, SetThreadPriority, ev, THREAD_PRIORITY_ABOVE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetThreadPriority, RT_PSEUDO_THREAD), THREAD_PRIORITY_HIGHEST);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, THREAD_PRIORITY_LOWEST), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core stops on a value that is not one of the seven, whatever the handle; no guard in
     * the shim.  Placed after the checks of the last case. */
    T_TRAPS(CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, 3), "not one of the seven");

    CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, THREAD_PRIORITY_NORMAL);
}
