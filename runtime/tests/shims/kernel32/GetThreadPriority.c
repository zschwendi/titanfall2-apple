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

TEST(kernel32, GetThreadPriority)
{
    HANDLE ev;
    unsigned i;

    T_CASE("the calling thread's pseudo-handle starts at THREAD_PRIORITY_NORMAL");
    T_EQ(CALL(kernel32, GetThreadPriority, RT_PSEUDO_THREAD), THREAD_PRIORITY_NORMAL);

    T_CASE("returns each of the seven values that SetThreadPriority stored");
    for (i = 0; i < 7; i++) {
        T_EQ(CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, levels[i]), TRUE);
        T_EQ(CALL(kernel32, GetThreadPriority, RT_PSEUDO_THREAD), levels[i]);
    }

    T_CASE("NULL and an event handle fail with THREAD_PRIORITY_ERROR_RETURN and ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, GetThreadPriority, NULL), THREAD_PRIORITY_ERROR_RETURN);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ev = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, GetThreadPriority, ev), THREAD_PRIORITY_ERROR_RETURN);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetThreadPriority, RT_PSEUDO_THREAD), levels[6]);   /* the last value stored */
    T_EQ(T_LAST_ERROR(), 0x1234);

    CALL(kernel32, SetThreadPriority, RT_PSEUDO_THREAD, THREAD_PRIORITY_NORMAL);
}
