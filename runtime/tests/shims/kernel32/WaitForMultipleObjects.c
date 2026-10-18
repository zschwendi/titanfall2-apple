#include "rt/test.h"
#include "rt/sync.h"
#include "rt/thread.h"

/* A guest APC: records each run's parameter.  The wait is not alertable, so the test checks
 * that a queued call is still pending after the wait and runs at the next alertable wait. */
static unsigned apc_runs;
static uint64_t apc_param;

GUEST_FN(record_apc)
{
    apc_runs++;
    apc_param = ctx->r[1];
    GUEST_RET(ctx);
}

/* An auto-reset event, set or not, made with the core. */
static HANDLE auto_event(BOOL set)
{
    return rt_event_create(T_CTX(), FALSE, set, NULL);
}

TEST(kernel32, WaitForMultipleObjects)
{
    HANDLE a, b, c;
    HANDLE h[3];
    HANDLE many[MAXIMUM_WAIT_OBJECTS + 1];
    unsigned i;

    T_CASE("wait-any returns the lowest signalled index and takes only that object");
    a = auto_event(FALSE);
    b = auto_event(TRUE);
    c = auto_event(TRUE);
    T_TRUE(a && b && c);
    h[0] = a; h[1] = b; h[2] = c;
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 3, h, FALSE, 0), WAIT_OBJECT_0 + 1);   /* b and c set: lowest is 1 */
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 3, h, FALSE, 0), WAIT_OBJECT_0 + 2);   /* b was taken */
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 3, h, FALSE, 0), WAIT_TIMEOUT);        /* c was taken */
    T_EQ(rt_event_set(T_CTX(), a), TRUE);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 3, h, FALSE, 0), WAIT_OBJECT_0);       /* a is now lowest */

    T_CASE("wait-all returns WAIT_OBJECT_0 once every object is signalled and takes them all together, and times out with nothing taken until then");
    a = auto_event(FALSE);
    b = auto_event(FALSE);
    T_TRUE(a && b);
    h[0] = a; h[1] = b;
    T_EQ(rt_event_set(T_CTX(), a), TRUE);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 2, h, TRUE, 10), WAIT_TIMEOUT);        /* b is not set */
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 1, h, FALSE, 0), WAIT_OBJECT_0);       /* a was not taken by the timed-out wait; this takes it */
    T_EQ(rt_event_set(T_CTX(), a), TRUE);
    T_EQ(rt_event_set(T_CTX(), b), TRUE);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 2, h, TRUE, 0), WAIT_OBJECT_0);        /* both set: both taken */
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 2, h, FALSE, 0), WAIT_TIMEOUT);        /* neither is left set */

    T_CASE("up to MAXIMUM_WAIT_OBJECTS objects");
    for (i = 0; i < MAXIMUM_WAIT_OBJECTS; i++) many[i] = auto_event(FALSE);
    T_EQ(rt_event_set(T_CTX(), many[63]), TRUE);
    T_EQ(rt_event_set(T_CTX(), many[40]), TRUE);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, MAXIMUM_WAIT_OBJECTS, many, FALSE, 0), WAIT_OBJECT_0 + 40);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, MAXIMUM_WAIT_OBJECTS, many, FALSE, 0), WAIT_OBJECT_0 + 63);

    T_CASE("a count of 0 or above MAXIMUM_WAIT_OBJECTS fails with ERROR_INVALID_PARAMETER");
    many[MAXIMUM_WAIT_OBJECTS] = many[0];
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 0, many, FALSE, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, MAXIMUM_WAIT_OBJECTS + 1, many, FALSE, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("wait-all with the same handle twice fails with ERROR_INVALID_PARAMETER and takes nothing");
    a = auto_event(TRUE);
    T_TRUE(a);
    h[0] = a; h[1] = a;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 2, h, TRUE, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 1, h, FALSE, 0), WAIT_OBJECT_0);       /* still set: the failed call took nothing */

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    a = auto_event(TRUE);
    T_TRUE(a);
    T_EQ(CALL(kernel32, CloseHandle, a), TRUE);
    h[0] = a;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 1, h, FALSE, 0), WAIT_FAILED);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("is not alertable, so a queued APC does not run");
    a = auto_event(FALSE);
    T_TRUE(a);
    h[0] = a;
    apc_runs = 0;
    T_EQ(rt_apc_queue(T_CTX(), RT_THREAD(T_CTX()), T_GUEST_ADDR(record_apc), 5), TRUE);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 1, h, FALSE, 10), WAIT_TIMEOUT);
    T_EQ(apc_runs, 0);
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), WAIT_IO_COMPLETION);       /* the next alertable wait runs it */
    T_EQ(apc_runs, 1);
    T_EQ(apc_param, 5);

    T_CASE("success does not change last error");
    a = auto_event(TRUE);
    T_TRUE(a);
    h[0] = a;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, WaitForMultipleObjects, 1, h, FALSE, 0), WAIT_OBJECT_0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* A stop the core makes (rt_wait_handles: NULL handles is fatal); the shim adds no guard. */
    T_TRAPS(CALL(kernel32, WaitForMultipleObjects, 1, NULL, FALSE, 0), "NULL handle array");
}
